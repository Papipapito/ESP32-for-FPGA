/*
 * UsbHost.ino - Teclado y mando USB del MSXnano sobre ESP32-S3 (companion).
 *
 * Sustituye a la Pico Zero RP2040: el FPGA NO CAMBIA, asi que este modulo habla
 * EXACTAMENTE el mismo protocolo (ver MsxHid.h, contrato congelado). Aqui solo
 * hay pegamento: USB host -> MsxHid -> UART2. Toda la logica esta en MsxHid.cpp,
 * que es C++11 puro y se verifica byte a byte en el PC (host_test/test_msxhid.sh).
 *
 * Arduino concatena los .ino: llamar usbHostSetup() en setup() y usbHostTask()
 * en loop() del .ino principal, igual que displaySetup()/displayTask().
 *
 * ---------------------------------------------------------------------------
 * REQUISITOS
 * ---------------------------------------------------------------------------
 *  - Arduino-ESP32 >= 3.2.0. Antes de esa version el soporte de HUBS del USB
 *    host de IDF venia DESACTIVADO, y aqui hace falta un hub para enchufar
 *    teclado y mando a la vez. Con 3.2.0 viene activado por defecto.
 *  - Libreria EspUsbHost (tanakamasayuki) >= 2.5.x.
 *  - Hub USB AUTO-ALIMENTADO: el S3 no da corriente al bus.
 *
 * ---------------------------------------------------------------------------
 * XINPUT (mandos Xbox): NO SOPORTADO HOY. Leelo antes de comprar un mando.
 * ---------------------------------------------------------------------------
 * Un mando XInput no es HID: expone una interfaz vendor-specific (clase 0xFF,
 * subclase 0x5D) con endpoints de INTERRUPCION. EspUsbHost 2.5.2 tiene API
 * vendor, pero su propia especificacion excluye explicitamente los endpoints de
 * interrupcion (solo bulk + control EP0), asi que no hay por donde leer los
 * reports del mando. En el RP2040 esto lo resolvia el driver vendorizado de
 * Ryzee119 sobre TinyUSB, que aqui no existe.
 * El MAPEO XInput se conserva intacto y probado en MsxHid::decodeXInput() (cruz,
 * stick, A/B a disparo, X/Y arman el autofire): el dia que haya transporte, se
 * engancha en una linea. Mientras tanto: mandos HID (DirectInput). Muchos pads
 * "de PC" llevan un interruptor XInput/DInput -> ponerlo en DInput.
 *
 * ---------------------------------------------------------------------------
 * CONCURRENCIA
 * ---------------------------------------------------------------------------
 * EspUsbHost levanta su PROPIA tarea de FreeRTOS y los callbacks salen de ella,
 * no de loop(). Como usbHostTask() (autofire + resync) corre en loop(), hay dos
 * contextos tocando la misma matriz y el mismo UART. Todo lo que entra en MsxHid
 * pasa por un mutex; los tramos son de microsegundos (rellenar un buffer y
 * escribir <= 34 bytes en el ring del UART), asi que no bloquea a nadie.
 */

// Solo el ESP32-S3 (y el P4) tienen USB OTG capaz de hacer de host. En el C6
// este fichero no debe ni intentar compilar: no existe la libreria ni el
// periferico. Las dos funciones publicas quedan como stubs vacios.
#if defined(CONFIG_IDF_TARGET_ESP32S3)

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "EspUsbHost.h"
#include "MsxHid.h"
#include "BoardS3.h"

#if !defined(ESP_ARDUINO_VERSION) || !defined(ESP_ARDUINO_VERSION_VAL) || \
    (ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 2, 0))
#error "UsbHost.ino necesita Arduino-ESP32 >= 3.2.0 (hubs USB activados por defecto)"
#endif

// UART hacia el FPGA: TX-only. GPIO42 -> pin 31 del Tang Nano (kbd_uart_rx.v).
// RX = -1 a proposito: el FPGA no contesta nada por este cable.
#define KBD_UART        Serial2
#define KBD_TX_PIN      S3_FPGA_KBD_TX
#define KBD_BAUD        S3_BAUD_KBD

// Segundo mando -> puerto 2 del MSX. Por defecto DESACTIVADO: el firmware
// RP2040 mandaba todos los pads al puerto 1 y aqui se clona su comportamiento.
// Ponerlo a 1 reparte: el primer mando que aparece va al puerto 1 y el segundo
// al 2 (el protocolo y MsxHid ya soportan los dos puertos).
#ifndef USBHOST_SECOND_PAD_TO_PORT2
#define USBHOST_SECOND_PAD_TO_PORT2 0
#endif

