// ============================================================================
// test_msxhid.cpp - Banco de pruebas de MsxHid en el PC (g++, sin Arduino).
//
// Verifica BYTE A BYTE lo que el companion S3 sacaria por el UART hacia el
// FPGA, contra el comportamiento del firmware RP2040 que sustituye. Ademas
// lleva un simulador del FSM de kbd_uart_rx.v (FpgaSim) para comprobar que el
// flujo es decodificable y que el estado que ve el FPGA coincide con la sombra
// interna de MsxHid: un fallo de sincronia entre ambas seria una tecla clavada
// en el MSX que los bytes sueltos no delatarian.
//
// Casos obligatorios: (a) A pulsada/soltada (b) Shift+2 (c) rollover
// (d) resync de 250 ms (e) joystick en los dos puertos (f) autofire
// (g) que NUNCA se emita el comando 0x04.
// ============================================================================
#include "../MsxHid.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Captura de la salida. El callback es una funcion libre (la API de MsxHid es
// un puntero a funcion a proposito: nada de std::function en un MCU).
// ---------------------------------------------------------------------------
static std::vector<uint8_t> g_cap;
static void capture(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) g_cap.push_back(data[i]);
}

// ---------------------------------------------------------------------------
// Contabilidad de casos: se ejecutan TODOS y se reporta PASS/FAIL de cada uno.
// ---------------------------------------------------------------------------
static int  g_caseFails = 0;
static int  g_totalFails = 0;
static const char* g_caseName = "";

static void beginCase(const char* name) {
    g_caseName  = name;
    g_caseFails = 0;
}
static bool endCase() {
    if (g_caseFails == 0) { printf("PASS  %s\n", g_caseName); return true; }
    printf("FAIL  %s (%d comprobacion(es))\n", g_caseName, g_caseFails);
    g_totalFails += g_caseFails;
    return false;
}

static void fail(const char* what) {
    printf("      x %s\n", what);
    g_caseFails++;
}

#define CHECK(cond, what)                                                     \
    do { if (!(cond)) fail(what); } while (0)

static std::string hex(const std::vector<uint8_t>& v) {
    std::string s;
    char b[8];
    for (size_t i = 0; i < v.size(); i++) {
        snprintf(b, sizeof(b), "%02X ", v[i]);
        s += b;
    }
    if (!s.empty()) s.erase(s.size() - 1);
    return s;
}

// Compara lo capturado con la secuencia esperada y VACIA la captura, para que
// cada paso del test se compare aislado.
static void expectBytes(const char* what, const std::vector<uint8_t>& want) {
    if (g_cap != want) {
        printf("      x %s\n         esperado: [%s]\n         obtenido: [%s]\n",
               what, hex(want).c_str(), hex(g_cap).c_str());
        g_caseFails++;
    }
    g_cap.clear();
}

// ---------------------------------------------------------------------------
// FpgaSim - decodificador espejo del FSM de fpga/src/kbd_uart_rx.v.
// Mismos estados, mismas reglas, mismos "se ignora" (opcode desconocido en
// D_IDLE). Sirve para dos cosas: comprobar que el flujo es interpretable y
// cazar cualquier comando 0x04 aunque el byte 0x04 aparezca como DATO (una
// celda, un byte de joystick o una fila del resync pueden valer 0x04; solo
// cuenta como comando el que cae en posicion de opcode).
// ---------------------------------------------------------------------------
struct FpgaSim {
    enum State { IDLE, CELL, LOAD, JOY_PORT, JOY_BYTE, VERSION };

    uint8_t matrix[16];
    uint8_t joy[2];
    uint8_t version;
    std::vector<uint8_t> commands;   // comandos decodificados (bit7==0)
    int     unknownOpcodes;
    State   state;
    bool    pendingMake;
    int     loadIdx;
    uint8_t joyPort;

    FpgaSim() : version(0), unknownOpcodes(0), state(IDLE), pendingMake(false),
                loadIdx(0), joyPort(0) {
        memset(matrix, 0xFF, sizeof(matrix));   // activo-bajo: todo suelto
        joy[0] = joy[1] = 0;                    // activo-alto: nada pulsado
    }

