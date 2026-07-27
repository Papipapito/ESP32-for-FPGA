// MsxHid.cpp - ver MsxHid.h. Port FIEL de MSXnano/fpga/rp2040/src/usbin.c;
// cada bloque referencia la funcion del RP2040 que clona. Si algo aqui se
// desvia del original es un BUG, no una mejora: el FPGA no cambia.
//
// C++11 portable, sin Arduino: este mismo fichero se compila en el PC y el
// banco de pruebas (host_test/test_msxhid.cpp) verifica los bytes emitidos.
#include "MsxHid.h"

#include <string.h>

namespace {

// ===========================================================================
// Tabla usage HID -> celda MSX. COPIA LITERAL de rp2040/inc/keymaps.h, con
// una sola diferencia deliberada: F11 (0x44) ya NO mapea al comando 0x04.
//
// C++11 no admite inicializadores designados (`[0x04] = ...`, que son C99), asi
// que la tabla se escribe como lista de pares y se despliega a 256 bytes la
// primera vez que se consulta. Los valores se dejan en la forma
// 0x80 | (bit<<4) | fila para que sigan siendo auditables de un vistazo.
//
// Coordenadas: traductor verificado MSXnano/fpga/src/usb/usb_keyboard_msx.vhd,
// distribucion Internacional/US.
//
// OJO: los modificadores (SHIFT/CTRL/GRAPH/CODE, fila 6) NO estan aqui. Salen
// del byte de modificadores HID, no de un usage. CapsLock (0x39) SI es una
// tecla normal (fila6 bit3) y se queda.
// ===========================================================================
struct KeyMapEntry { uint8_t usage; uint8_t cell; };

const KeyMapEntry kKeyMap[] = {
    // ---- Letras (a-z), filas 2..5 ----
    { 0x04, 0x80 | (6 << 4) | 2 },  // 'a' -> fila2 bit6  (0xE2)
    { 0x05, 0x80 | (7 << 4) | 2 },  // 'b' -> fila2 bit7  (0xF2)
    { 0x06, 0x80 | (0 << 4) | 3 },  // 'c' -> fila3 bit0  (0x83)
    { 0x07, 0x80 | (1 << 4) | 3 },  // 'd' -> fila3 bit1  (0x93)
    { 0x08, 0x80 | (2 << 4) | 3 },  // 'e' -> fila3 bit2  (0xA3)
    { 0x09, 0x80 | (3 << 4) | 3 },  // 'f' -> fila3 bit3  (0xB3)
    { 0x0A, 0x80 | (4 << 4) | 3 },  // 'g' -> fila3 bit4  (0xC3)
    { 0x0B, 0x80 | (5 << 4) | 3 },  // 'h' -> fila3 bit5  (0xD3)
    { 0x0C, 0x80 | (6 << 4) | 3 },  // 'i' -> fila3 bit6  (0xE3)
    { 0x0D, 0x80 | (7 << 4) | 3 },  // 'j' -> fila3 bit7  (0xF3)
    { 0x0E, 0x80 | (0 << 4) | 4 },  // 'k' -> fila4 bit0  (0x84)
    { 0x0F, 0x80 | (1 << 4) | 4 },  // 'l' -> fila4 bit1  (0x94)
    { 0x10, 0x80 | (2 << 4) | 4 },  // 'm' -> fila4 bit2  (0xA4)
    { 0x11, 0x80 | (3 << 4) | 4 },  // 'n' -> fila4 bit3  (0xB4)
    { 0x12, 0x80 | (4 << 4) | 4 },  // 'o' -> fila4 bit4  (0xC4)
    { 0x13, 0x80 | (5 << 4) | 4 },  // 'p' -> fila4 bit5  (0xD4)
    { 0x14, 0x80 | (6 << 4) | 4 },  // 'q' -> fila4 bit6  (0xE4)
    { 0x15, 0x80 | (7 << 4) | 4 },  // 'r' -> fila4 bit7  (0xF4)
    { 0x16, 0x80 | (0 << 4) | 5 },  // 's' -> fila5 bit0  (0x85)
    { 0x17, 0x80 | (1 << 4) | 5 },  // 't' -> fila5 bit1  (0x95)
    { 0x18, 0x80 | (2 << 4) | 5 },  // 'u' -> fila5 bit2  (0xA5)
    { 0x19, 0x80 | (3 << 4) | 5 },  // 'v' -> fila5 bit3  (0xB5)
    { 0x1A, 0x80 | (4 << 4) | 5 },  // 'w' -> fila5 bit4  (0xC5)
    { 0x1B, 0x80 | (5 << 4) | 5 },  // 'x' -> fila5 bit5  (0xD5)
    { 0x1C, 0x80 | (6 << 4) | 5 },  // 'y' -> fila5 bit6  (0xE5)
    { 0x1D, 0x80 | (7 << 4) | 5 },  // 'z' -> fila5 bit7  (0xF5)

    // ---- Digitos de la fila superior, filas 0 y 1 ----
    { 0x1E, 0x80 | (1 << 4) | 0 },  // '1' -> fila0 bit1  (0x90)
    { 0x1F, 0x80 | (2 << 4) | 0 },  // '2' -> fila0 bit2  (0xA0)
    { 0x20, 0x80 | (3 << 4) | 0 },  // '3' -> fila0 bit3  (0xB0)
    { 0x21, 0x80 | (4 << 4) | 0 },  // '4' -> fila0 bit4  (0xC0)
    { 0x22, 0x80 | (5 << 4) | 0 },  // '5' -> fila0 bit5  (0xD0)
    { 0x23, 0x80 | (6 << 4) | 0 },  // '6' -> fila0 bit6  (0xE0)
    { 0x24, 0x80 | (7 << 4) | 0 },  // '7' -> fila0 bit7  (0xF0)
    { 0x25, 0x80 | (0 << 4) | 1 },  // '8' -> fila1 bit0  (0x81)
    { 0x26, 0x80 | (1 << 4) | 1 },  // '9' -> fila1 bit1  (0x91)
    { 0x27, 0x80 | (0 << 4) | 0 },  // '0' -> fila0 bit0  (0x80)

    // ---- Caracteres de la fila 1 ----
    { 0x2D, 0x80 | (2 << 4) | 1 },  // '-' '_' -> fila1 bit2  (0xA1)
    { 0x2E, 0x80 | (3 << 4) | 1 },  // '=' '+' -> fila1 bit3  (0xB1)
    { 0x31, 0x80 | (4 << 4) | 1 },  // '\' '|' (backslash US, HID 0x31) -> fila1 bit4 (0xC1)
    { 0x2F, 0x80 | (5 << 4) | 1 },  // '[' '{' -> fila1 bit5  (0xD1)
    { 0x30, 0x80 | (6 << 4) | 1 },  // ']' '}' -> fila1 bit6  (0xE1)
    { 0x33, 0x80 | (7 << 4) | 1 },  // ';' ':' -> fila1 bit7  (0xF1)

    // ---- Fila 2: puntuacion / tecla muerta ----
    { 0x34, 0x80 | (0 << 4) | 2 },  // '\'' '"' -> fila2 bit0  (0x82)
    { 0x35, 0x80 | (1 << 4) | 2 },  // '`' '~'  -> fila2 bit1  (0x92)
    { 0x36, 0x80 | (2 << 4) | 2 },  // ',' '<'  -> fila2 bit2  (0xA2)
    { 0x37, 0x80 | (3 << 4) | 2 },  // '.' '>'  -> fila2 bit3  (0xB2)
    { 0x38, 0x80 | (4 << 4) | 2 },  // '/' '?'  -> fila2 bit4  (0xC2)
    { 0x32, 0x80 | (5 << 4) | 2 },  // '#'/'~' no-US (HID 0x32, acento) -> fila2 bit5 (0xD2)

    // ---- CapsLock como CAPS del MSX (fila 6, bit3) ----
    { 0x39, 0x80 | (3 << 4) | 6 },  // CapsLock -> fila6 bit3  (0xB6)

    // ---- Espacio / Enter / Esc / Tab / BS / Stop / Select ----
    { 0x2C, 0x80 | (0 << 4) | 8 },  // Espacio    -> fila8 bit0  (0x88)
    { 0x28, 0x80 | (7 << 4) | 7 },  // Enter      -> fila7 bit7  (0xF7)
    { 0x29, 0x80 | (2 << 4) | 7 },  // Esc        -> fila7 bit2  (0xA7)
    { 0x2B, 0x80 | (3 << 4) | 7 },  // Tab        -> fila7 bit3  (0xB7)
    { 0x2A, 0x80 | (5 << 4) | 7 },  // Backspace  -> fila7 bit5  (0xD7)
    { 0x47, 0x80 | (4 << 4) | 7 },  // ScrollLock -> STOP   fila7 bit4 (0xC7)
    { 0x4D, 0x80 | (6 << 4) | 7 },  // End        -> SELECT fila7 bit6 (0xE7)

    // ---- Flechas + Home / Ins / Del (fila 8) ----
    { 0x4F, 0x80 | (7 << 4) | 8 },  // Derecha -> fila8 bit7 (0xF8)
    { 0x51, 0x80 | (6 << 4) | 8 },  // Abajo   -> fila8 bit6 (0xE8)
    { 0x52, 0x80 | (5 << 4) | 8 },  // Arriba  -> fila8 bit5 (0xD8)
    { 0x50, 0x80 | (4 << 4) | 8 },  // Izqda   -> fila8 bit4 (0xC8)
    { 0x4C, 0x80 | (3 << 4) | 8 },  // Delete  -> fila8 bit3 (0xB8)
    { 0x49, 0x80 | (2 << 4) | 8 },  // Insert  -> fila8 bit2 (0xA8)
    { 0x4A, 0x80 | (1 << 4) | 8 },  // Home    -> fila8 bit1 (0x98)

    // ---- Teclado numerico (filas 9 y 10) ----
    { 0x5C, 0x80 | (7 << 4) | 9 },  // KP 4 -> fila9 bit7 (0xF9)
    { 0x5B, 0x80 | (6 << 4) | 9 },  // KP 3 -> fila9 bit6 (0xE9)
    { 0x5A, 0x80 | (5 << 4) | 9 },  // KP 2 -> fila9 bit5 (0xD9)
    { 0x59, 0x80 | (4 << 4) | 9 },  // KP 1 -> fila9 bit4 (0xC9)
    { 0x62, 0x80 | (3 << 4) | 9 },  // KP 0 -> fila9 bit3 (0xB9)
    { 0x54, 0x80 | (2 << 4) | 9 },  // KP / -> fila9 bit2 (0xA9)
    { 0x57, 0x80 | (1 << 4) | 9 },  // KP + -> fila9 bit1 (0x99)
    { 0x55, 0x80 | (0 << 4) | 9 },  // KP * -> fila9 bit0 (0x89)

    { 0x63, 0x80 | (7 << 4) | 10 }, // KP . -> fila10 bit7 (0xFA)
    { 0x56, 0x80 | (5 << 4) | 10 }, // KP - -> fila10 bit5 (0xDA)
    { 0x61, 0x80 | (4 << 4) | 10 }, // KP 9 -> fila10 bit4 (0xCA)
    { 0x60, 0x80 | (3 << 4) | 10 }, // KP 8 -> fila10 bit3 (0xBA)
    { 0x5F, 0x80 | (2 << 4) | 10 }, // KP 7 -> fila10 bit2 (0xAA)
    { 0x5E, 0x80 | (1 << 4) | 10 }, // KP 6 -> fila10 bit1 (0x9A)
    { 0x5D, 0x80 | (0 << 4) | 10 }, // KP 5 -> fila10 bit0 (0x8A)

    // ---- F1..F5 -> celdas de matriz (filas 6/7 del VHDL) ----
    { 0x3A, 0x80 | (5 << 4) | 6 },  // F1 -> fila6 bit5 (0xD6)
    { 0x3B, 0x80 | (6 << 4) | 6 },  // F2 -> fila6 bit6 (0xE6)
    { 0x3C, 0x80 | (7 << 4) | 6 },  // F3 -> fila6 bit7 (0xF6)
    { 0x3D, 0x80 | (0 << 4) | 7 },  // F4 -> fila7 bit0 (0x87)
    { 0x3E, 0x80 | (1 << 4) | 7 },  // F5 -> fila7 bit1 (0x97)

    // ---- F6, F7 -> celdas STOP / GRAPH (se conserva el comportamiento viejo) ----
    { 0x3F, 0x80 | (4 << 4) | 7 },  // F6 -> fila7 bit4 (0xC7) STOP
    { 0x40, 0x80 | (2 << 4) | 6 },  // F7 -> fila6 bit2 (0xA6) GRAPH

    // ---- F8..F12 -> comandos crudos (bit7==0), solo en el flanco de pulsacion ----
    //   0x01 scanline y 0x03 OSD se decodifican pero estan sin conectar (no-op).
    { 0x41, 0x01 },                 // F8  -> comando 0x01 (scanline, no-op)
    { 0x42, 0x03 },                 // F9  -> comando 0x03 (OSD, no-op)
    { 0x43, 0x03 },                 // F10 -> comando 0x03 (OSD, no-op)
    // { 0x44, 0x04 },              // F11 -> TURBO: ELIMINADO A PROPOSITO.
    //   El turbo ya no se cambia desde el teclado (ni el fisico ni el USB): va
    //   por OUT &H41 y por el menu de arranque. Reponer esta linea reintroduce
    //   un cambio de velocidad de CPU fantasma. Ver la cabecera de MsxHid.h.
    { 0x45, 0x02 },                 // F12 -> comando 0x02 (reset, no-op)
};

// Despliegue perezoso a 256 bytes. El static local da inicializacion unica y
// thread-safe en C++11 y evita el lio de orden de inicializacion de estaticos
// (el objeto MsxHid del sketch es global y podria construirse antes).
struct KeymapTable {
    uint8_t cell[256];
    KeymapTable() {
        memset(cell, 0, sizeof(cell));
        for (size_t i = 0; i < sizeof(kKeyMap) / sizeof(kKeyMap[0]); i++)
            cell[kKeyMap[i].usage] = kKeyMap[i].cell;
    }
};

const KeymapTable& keymap() {
    static const KeymapTable t;
    return t;
}

// Bits logicos de los modificadores derivados (equivalen a DRV_* de usbin.c).
const uint8_t DRV_SHIFT = 0x01;
const uint8_t DRV_CTRL  = 0x02;
const uint8_t DRV_GRAPH = 0x04;
const uint8_t DRV_CODE  = 0x08;

// derive_modifiers(): empaqueta el byte de modificadores HID en los 4
// modificadores logicos del MSX, haciendo OR de las variantes izqda/dcha para
// que soltar UN shift teniendo el otro pulsado NO suelte el SHIFT del MSX.
//   bits HID: 0 LCtrl,1 LShift,2 LAlt,3 LGUI,4 RCtrl,5 RShift,6 RAlt,7 RGUI
//   SHIFT = bits 1|5 (0x22)   CTRL = bits 0|4 (0x11)
//   GRAPH = bit  2   (0x04)   CODE = bit  6   (0x40)
inline uint8_t deriveModifiers(uint8_t mods) {
    uint8_t d = 0;
    if (mods & 0x22) d |= DRV_SHIFT;
    if (mods & 0x11) d |= DRV_CTRL;
    if (mods & 0x04) d |= DRV_GRAPH;
    if (mods & 0x40) d |= DRV_CODE;
    return d;
}

// Umbral digital sobre el eje YA ESCALADO. scaleAxis() centra el eje en 0 y lo
// justifica a un signo de 16 bits (+-32767) sea cual sea su ancho nativo, asi
// que un umbral fijo == "la mitad del rango logico" para cualquier resolucion.
const int32_t JOY_AXIS_T = 16384;

// to_signed_value() del RP2040, con el mismo orden de operaciones.
// UNICA divergencia: se protege el desplazamiento para ejes de mas de 16 bits
// (16 - bitSize seria negativo = comportamiento indefinido en C++). Ningun eje
// real pasa de 16 bits, asi que para cualquier mando el resultado es identico.
int32_t scaleAxis(const MsxJoyAxis& a) {
    int32_t value  = a.value;
    int32_t midval = ((a.logicalMax - a.logicalMin) >> 1) + 1;
    value -= midval;
    if (a.bitSize < 16) value <<= (16 - a.bitSize);
    if (value >  32767) value =  32767;
    if (value < -32767) value = -32767;
    return value;
}

}  // namespace

