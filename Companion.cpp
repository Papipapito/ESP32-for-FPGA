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