    void feed(uint8_t b) {
        switch (state) {
        case IDLE:
            switch (b) {
            case 0x90: pendingMake = true;  state = CELL;     break;
            case 0xA0: pendingMake = false; state = CELL;     break;
            case 0xB0:                      state = JOY_PORT; break;
            case 0xC0:                      state = VERSION;  break;
            case 0xFE: loadIdx = 0;         state = LOAD;     break;
            case 0x01: case 0x02: case 0x03: case 0x04:
                commands.push_back(b);
                break;
            default:
                // El FPGA ignora cualquier otra cosa (incluido el 0xFF final del
                // resync). Solo el 0xFF es legitimo aqui; lo demas es basura.
                if (b != 0xFF) unknownOpcodes++;
                break;
            }
            break;
        case CELL: {
            uint8_t row = b & 0x0F, bit = (b >> 4) & 0x07;
            if (row <= 10) {
                if (pendingMake) matrix[row] &= (uint8_t)~(1u << bit);
                else             matrix[row] |=  (uint8_t) (1u << bit);
            }
            state = IDLE;
            break; }
        case LOAD:
            matrix[loadIdx] = b;
            if (loadIdx == 10) state = IDLE; else loadIdx++;
            break;
        case JOY_PORT: joyPort = (uint8_t)(b & 1); state = JOY_BYTE; break;
        case JOY_BYTE: joy[joyPort] = b;           state = IDLE;     break;
        case VERSION:  version = b;                state = IDLE;     break;
        }
    }

    void feedAll(const std::vector<uint8_t>& v) {
        for (size_t i = 0; i < v.size(); i++) feed(v[i]);
    }
};

// Comprueba que la matriz que ve el FPGA == la sombra interna de MsxHid.
static void checkMatrixSync(const FpgaSim& sim, const MsxHid& hid) {
    for (uint8_t r = 0; r < MSXHID_ROWS; r++) {
        if (sim.matrix[r] != hid.matrixRow(r)) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "fila %u desincronizada: FPGA=0x%02X sombra=0x%02X",
                     r, sim.matrix[r], hid.matrixRow(r));
            fail(buf);
        }
    }
}

// Atajos de celdas usadas en los tests (sacadas de keymaps.h).
static const uint8_t CELL_A      = 0xE2;  // 'a'  fila2 bit6
static const uint8_t CELL_S      = 0x85;  // 's'  fila5 bit0
static const uint8_t CELL_D      = 0x93;  // 'd'  fila3 bit1
static const uint8_t CELL_F      = 0xB3;  // 'f'  fila3 bit3
static const uint8_t CELL_2      = 0xA0;  // '2'  fila0 bit2 (== opcode BREAK: posicional)
static const uint8_t HID_A       = 0x04;
static const uint8_t HID_S       = 0x16;
static const uint8_t HID_D       = 0x07;
static const uint8_t HID_F       = 0x09;
static const uint8_t HID_2       = 0x1F;
static const uint8_t HID_F11     = 0x44;

// ===========================================================================
// (a) Pulsar y soltar 'A'
// ===========================================================================
static void test_key_a() {
    beginCase("(a) 'A' pulsada y soltada -> MAKE y BREAK correctos");
    MsxHid hid; FpgaSim sim;
    g_cap.clear();
    hid.begin(capture);

    uint8_t k1[1] = { HID_A };
    hid.keyboardReport(0x00, k1, 1);
    std::vector<uint8_t> got = g_cap;
    expectBytes("MAKE de 'a'", { MSXHID_OP_MAKE, CELL_A });
    sim.feedAll(got);
    CHECK(hid.matrixRow(2) == (uint8_t)(0xFF & ~(1u << 6)), "fila2 debe tener el bit6 a 0 (pulsada)");
    checkMatrixSync(sim, hid);

    hid.keyboardReport(0x00, 0, 0);
    got = g_cap;
    expectBytes("BREAK de 'a'", { MSXHID_OP_BREAK, CELL_A });
    sim.feedAll(got);
    CHECK(hid.matrixRow(2) == 0xFF, "fila2 vuelve a 0xFF al soltar");
    checkMatrixSync(sim, hid);

    // Repetir el mismo report NO debe generar nada (modelo por eventos).
    hid.keyboardReport(0x00, k1, 1); g_cap.clear();
    hid.keyboardReport(0x00, k1, 1);
    expectBytes("report repetido no emite nada", {});

    endCase();
}

