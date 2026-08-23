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
    return 4;
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
    // hid.v CMD 0: data_out = 1 en el estado 0 y 0 en el 1. Como el SPI es
    // full duplex y va un byte por detras, esos valores salen en los huecos
    // que dejamos al final de la trama.
    if (version)    *version    = rx[n - 2];
    if (subversion) *subversion = rx[n - 1];
    return rx[n - 2] != 0x00 && rx[n - 2] != 0xFF;   // 00/FF = nadie contesta
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

void compProbe(Companion *c, uint8_t destino, uint8_t comando, uint8_t *rx8)
{
    if (!c || !c->xfer || !rx8) return;
    uint8_t tx[8] = { destino, comando, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 8; i++) rx8[i] = 0;
    c->xfer(tx, rx8, 8, c->user);
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
