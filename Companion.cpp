/*
 * Companion.cpp - ver Companion.h para el protocolo y por que es este.
 */
#include "Companion.h"

void compBegin(Companion *c, CompXfer xfer, void *user)
{
    if (!c) return;
    c->xfer = xfer;
    c->user = user;
}

size_t compBuildStatus(uint8_t *dst)
{
    dst[0] = COMP_TGT_HID;
    dst[1] = COMP_HID_STATUS;
    dst[2] = 0;             // relleno: la respuesta llega en estos huecos
    dst[3] = 0;
    dst[4] = 0;
    return 5;
}

size_t compBuildKey(uint8_t *dst, uint8_t codigo, bool pulsada)
{
    dst[0] = COMP_TGT_HID;
    dst[1] = COMP_HID_KEYBOARD;
    // hid.v:92 -> keyboard[data_in[6:0]] <= ~data_in[7]
    // o sea que el bit 7 puesto significa SOLTAR. Es al reves de lo que uno
    // escribiria de memoria, y equivocarse deja teclas pegadas.
    dst[2] = (uint8_t)((codigo & 0x7F) | (pulsada ? 0x00 : 0x80));
    return 3;
}

size_t compBuildMouse(uint8_t *dst, int8_t dx, int8_t dy, uint8_t botones)
{
    dst[0] = COMP_TGT_HID;
    dst[1] = COMP_HID_MOUSE;
    dst[2] = (uint8_t)(botones & 0x03);
    // La FPGA SUMA estos dos a un acumulador (hid.v:99-100), no los sustituye:
    // hay que mandar el DELTA desde el ultimo envio, nunca la posicion.
    dst[3] = (uint8_t)dx;
    dst[4] = (uint8_t)dy;
    return 5;
}

size_t compBuildJoystick(uint8_t *dst, uint8_t dispositivo, uint8_t bits)
{
    dst[0] = COMP_TGT_HID;
    dst[1] = COMP_HID_JOYSTICK;
    dst[2] = dispositivo;       // 0 o 1
    dst[3] = bits;
    return 4;
}

static void enviar(Companion *c, const uint8_t *tx, size_t n)
{
    if (c && c->xfer) c->xfer(tx, 0, n, c->user);
}

void compKey(Companion *c, uint8_t codigo, bool pulsada)
{
    uint8_t t[COMP_MAX_FRAME];
    enviar(c, t, compBuildKey(t, codigo, pulsada));
}

void compMouse(Companion *c, int8_t dx, int8_t dy, uint8_t botones)
{
    uint8_t t[COMP_MAX_FRAME];
    enviar(c, t, compBuildMouse(t, dx, dy, botones));
}

void compJoystick(Companion *c, uint8_t dispositivo, uint8_t bits)
{
    uint8_t t[COMP_MAX_FRAME];
    enviar(c, t, compBuildJoystick(t, dispositivo, bits));
}

bool compStatus(Companion *c, uint8_t *version, uint8_t *subversion)
{
    if (!c || !c->xfer) return false;
    uint8_t tx[COMP_MAX_FRAME], rx[COMP_MAX_FRAME] = {0};
    size_t n = compBuildStatus(tx);
    c->xfer(tx, rx, n, c->user);
    // OJO al byte exacto, que costo caro. hid.v NO carga data_out en el byte
    // del comando: lo hace en el ESTADO 0, o sea un byte mas tarde. Con el
    // retraso de un byte del full duplex, la version aterriza en rx[3].
    //
    // Es DISTINTO de sdc_bridge y launcher_svc, que si cargan dout en el byte
    // del comando y por eso responden en rx[2]. Leer rx[2] aqui devolvia
    // siempre 0 y hacia creer que la FPGA no contestaba (el famoso "SPI v0"),
    // cuando llevaba contestando desde el primer dia.
    (void)n;
    if (version)    *version    = rx[3];
    if (subversion) *subversion = rx[4];
    return rx[3] != 0x00 && rx[3] != 0xFF;   // 00/FF = nadie contesta
}