// ===========================================================================
// (b) Shift+2: celda de SHIFT (derivada) + celda del '2'
// ===========================================================================
static void test_shift_2() {
    beginCase("(b) Shift+2 -> celda SHIFT + celda del '2'");
    MsxHid hid; FpgaSim sim;
    g_cap.clear();
    hid.begin(capture);

    uint8_t k[1] = { HID_2 };
    hid.keyboardReport(0x02, k, 1);            // 0x02 = LeftShift
    std::vector<uint8_t> got = g_cap;
    expectBytes("MAKE SHIFT + MAKE '2'",
                { MSXHID_OP_MAKE, MSXHID_CELL_SHIFT, MSXHID_OP_MAKE, CELL_2 });
    sim.feedAll(got);
    CHECK(hid.matrixRow(6) == (uint8_t)(0xFF & ~(1u << 0)), "SHIFT = fila6 bit0");
    CHECK(hid.matrixRow(0) == (uint8_t)(0xFF & ~(1u << 2)), "'2' = fila0 bit2");
    checkMatrixSync(sim, hid);

    // El shift DERECHO tambien cuenta: soltar el izquierdo teniendo el derecho
    // pulsado NO debe soltar el SHIFT del MSX (modificadores derivados).
    hid.keyboardReport(0x22, k, 1);            // ambos shifts
    expectBytes("los dos shifts a la vez no emiten nada nuevo", {});
    hid.keyboardReport(0x20, k, 1);            // solo el derecho
    expectBytes("soltar un shift teniendo el otro no suelta SHIFT", {});

    hid.keyboardReport(0x00, 0, 0);
    got = g_cap;
    expectBytes("BREAK SHIFT + BREAK '2'",
                { MSXHID_OP_BREAK, MSXHID_CELL_SHIFT, MSXHID_OP_BREAK, CELL_2 });
    sim.feedAll(got);
    checkMatrixSync(sim, hid);

    // CTRL / GRAPH / CODE salen de los bits HID 0|4, 2 y 6 respectivamente.
    hid.keyboardReport(0x01, 0, 0);            // LeftCtrl
    expectBytes("MAKE CTRL", { MSXHID_OP_MAKE, MSXHID_CELL_CTRL });
    hid.keyboardReport(0x04, 0, 0);            // LeftAlt -> GRAPH (y suelta CTRL)
    expectBytes("BREAK CTRL + MAKE GRAPH",
                { MSXHID_OP_BREAK, MSXHID_CELL_CTRL, MSXHID_OP_MAKE, MSXHID_CELL_GRAPH });
    hid.keyboardReport(0x40, 0, 0);            // RightAlt -> CODE
    expectBytes("BREAK GRAPH + MAKE CODE",
                { MSXHID_OP_BREAK, MSXHID_CELL_GRAPH, MSXHID_OP_MAKE, MSXHID_CELL_CODE });
    hid.keyboardReport(0x00, 0, 0);
    expectBytes("BREAK CODE", { MSXHID_OP_BREAK, MSXHID_CELL_CODE });

    endCase();
}