// ===========================================================================
// Construccion / arranque
// ===========================================================================
MsxHid::MsxHid()
    : m_emit(0), m_prevDerived(0), m_afPhase(false), m_afNextMs(0),
      m_resyncNextMs(0), m_started(false) {
    memset(m_matrix, 0xFF, sizeof(m_matrix));   // activo-bajo: todo suelto
    memset(m_prevKeys, 0, sizeof(m_prevKeys));
    m_joyState[0] = m_joyState[1] = 0;          // activo-alto: nada pulsado
    m_joyBase[0]  = m_joyBase[1]  = 0;
    m_joyAf[0]    = m_joyAf[1]    = 0;
}

// kb_uart_init(): deja la sombra en reposo y NO manda nada. El primer resync
// sale 250 ms despues del primer tick(), igual que en el bucle del RP2040.
void MsxHid::begin(MsxHidEmit emit) {
    m_emit = emit;
    memset(m_matrix, 0xFF, sizeof(m_matrix));
    clearKeyboardState();
    m_joyState[0] = m_joyState[1] = 0;
    m_joyBase[0]  = m_joyBase[1]  = 0;
    m_joyAf[0]    = m_joyAf[1]    = 0;
    m_afPhase = false;
    m_started = false;
}

void MsxHid::clearKeyboardState() {
    memset(m_prevKeys, 0, sizeof(m_prevKeys));
    m_prevDerived = 0;
}

