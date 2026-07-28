// ============================================================================
// XInputHost - Transporte de mandos XInput (Xbox) sobre el USB Host del ESP32-S3.
//
// POR QUE EXISTE ESTE FICHERO
// ---------------------------------------------------------------------------
// Un mando XInput NO es HID. Expone una interfaz VENDOR-SPECIFIC (bInterfaceClass
// 0xFF, bInterfaceSubClass 0x5D en los 360; 0x47/0xD0 en los One; clase 0x58 en
// el Xbox original) cuyos reports viajan por un endpoint de INTERRUPCION-IN.
// EspUsbHost 2.5.2 no lo cubre: su rama "vendor" solo se activa para chips
// USB-serie conocidos (isKnownVendorSerial(), lista blanca VID/PID de CH34x /
// CP210x / FTDI...), asi que la interfaz del mando queda SIN reclamar y sin
// nadie que la sondee. Ver la evidencia en el informe: EspUsbHost.cpp claim
// solo si HID / CDC / Audio / MIDI / MSC / vendor-serie-conocido.
//
// La buena noticia: eso NO es un limite del S3. El stack usb_host de ESP-IDF
// admite VARIOS clientes en el mismo bus y transferencias de interrupcion sobre
// cualquier endpoint reclamado. Doc oficial (USB Host Library, ESP32-S3):
//   "It is possible for two or more clients to simultaneously communicate with
//    the same device as long as they are not communicating to the same interface."
//   "Supports all four transfer types: Control, Bulk, Interrupt, and Isochronous."
// Asi que este modulo registra un SEGUNDO cliente (usb_host_client_register())
// junto al de EspUsbHost, reclama SOLO la interfaz del mando y sondea su
// interrupt-IN. EspUsbHost sigue siendo el dueno del usb_host_install() y de
// usb_host_lib_handle_events(); aqui no se toca ninguna de las dos cosas.
//
// SOLO INTERRUPT-IN (regla heredada del RP2040, ver usbin.c ~L774)
// ---------------------------------------------------------------------------
// En la Pico se quitaron A PROPOSITO las transferencias OUT (LED y rumble):
// fue lo que hizo funcionar el mando detras de un hub. Aqui se respeta: este
// driver NUNCA envia nada por el endpoint OUT. Consecuencia honesta y asumida:
// los mandos Xbox One / Series (GIP) necesitan un paquete de encendido por OUT
// antes de emitir nada, asi que se DETECTAN pero NO se reclaman (ver
// XInputHost.cpp, classifyAndClaim). Mandos 360 (cable y receptor inalambrico)
// y Xbox original funcionan sin una sola transferencia OUT.
//
// PRESUPUESTO DE CANALES (8 en el S3, uno por endpoint abierto)
// ---------------------------------------------------------------------------
// Doc oficial: "Supported amount of channels for ESP32-S3 is 8",
// "One free channel is required to enumerate the device",
// "From 1 to N (when N - number of EPs) free channels are required to claim
//  the interface".
// OJO, dato importante y contraintuitivo: usb_host_interface_claim() abre TODOS
// los endpoints de la interfaz, se usen o no (esp-idf/components/usb/usb_host.c,
// interface_claim() -> bucle sobre bNumEndpoints -> ep_wrapper_alloc()). La
// interfaz 0 de un mando 360 tiene 2 endpoints (IN y OUT), asi que el mando
// cuesta 3 canales: 1 de su pipe de control + 2 de la interfaz. Cuenta real:
//     hub    1 ctrl + 1 intr(status)      = 2
//     teclado 1 ctrl + 1 intr(IN)         = 2
//     mando  1 ctrl + 2 (intr IN + OUT)   = 3
//                                     TOTAL 7 / 8
// Cabe, pero con UN canal de margen, no dos. Y NO se puede hacer mejor: la API
// publica de ESP-IDF no permite reclamar endpoints sueltos. Corolario: no metas
// un tercer periferico (segundo mando, pendrive) en el mismo hub.
//
// Este header es C++11 puro (solo stdint/stddef) para que el banco de pruebas
// del PC pueda incluirlo y verificar el troceado de reports sin ESP-IDF.
// ============================================================================
#ifndef XINPUTHOST_H
#define XINPUTHOST_H

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Mascara de botones. VALORES IDENTICOS a los de xinput_host.h (Ryzee119), que
// es exactamente lo que espera MsxHid::decodeXInput(): ese mapeo ya esta
// probado byte a byte en host_test/test_msxhid.cpp y NO se replica aqui.
// ---------------------------------------------------------------------------
#define XINPUT_GAMEPAD_DPAD_UP          0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN        0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT        0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT       0x0008
#define XINPUT_GAMEPAD_START            0x0010
#define XINPUT_GAMEPAD_BACK             0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB       0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB      0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER    0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER   0x0200
#define XINPUT_GAMEPAD_GUIDE            0x0400
#define XINPUT_GAMEPAD_SHARE            0x0800
#define XINPUT_GAMEPAD_A                0x1000
#define XINPUT_GAMEPAD_B                0x2000
#define XINPUT_GAMEPAD_X                0x4000
#define XINPUT_GAMEPAD_Y                0x8000

