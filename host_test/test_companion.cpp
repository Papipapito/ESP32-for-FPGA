/*
 * test_companion.cpp - banco del enlace SPI con la FPGA, en el PC.
 *
 * Companion.cpp es C++ puro a proposito, asi que esto compila sin placa.
 *
 * Lo que vigila, y por que cada cosa:
 *  1. El BIT 7 DEL TECLADO VA AL REVES de lo que uno escribiria de memoria:
 *     hid.v hace keyboard[data_in[6:0]] <= ~data_in[7], o sea que el bit
 *     PUESTO significa SOLTAR. Equivocarse deja teclas pegadas y el sintoma
 *     (el MSX escribiendo solo) no apunta al sitio.
 *  2. Los deltas del raton son CON SIGNO y la FPGA los SUMA a un acumulador
 *     (hid.v:99-100). Mandar la posicion en vez del delta da un puntero que
 *     se va corriendo sin parar.
 *  3. La trama va en UNA sola transferencia. mcu_spi_new reinicia el contador
 *     de bytes con el CS, asi que soltarlo a mitad tira la trama y la
 *     siguiente se lee corrida.
 *  4. El orden destino-comando-carga, byte a byte, contra lo que espera el RTL.
 */
#include <stdio.h>
#include <string.h>
#include "../Companion.cpp"

static int fallos = 0;
#define CHECK(c, ...) do { if(!(c)) { printf("  FALLO: "); printf(__VA_ARGS__); \
                            printf("\n"); fallos++; } } while(0)

// ---- SPI de mentira: apunta cada transferencia entera -------------------
struct Bus {
    uint8_t  tramas[16][COMP_MAX_FRAME];
    size_t   largos[16];
    int      n;
    uint8_t  respuesta[COMP_MAX_FRAME];   // lo que "devuelve" la FPGA
};
static void bus_xfer(const uint8_t *tx, uint8_t *rx, size_t n, void *user)
{
    Bus *b = (Bus *)user;
    if (b->n < 16) {
        memcpy(b->tramas[b->n], tx, n);
        b->largos[b->n] = n;
        b->n++;
    }
    if (rx) memcpy(rx, b->respuesta, n);
}

static void volcar(const uint8_t *t, size_t n)
{
    for (size_t i = 0; i < n; i++) printf("%02x ", t[i]);
}

int main(void)
{
    printf("=== test_companion: enlace SPI con la FPGA ===\n");
    Bus bus; memset(&bus, 0, sizeof(bus));
    Companion c; compBegin(&c, bus_xfer, &bus);

    // -----------------------------------------------------------------
    printf("== 1. teclado: bit7 PUESTO = SOLTAR (no al reves) ==\n");
    uint8_t t[COMP_MAX_FRAME];
    size_t n = compBuildKey(t, 0x2C, true);          // pulsar la tecla 0x2C
    CHECK(n == 3, "la trama mide %zu, esperaba 3", n);
    CHECK(t[0] == COMP_TGT_HID, "destino %02x, esperaba 01", t[0]);
    CHECK(t[1] == COMP_HID_KEYBOARD, "comando %02x, esperaba 01", t[1]);
    CHECK(t[2] == 0x2C, "PULSAR dio %02x, esperaba 2c (bit7 a CERO)", t[2]);
    n = compBuildKey(t, 0x2C, false);                // soltarla
    CHECK(t[2] == 0xAC, "SOLTAR dio %02x, esperaba ac (bit7 a UNO)", t[2]);
    // el indice se recorta a 7 bits: un codigo >127 no debe pisar el bit de soltar
    compBuildKey(t, 0xFF, true);
    CHECK(t[2] == 0x7F, "un codigo 0xFF pulsado dio %02x, esperaba 7f", t[2]);

    // -----------------------------------------------------------------
    printf("== 2. raton: deltas con signo en complemento a 2 ==\n");
    n = compBuildMouse(t, -3, 5, 0x02);
    CHECK(n == 5, "la trama mide %zu, esperaba 5", n);
    CHECK(t[0] == 0x01 && t[1] == 0x02, "cabecera %02x %02x, esperaba 01 02", t[0], t[1]);
    CHECK(t[2] == 0x02, "botones %02x, esperaba 02", t[2]);
    CHECK(t[3] == 0xFD, "dx=-3 dio %02x, esperaba fd", t[3]);
    CHECK(t[4] == 0x05, "dy=5 dio %02x, esperaba 05", t[4]);
    compBuildMouse(t, 0, 0, 0xFF);
    CHECK(t[2] == 0x03, "los botones deben recortarse a 2 bits, dio %02x", t[2]);

    // -----------------------------------------------------------------
    printf("== 3. joystick ==\n");
    n = compBuildJoystick(t, 1, 0x5A);
    CHECK(n == 4, "la trama mide %zu, esperaba 4", n);
    CHECK(t[0] == 0x01 && t[1] == 0x03 && t[2] == 0x01 && t[3] == 0x5A,
          "trama %02x %02x %02x %02x, esperaba 01 03 01 5a", t[0], t[1], t[2], t[3]);

    // -----------------------------------------------------------------
    printf("== 4. cada trama sale en UNA sola transferencia ==\n");
    bus.n = 0;
    compKey(&c, 0x10, true);
    compMouse(&c, 1, -1, 0);
    compJoystick(&c, 0, 0x0F);
    CHECK(bus.n == 3, "%d transferencias, esperaba 3 (una por trama)", bus.n);
    if (bus.n == 3) {
        CHECK(bus.largos[0] == 3 && bus.largos[1] == 5 && bus.largos[2] == 4,
              "largos %zu/%zu/%zu, esperaba 3/5/4",
              bus.largos[0], bus.largos[1], bus.largos[2]);
        printf("   ");
        for (int i = 0; i < 3; i++) { volcar(bus.tramas[i], bus.largos[i]); printf("| "); }
        printf("\n");
    }

    // -----------------------------------------------------------------
    printf("== 5. estado: se lee la version que devuelve la FPGA ==\n");
    // CMD 0 pone data_out=1 en el estado 0 y 0 en el 1; con el full duplex
    // eso cae en los dos huecos del final de la trama.
    bus.respuesta[2] = 0x01; bus.respuesta[3] = 0x00;
    uint8_t v = 0xEE, sv = 0xEE;
    bool ok = compStatus(&c, &v, &sv);
    CHECK(ok, "compStatus dijo que no contesta nadie");
    CHECK(v == 1 && sv == 0, "version %d.%d, esperaba 1.0", v, sv);
    // y con el bus muerto (todo a FF) tiene que decir que no hay nadie
    memset(bus.respuesta, 0xFF, sizeof(bus.respuesta));
    CHECK(!compStatus(&c, &v, &sv), "con el bus a FF deberia decir que no hay nadie");
    memset(bus.respuesta, 0x00, sizeof(bus.respuesta));
    CHECK(!compStatus(&c, &v, &sv), "con el bus a 00 deberia decir que no hay nadie");

    printf("\n");
    if (fallos == 0) printf("*** TEST_COMPANION: OK (5 pruebas) ***\n");
    else             printf("*** TEST_COMPANION: %d FALLOS ***\n", fallos);
    return fallos ? 1 : 0;
}