// ===========================================================================
// Salida de bytes
// ===========================================================================
void MsxHid::emit(const uint8_t* data, size_t len) {
    if (m_emit) m_emit(data, len);
}

// emit_event(): manda MAKE/BREAK de una celda y actualiza la sombra local.
// La sombra es la que viaja en el resync, asi que TIENE que seguir al evento.
void MsxHid::emitEvent(uint8_t op, uint8_t cell) {
    uint8_t row = (uint8_t)(cell & 0x0F);
    uint8_t bit = (uint8_t)((cell >> 4) & 0x07);
    if (row < MSXHID_ROWS) {
        if (op == MSXHID_OP_MAKE) m_matrix[row] &= (uint8_t)~(1u << bit);  // pulsada -> 0
        else                      m_matrix[row] |=  (uint8_t) (1u << bit); // suelta  -> 1
    }
    uint8_t msg[2] = { op, cell };
    emit(msg, 2);
}

// emit_command(): opcode crudo con bit7==0. El 0x04 (turbo) se filtra AQUI
// ademas de estar fuera de la tabla: si alguien reintroduce el mapeo por error,
// el byte sigue sin llegar al FPGA (ver cabecera de MsxHid.h).
void MsxHid::emitCommand(uint8_t cmd) {
    if (cmd == MSXHID_CMD_FORBIDDEN_TURBO) return;
    emit(&cmd, 1);
}