// Familias de mando. La deteccion es por descriptor de interfaz, NUNCA por
// VID/PID: los clones "XInput" copian la firma de interfaz pero no el VID.
enum XInputType {
    XINPUT_TYPE_NONE = 0,      // no es un mando XInput
    XINPUT_TYPE_360_WIRED,     // 0xFF/0x5D/0x01  - mando 360 por cable y clones
    XINPUT_TYPE_360_WIRELESS,  // 0xFF/0x5D/0x81  - receptor inalambrico 360
    XINPUT_TYPE_XBOXONE,       // 0xFF/0x47/0xD0  - One / Series (GIP)
    XINPUT_TYPE_XBOXOG         // 0x58/0x42       - Xbox original
};

// Que hemos sacado de un paquete: la mayoria de paquetes NO son de estado.
enum XInputParse {
    XINPUT_PARSE_IGNORED = 0,  // paquete valido pero sin interes (keepalive...)
    XINPUT_PARSE_PAD,          // hay estado de mando nuevo en wButtons/thumbL*
    XINPUT_PARSE_LINK_UP,      // receptor 360: se ha encendido un mando
    XINPUT_PARSE_LINK_DOWN     // receptor 360: el mando se ha apagado
};

// ---------------------------------------------------------------------------
// Callbacks. TODOS salen de la tarea propia de este modulo (no de loop()), asi
// que el receptor debe serializar igual que hace con los de EspUsbHost.
// ---------------------------------------------------------------------------
typedef void (*XInputReportCb)(uint8_t address, uint16_t wButtons,
                               int16_t thumbLX, int16_t thumbLY);
typedef void (*XInputMountCb)(uint8_t address, XInputType type);
typedef void (*XInputUmountCb)(uint8_t address);

// ---------------------------------------------------------------------------
// API publica. En placas que no sean ESP32-S3 son stubs vacios (xinputHostBegin
// devuelve false) para que el .ino pueda llamarlas sin #ifdefs.
//
// ORDEN OBLIGATORIO: llamar SIEMPRE despues de EspUsbHost::begin(). ESP-IDF
// exige usb_host_install() antes de cualquier usb_host_client_register()
// (devuelve ESP_ERR_INVALID_STATE si no), y el install lo hace EspUsbHost
// DENTRO de su tarea: al volver de begin() puede no estar hecho todavia. Por
// eso se reintenta durante readyTimeoutMs. 2 s es de sobra (el install es lo
// primero que hace esa tarea); se deja parametrizable pero conviene no subirlo:
// esto corre en setup() y bloquea el arranque del resto del firmware.
// ---------------------------------------------------------------------------
bool xinputHostBegin(XInputReportCb onReport,
                     XInputMountCb  onMount   = 0,
                     XInputUmountCb onUmount  = 0,
                     uint32_t       readyTimeoutMs = 2000);

// Numero de mandos XInput activos ahora mismo (diagnostico / pantalla).
uint8_t xinputHostPadCount();

// ---------------------------------------------------------------------------
// Piezas PURAS (sin USB, sin FreeRTOS): se compilan y verifican en el PC.
// ---------------------------------------------------------------------------

// Clasifica una interfaz por su descriptor. Clon de xinputh_open() de Ryzee119,
// incluida su condicion de bNumEndpoints >= 2.
XInputType xinputClassifyInterface(uint8_t bInterfaceClass,
                                   uint8_t bInterfaceSubClass,
                                   uint8_t bInterfaceProtocol,
                                   uint8_t bNumEndpoints);

// Trocea un paquete de interrupt-IN. Clon de xinputh_xfer_cb() (rama TUSB_DIR_IN)
// con comprobaciones de longitud anadidas: alli el buffer era fijo de 32 bytes y
// se indexaba a pelo; aqui entra lo que diga actual_num_bytes, que puede ser corto.
// Los punteros de salida solo se tocan si el retorno es XINPUT_PARSE_PAD.
XInputParse xinputParseReport(XInputType type, const uint8_t* data, size_t len,
                              uint16_t* wButtons, int16_t* thumbLX, int16_t* thumbLY);

#endif // XINPUTHOST_H