// ===========================================================================
// (c) Rollover: varias teclas a la vez, sueltas por separado
// ===========================================================================
static void test_rollover() {
    beginCase("(c) rollover de varias teclas simultaneas");
    MsxHid hid; FpgaSim sim;
    g_cap.clear();
    hid.begin(capture);

    uint8_t four[4] = { HID_A, HID_S, HID_D, HID_F };
    hid.keyboardReport(0x00, four, 4);
    std::vector<uint8_t> got = g_cap;
    expectBytes("4 MAKEs en el orden del report",
                { MSXHID_OP_MAKE, CELL_A, MSXHID_OP_MAKE, CELL_S,
                  MSXHID_OP_MAKE, CELL_D, MSXHID_OP_MAKE, CELL_F });
    sim.feedAll(got);
    checkMatrixSync(sim, hid);
    CHECK(hid.matrixRow(3) == (uint8_t)(0xFF & ~((1u << 1) | (1u << 3))),
          "'d' y 'f' comparten la fila 3: los dos bits a 0");

    // Se suelta solo la 's'; las otras tres siguen pulsadas.
    uint8_t three[3] = { HID_A, HID_D, HID_F };
    hid.keyboardReport(0x00, three, 3);
    got = g_cap;
    expectBytes("solo el BREAK de la 's'", { MSXHID_OP_BREAK, CELL_S });
    sim.feedAll(got);
    checkMatrixSync(sim, hid);

    // Se anade una tecla nueva sin soltar las otras.
    uint8_t four2[4] = { HID_A, HID_D, HID_F, HID_2 };
    hid.keyboardReport(0x00, four2, 4);
    got = g_cap;
    expectBytes("solo el MAKE del '2'", { MSXHID_OP_MAKE, CELL_2 });
    sim.feedAll(got);
    checkMatrixSync(sim, hid);

    // Soltarlo todo: 4 BREAKs, en el orden en que estaban guardadas.
    hid.keyboardReport(0x00, 0, 0);
    got = g_cap;
    expectBytes("4 BREAKs al soltar todo",
                { MSXHID_OP_BREAK, CELL_A, MSXHID_OP_BREAK, CELL_D,
                  MSXHID_OP_BREAK, CELL_F, MSXHID_OP_BREAK, CELL_2 });
    sim.feedAll(got);
    for (uint8_t r = 0; r < MSXHID_ROWS; r++)
        CHECK(hid.matrixRow(r) == 0xFF, "toda la matriz suelta al final");
    checkMatrixSync(sim, hid);

    // Camino NKRO: mapa de bits -> misma traduccion. bit index = usage HID.
    // Se marcan 'a' (0x04), 'd' (0x07) y LeftShift (0xE1, que NO debe gastar
    // hueco de rollover: los modificadores van por su byte).
    uint8_t bitmap[32];
    memset(bitmap, 0, sizeof(bitmap));
    bitmap[HID_A >> 3] |= (uint8_t)(1u << (HID_A & 7));
    bitmap[HID_D >> 3] |= (uint8_t)(1u << (HID_D & 7));
    bitmap[0xE1 >> 3]  |= (uint8_t)(1u << (0xE1 & 7));
    hid.keyboardBitmap(0x02, bitmap, sizeof(bitmap));   // 0x02 = LeftShift
    got = g_cap;
    expectBytes("bitmap NKRO -> SHIFT + 'a' + 'd' (usage ascendente)",
                { MSXHID_OP_MAKE, MSXHID_CELL_SHIFT,
                  MSXHID_OP_MAKE, CELL_A, MSXHID_OP_MAKE, CELL_D });
    sim.feedAll(got);
    checkMatrixSync(sim, hid);

    // Mas de 16 teclas: el rollover efectivo se corta en 16 (como el RP2040).
    // Se sueltan antes las teclas Y el shift, para contar solo los MAKEs nuevos.
    hid.keyboardReport(0x00, 0, 0);
    got = g_cap; g_cap.clear(); sim.feedAll(got);   // el FPGA tambien ve la suelta
    uint8_t many[20];
    for (int i = 0; i < 20; i++) many[i] = (uint8_t)(HID_A + i);   // 0x04..0x17
    hid.keyboardReport(0x00, many, 20);
    got = g_cap;
    CHECK(got.size() == 2 * MSXHID_MAX_KEYS, "20 teclas -> solo 16 MAKEs (tope de rollover)");
    g_cap.clear();
    sim.feedAll(got);
    checkMatrixSync(sim, hid);

    endCase();
}

