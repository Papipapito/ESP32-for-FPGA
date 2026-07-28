// ============================================================================
// MsxHid - Nucleo del companion de teclado/joystick USB del MSXnano.
//
// Port del firmware RP2040 (MSXnano/fpga/rp2040/src/usbin.c) a C++11 PORTABLE:
// SIN Arduino, SIN Serial, SIN millis(), SIN delay(). Toda la logica pura vive
// aqui (matriz 11x8, keymap, modificadores derivados, autofire, construccion de
// los mensajes) y los bytes salen por un CALLBACK, asi que este mismo fichero se
// compila en el PC y se verifica byte a byte (host_test/), igual que tsx2cvs.
//
// El tiempo ENTRA como parametro: tick(now_ms). Nada de relojes internos.
//
// ---------------------------------------------------------------------------
// CONTRATO CONGELADO CON EL FPGA (fpga/src/kbd_uart_rx.v). No se toca.
// ---------------------------------------------------------------------------
//   UART 115200 8N1, TX-only ESP -> FPGA (en el S3: UART2 TX = GPIO42 -> pin 31).
//   celda        = 0x80 | (bit<<4) | fila     fila 0..10, bit 0..7
//   0x90 <celda> = MAKE  (pulsar  -> el bit de la matriz pasa a 0)
//   0xA0 <celda> = BREAK (soltar  -> el bit pasa a 1)     [matriz ACTIVO-BAJO]
//   comandos (bit7==0): 0x01 scanline, 0x02 reset, 0x03 OSD.
//                       0x04 (turbo) PROHIBIDO -- ver mas abajo.
//   0xB0 <puerto> <byte>  joystick USB. puerto 0/1. byte ACTIVO-ALTO:
//                       bit0=R bit1=L bit2=D bit3=U bit4=A bit5=B
//   0xC0 <version>        anuncio de version del firmware
//   0xFE m0..m10 0xFF     resync de la matriz completa (11 filas activo-bajo)
//
// El MAKE/BREAK va en un byte SEPARADO delante de la celda a proposito: asi una
// celda que valga 0xA0 (el '2', fila0 bit2) no se confunde nunca con el opcode
// BREAK. Son posicionales en el hilo, no auto-descriptivos.
//
// EL RESYNC DE 250 ms ES OBLIGATORIO: el FPGA tiene un watchdog de ~1 s que
// suelta TODAS las teclas y los dos joysticks si deja de recibir bytes. Si el
// bucle principal se bloquea mas de un segundo, al MSX se le sueltan las teclas.
//
// ---------------------------------------------------------------------------
// EL COMANDO 0x04 (turbo) NO SE EMITE NUNCA
// ---------------------------------------------------------------------------
// El viejo F11 mandaba 0x04 y el FPGA lo cableaba a config2_ff[4]. Se quito de
// los DOS teclados (fisico y USB): hoy el turbo se cambia por OUT &H41 y desde
// el menu de arranque. Un 0x04 suelto aqui volveria a cambiar la velocidad de
// la CPU a espaldas del usuario. Por eso F11 queda SIN MAPEAR en la tabla y
// ademas emitCommand() lo filtra defensivamente (cinturon y tirantes).
// ============================================================================
#ifndef MSXHID_H
#define MSXHID_H

#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Opcodes del protocolo (mismos nombres que usbin.c para poder diffear).
// ---------------------------------------------------------------------------
#define MSXHID_OP_MAKE      0x90
#define MSXHID_OP_BREAK     0xA0
#define MSXHID_OP_JOY       0xB0
#define MSXHID_OP_VERSION   0xC0
#define MSXHID_RESYNC_START 0xFE
#define MSXHID_RESYNC_END   0xFF

// Comando PROHIBIDO (turbo del viejo F11). Se define solo para poder filtrarlo.
#define MSXHID_CMD_FORBIDDEN_TURBO 0x04

// Bits del byte de joystick (ACTIVO-ALTO, tal cual viaja por el hilo).
#define MSXHID_JOY_RIGHT 0x01
#define MSXHID_JOY_LEFT  0x02
#define MSXHID_JOY_DOWN  0x04
#define MSXHID_JOY_UP    0x08
#define MSXHID_JOY_A     0x10
#define MSXHID_JOY_B     0x20

