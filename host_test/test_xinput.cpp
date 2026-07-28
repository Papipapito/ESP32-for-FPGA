// test_xinput.cpp - banco de pruebas de la PARTE PURA de XInputHost (troceado
// de reports XInput + clasificacion de interfaces), verificada en el PC.
//
//   wsl -d Ubuntu-24.04 bash -lc "/mnt/c/.../host_test/test_xinput.sh"
//
// Que se prueba y que NO:
//   SI  - los offsets de byte de cada formato de report (360 cable, receptor
//         inalambrico, Xbox original, One) y el reordenado de bits a la mascara
//         canonica XINPUT_GAMEPAD_*.
//   SI  - la cadena COMPLETA report -> xinputParseReport -> MsxHid::decodeXInput
//         -> byte MSX. Es la prueba que de verdad importa: el mapeo final ya
//         estaba verificado, lo nuevo es que le lleguen los datos correctos.
//   SI  - que un paquete corto o con cabecera equivocada no se interpreta.
//   NO  - el transporte USB (PARTE B de XInputHost.cpp). Eso solo se comprueba
//         a nivel de compilacion contra las cabeceras de ESP-IDF (ver
//         test_xinput.sh) y hace falta hardware para validarlo de verdad.
#include "../XInputHost.h"
#include "../MsxHid.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static int g_total = 0;

static void check(bool ok, const char* what) {
    g_total++;
    if (!ok) { g_fail++; printf("  FAIL: %s\n", what); }
}

// Report real de un mando 360 por cable (20 bytes). Formato:
//   [0]=0x00 [1]=0x14 [2..3]=botones LE [4]=LT [5]=RT
//   [6..7]=LX [8..9]=LY [10..11]=RX [12..13]=RY
static void make360Wired(uint8_t* r, uint16_t rawButtons, int16_t lx, int16_t ly) {
    memset(r, 0, 20);
    r[0] = 0x00; r[1] = 0x14;
    r[2] = (uint8_t)(rawButtons & 0xFF);
    r[3] = (uint8_t)(rawButtons >> 8);
    r[6] = (uint8_t)((uint16_t)lx & 0xFF);  r[7] = (uint8_t)((uint16_t)lx >> 8);
    r[8] = (uint8_t)((uint16_t)ly & 0xFF);  r[9] = (uint8_t)((uint16_t)ly >> 8);
}

// Paquete de estado del receptor inalambrico 360 (29 bytes en el cable).
//   [0]=0x00 [1]=bit0 marca "hay datos" [5]=0x13 [6..7]=botones LE
//   [8]=LT [9]=RT [10..11]=LX [12..13]=LY
static void make360Wireless(uint8_t* r, uint16_t rawButtons, int16_t lx, int16_t ly) {
    memset(r, 0, 29);
    r[0] = 0x00; r[1] = 0x01; r[5] = 0x13;
    r[6] = (uint8_t)(rawButtons & 0xFF);
    r[7] = (uint8_t)(rawButtons >> 8);
    r[10] = (uint8_t)((uint16_t)lx & 0xFF); r[11] = (uint8_t)((uint16_t)lx >> 8);
    r[12] = (uint8_t)((uint16_t)ly & 0xFF); r[13] = (uint8_t)((uint16_t)ly >> 8);
}