// ===========================================================================
// (d) Resync de 250 ms: 0xC0 <ver> + 0xFE + 11 filas + 0xFF (+ los 2 joysticks)
// ===========================================================================
static void test_resync() {
    beginCase("(d) resync de 250 ms (0xC0 + 0xFE + 11 filas + 0xFF)");
    MsxHid hid; FpgaSim sim;
    g_cap.clear();
    hid.begin(capture);

    hid.tick(0);
    expectBytes("el primer tick solo arma los plazos, no emite", {});

    hid.tick(249);
    expectBytes("a 249 ms todavia no toca resync", {});

    uint8_t k[1] = { HID_A };
    hid.keyboardReport(0x00, k, 1);
    g_cap.clear();                              // el MAKE ya se valido en (a)

    hid.tick(250);
    std::vector<uint8_t> got = g_cap;
    std::vector<uint8_t> want;
    want.push_back(MSXHID_OP_VERSION);
    want.push_back(MSXHID_FW_VERSION);
    want.push_back(MSXHID_RESYNC_START);
    for (int r = 0; r < MSXHID_ROWS; r++)
        want.push_back(r == 2 ? (uint8_t)(0xFF & ~(1u << 6)) : 0xFF);   // 'a' pulsada
    want.push_back(MSXHID_RESYNC_END);
    want.push_back(MSXHID_OP_JOY); want.push_back(0); want.push_back(0);
    want.push_back(MSXHID_OP_JOY); want.push_back(1); want.push_back(0);
    expectBytes("trama de resync completa con la 'a' pulsada", want);

    sim.feedAll(got);
    CHECK(sim.version == (uint8_t)MSXHID_FW_VERSION, "el FPGA recibe la version anunciada");
    checkMatrixSync(sim, hid);

    // El resync se repite exactamente cada 250 ms, indefinidamente.
    hid.tick(499);
    expectBytes("a 499 ms aun no toca el segundo resync", {});
    hid.tick(500);
    CHECK(g_cap.size() == 21, "segundo resync: 15 bytes de teclado + 6 de joystick");
    g_cap.clear();

    // Un salto largo (bucle bloqueado) NO debe encadenar resyncs de golpe:
    // el plazo es "ahora + 250", no "+= 250".
    hid.tick(2000);
    CHECK(g_cap.size() == 21, "tras un salto largo se manda UN solo resync");
    g_cap.clear();

    endCase();
}

// ===========================================================================
// (e) Joystick: direcciones y botones -> byte activo-alto en los dos puertos
// ===========================================================================
static void test_joystick() {
    beginCase("(e) joystick: direcciones y botones en los dos puertos");
    MsxHid hid; FpgaSim sim;
    g_cap.clear();
    hid.begin(capture);
    hid.tick(0);
    expectBytes("arranque en reposo", {});

    // Puerto 0: derecha + disparo A.
    hid.joystickInput(0, MSXHID_JOY_RIGHT | MSXHID_JOY_A, 0);
    expectBytes("joystickInput solo latchea, no emite", {});
    hid.tick(10);
    std::vector<uint8_t> got = g_cap;
    expectBytes("puerto 0: derecha+A = 0x11",
                { MSXHID_OP_JOY, 0x00, MSXHID_JOY_RIGHT | MSXHID_JOY_A });
    sim.feedAll(got);
    CHECK(sim.joy[0] == 0x11, "el FPGA ve 0x11 en el puerto 0");

    // Puerto 1: arriba + disparo B.
    hid.joystickInput(1, MSXHID_JOY_UP | MSXHID_JOY_B, 0);
    hid.tick(20);
    got = g_cap;
    expectBytes("puerto 1: arriba+B = 0x28",
                { MSXHID_OP_JOY, 0x01, MSXHID_JOY_UP | MSXHID_JOY_B });
    sim.feedAll(got);
    CHECK(sim.joy[1] == 0x28, "el FPGA ve 0x28 en el puerto 1");
    CHECK(sim.joy[0] == 0x11, "el puerto 0 no se toca al mover el 1");

    // Sin cambios -> no se reemite nada (send-on-change).
    hid.tick(30);
    expectBytes("sin cambios no se reemite", {});

    // Soltar el puerto 0.
    hid.joystickInput(0, 0, 0);
    hid.tick(40);
    got = g_cap;
    expectBytes("puerto 0 a reposo", { MSXHID_OP_JOY, 0x00, 0x00 });
    sim.feedAll(got);
    CHECK(sim.joy[0] == 0x00, "el FPGA ve el puerto 0 en reposo");

    // Desconectar el mando del puerto 1 suelta al instante (sin esperar tick).
    hid.joystickDetached(1);
    got = g_cap;
    expectBytes("desconexion -> suelta inmediata", { MSXHID_OP_JOY, 0x01, 0x00 });
    sim.feedAll(got);
    CHECK(sim.joy[1] == 0x00, "nada clavado tras desconectar");

    // --- decodeGamepad: ejes de 8 bits, hat y botones ---
    MsxJoyInput in;
    in.x.present = true; in.x.logicalMin = 0; in.x.logicalMax = 255; in.x.bitSize = 8;
    in.y.present = true; in.y.logicalMin = 0; in.y.logicalMax = 255; in.y.bitSize = 8;
    uint8_t base = 0, af = 0;

    in.x.value = 255; in.y.value = 128;
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == MSXHID_JOY_RIGHT, "eje X al maximo = derecha");
    in.x.value = 0;
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == MSXHID_JOY_LEFT, "eje X al minimo = izquierda");
    in.x.value = 128; in.y.value = 0;
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == MSXHID_JOY_UP, "eje Y al minimo = arriba (+Y del HID es abajo)");
    in.y.value = 255;
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == MSXHID_JOY_DOWN, "eje Y al maximo = abajo");
    in.x.value = 128; in.y.value = 128;
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == 0, "centrado = nada");

    in.hatPresent = true; in.hatLogicalMin = 0;
    in.hat = 3;                       // SE
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == (MSXHID_JOY_DOWN | MSXHID_JOY_RIGHT), "hat 3 = abajo+derecha");
    in.hat = 8;                       // fuera de rango = centrado
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == 0, "hat >= 8 = centrado");
    in.hatLogicalMin = 1; in.hat = 1; // mandos que numeran 1..8
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == MSXHID_JOY_UP, "hat normalizado con logicalMin=1");
    in.hatPresent = false;

    in.buttons = 0x01;
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == MSXHID_JOY_A && af == 0, "boton 1 = disparo A");
    in.buttons = 0x02;
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == MSXHID_JOY_B && af == 0, "boton 2 = disparo B");
    in.buttons = 0x04;
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == 0 && af == MSXHID_JOY_A, "boton 3 = ARMA autofire A");
    in.buttons = 0x08;
    MsxHid::decodeGamepad(in, &base, &af);
    CHECK(base == 0 && af == MSXHID_JOY_B, "boton 4 = ARMA autofire B");

    // --- decodeXInput: mismo mapeo, mando Xbox ---
    MsxHid::decodeXInput(0x0008, 0, 0, &base, &af);   // DPAD_RIGHT
    CHECK(base == MSXHID_JOY_RIGHT, "XInput DPAD derecha");
    MsxHid::decodeXInput(0x1000, 0, 0, &base, &af);   // A
    CHECK(base == MSXHID_JOY_A, "XInput A = disparo 1");
    MsxHid::decodeXInput(0x4000, 0, 0, &base, &af);   // X
    CHECK(base == 0 && af == MSXHID_JOY_A, "XInput X = arma autofire A");
    MsxHid::decodeXInput(0x0000, 0, 32000, &base, &af);
    CHECK(base == MSXHID_JOY_UP, "XInput stick arriba (+Y es arriba)");
    MsxHid::decodeXInput(0x0000, -32000, 0, &base, &af);
    CHECK(base == MSXHID_JOY_LEFT, "XInput stick izquierda");

    endCase();
}

