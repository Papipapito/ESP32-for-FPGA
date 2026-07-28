/*
 * ScreenS3_internal.h - Costura interna entre las dos mitades de la pantalla.
 *
 * NO es la API publica del modulo: esa es ScreenS3.h. Este fichero solo lo
 * incluyen ScreenS3.cpp y ScreenS3Boot.cpp.
 *
 * ---------------------------------------------------------------------------
 * POR QUE ESTAN PARTIDOS EN DOS
 * ---------------------------------------------------------------------------
 * Hay dos cosas distintas metidas en el mismo sitio si no se separan:
 *
 *   ScreenS3.cpp      EL DRIVER. LCD, backlight, fuente y motor de celdas.
 *                     Depende del hardware (pines, ST7789, LEDC) pero NO sabe
 *                     nada de MSX: solo pinta caracteres en una rejilla.
 *
 *   ScreenS3Boot.cpp  LA HISTORIA. El BASIC falso, la espera, `call system`,
 *                     la consola MSX-DOS. No toca un solo registro: solo
 *                     escribe caracteres en la rejilla del driver.
 *
 * Esa frontera es justo por donde hay que cortar para portar al MSXimus: se
 * reaprovecha el driver entero cambiando cuatro pines, y la narrativa se
 * retoca (o se reescribe) sin rozar el hardware. Ademas mantiene los dos
 * ficheros por debajo del limite de 500 lineas del proyecto.
 */

#ifndef _SCREENS3_INTERNAL_H
#define _SCREENS3_INTERNAL_H

#include <stdint.h>

// ===========================================================================
//  GEOMETRIA DE LA REJILLA
// ===========================================================================
// 320 px / 8 = 40 columnas EXACTAS: las mismas que el SCREEN 0 del MSX. Toda
// la estetica del arranque cuelga de esa coincidencia.
// 170 px / 8 = 21,25 -> 21 filas (168 px). Sobran 2 px que se reparten arriba
// y abajo como margen.
#define SCR_COLS        40
#define SCR_ROWS        21
#define SCR_TEXT_ROWS   (SCR_ROWS - 1)   // la ultima fila es la franja de teclas

// ===========================================================================
//  PALETA
// ===========================================================================
// Los colores exactos estan documentados en SCREEN_DESIGN.md. El atributo de
// cada celda empaqueta fondo y tinta en un byte: (fondo << 4) | tinta.
enum {
    PAL_MSX_BG = 0,   // #3A31D2 azul MSX
    PAL_MSX_FG,       // #FFFFFF blanco
    PAL_STRIP_BG,     // #6EC2F0 cian de la franja de teclas
    PAL_STRIP_FG,     // #3A31D2 azul oscuro sobre el cian
    PAL_DOS_BG,       // #08090A casi negro
    PAL_DOS_FG,       // #FFB53C fosforo ambar
    PAL_DOS_DIM,      // #8C6321 ambar apagado (etiquetas y separadores)
    PAL_COUNT
};

#define ATTR(bg, fg)   ((uint8_t)(((bg) << 4) | (fg)))
#define ATTR_BASIC     ATTR(PAL_MSX_BG,   PAL_MSX_FG)
#define ATTR_STRIP     ATTR(PAL_STRIP_BG, PAL_STRIP_FG)
#define ATTR_DOS       ATTR(PAL_DOS_BG,   PAL_DOS_FG)
#define ATTR_DOS_DIM   ATTR(PAL_DOS_BG,   PAL_DOS_DIM)
#define ATTR_INVERT(a) ((uint8_t)((((a) & 0x0F) << 4) | (((a) >> 4) & 0x0F)))

typedef struct { uint8_t ch; uint8_t attr; } ScrCell;

// ===========================================================================
//  MOTOR DE CELDAS  (lo implementa ScreenS3.cpp)
// ===========================================================================
// Escribir en la rejilla es GRATIS: solo toca RAM. El volcado al LCD lo hace
// screenTick() y solo para las celdas que de verdad cambiaron. Por eso los
// renderizadores pueden repintarlo todo cada 250 ms sin generar parpadeo ni
// trafico SPI.
void cellSet(int r, int c, char ch, uint8_t attr);
void cellPuts(int r, int c, const char *s, uint8_t attr);
// Campo de ancho FIJO: trunca si sobra y rellena con espacios si falta. Es lo
// que impide que al acortarse un valor vivo quede basura del anterior.
void cellField(int r, int c, const char *s, int w, uint8_t attr);
void cellFieldRight(int r, const char *s, int w, uint8_t attr);
void cellRowFill(int r, char ch, uint8_t attr);
void cellClearAll(uint8_t attr);
// Sube una fila el bloque [firstRow..lastRow] y deja lastRow en blanco.
void cellScrollUp(int firstRow, int lastRow, uint8_t fillAttr);

// ===========================================================================
//  ESTADO COMPARTIDO
// ===========================================================================
// Lo rellenan los setters publicos (en ScreenS3.cpp) y lo leen los
// renderizadores (en ScreenS3Boot.cpp).
typedef struct {
    char     ssid[33];
    char     ip[16];
    int      rssi;
    bool     wifiOk, wifiResolved;
    bool     usbKbd, usbPad, usbResolved;
    bool     turbo;
    uint32_t rxBps, txBps;
    float    tempC;
    // Reloj: si alguien llama a screenSetClock() pasamos a modo "empujado" y
    // no se vuelve a mirar time(). La base + millis permite que siga corriendo
    // aunque solo nos empujen la hora de vez en cuando.
    bool     clockPushed;
    int32_t  clockBaseSec;
    uint32_t clockBaseMs;
    // Cursor de texto, compartido por las dos pantallas.
    uint8_t  curRow, curCol;
} ScrShared;

extern ScrShared g_scr;

// ===========================================================================
//  NARRATIVA DEL ARRANQUE  (la implementa ScreenS3Boot.cpp)
// ===========================================================================
void scrBootReset();               // vuelve a la fase 1
void scrBootTick(uint32_t now);    // un paso de la maquina de estados
void scrBootFail(const char *motivo);
bool scrBootIsDos();               // true cuando la consola DOS esta en pantalla

#endif // _SCREENS3_INTERNAL_H