// kb_send_resync(): el anuncio de version viaja con CADA resync, asi que el
// FPGA siempre acaba enterandose de la version aunque se pierda un byte.
void MsxHid::sendKbResync() {
    uint8_t frame[2 + 1 + MSXHID_ROWS + 1];
    size_t  n = 0;
    frame[n++] = MSXHID_OP_VERSION;
    frame[n++] = (uint8_t)MSXHID_FW_VERSION;
    frame[n++] = MSXHID_RESYNC_START;
    for (uint8_t r = 0; r < MSXHID_ROWS; r++) frame[n++] = m_matrix[r];
    frame[n++] = MSXHID_RESYNC_END;
    emit(frame, n);
}

// joy_send_resync(): reemite los DOS puertos sin pasar por send-on-change
// (auto-curacion de un 0xB0 perdido).
void MsxHid::sendJoyResync() {
    uint8_t frame[6];
    frame[0] = MSXHID_OP_JOY; frame[1] = 0; frame[2] = m_joyState[0];
    frame[3] = MSXHID_OP_JOY; frame[4] = 1; frame[5] = m_joyState[1];
    emit(frame, 6);
}

// joy_emit(): send-on-change. Sin el, el autofire inundaria el enlace a 10 Hz
// con bytes identicos.
void MsxHid::joyEmit(uint8_t port, uint8_t value) {
    if (port > 1 || value == m_joyState[port]) return;
    m_joyState[port] = value;
    uint8_t msg[3] = { MSXHID_OP_JOY, port, value };
    emit(msg, 3);
}