// ==========================================================================
//  LANZADOR — pintar por el VDP y gobernar la SD/Z80
//  Ver launcher_svc.v (destino 2) y sdc_bridge.v (destino 3).
// ==========================================================================

size_t compBuildVdpWrite(uint8_t *dst, uint8_t puerto, uint8_t dato)
{
    dst[0] = COMP_TGT_OSD;
    dst[1] = COMP_LNZ_WRITE;
    dst[2] = (uint8_t)(puerto & 0x03);
    dst[3] = dato;
    return 4;
}

size_t compBuildVdpReg(uint8_t *dst, uint8_t registro, uint8_t valor)
{
    dst[0] = COMP_TGT_OSD;
    dst[1] = COMP_LNZ_BULK;
    dst[2] = VDP_PORT_REG;
    // El orden NO es opcional: el VDP espera primero el DATO y luego el numero
    // de registro con el bit 7 puesto. Al reves se escribe en un registro que
    // no toca, y encima sin dar error.
    dst[3] = valor;
    dst[4] = (uint8_t)(0x80 | (registro & 0x3F));
    return 5;
}

size_t compBuildLnzStatus(uint8_t *dst)
{
    dst[0] = COMP_TGT_OSD;
    dst[1] = COMP_LNZ_STATUS;
    dst[2] = 0;             // relleno: aqui llega VERSION
    dst[3] = 0;             //          aqui, lleno
    dst[4] = 0;             //          aqui, perdidos_hi
    dst[5] = 0;             //          aqui, perdidos_lo
    return 6;
}

size_t compBuildLnzKeys(uint8_t *dst)
{
    dst[0] = COMP_TGT_OSD;
    dst[1] = COMP_LNZ_KEYS;
    for (int i = 0; i < 16; i++) dst[2 + i] = 0;   // relleno de lectura
    return 18;
}

size_t compBuildSdTake(uint8_t *dst)
{
    dst[0] = COMP_TGT_SDC;
    dst[1] = COMP_SDC_TAKE;
    return 2;
}

size_t compBuildSdRelease(uint8_t *dst)
{
    dst[0] = COMP_TGT_SDC;
    dst[1] = COMP_SDC_RELEASE;
    return 2;
}

size_t compBuildSdRead(uint8_t *dst, uint32_t lba)
{
    dst[0] = COMP_TGT_SDC;
    dst[1] = COMP_SDC_READ;
    dst[2] = (uint8_t)( lba        & 0xFF);   // little endian, como sdc_bridge.v
    dst[3] = (uint8_t)((lba >>  8) & 0xFF);
    dst[4] = (uint8_t)((lba >> 16) & 0xFF);
    dst[5] = (uint8_t)((lba >> 24) & 0xFF);
    return 6;
}

// ---- envio ---------------------------------------------------------------

void compVdpWrite(Companion *c, uint8_t puerto, uint8_t dato)
{
    uint8_t t[COMP_MAX_FRAME];
    enviar(c, t, compBuildVdpWrite(t, puerto, dato));
}

void compVdpReg(Companion *c, uint8_t registro, uint8_t valor)
{
    uint8_t t[COMP_MAX_FRAME];
    enviar(c, t, compBuildVdpReg(t, registro, valor));
}

void compProbe(Companion *c, uint8_t destino, uint8_t comando, uint8_t *rx16)
{
    if (!c || !c->xfer || !rx16) return;
    // 16 bytes, y NADA se sobrescribe despues. La version de 8 se quedaba justo
    // en el byte donde empiezan los contadores, y ademas la prueba del pull-up
    // machacaba los dos ultimos: la sonda tapaba el dato que hacia falta.
    uint8_t tx[16];
    for (int i = 0; i < 16; i++) { tx[i] = 0; rx16[i] = 0; }
    tx[0] = destino; tx[1] = comando;
    c->xfer(tx, rx16, 16, c->user);
}