// ===========================================================================
// (f) Autofire: alterna JOY_A / JOY_B a 10 Hz (semiperiodo de 50 ms)
// ===========================================================================
static void test_autofire() {
    beginCase("(f) autofire alterna a 10 Hz (semiperiodo 50 ms)");
    MsxHid hid; FpgaSim sim;
    g_cap.clear();
    hid.begin(capture);

    hid.joystickInput(0, 0, MSXHID_JOY_A);   // boton 3 mantenido: autofire A
    hid.tick(0);
    expectBytes("fase inicial baja: nada pulsado", {});

    hid.tick(49);
    expectBytes("antes de los 50 ms no cambia", {});
    hid.tick(50);
    expectBytes("primer flanco: dispara A", { MSXHID_OP_JOY, 0x00, MSXHID_JOY_A });
    hid.tick(99);
    expectBytes("dentro del mismo semiciclo no se reemite", {});
    hid.tick(100);
    expectBytes("segundo flanco: suelta A", { MSXHID_OP_JOY, 0x00, 0x00 });
    hid.tick(150);
    expectBytes("tercer flanco: dispara A otra vez", { MSXHID_OP_JOY, 0x00, MSXHID_JOY_A });

    // El disparo MANUAL se mantiene pulsado mientras el autofire parpadea:
    // base=JOY_B (manual), af=JOY_A (autofire). B nunca debe soltarse.
    hid.joystickInput(0, MSXHID_JOY_B, MSXHID_JOY_A);
    hid.tick(160);
    expectBytes("B manual entra sin esperar al flanco",
                { MSXHID_OP_JOY, 0x00, (uint8_t)(MSXHID_JOY_B | MSXHID_JOY_A) });
    hid.tick(200);
    expectBytes("flanco bajo: cae A, B sigue", { MSXHID_OP_JOY, 0x00, MSXHID_JOY_B });

    // --- Frecuencia medida sobre 2 segundos, con la maquina completa ---
    MsxHid hid2; FpgaSim sim2;
    g_cap.clear();
    hid2.begin(capture);
    hid2.joystickInput(0, 0, MSXHID_JOY_B);   // autofire en el disparo B
    std::vector<uint32_t> risingEdges;
    uint8_t prevJoy = 0;
    for (uint32_t t = 0; t <= 2000; t++) {
        g_cap.clear();
        hid2.tick(t);
        std::vector<uint8_t> step = g_cap;
        sim2.feedAll(step);
        if ((sim2.joy[0] & MSXHID_JOY_B) && !(prevJoy & MSXHID_JOY_B))
            risingEdges.push_back(t);
        prevJoy = sim2.joy[0];
    }
    g_cap.clear();
    // 2000 ms a 10 Hz: flancos de subida en 50, 150, 250 ... = 20 disparos.
    CHECK(risingEdges.size() == 20, "20 disparos en 2 s = 10 Hz");
    bool spacingOk = true;
    for (size_t i = 1; i < risingEdges.size(); i++)
        if (risingEdges[i] - risingEdges[i - 1] != 100) spacingOk = false;
    CHECK(spacingOk, "periodo de 100 ms exacto entre disparos");
    if (!risingEdges.empty())
        CHECK(risingEdges[0] == 50, "el primer disparo cae a los 50 ms");
    CHECK(sim2.joy[1] == 0, "el puerto 1 sigue en reposo durante el autofire");

    // Desarmar el autofire deja el mando quieto (sin parpadeo residual).
    hid2.joystickInput(0, 0, 0);
    hid2.tick(2050);
    std::vector<uint8_t> tail = g_cap;
    sim2.feedAll(tail);
    g_cap.clear();
    CHECK(sim2.joy[0] == 0, "al desarmar, el disparo queda suelto");
    for (uint32_t t = 2051; t < 2300; t++) { hid2.tick(t); }
    // Solo deberian quedar tramas de resync (21 bytes cada 250 ms), ningun 0xB0
    // suelto de autofire. Se comprueba decodificando: el joystick no cambia.
    std::vector<uint8_t> rest = g_cap;
    g_cap.clear();
    sim2.feedAll(rest);
    CHECK(sim2.joy[0] == 0, "sin autofire armado el disparo no vuelve a subir");

    endCase();
}