// ===========================================================================
// Teclado
// ===========================================================================

// tuh_hid_mount_cb() rama teclado: linea base limpia en el FPGA.
void MsxHid::keyboardAttached() {
    memset(m_matrix, 0xFF, sizeof(m_matrix));
    clearKeyboardState();
    sendKbResync();
}

// tuh_hid_umount_cb(): teclado fuera -> soltar todo o se queda una tecla clavada.
void MsxHid::keyboardDetached() {
    memset(m_matrix, 0xFF, sizeof(m_matrix));
    clearKeyboardState();
    sendKbResync();
}

// kb_report_receive(): traduccion make/break dirigida por eventos.
// El FPGA es el dueno de la matriz y el BIOS del MSX hace la autorepeticion, asi
// que aqui NUNCA se auto-suelta una tecla ni se bloquea esperando nada.
void MsxHid::keyboardReport(uint8_t modifiers, const uint8_t* keys, size_t len) {
    if (keys == 0) len = 0;
    if (len > MSXHID_MAX_KEYS) len = MSXHID_MAX_KEYS;   // rollover efectivo del RP2040

    // ---- 1. Modificadores: se diffean los 4 bits LOGICOS, no los bits HID ----
    uint8_t derived = deriveModifiers(modifiers);
    if (derived != m_prevDerived) {
        uint8_t changed = (uint8_t)(derived ^ m_prevDerived);
        if (changed & DRV_SHIFT)
            emitEvent((derived & DRV_SHIFT) ? MSXHID_OP_MAKE : MSXHID_OP_BREAK, MSXHID_CELL_SHIFT);
        if (changed & DRV_CTRL)
            emitEvent((derived & DRV_CTRL)  ? MSXHID_OP_MAKE : MSXHID_OP_BREAK, MSXHID_CELL_CTRL);
        if (changed & DRV_GRAPH)
            emitEvent((derived & DRV_GRAPH) ? MSXHID_OP_MAKE : MSXHID_OP_BREAK, MSXHID_CELL_GRAPH);
        if (changed & DRV_CODE)
            emitEvent((derived & DRV_CODE)  ? MSXHID_OP_MAKE : MSXHID_OP_BREAK, MSXHID_CELL_CODE);
        m_prevDerived = derived;
    }

    // ---- 2. BREAKs: teclas que estaban y ya no ----
    for (size_t i = 0; i < MSXHID_MAX_KEYS; i++) {
        uint8_t k = m_prevKeys[i];
        if (k == 0) continue;
        bool stillDown = false;
        for (size_t j = 0; j < len; j++) {
            if (keys[j] == k) { stillDown = true; break; }
        }
        if (!stillDown) {
            uint8_t cell = cellForKey(k);
            if (cell & 0x80) emitEvent(MSXHID_OP_BREAK, cell);  // los comandos no tienen break
        }
    }

    // ---- 3. MAKEs: teclas nuevas ----
    for (size_t i = 0; i < len; i++) {
        uint8_t k = keys[i];
        if (k == 0) continue;
        bool wasDown = false;
        for (size_t j = 0; j < MSXHID_MAX_KEYS; j++) {
            if (m_prevKeys[j] == k) { wasDown = true; break; }
        }
        if (!wasDown) {
            uint8_t cell = cellForKey(k);
            if (cell & 0x80) emitEvent(MSXHID_OP_MAKE, cell);   // celda de matriz
            else if (cell)   emitCommand(cell);                 // comando (F8..F12)
        }
    }

    // ---- 4. Foto del nuevo conjunto ----
    memset(m_prevKeys, 0, sizeof(m_prevKeys));
    if (len) memcpy(m_prevKeys, keys, len);
}