// Celdas de la matriz de los cuatro modificadores MSX (todas en la fila 6).
// NO salen de la tabla de keymap: se derivan del byte de modificadores HID.
#define MSXHID_CELL_SHIFT 0x86   // fila6 bit0
#define MSXHID_CELL_CTRL  0x96   // fila6 bit1
#define MSXHID_CELL_GRAPH 0xA6   // fila6 bit2
#define MSXHID_CELL_CODE  0xC6   // fila6 bit4

// Version anunciada con 0xC0. Se clona la del RP2040 (0x13 = v1.3): hoy el
// puerto fw_version del kbd_uart_rx queda SIN CONECTAR en top.v (la guarda de
// version lee FPGA_VERSION en el 0x2F), asi que el byte es informativo. Si
// algun dia se reconecta, hay que subirlo EN BLOQUE con FPGA_VERSION y el pack.
#ifndef MSXHID_FW_VERSION
#define MSXHID_FW_VERSION 0x13
#endif

// Cadencia del resync completo y semiperiodo del autofire, en milisegundos.
// 50 ms de semiperiodo = 10 Hz al 50%: >= 2 frames de PSG pulsado Y soltado
// incluso en un MSX PAL de 50 Hz, asi que no se pierde ni un disparo.
#define MSXHID_RESYNC_MS   250
#define MSXHID_AF_HALF_MS  50

// Filas utiles de la matriz (0..10) y tope de teclas simultaneas.
// 16 = el tamano de prev_keys[] del RP2040; se conserva para clonar el
// rollover efectivo (una NKRO da mas, pero el firmware viejo cortaba aqui).
#define MSXHID_ROWS      11
#define MSXHID_MAX_KEYS  16

// Callback de salida. Recibe un mensaje COMPLETO (2, 3 o 15 bytes); el flujo de
// bytes resultante es identico al del RP2040, que empujaba byte a byte.
typedef void (*MsxHidEmit)(const uint8_t* data, size_t len);

// ---------------------------------------------------------------------------
// Descripcion de un eje analogico tal y como lo publica el descriptor HID.
// Se pasa crudo para poder reproducir EXACTAMENTE to_signed_value() del RP2040
// (centrado + justificado a 16 bits) sin depender de la libreria USB.
// ---------------------------------------------------------------------------
struct MsxJoyAxis {
    bool    present;      // false = el mando no expone este eje
    int32_t value;        // valor ya extraido del report (con signo si logicalMin<0)
    int32_t logicalMin;
    int32_t logicalMax;
    uint8_t bitSize;      // ancho del campo en bits

    MsxJoyAxis() : present(false), value(0), logicalMin(0), logicalMax(0), bitSize(8) {}
};

// Entrada generica de un mando HID, ya despiezada por el llamante.
struct MsxJoyInput {
    MsxJoyAxis x;
    MsxJoyAxis y;
    bool     hatPresent;     // hat switch (usage 0x39)
    int32_t  hat;            // valor crudo
    int32_t  hatLogicalMin;  // algunos mandos numeran 1..8 en vez de 0..7
    uint32_t buttons;        // bit0 = boton 1, bit1 = boton 2, ...

    MsxJoyInput() : hatPresent(false), hat(0), hatLogicalMin(0), buttons(0) {}
};

// ---------------------------------------------------------------------------
// MsxHid: una instancia = una matriz virtual + dos puertos de joystick.
// Todos los metodos son no bloqueantes y no reservan memoria.
// ---------------------------------------------------------------------------
class MsxHid {
public:
    MsxHid();

    // Conecta el sumidero de bytes y deja todo en reposo (nada pulsado).
    // NO emite nada: el primer resync sale del primer tick() + 250 ms, igual
    // que en el RP2040 (donde next_resync se armaba antes del bucle).
    void begin(MsxHidEmit emit);