// ===========================================================================
// (g) NUNCA se emite el comando 0x04 (turbo del viejo F11)
// ===========================================================================
static void test_no_turbo_command() {
    beginCase("(g) el comando 0x04 (turbo) no se emite JAMAS");

    // 1. F11 no esta mapeado en la tabla.
    CHECK(MsxHid::cellForKey(HID_F11) == 0, "F11 (0x44) debe quedar SIN mapear");

    // 2. Barrido de los 256 usages HID x pulsar/soltar, mas los 256 estados
    //    posibles del byte de modificadores. Todo el flujo se decodifica con el
    //    FSM del FPGA y se exige que no aparezca ningun comando 0x04.
    MsxHid hid; FpgaSim sim;
    g_cap.clear();
    hid.begin(capture);

    for (int u = 0; u < 256; u++) {
        uint8_t k[1] = { (uint8_t)u };
        hid.keyboardReport(0x00, k, 1);
        hid.keyboardReport(0x00, 0, 0);
    }
    for (int m = 0; m < 256; m++) {
        hid.keyboardReport((uint8_t)m, 0, 0);
    }
    hid.keyboardReport(0x00, 0, 0);

    // Todas las teclas F a la vez (F8..F12 son los comandos), varias veces.
    uint8_t fkeys[5] = { 0x41, 0x42, 0x43, 0x44, 0x45 };
    for (int rep = 0; rep < 3; rep++) {
        hid.keyboardReport(0x00, fkeys, 5);
        hid.keyboardReport(0x00, 0, 0);
    }

    // Trafico de joystick y resyncs: los bytes de datos pueden VALER 0x04
    // (JOY_DOWN es 0x04), y eso es legitimo: el FSM distingue posicion.
    hid.joystickInput(0, MSXHID_JOY_DOWN, 0);
    hid.joystickInput(1, MSXHID_JOY_DOWN, 0);
    for (uint32_t t = 0; t <= 1000; t += 10) hid.tick(t);

    std::vector<uint8_t> stream = g_cap;
    g_cap.clear();
    sim.feedAll(stream);

    int turbo = 0;
    for (size_t i = 0; i < sim.commands.size(); i++)
        if (sim.commands[i] == MSXHID_CMD_FORBIDDEN_TURBO) turbo++;
    CHECK(turbo == 0, "ningun comando 0x04 en todo el barrido");
    CHECK(sim.unknownOpcodes == 0, "el flujo no lleva opcodes basura");
    CHECK(!sim.commands.empty(), "los comandos validos (F8..F10, F12) si salen");
    bool onlyValid = true;
    for (size_t i = 0; i < sim.commands.size(); i++)
        if (sim.commands[i] != 0x01 && sim.commands[i] != 0x02 && sim.commands[i] != 0x03)
            onlyValid = false;
    CHECK(onlyValid, "solo se emiten los comandos 0x01/0x02/0x03");
    CHECK(sim.joy[0] == MSXHID_JOY_DOWN && sim.joy[1] == MSXHID_JOY_DOWN,
          "un byte de joystick que vale 0x04 (abajo) llega intacto");
    checkMatrixSync(sim, hid);

    // 3. Aunque alguien reintrodujera el mapeo, el filtro de emitCommand()
    //    lo pararia. Se comprueba el filtro por el unico camino publico que
    //    existe: ningun usage produce un 0x04 (ya barrido arriba) y la tabla
    //    no lo contiene para ningun usage.
    int cellsWithTurbo = 0;
    for (int u = 0; u < 256; u++)
        if (MsxHid::cellForKey((uint8_t)u) == MSXHID_CMD_FORBIDDEN_TURBO) cellsWithTurbo++;
    CHECK(cellsWithTurbo == 0, "ningun usage mapea al comando 0x04 en la tabla");

    endCase();
}