size_t compVdpBulk(Companion *c, uint8_t puerto, const uint8_t *datos, size_t n)
{
    if (!c || !c->xfer || !datos) return 0;
    // Trozos de 48: cada trama son 3 de cabecera + los datos, y asi se queda
    // holgadamente por debajo del maximo del driver SPI sin DMA.
    uint8_t t[51];
    size_t puestos = 0;
    while (puestos < n) {
        size_t k = n - puestos; if (k > 48) k = 48;
        t[0] = COMP_TGT_OSD;
        t[1] = COMP_LNZ_BULK;
        t[2] = (uint8_t)(puerto & 0x03);
        for (size_t i = 0; i < k; i++) t[3 + i] = datos[puestos + i];
        c->xfer(t, NULL, k + 3, c->user);
        puestos += k;
    }
    return puestos;
}

bool compLnzStatus(Companion *c, uint8_t *version, bool *lleno, uint16_t *perdidos)
{
    if (!c || !c->xfer) return false;
    uint8_t tx[COMP_MAX_FRAME], rx[COMP_MAX_FRAME] = {0};
    size_t n = compBuildLnzStatus(tx);
    c->xfer(tx, rx, n, c->user);
    // Full duplex con un byte de retraso: lo que el esclavo carga durante el
    // byte N se recibe en el N+1. Por eso la respuesta empieza en rx[2].
    uint8_t v = rx[2];
    if (version)  *version  = v;
    if (lleno)    *lleno    = (rx[3] & 1) != 0;
    if (perdidos) *perdidos = (uint16_t)((rx[4] << 8) | rx[5]);
    return v != 0x00 && v != 0xFF;      // 00/FF = no contesta nadie
}

bool compLnzKeys(Companion *c, uint8_t *destino16)
{
    if (!c || !c->xfer || !destino16) return false;
    uint8_t tx[COMP_MAX_FRAME], rx[COMP_MAX_FRAME] = {0};
    size_t n = compBuildLnzKeys(tx);
    c->xfer(tx, rx, n, c->user);
    for (int i = 0; i < 16; i++) destino16[i] = rx[2 + i];
    return true;
}

void compSdRead(Companion *c, uint32_t lba)
{
    uint8_t t[COMP_MAX_FRAME];
    enviar(c, t, compBuildSdRead(t, lba));
}

uint8_t g_ultimo_sdstat = 0;
uint8_t g_sd_pide = 0, g_sd_acaba = 0, g_sd_sector = 0;
uint16_t g_sd_ultsec = 0;

// 🚨 EL ENLACE CORROMPE TRAMAS. Medido en placa: la MISMA consulta alterna
// entre datos correctos y la trama ENTERA a ceros, y no mejora bajando el reloj
// de 13 MHz a 4, ni a 500 kHz, ni metiendo hueco entre tramas. Sin conocer aun
// la causa, el protocolo tiene que sobrevivir a ello: se VALIDA la respuesta
// (la version del puente es 1 por contrato) y se REINTENTA.
//
// Esto no tapa el fallo -- se sigue contando cuantas veces hay que reintentar,
// para no perder de vista que el canal esta mal.
uint16_t g_sd_reintentos = 0;

