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
// 520: la trama mas larga ya no es una de control, es un SECTOR (2 de
// cabecera + 512). Con el tamano viejo (COMP_MAX_FRAME) el memcpy se salia del
// array y el banco corrompia su propia memoria.
#define BUS_MAX 520
struct Bus {
    uint8_t  tramas[16][BUS_MAX];
    size_t   largos[16];
    int      n;
    uint8_t  respuesta[BUS_MAX];          // lo que "devuelve" la FPGA
};
static void bus_xfer(const uint8_t *tx, uint8_t *rx, size_t n, void *user)
{
    Bus *b = (Bus *)user;
    if (n > BUS_MAX) { printf("  FALLO: trama de %zu bytes, el bus solo tiene %d\n", n, BUS_MAX); return; }
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
    // hid.v carga data_out en el ESTADO 0, un byte DESPUES del comando: con el
    // retraso del full duplex la version cae en rx[3], no en rx[2]. Es distinto
    // de sdc_bridge/launcher_svc, que cargan en el byte del comando (rx[2]).
    bus.respuesta[3] = 0x01; bus.respuesta[4] = 0x00;
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
    // -----------------------------------------------------------------
    printf("== 6. lanzador: escritura suelta al VDP ==\n");
    {
        uint8_t t[COMP_MAX_FRAME];
        size_t n = compBuildVdpWrite(t, VDP_PORT_REG, 0x5C);
        CHECK(n == 4, "largo %zu, esperaba 4", n);
        CHECK(t[0] == COMP_TGT_OSD, "destino %d, esperaba OSD(2)", t[0]);
        CHECK(t[1] == COMP_LNZ_WRITE, "comando %d, esperaba ESCRIBIR(1)", t[1]);
        CHECK(t[2] == VDP_PORT_REG && t[3] == 0x5C, "puerto/dato mal");
        printf("   "); volcar(t, n); printf("\n");
    }

    // -----------------------------------------------------------------
    printf("== 7. lanzador: un REGISTRO es dato-primero y luego 0x80|reg ==\n");
    {
        uint8_t t[COMP_MAX_FRAME];
        size_t n = compBuildVdpReg(t, 7, 0x06);      // R#7 = color de fondo
        CHECK(n == 5, "largo %zu, esperaba 5", n);
        CHECK(t[1] == COMP_LNZ_BULK, "un registro debe ir en UNA trama de volcado");
        CHECK(t[2] == VDP_PORT_REG, "un registro se escribe por el puerto #99");
        // El orden es justo lo que rompe si alguien 'ordena' esto algun dia.
        CHECK(t[3] == 0x06, "el DATO va primero, y llego %02X", t[3]);
        CHECK(t[4] == 0x87, "luego 0x80|reg = 87, y llego %02X", t[4]);
        printf("   "); volcar(t, n); printf("\n");
    }

    // -----------------------------------------------------------------
    printf("== 8. lanzador: el LBA va en little endian ==\n");
    {
        uint8_t t[COMP_MAX_FRAME];
        size_t n = compBuildSdRead(t, 0x12345678u);
        CHECK(n == 6, "largo %zu, esperaba 6", n);
        CHECK(t[0] == COMP_TGT_SDC && t[1] == COMP_SDC_READ, "destino/comando mal");
        CHECK(t[2] == 0x78 && t[3] == 0x56 && t[4] == 0x34 && t[5] == 0x12,
              "LBA mal: %02X %02X %02X %02X (sdc_bridge.v lo lee little endian)",
              t[2], t[3], t[4], t[5]);
        printf("   "); volcar(t, n); printf("\n");
    }

    // -----------------------------------------------------------------
    printf("== 9. lanzador: tomar y soltar el mando ==\n");
    {
        uint8_t a[COMP_MAX_FRAME], b[COMP_MAX_FRAME];
        CHECK(compBuildSdTake(a) == 2 && a[0] == COMP_TGT_SDC && a[1] == COMP_SDC_TAKE,
              "trama de TOMAR mal");
        CHECK(compBuildSdRelease(b) == 2 && b[0] == COMP_TGT_SDC && b[1] == COMP_SDC_RELEASE,
              "trama de SOLTAR mal");
        // Confundir TOMAR con SOLTAR deja la maquina sin arrancar hasta que
        // salte el perro guardian: merece su propia comprobacion.
        CHECK(a[1] != b[1], "TOMAR y SOLTAR llevan el mismo comando");
    }

    // -----------------------------------------------------------------
    printf("== 10. lanzador: el estado trae version y PERDIDOS ==\n");
    {
        memset(bus.respuesta, 0x00, sizeof(bus.respuesta));
        bus.respuesta[2] = 0x01;   // VERSION
        bus.respuesta[3] = 0x00;   // lleno = no
        bus.respuesta[4] = 0x00;   // perdidos_hi
        bus.respuesta[5] = 0x78;   // perdidos_lo = 120
        uint8_t lv = 0; bool lleno = true; uint16_t perd = 0;
        CHECK(compLnzStatus(&c, &lv, &lleno, &perd), "compLnzStatus dice que no hay nadie");
        CHECK(lv == 1, "version %d, esperaba 1", lv);
        CHECK(!lleno, "deberia decir que la cola NO esta llena");
        CHECK(perd == 120, "perdidos %u, esperaba 120", perd);
        memset(bus.respuesta, 0x00, sizeof(bus.respuesta));
    }

    // -----------------------------------------------------------------
    printf("== 11. lanzador: los 16 bytes de teclas llegan enteros ==\n");
    {
        memset(bus.respuesta, 0x00, sizeof(bus.respuesta));
        for (int i = 0; i < 16; i++) bus.respuesta[2 + i] = (uint8_t)(0xA0 + i);
        uint8_t k[16];
        memset(k, 0, sizeof(k));
        CHECK(compLnzKeys(&c, k), "compLnzKeys fallo");
        int malos = 0;
        for (int i = 0; i < 16; i++) if (k[i] != (uint8_t)(0xA0 + i)) malos++;
        CHECK(malos == 0, "%d bytes de teclado mal colocados", malos);
        memset(bus.respuesta, 0x00, sizeof(bus.respuesta));
    }

    // -----------------------------------------------------------------
    printf("== 12. sector: los 512 bytes salen desplazados DOS ==\n");
    {
        // El puente devuelve buf_mem[0] durante el byte 2 de la trama, asi que
        // el dato empieza en rx[2]. Un desplazamiento aqui corrompe el sector
        // entero sin que nada de error.
        memset(bus.respuesta, 0x00, sizeof(bus.respuesta));
        for (int i = 0; i < 40; i++) bus.respuesta[2 + i] = (uint8_t)(0x10 + i);
        uint8_t sec[512];
        memset(sec, 0xEE, sizeof(sec));
        bus.n = 0;
        CHECK(compSdSector(&c, sec), "compSdSector fallo");
        // La trama tambien se comprueba: sin esto, cambiar DATOS por LEER
        // pasaba el banco (el bus de mentira contesta igual sea cual sea el
        // comando). Un sabotaje que sale no-op es un agujero, no un aprobado.
        CHECK(bus.n == 1, "%d transferencias, esperaba 1", bus.n);
        CHECK(bus.largos[0] == 514, "trama de %zu, esperaba 514", bus.largos[0]);
        CHECK(bus.tramas[0][0] == COMP_TGT_SDC, "destino equivocado");
        CHECK(bus.tramas[0][1] == COMP_SDC_DATA, "comando equivocado: no es DATOS");
        int malos = 0;
        for (int i = 0; i < 40; i++) if (sec[i] != (uint8_t)(0x10 + i)) malos++;
        CHECK(malos == 0, "%d bytes del sector mal alineados", malos);
        memset(bus.respuesta, 0x00, sizeof(bus.respuesta));
    }

    // -----------------------------------------------------------------
    printf("== 13. ocupado: el bit busy sale en rx[3] ==\n");
    {
        memset(bus.respuesta, 0x00, sizeof(bus.respuesta));
        bus.respuesta[3] = 0x01;
        CHECK(compSdBusy(&c), "deberia decir que la tarjeta esta ocupada");
        bus.respuesta[3] = 0x00;
        CHECK(!compSdBusy(&c), "deberia decir que ya termino");
    }

    if (fallos == 0) printf("*** TEST_COMPANION: OK (13 pruebas) ***\n");
    else             printf("*** TEST_COMPANION: %d FALLOS ***\n", fallos);
    return fallos ? 1 : 0;
}