// ---------------------------------------------------------------------------
// Estado del modulo
// ---------------------------------------------------------------------------
static EspUsbHost      s_usb;
static MsxHid          s_hid;
static SemaphoreHandle_t s_lock = NULL;

// Direccion USB del teclado y de los mandos ya vistos (0 = ninguno). Sirven
// para saber a quien pertenece una desconexion: el evento de desconexion solo
// trae la direccion, no el "rol", que se deduce del tipo de report recibido.
static uint8_t s_kbdAddr    = 0;
static uint8_t s_padAddr[2] = { 0, 0 };

// Diagnostico opcional (pantalla / depuracion). No se usa desde MsxHid.
volatile uint32_t g_usbHostLastKeyMs = 0;   // millis() del ultimo report de teclado
volatile uint8_t  g_usbHostKbdUp     = 0;   // 1 mientras hay teclado enchufado
volatile uint8_t  g_usbHostPadUp     = 0;   // 1 mientras hay algun mando enchufado

// ---------------------------------------------------------------------------
// Sumidero de bytes: MsxHid emite mensajes completos (2, 3 o 15 bytes) y aqui
// se vuelcan al UART. write() del ESP32 copia al ring de TX y solo bloquea si
// se llena; la rafaga mayor posible son ~34 bytes (16 MAKEs de un teclado NKRO)
// y el ring por defecto son 256, asi que en la practica nunca espera.
// ---------------------------------------------------------------------------
static void kbdUartEmit(const uint8_t* data, size_t len) {
    KBD_UART.write(data, len);
}