int main() {
    uint16_t b = 0;
    int16_t  lx = 0, ly = 0;
    uint8_t  r[32];

    // =====================================================================
    printf("== clasificacion de interfaces ==\n");
    check(xinputClassifyInterface(0xFF, 0x5D, 0x01, 2) == XINPUT_TYPE_360_WIRED,
          "0xFF/0x5D/0x01 = 360 por cable");
    check(xinputClassifyInterface(0xFF, 0x5D, 0x81, 2) == XINPUT_TYPE_360_WIRELESS,
          "0xFF/0x5D/0x81 = receptor inalambrico 360");
    check(xinputClassifyInterface(0xFF, 0x47, 0xD0, 2) == XINPUT_TYPE_XBOXONE,
          "0xFF/0x47/0xD0 = Xbox One/Series");
    check(xinputClassifyInterface(0x58, 0x42, 0x00, 2) == XINPUT_TYPE_XBOXOG,
          "0x58/0x42 = Xbox original");
    // Rechazos: lo importante es NO reclamar interfaces ajenas (gastan canales).
    check(xinputClassifyInterface(0x03, 0x01, 0x01, 1) == XINPUT_TYPE_NONE,
          "un teclado HID no es XInput");
    check(xinputClassifyInterface(0xFF, 0x5D, 0x01, 1) == XINPUT_TYPE_NONE,
          "vendor 0x5D con un solo endpoint no es un mando");
    check(xinputClassifyInterface(0xFF, 0x5D, 0x03, 2) == XINPUT_TYPE_NONE,
          "0xFF/0x5D/0x03 (auriculares del 360) no es un mando");
    check(xinputClassifyInterface(0xFF, 0xFD, 0x13, 2) == XINPUT_TYPE_NONE,
          "interfaz de seguridad del 360 no es un mando");
    check(xinputClassifyInterface(0xFF, 0x01, 0x01, 2) == XINPUT_TYPE_NONE,
          "vendor generico (adaptador serie) no es XInput");

    // =====================================================================
    printf("== 360 por cable: troceado ==\n");
    make360Wired(r, 1u << 3, 0, 0);                  // bit3 = cruceta derecha
    check(xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly) == XINPUT_PARSE_PAD,
          "paquete 0x00 0x14 se acepta");
    check(b == XINPUT_GAMEPAD_DPAD_RIGHT, "bit3 -> DPAD_RIGHT");

    make360Wired(r, 1u << 12, 0, 0);                 // bit12 = A
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    check(b == XINPUT_GAMEPAD_A, "bit12 -> A");

    make360Wired(r, 1u << 15, 0, 0);                 // bit15 = Y
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    check(b == XINPUT_GAMEPAD_Y, "bit15 -> Y");

    make360Wired(r, 0, -20000, 25000);
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    check(lx == -20000 && ly == 25000, "sticks con signo, little-endian");

    // Robustez: en el original de Ryzee119 se indexaba hasta rdata[13] sin mirar
    // la longitud. Aqui un paquete corto NO debe interpretarse.
    make360Wired(r, 1u << 12, 0, 0);
    check(xinputParseReport(XINPUT_TYPE_360_WIRED, r, 8, &b, &lx, &ly) == XINPUT_PARSE_IGNORED,
          "paquete corto (8 bytes) se ignora");
    r[1] = 0x00;
    check(xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly) == XINPUT_PARSE_IGNORED,
          "cabecera distinta de 0x14 se ignora");

    // =====================================================================
    printf("== receptor inalambrico 360 ==\n");
    memset(r, 0, sizeof(r));
    r[0] = 0x08; r[1] = 0x80;                        // paquete de enlace, mando ON
    check(xinputParseReport(XINPUT_TYPE_360_WIRELESS, r, 29, &b, &lx, &ly) == XINPUT_PARSE_LINK_UP,
          "aviso de mando encendido");
    r[0] = 0x08; r[1] = 0x00;                        // mando OFF
    check(xinputParseReport(XINPUT_TYPE_360_WIRELESS, r, 29, &b, &lx, &ly) == XINPUT_PARSE_LINK_DOWN,
          "aviso de mando apagado (hay que soltar el joystick)");

    make360Wireless(r, (1u << 2) | (1u << 13), -30000, 0);   // izquierda + B
    check(xinputParseReport(XINPUT_TYPE_360_WIRELESS, r, 29, &b, &lx, &ly) == XINPUT_PARSE_PAD,
          "paquete de estado inalambrico");
    check(b == (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_B), "botones en offset 6");
    check(lx == -30000, "stick X en offset 10");

    memset(r, 0, sizeof(r));
    r[1] = 0x01; r[5] = 0x00;                        // sin la marca 0x13
    check(xinputParseReport(XINPUT_TYPE_360_WIRELESS, r, 29, &b, &lx, &ly) == XINPUT_PARSE_IGNORED,
          "paquete sin marca 0x13 se ignora");

    // =====================================================================
    printf("== Xbox original: botones analogicos ==\n");
    memset(r, 0, sizeof(r));
    r[1] = 0x14;
    r[4] = 0x40;                                     // A pisado a fondo
    r[5] = 0x10;                                     // B por debajo del umbral 0x20
    r[12] = 0x00; r[13] = 0x80;                      // LX = -32768
    check(xinputParseReport(XINPUT_TYPE_XBOXOG, r, 20, &b, &lx, &ly) == XINPUT_PARSE_PAD,
          "paquete OG valido");
    check((b & XINPUT_GAMEPAD_A) != 0, "presion 0x40 > umbral -> A pulsado");
    check((b & XINPUT_GAMEPAD_B) == 0, "presion 0x10 <= umbral -> B suelto");
    check(lx == -32768, "stick OG en offset 12");

    // =====================================================================
    printf("== Xbox One (GIP): troceado listo aunque hoy no se reclame ==\n");
    memset(r, 0, sizeof(r));
    r[0] = 0x20;                                     // GIP_CMD_INPUT
    r[4] = (uint8_t)(1u << 4);                       // bit4 = A
    r[5] = (uint8_t)(1u << 0);                       // bit8 = cruceta arriba
    r[10] = 0x00; r[11] = 0x40;                      // LX = +16384
    check(xinputParseReport(XINPUT_TYPE_XBOXONE, r, 18, &b, &lx, &ly) == XINPUT_PARSE_PAD,
          "paquete GIP de entrada");
    check(b == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_UP), "reordenado de bits del One");
    r[0] = 0x07;                                     // GIP_CMD_VIRTUAL_KEY
    check(xinputParseReport(XINPUT_TYPE_XBOXONE, r, 18, &b, &lx, &ly) == XINPUT_PARSE_IGNORED,
          "paquete GIP que no es de entrada se ignora");

    // =====================================================================
    // LA PRUEBA QUE IMPORTA: report crudo del cable -> byte que ve el MSX.
    // Encadena xinputParseReport con el mapeo ya congelado de MsxHid.
    // =====================================================================
    printf("== cadena completa report -> byte MSX ==\n");
    uint8_t base = 0, af = 0;

    make360Wired(r, 1u << 3, 0, 0);                  // cruceta derecha
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    MsxHid::decodeXInput(b, lx, ly, &base, &af);
    check(base == MSXHID_JOY_RIGHT && af == 0, "cruceta derecha -> bit0 del MSX");

    make360Wired(r, 1u << 12, 0, 0);                 // A
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    MsxHid::decodeXInput(b, lx, ly, &base, &af);
    check(base == MSXHID_JOY_A && af == 0, "A -> disparo 1");

    make360Wired(r, 1u << 14, 0, 0);                 // X arma el autofire de A
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    MsxHid::decodeXInput(b, lx, ly, &base, &af);
    check(base == 0 && af == MSXHID_JOY_A, "X -> arma autofire A");

    // Stick arriba: en XInput +Y es arriba. Si algun dia se invirtiera el signo
    // al trocear el report, este caso lo caza.
    make360Wired(r, 0, 0, 30000);
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    MsxHid::decodeXInput(b, lx, ly, &base, &af);
    check(base == MSXHID_JOY_UP, "stick +Y -> arriba en el MSX");

    make360Wired(r, 0, 0, -30000);
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    MsxHid::decodeXInput(b, lx, ly, &base, &af);
    check(base == MSXHID_JOY_DOWN, "stick -Y -> abajo en el MSX");

    // Zona muerta: media escala. Un stick en reposo no debe mover nada.
    make360Wired(r, 0, 5000, -4000);
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    MsxHid::decodeXInput(b, lx, ly, &base, &af);
    check(base == 0 && af == 0, "stick dentro de la zona muerta no mueve nada");

    // Diagonal + dos disparos a la vez (caso tipico de juego).
    make360Wired(r, (1u << 0) | (1u << 3) | (1u << 12) | (1u << 13), 0, 0);
    xinputParseReport(XINPUT_TYPE_360_WIRED, r, 20, &b, &lx, &ly);
    MsxHid::decodeXInput(b, lx, ly, &base, &af);
    check(base == (MSXHID_JOY_UP | MSXHID_JOY_RIGHT | MSXHID_JOY_A | MSXHID_JOY_B),
          "diagonal arriba-derecha con A y B");

    printf("\n%d casos, %d fallos\n", g_total, g_fail);
    return g_fail ? 1 : 0;
}