bool compSdStatus(Companion *c, uint8_t *ver, bool *busy, bool *hold)
{
    if (!c || !c->xfer) return false;
    // 9 bytes: ademas de version/ocupado/mando y card_stat, el puente devuelve
    // TRES CONTADORES (v31m) que separan lo que desde fuera se ve igual:
    //   n_pide   veces que ha levantado rstart
    //   n_acaba  pulsos de rdone vistos
    //   n_sector sectores volcados al buffer
    uint8_t tx[16] = { COMP_TGT_SDC, COMP_SDC_STATUS, 0,0,0,0,0,0,0,0,0 };
    uint8_t rx[16] = {0};
    // Hasta 6 intentos: la version tiene que salir 1. Una trama a ceros o con
    // cualquier otro valor ahi es basura y se descarta entera.
    for (int intento = 0; intento < 6; intento++) {
        for (int k = 0; k < 16; k++) rx[k] = 0;
        c->xfer(tx, rx, 11, c->user);
        if (rx[2] == 0x01) break;
        g_sd_reintentos++;
    }
    // sdc_bridge carga dout EN el byte del comando (a diferencia de hid.v):
    // rx[2]=VERSION, rx[3]={0,busy}, rx[4]={0,hold}.
    if (ver)  *ver  = rx[2];
    if (busy) *busy = (rx[3] & 1) != 0;
    if (hold) *hold = (rx[4] & 1) != 0;
    // rx[5] = {3'b0, sd_init, card_stat}: DONDE esta la maquina de la tarjeta.
    // Se anadio al RTL en la v31i y no se habia leido nunca; es justo el dato
    // que distingue "el lector esta aparcado en STANDBY" de "se quedo a medias
    // de una lectura", que se ven igual desde fuera (ocupado = 1 en ambos).
    // card_stat se trunca de 5 a 4 bits en sd_reader, asi que IDLING(17)->1 y
    // STANDBY(18)->2. Sigue valiendo para distinguirlos de READING(13/14).
    g_ultimo_sdstat = rx[5];
    g_sd_pide   = rx[6];
    g_sd_acaba  = rx[7];
    g_sd_sector = rx[10];
    // rx[8]/rx[9] = los 16 bits bajos del rsector con el que se lanzo la
    // ultima lectura, congelado DENTRO del puente. Si aqui sale 0 cuando
    // pedimos otro sector, el numero se pierde antes de llegar al lector.
    g_sd_ultsec = (uint16_t)(rx[8] | (rx[9] << 8));
    return rx[2] != 0x00 && rx[2] != 0xFF;
}

bool compSdBusy(Companion *c)
{
    if (!c || !c->xfer) return true;
    uint8_t tx[COMP_MAX_FRAME] = { COMP_TGT_SDC, COMP_SDC_STATUS, 0, 0, 0 };
    uint8_t rx[COMP_MAX_FRAME] = {0};
    c->xfer(tx, rx, 5, c->user);
    // sdc_bridge carga dout EN el byte del comando: rx[2]=VERSION, rx[3]=busy.
    return (rx[3] & 1) != 0;
}

// Pide un sector y COMPRUEBA, releyendo el estado, que el LBA quedo guardado.
// Sin esto, una trama de LEER corrupta lanza la lectura con sector 0 y devuelve
// el sector 0 sin dar el menor error: es justo lo que llevaba pasando.
bool compSdPedirVerificado(Companion *c, uint32_t lba)
{
    for (int intento = 0; intento < 6; intento++) {
        compSdRead(c, lba);
        uint8_t v = 0; bool b = false, h = false;
        compSdStatus(c, &v, &b, &h);
        if (v == 0x01 && g_sd_ultsec == (uint16_t)(lba & 0xFFFF)) return true;
        g_sd_reintentos++;
    }
    return false;
}

bool compSdSector(Companion *c, uint8_t *buf512)
{
    if (!c || !c->xfer || !buf512) return false;
    // 2 de cabecera + 512 de datos. NO se puede trocear: sdc_bridge reinicia
    // su puntero de lectura en cada trama (rd_ptr <= 0 en `start`), asi que
    // partirlo en dos devolveria dos veces el principio del sector.
    static uint8_t tx[514], rx[514];
    for (int i = 0; i < 514; i++) { tx[i] = 0; rx[i] = 0; }
    tx[0] = COMP_TGT_SDC;
    tx[1] = COMP_SDC_DATA;
    c->xfer(tx, rx, 514, c->user);
    for (int i = 0; i < 512; i++) buf512[i] = rx[2 + i];
    return true;
}

void compSdTake(Companion *c)
{
    uint8_t t[COMP_MAX_FRAME];
    enviar(c, t, compBuildSdTake(t));
}

void compSdRelease(Companion *c)
{
    uint8_t t[COMP_MAX_FRAME];
    enviar(c, t, compBuildSdRelease(t));
}