// Aplana el mapa de bits NKRO (1 bit por usage) a la lista plana que espera
// keyboardReport(). Se SALTAN los usages 0xE0..0xE7 (los modificadores): ya
// vienen por el byte de modificadores y, si no, gastarian huecos del rollover.
// El orden es ascendente por usage = estable entre reports (no da tirones).
void MsxHid::keyboardBitmap(uint8_t modifiers, const uint8_t* bitmap, size_t bitmapLen) {
    uint8_t flat[MSXHID_MAX_KEYS];
    size_t  n = 0;
    if (bitmap) {
        for (size_t byteIdx = 0; byteIdx < bitmapLen && n < MSXHID_MAX_KEYS; byteIdx++) {
            uint8_t b = bitmap[byteIdx];
            if (!b) continue;
            for (uint8_t bit = 0; bit < 8 && n < MSXHID_MAX_KEYS; bit++) {
                if (!(b & (uint8_t)(1u << bit))) continue;
                size_t usage = byteIdx * 8 + bit;
                if (usage == 0 || usage > 0xFF) continue;
                if (usage >= 0xE0 && usage <= 0xE7) continue;   // modificadores
                flat[n++] = (uint8_t)usage;
            }
        }
    }
    keyboardReport(modifiers, flat, n);
}

// ===========================================================================
// Joystick
// ===========================================================================