// ===========================================================================
// Extra: conexion/desconexion del teclado no deja teclas clavadas
// ===========================================================================
static void test_attach_detach() {
    beginCase("(h) enchufar/desenchufar el teclado deja todo suelto");
    MsxHid hid; FpgaSim sim;
    g_cap.clear();
    hid.begin(capture);

    hid.keyboardAttached();
    std::vector<uint8_t> got = g_cap;
    std::vector<uint8_t> want;
    want.push_back(MSXHID_OP_VERSION);
    want.push_back(MSXHID_FW_VERSION);
    want.push_back(MSXHID_RESYNC_START);
    for (int r = 0; r < MSXHID_ROWS; r++) want.push_back(0xFF);
    want.push_back(MSXHID_RESYNC_END);
    expectBytes("al enchufar: resync con la matriz limpia", want);
    sim.feedAll(got);

    uint8_t k[2] = { HID_A, HID_S };
    hid.keyboardReport(0x02, k, 2);            // shift + a + s pulsadas
    got = g_cap; g_cap.clear();
    sim.feedAll(got);
    CHECK(sim.matrix[2] != 0xFF, "hay teclas pulsadas antes de desconectar");

    hid.keyboardDetached();
    got = g_cap;
    expectBytes("al desenchufar: resync con la matriz limpia", want);
    sim.feedAll(got);
    for (int r = 0; r < MSXHID_ROWS; r++)
        CHECK(sim.matrix[r] == 0xFF, "el FPGA ve TODO suelto tras desconectar");
    checkMatrixSync(sim, hid);

    // Y el estado interno queda limpio: la siguiente pulsacion es un MAKE nuevo.
    hid.keyboardReport(0x00, k, 2);
    expectBytes("tras reconectar, MAKEs limpios",
                { MSXHID_OP_MAKE, CELL_A, MSXHID_OP_MAKE, CELL_S });

    endCase();
}

int main() {
    printf("== MsxHid: verificacion del protocolo del companion MSXnano ==\n");
    test_key_a();
    test_shift_2();
    test_rollover();
    test_resync();
    test_joystick();
    test_autofire();
    test_no_turbo_command();
    test_attach_detach();

    if (g_totalFails == 0) {
        printf("== TODO OK ==\n");
        return 0;
    }
    printf("== %d COMPROBACION(ES) FALLIDA(S) ==\n", g_totalFails);
    return 1;
}