    // ---- Teclado -------------------------------------------------------
    // Teclado enchufado: linea base limpia en el FPGA (resync inmediato).
    void keyboardAttached();
    // Teclado desenchufado: suelta todo para que no se quede una tecla clavada.
    void keyboardDetached();
    // Report "plano": lista de usages HID pulsados (0 = hueco vacio) + el byte
    // de modificadores HID. Es la forma boot/6KRO y tambien la NKRO ya expandida.
    void keyboardReport(uint8_t modifiers, const uint8_t* keys, size_t len);
    // Report en mapa de bits (1 bit por usage, LSB primero), como lo entrega
    // EspUsbHostKeyboardState. Se aplana a lista y se reusa keyboardReport().
    void keyboardBitmap(uint8_t modifiers, const uint8_t* bitmap, size_t bitmapLen);

    // ---- Joystick ------------------------------------------------------
    // Late la INTENCION del mando; el emisor real es tick() (ver autofire).
    //   base = direcciones + disparos manuales (activo-alto)
    //   af   = que disparos (JOY_A/JOY_B) estan armados en autofire
    void joystickInput(uint8_t port, uint8_t base, uint8_t af);
    // Mando enchufado/desenchufado: linea base a cero (nada pulsado), inmediata.
    void joystickAttached(uint8_t port);
    void joystickDetached(uint8_t port);

    // ---- Tiempo --------------------------------------------------------
    // Llamar SIEMPRE desde el bucle principal, lo mas a menudo posible.
    // Hace dos cosas: mueve la onda cuadrada del autofire (y emite el byte de
    // joystick compuesto, send-on-change) y lanza el resync cada 250 ms.
    void tick(uint32_t now_ms);

    // ---- Traductores puros (estaticos: testeables sin instancia) --------
    // usage HID -> celda de matriz (bit7=1) o comando (bit7=0) o 0 = no mapeada.
    static uint8_t cellForKey(uint8_t usage);
    // Mando HID generico -> (base, autofire), clon de gamepad_report_receive().
    static void decodeGamepad(const MsxJoyInput& in, uint8_t* base, uint8_t* af);
    // Mando XInput (Xbox) -> (base, autofire), clon de tuh_xinput_report_received_cb().
    // El transporte en el S3 lo pone XInputHost.cpp (ver UsbHost.ino).
    static void decodeXInput(uint16_t wButtons, int16_t thumbLX, int16_t thumbLY,
                             uint8_t* base, uint8_t* af);

    // ---- Introspeccion (tests, pantalla de estado) ----------------------
    uint8_t matrixRow(uint8_t row) const;   // fila activo-baja (0xFF = todo suelto)
    uint8_t joyState(uint8_t port) const;   // ultimo byte TRANSMITIDO del puerto
    uint8_t joyBase(uint8_t port) const;    // capa base latcheada (sin autofire)

private:
    void emit(const uint8_t* data, size_t len);
    void emitEvent(uint8_t op, uint8_t cell);   // MAKE/BREAK + sombra de la matriz
    void emitCommand(uint8_t cmd);              // comando crudo (filtra el 0x04)
    void joyEmit(uint8_t port, uint8_t value);  // send-on-change del 0xB0
    void sendKbResync();                        // 0xC0 ver + 0xFE + 11 filas + 0xFF
    void sendJoyResync();                       // 0xB0 x2 (reemite ambos puertos)
    void clearKeyboardState();

    MsxHidEmit m_emit;

    uint8_t  m_matrix[MSXHID_ROWS];        // sombra ACTIVO-BAJA de las 11 filas
    uint8_t  m_prevKeys[MSXHID_MAX_KEYS];  // ultimo conjunto de usages visto
    uint8_t  m_prevDerived;                // ultimos modificadores logicos

    uint8_t  m_joyState[2];                // ultimo byte transmitido por puerto
    uint8_t  m_joyBase[2];                 // capa base (direcciones + disparo manual)
    uint8_t  m_joyAf[2];                   // capa armada para autofire

    bool     m_afPhase;                    // fase compartida de la onda cuadrada
    uint32_t m_afNextMs;                   // vencimiento del proximo cambio de fase
    uint32_t m_resyncNextMs;               // vencimiento del proximo resync
    bool     m_started;                    // los vencimientos se arman en el 1er tick
};

#endif // MSXHID_H