// Solo LATCHEA la intencion: el emisor es tick(). Esto es deliberado y viene
// del RP2040: un mando pulsado pero quieto no manda reports nuevos, asi que un
// autofire movido por callbacks se congelaria con el boton apretado.
void MsxHid::joystickInput(uint8_t port, uint8_t base, uint8_t af) {
    if (port > 1) return;
    m_joyBase[port] = base;
    m_joyAf[port]   = af;
}

void MsxHid::joystickAttached(uint8_t port) {
    if (port > 1) return;
    m_joyBase[port] = 0;
    m_joyAf[port]   = 0;
    joyEmit(port, 0);   // linea base limpia: ni direccion ni disparo
}

void MsxHid::joystickDetached(uint8_t port) {
    if (port > 1) return;
    m_joyBase[port] = 0;
    m_joyAf[port]   = 0;
    joyEmit(port, 0);   // soltar, o se queda una direccion clavada en el MSX
}

// ===========================================================================
// Tiempo: autofire + resync (bucle principal del RP2040, main.c)
// ===========================================================================
void MsxHid::tick(uint32_t now_ms) {
    if (!m_started) {                       // armado perezoso de los vencimientos
        m_started      = true;
        m_afNextMs     = now_ms + MSXHID_AF_HALF_MS;
        m_resyncNextMs = now_ms + MSXHID_RESYNC_MS;
    }

    // ---- joy_autofire_tick(): onda cuadrada + composicion de los dos puertos ----
    // Las restas en int32_t sobreviven al vuelco de millis() a los ~49 dias.
    if ((int32_t)(now_ms - m_afNextMs) >= 0) {
        m_afPhase  = !m_afPhase;
        m_afNextMs = now_ms + MSXHID_AF_HALF_MS;   // = ahora+medio (no +=): sin rafagas
    }
    for (uint8_t p = 0; p < 2; p++) {
        uint8_t out = m_joyBase[p];
        if (m_afPhase) out |= m_joyAf[p];          // en esta fase, los armados disparan
        joyEmit(p, out);                           // send-on-change
    }

    // ---- Resync periodico: OBLIGATORIO (watchdog de ~1 s del FPGA) ----
    if ((int32_t)(now_ms - m_resyncNextMs) >= 0) {
        sendKbResync();
        sendJoyResync();
        m_resyncNextMs = now_ms + MSXHID_RESYNC_MS;
    }
}

// ===========================================================================
// Traductores puros
// ===========================================================================
uint8_t MsxHid::cellForKey(uint8_t usage) {
    return keymap().cell[usage];
}