// Envoltorios de bloqueo. El mutex protege la matriz Y el orden de los bytes en
// el cable: dos mensajes intercalados a medias romperian el parseo posicional
// del FPGA (opcode + dato).
static inline void hidLock()   { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static inline void hidUnlock() { if (s_lock) xSemaphoreGive(s_lock); }

// ---------------------------------------------------------------------------
// Teclado. onKeyboardState() entrega una FOTO normalizada de la pagina
// Keyboard/Keypad: mapa de 256 bits con las teclas pulsadas, venga el teclado
// en formato boot (6KRO) o NKRO. Justo lo que quiere MsxHid::keyboardBitmap().
// ---------------------------------------------------------------------------
static void onKeyboardState(const EspUsbHostKeyboardState& state) {
    hidLock();
    if (s_kbdAddr == 0) {
        // Primer report de este teclado: linea base limpia en el FPGA. Si ya
        // habia otro teclado, NO se re-sincroniza (los dos se mezclan en la
        // misma matriz, como hacia el RP2040 con varios teclados).
        s_kbdAddr = state.address;
        g_usbHostKbdUp = 1;
        s_hid.keyboardAttached();
    }
    s_hid.keyboardBitmap(state.modifiers, state.keys, sizeof(state.keys));
    hidUnlock();
    g_usbHostLastKeyMs = millis();
}

// ---------------------------------------------------------------------------
// Mando HID generico. Se despieza el report por USAGE (nada de tablas por
// VID/PID) y se deja que MsxHid::decodeGamepad() haga la traduccion verificada.
// ---------------------------------------------------------------------------
static uint8_t padPortFor(uint8_t address) {
    for (uint8_t p = 0; p < 2; p++)
        if (s_padAddr[p] == address) return p;
    if (s_padAddr[0] == 0) { s_padAddr[0] = address; return 0; }
#if USBHOST_SECOND_PAD_TO_PORT2
    if (s_padAddr[1] == 0) { s_padAddr[1] = address; return 1; }
#endif
    return 0;   // clon del RP2040: todo lo demas cae en el puerto 1 del MSX
}

static void onGamepad(const EspUsbHostGamepadEvent& event) {
    MsxJoyInput in;

    for (size_t i = 0; i < event.fieldCount; i++) {
        const EspUsbHostHIDFieldValue& f = event.fields[i];
        if (f.usagePage == 0x01) {                 // Generic Desktop
            if (f.usage == 0x30 && !in.x.present) {          // X
                in.x.present = true; in.x.value = f.value;
                in.x.logicalMin = f.logicalMin; in.x.logicalMax = f.logicalMax;
                in.x.bitSize = f.bitSize;
            } else if (f.usage == 0x31 && !in.y.present) {   // Y
                in.y.present = true; in.y.value = f.value;
                in.y.logicalMin = f.logicalMin; in.y.logicalMax = f.logicalMax;
                in.y.bitSize = f.bitSize;
            } else if (f.usage == 0x39 && !in.hatPresent) {  // Hat switch
                in.hatPresent = true; in.hat = f.value;
                in.hatLogicalMin = f.logicalMin;
            }
            // Z/Rz (stick derecho) se ignoran, igual que en el RP2040: el MSX
            // solo tiene una cruceta y mezclarlos daria direcciones fantasma.
        } else if (f.usagePage == 0x09) {          // Button
            if (f.value && f.usage >= 1 && f.usage <= 32)
                in.buttons |= (uint32_t)1u << (f.usage - 1);
        }
    }

    uint8_t base = 0, af = 0;
    MsxHid::decodeGamepad(in, &base, &af);

    hidLock();
    uint8_t port = padPortFor(event.address);
    if (!g_usbHostPadUp) g_usbHostPadUp = 1;
    // Solo se LATCHEA: el emisor real es usbHostTask() -> tick(), porque un
    // mando pulsado pero quieto deja de mandar reports y el autofire se
    // congelaria si dependiera de este callback.
    s_hid.joystickInput(port, base, af);
    hidUnlock();
}

// ---------------------------------------------------------------------------
// Conexion / desconexion. El rol (teclado o mando) se sabe por el tipo de
// report que llega, asi que en la conexion solo se anota y en la desconexion se
// libera lo que corresponda: lo importante es no dejar una tecla o una
// direccion CLAVADA en el MSX.
// ---------------------------------------------------------------------------
static void onDeviceDisconnected(const EspUsbHostDeviceInfo& device) {
    hidLock();
    if (device.address == s_kbdAddr) {
        s_kbdAddr = 0;
        g_usbHostKbdUp = 0;
        s_hid.keyboardDetached();     // suelta toda la matriz + resync
    }
    for (uint8_t p = 0; p < 2; p++) {
        if (s_padAddr[p] == device.address) {
            s_padAddr[p] = 0;
            s_hid.joystickDetached(p);   // suelta direcciones y disparos
        }
    }
    g_usbHostPadUp = (s_padAddr[0] || s_padAddr[1]) ? 1 : 0;
    hidUnlock();
}

// ---------------------------------------------------------------------------
// API publica (se llama desde el .ino principal)
// ---------------------------------------------------------------------------
void usbHostSetup() {
    s_lock = xSemaphoreCreateMutex();

    // UART2 TX-only hacia el FPGA. 115200 8N1 = contrato congelado.
    KBD_UART.begin(KBD_BAUD, SERIAL_8N1, -1, KBD_TX_PIN);

    s_hid.begin(kbdUartEmit);

    s_usb.onKeyboardState(onKeyboardState);
    s_usb.onGamepad(onGamepad);
    s_usb.onDeviceDisconnected(onDeviceDisconnected);
    // No se toca setKeyboardLayout(): la traduccion a la matriz MSX la hace
    // MsxHid con la tabla verificada (Internacional/US del core), y el ASCII que
    // calcularia la libreria no se usa para nada.

    s_usb.begin();   // arranca la tarea del host USB
}

void usbHostTask() {
    // El tiempo entra como parametro: MsxHid no conoce millis(). Aqui se hacen
    // dos cosas criticas, las DOS dentro de tick():
    //   - autofire: onda cuadrada de 10 Hz sobre los disparos armados.
    //   - resync cada 250 ms: OBLIGATORIO. El FPGA tiene un watchdog de ~1 s
    //     que suelta todas las teclas si deja de recibir bytes, asi que loop()
    //     no puede quedarse mas de un segundo sin pasar por aqui.
    hidLock();
    s_hid.tick(millis());
    hidUnlock();
}

#else  // !CONFIG_IDF_TARGET_ESP32S3

// En el ESP32-C6 (placa vieja) no hay USB host: el teclado y el mando siguen
// viniendo de la Pico RP2040 por su propio cable. Stubs para que el .ino
// principal pueda llamarlos sin #ifdefs.
void usbHostSetup() {}
void usbHostTask()  {}

#endif // CONFIG_IDF_TARGET_ESP32S3