// gamepad_report_receive(): mando HID generico -> byte MSX activo-alto.
//  * Parseo por USAGE, sin trucos por VID/PID: vale para cualquier mando.
//  * Ejes X/Y umbralizados a digital; el hat se OR-ea con el resultado, asi que
//    mandos con cruceta, con stick o con los dos funcionan igual.
//  * Botones 1 y 2 -> disparo A y B. Botones 3 y 4 -> ARMAN el autofire de A y B.
void MsxHid::decodeGamepad(const MsxJoyInput& in, uint8_t* base, uint8_t* af) {
    uint8_t b = 0;

    if (in.x.present) {
        int32_t x = scaleAxis(in.x);
        if (x < -JOY_AXIS_T) b |= MSXHID_JOY_LEFT;
        if (x >  JOY_AXIS_T) b |= MSXHID_JOY_RIGHT;
    }
    if (in.y.present) {
        int32_t y = scaleAxis(in.y);
        if (y < -JOY_AXIS_T) b |= MSXHID_JOY_UP;    // en HID +Y es abajo
        if (y >  JOY_AXIS_T) b |= MSXHID_JOY_DOWN;
    }

    if (in.hatPresent) {
        // Normalizado a 0..7 con el minimo logico (hay mandos que dan 1..8).
        int32_t hat = in.hat - in.hatLogicalMin;
        switch (hat) {
            case 0: b |= MSXHID_JOY_UP;                       break;  // N
            case 1: b |= MSXHID_JOY_UP   | MSXHID_JOY_RIGHT;  break;  // NE
            case 2: b |= MSXHID_JOY_RIGHT;                    break;  // E
            case 3: b |= MSXHID_JOY_DOWN | MSXHID_JOY_RIGHT;  break;  // SE
            case 4: b |= MSXHID_JOY_DOWN;                     break;  // S
            case 5: b |= MSXHID_JOY_DOWN | MSXHID_JOY_LEFT;   break;  // SW
            case 6: b |= MSXHID_JOY_LEFT;                     break;  // W
            case 7: b |= MSXHID_JOY_UP   | MSXHID_JOY_LEFT;   break;  // NW
            default: break;                                           // >=8 = centrado
        }
    }

    if (in.buttons & 0x01) b |= MSXHID_JOY_A;   // boton 1 -> disparo A
    if (in.buttons & 0x02) b |= MSXHID_JOY_B;   // boton 2 -> disparo B

    uint8_t a = 0;
    if (in.buttons & 0x04) a |= MSXHID_JOY_A;   // boton 3 -> autofire A
    if (in.buttons & 0x08) a |= MSXHID_JOY_B;   // boton 4 -> autofire B

    if (base) *base = b;
    if (af)   *af   = a;
}

// tuh_xinput_report_received_cb(): mando XInput (Xbox) -> mismo byte MSX.
// Los valores de wButtons son los de xinput_host.h (Ryzee119).
// NOTA: hoy no hay transporte XInput en el S3 (EspUsbHost no lo cubre, ver
// UsbHost.ino); esta funcion existe para conservar el mapeo verificado y
// poder engancharlo el dia que haya driver, sin volver a decidir nada.
void MsxHid::decodeXInput(uint16_t wButtons, int16_t thumbLX, int16_t thumbLY,
                          uint8_t* base, uint8_t* af) {
    uint8_t b = 0;
    if (wButtons & 0x0008) b |= MSXHID_JOY_RIGHT;   // DPAD_RIGHT
    if (wButtons & 0x0004) b |= MSXHID_JOY_LEFT;    // DPAD_LEFT
    if (wButtons & 0x0002) b |= MSXHID_JOY_DOWN;    // DPAD_DOWN
    if (wButtons & 0x0001) b |= MSXHID_JOY_UP;      // DPAD_UP
    // Stick izquierdo: en XInput +Y es arriba. Zona muerta a media escala.
    if (thumbLX >  JOY_AXIS_T) b |= MSXHID_JOY_RIGHT;
    if (thumbLX < -JOY_AXIS_T) b |= MSXHID_JOY_LEFT;
    if (thumbLY < -JOY_AXIS_T) b |= MSXHID_JOY_DOWN;
    if (thumbLY >  JOY_AXIS_T) b |= MSXHID_JOY_UP;
    if (wButtons & 0x1000) b |= MSXHID_JOY_A;       // A -> disparo 1
    if (wButtons & 0x2000) b |= MSXHID_JOY_B;       // B -> disparo 2

    uint8_t a = 0;                                   // X/Y arman el autofire
    if (wButtons & 0x4000) a |= MSXHID_JOY_A;       // X
    if (wButtons & 0x8000) a |= MSXHID_JOY_B;       // Y

    if (base) *base = b;
    if (af)   *af   = a;
}

// ===========================================================================
// Introspeccion
// ===========================================================================
uint8_t MsxHid::matrixRow(uint8_t row) const {
    return (row < MSXHID_ROWS) ? m_matrix[row] : 0xFF;
}

uint8_t MsxHid::joyState(uint8_t port) const {
    return (port < 2) ? m_joyState[port] : 0;
}

uint8_t MsxHid::joyBase(uint8_t port) const {
    return (port < 2) ? m_joyBase[port] : 0;
}
