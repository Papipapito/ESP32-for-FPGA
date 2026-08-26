/*
 * ScreenS3.cpp - Driver de pantalla del companion MSXnano sobre
 *                ESP32-1732S019 (ESP32-S3-WROOM-1 N16R8 +
 *                LCD ST7789V 170x320 IPS, en HORIZONTAL: 320x170).
 *
 * El contrato con el resto del firmware esta en ScreenS3.h. Aqui va el como.
 *
 * Este fichero es SOLO EL DRIVER: LCD, backlight, fuente y motor de celdas.
 * Depende del hardware (pines, ST7789, LEDC) pero no sabe nada de MSX: se
 * limita a pintar caracteres en una rejilla de 40x21. La narrativa del
 * arranque -el BASIC falso, la espera, `call system`, la consola MSX-DOS-
 * vive aparte, en ScreenS3Boot.cpp, y habla con este por la costura de
 * ScreenS3_internal.h. El porque de cortar justo por ahi esta razonado en
 * ese header.
 *
 * ===========================================================================
 * POR QUE Arduino_GFX (moononournation) Y NO TFT_eSPI
 * ===========================================================================
 * Las dos librerias mueven un ST7789 de sobra. La decision es de MANTENIMIENTO,
 * no de rendimiento:
 *
 *  1. TFT_eSPI configura los pines en User_Setup.h, un fichero DENTRO de la
 *     libreria, fuera del repo. Es decir: el pinout mas delicado del proyecto
 *     -el que YA tiene una version falsa circulando por internet (ver el aviso
 *     de BoardS3.h)- viviria en un sitio que no se versiona, que se pisa al
 *     actualizar la libreria y que no se ve al leer el codigo. Con Arduino_GFX
 *     los pines son ARGUMENTOS DEL CONSTRUCTOR, tres lineas mas abajo, visibles
 *     y versionados. Para este proyecto en concreto eso lo decide todo.
 *  2. Un User_Setup global rompe cualquier otro sketch que use TFT_eSPI con
 *     otra pantalla en la misma instalacion de Arduino.
 *  3. Display.ino (la placa vieja C6) ya usa Arduino_GFX. Una sola libreria de
 *     graficos en el arbol = un solo juego de rarezas que aprender.
 *
 * Contrapartida asumida: Arduino_GFX no trae sprites ni framebuffer con doble
 * buffer. No hacen falta: esto es una pantalla de TEXTO y aqui abajo hay un
 * motor de celdas que solo repinta lo que cambia (ver "MOTOR DE CELDAS").
 *
 * ===========================================================================
 * COMPROBADO CONTRA EL CODIGO FUENTE DE LA LIBRERIA (no de memoria)
 * ===========================================================================
 * Todo esto se leyo en el repo moononournation/Arduino_GFX antes de escribirlo:
 *
 *  - Arduino_ST7789::begin() hace `_override_datamode = SPI_MODE3` cuando
 *    compila para ESP32. O sea: el MODO SPI 3 que pide esta placa YA SALE
 *    SOLO, no hay que (ni se puede) pedirlo desde fuera. Es justo el modo que
 *    necesita este panel.  [src/display/Arduino_ST7789.cpp]
 *  - Arduino_ESP32SPI(dc, cs, sck, mosi, miso, spi_num, is_shared_interface).
 *    En el S3, FSPI vale 0 y el core lo documenta como "SPI 2 bus", o sea
 *    SPI2_HOST, que es el que pide la placa.  [esp32-hal-spi.h]
 *  - Arduino_GFX::setRotation() intercambia ancho y alto en las rotaciones
 *    impares. Por eso al constructor se le pasan las medidas NATIVAS del panel
 *    (170 ancho x 320 alto) y con rotacion 1 quedan los 320x170 utiles.
 *  - Los offsets se reparten asi en Arduino_TFT::setRotation():
 *        rot 0: xStart=COL_OFF1  yStart=ROW_OFF1
 *        rot 1: xStart=ROW_OFF1  yStart=COL_OFF2
 *        rot 2: xStart=COL_OFF2  yStart=ROW_OFF2
 *        rot 3: xStart=ROW_OFF2  yStart=COL_OFF1
 *    En horizontal el eje X de pantalla es el eje de FILAS del controlador, y
 *    el eje Y es el de COLUMNAS. Por eso el offset de 35 hay que darlo como
 *    offset de COLUMNA, y en los DOS huecos (COL_OFF1 y COL_OFF2) para que
 *    valga en las cuatro rotaciones. Es legitimo porque el panel de 170 px
 *    esta CENTRADO en la RAM de 240 columnas del ST7789: 35 + 170 + 35 = 240,
 *    simetrico, mismo offset mires desde donde mires.
 *
 * ===========================================================================
 * MOTOR DE CELDAS: por que no se pinta con gfx->print()
 * ===========================================================================
 * La consola DOS se refresca en vivo (reloj, trafico, temperatura). Repintar
 * la pantalla entera cada vez daria parpadeo y, sobre todo, bloquearia el
 * loop() unos 50 ms: inaceptable con el enlace UNAPI a 859372 bps corriendo
 * en el mismo loop.
 *
 * Solucion: una rejilla de 40x21 celdas de 8x8 px con DOS buffers en RAM.
 *   - s_buf    = lo que DEBE verse.  Lo escriben los renderizadores.
 *   - s_shadow = lo que YA esta en el LCD.
 * Los renderizadores pintan sobre s_buf tan a menudo como quieran (es RAM,
 * es gratis) y screenTick() vuelca al LCD solo las celdas donde los dos
 * buffers difieren, COMO MUCHO SCREEN_CELLS_PER_TICK por vuelta. Resultado:
 * cero parpadeo, coste acotado y nada de delay() en ningun sitio.
 *
 * 40 columnas no es capricho: 320/8 = 40 EXACTAS, las mismas que el SCREEN 0
 * del MSX. Toda la estetica del arranque cuelga de esa coincidencia.
 *
 * Copyright (c) 2026 - proyecto MSXnano. LGPL-2.1 o posterior, como el resto.
 */

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <string.h>
#include <stdio.h>

#include "BoardS3.h"        // pinout UNICO y verificado de la placa
#include "ScreenS3.h"
#include "ScreenS3_internal.h"   // geometria, paleta, costura con la narrativa
#include "LogoMsxBcn.h"          // bitmap RLE del logo (tools/make_logo_lcd.py)
#include "Font8x8MSX.h"

// ===========================================================================
//  AJUSTES
// ===========================================================================
// La geometria (SCR_COLS, SCR_ROWS, SCR_TEXT_ROWS) vive en ScreenS3_internal.h
// porque la comparten las dos mitades. Lo de aqui abajo es solo del driver.
#define SCREEN_ROTATION       1        // 1 = horizontal (320x170)
// 24 MHz es CONSERVADOR a proposito: no se ha podido probar en hardware. El
// ST7789 aguanta 40-60 MHz con buen ruteo y esta placa lleva el LCD soldado
// con pistas cortas, asi que en produccion deberia subirse. Si a 24 MHz se ven
// pixeles sueltos mal, el problema NO es la velocidad: mira antes el pinout.
#define SCREEN_SPI_HZ         24000000

#define SCR_ORIGIN_Y          1        // 170-168=2 px sobrantes: 1 arriba, 1 abajo

// Techo de celdas volcadas al LCD por llamada a screenTick(). Cada celda son
// 64 px = 128 bytes por SPI: a 24 MHz, ~43 us + gastos. 64 celdas ~ 4 ms en el
// PEOR caso (repintado completo), y solo ocurre al cambiar de fase. En regimen
// normal cambian 1-10 celdas por segundo y el coste es despreciable.
#define SCREEN_CELLS_PER_TICK 64

#define SCR_T_SENSOR          1000     // lectura del sensor de temperatura
#define SCR_T_BL_STEP         6        // paso del fundido de backlight

// ===========================================================================
//  PALETA
// ===========================================================================
// Los indices (PAL_*) y el empaquetado de atributos (ATTR*) estan en
// ScreenS3_internal.h: los usan los dos ficheros. Aqui vive solo la conversion
// a lo que entiende el panel.
// De 24 bits a RGB565: 5 bits de rojo, 6 de verde, 5 de azul.
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const uint16_t SCR_PAL[PAL_COUNT] = {
    RGB565(0x3A, 0x31, 0xD2),   // 0 #3A31D2 azul MSX      -> 0x399A
    RGB565(0xFF, 0xFF, 0xFF),   // 1 #FFFFFF blanco        -> 0xFFFF
    RGB565(0x6E, 0xC2, 0xF0),   // 2 #6EC2F0 cian franja   -> 0x6E1E
    RGB565(0x3A, 0x31, 0xD2),   // 3 #3A31D2 azul oscuro sobre el cian
    RGB565(0x08, 0x09, 0x0A),   // 4 #08090A fondo DOS     -> 0x0841
    RGB565(0xFF, 0xB5, 0x3C),   // 5 #FFB53C ambar         -> 0xFDA7
    RGB565(0x8C, 0x63, 0x21),   // 6 #8C6321 ambar al 55 %
};
// El "azul oscuro" de la franja es EL MISMO #3A31D2 del fondo BASIC. No es
// pereza: es el emparejamiento del MSX real (la franja de teclas usa el color
// de fondo como tinta) y ademas ata las dos pantallas a una sola paleta.

// ===========================================================================
//  ESTADO
// ===========================================================================
static Arduino_DataBus *s_bus = nullptr;
// Arduino_TFT y no Arduino_GFX: writeAddrWindow() y writeRepeat() -que usa el
// volcado del logo- viven en Arduino_TFT, no en la clase base. Arduino_ST7789
// deriva de Arduino_TFT, que a su vez deriva de Arduino_GFX, asi que todo lo
// demas sigue disponible igual. (Comprobado en las cabeceras de la libreria
// instalada, no de memoria: Arduino_GFX.h:288/293 y Arduino_TFT.h:21/31.)
static Arduino_TFT     *s_gfx = nullptr;
static bool             s_lcdOk = false;

static ScrCell  s_buf[SCR_ROWS][SCR_COLS];      // lo que debe verse
static ScrCell  s_shadow[SCR_ROWS][SCR_COLS];   // lo que ya esta en el LCD
static uint8_t  s_rowDirty[SCR_ROWS];
static uint8_t  s_scanRow = 0;                  // reparto justo del volcado

// Estado que empuja el resto del firmware y leen los renderizadores de
// ScreenS3Boot.cpp. Se define UNA sola vez, aqui; el struct esta declarado en
// ScreenS3_internal.h. Al ser global de verdad arranca a cero, igual que los
// static que sustituye.
ScrShared g_scr;

// Backlight
static uint8_t  s_blTarget = 0, s_blCur = 0;
static uint32_t s_tBl = 0;
#if !(defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define SCR_BL_CHANNEL 7      // core 2.x: canal LEDC alto, lejos de los tipicos
#endif

// ===========================================================================
//  MOTOR DE CELDAS
// ===========================================================================
// Las que NO llevan `static` son las que usa la narrativa (van declaradas en
// ScreenS3_internal.h). El resto -el glifo, el volcado por celda y el barrido-
// es cocina interna del driver y no sale de aqui.
static inline const uint8_t *glyphOf(uint8_t ch)
{
    if (ch < FONT8X8_FIRST || ch > FONT8X8_LAST) ch = ' ';   // fuera de tabla -> espacio
    return FONT8X8[ch - FONT8X8_FIRST];
}

// Escribe una celda en el buffer logico. Marca la fila como sucia SOLO si el
// resultado difiere de lo que hay pintado: por eso los renderizadores pueden
// reescribir la pantalla entera cada 250 ms sin generar ni un byte de SPI.
void cellSet(int r, int c, char ch, uint8_t attr)
{
    if ((unsigned)r >= SCR_ROWS || (unsigned)c >= SCR_COLS) return;   // clip
    s_buf[r][c].ch = (uint8_t)ch;
    s_buf[r][c].attr = attr;
    if (s_buf[r][c].ch != s_shadow[r][c].ch || s_buf[r][c].attr != s_shadow[r][c].attr)
        s_rowDirty[r] = 1;
}

void cellPuts(int r, int c, const char *s, uint8_t attr)
{
    if (!s) return;
    while (*s && c < SCR_COLS) cellSet(r, c++, *s++, attr);
}

// Campo de ancho FIJO: trunca si sobra y rellena con espacios si falta. Es lo
// que evita que al acortarse un valor vivo (SSID, caudal) quede basura del
// valor anterior a la derecha.
void cellField(int r, int c, const char *s, int w, uint8_t attr)
{
    int i = 0;
    if (s) for (; i < w && s[i]; i++) cellSet(r, c + i, s[i], attr);
    for (; i < w; i++) cellSet(r, c + i, ' ', attr);   // relleno hasta el ancho
}

void cellFieldRight(int r, const char *s, int w, uint8_t attr)
{
    cellField(r, SCR_COLS - w, s, w, attr);
}

void cellRowFill(int r, char ch, uint8_t attr)
{
    for (int c = 0; c < SCR_COLS; c++) cellSet(r, c, ch, attr);
}

void cellClearAll(uint8_t attr)
{
    for (int r = 0; r < SCR_ROWS; r++) cellRowFill(r, ' ', attr);
}

// Sube una fila el bloque [firstRow..lastRow] y deja lastRow en blanco, que es
// lo que hace cualquier terminal al llegar al final. Vive aqui y no en la
// narrativa porque es el UNICO que necesita leer s_buf, y s_buf no sale del
// driver. Se copia celda a celda con cellSet() a proposito: asi el marcado de
// filas sucias sale gratis y solo bajan al LCD las que de verdad cambian.
void cellScrollUp(int firstRow, int lastRow, uint8_t fillAttr)
{
    if (firstRow < 0) firstRow = 0;
    if (lastRow > SCR_ROWS - 1) lastRow = SCR_ROWS - 1;
    for (int r = firstRow + 1; r <= lastRow; r++)
        for (int c = 0; c < SCR_COLS; c++)
            cellSet(r - 1, c, s_buf[r][c].ch, s_buf[r][c].attr);
    cellRowFill(lastRow, ' ', fillAttr);
}

// Vuelca UNA celda al LCD. 64 px en un solo draw16bitRGBBitmap = una sola
// ventana de direccionamiento y una sola rafaga SPI.
static void cellBlit(int r, int c)
{
    const ScrCell cell = s_buf[r][c];
    const uint16_t fg = SCR_PAL[cell.attr & 0x0F];
    const uint16_t bg = SCR_PAL[(cell.attr >> 4) & 0x0F];
    const uint8_t *g = glyphOf(cell.ch);
    uint16_t px[FONT8X8_W * FONT8X8_H];
    int i = 0;
    for (int y = 0; y < FONT8X8_H; y++) {
        const uint8_t bits = g[y];
        // OJO: en esta fuente el bit 0 es el pixel de la IZQUIERDA (al reves
        // que en Adafruit_GFX). Verificado contra el render.c del repo origen.
        for (int x = 0; x < FONT8X8_W; x++) px[i++] = ((bits >> x) & 1) ? fg : bg;
    }
    s_gfx->draw16bitRGBBitmap(c * FONT8X8_W, SCR_ORIGIN_Y + r * FONT8X8_H,
                              px, FONT8X8_W, FONT8X8_H);
}

// Vuelca hasta SCREEN_CELLS_PER_TICK celdas sucias y vuelve. Recorre las filas
// en round-robin para que ninguna se quede sin volcar si el presupuesto se
// agota siempre en la misma.
// Mientras hay un bitmap en pantalla (el logo de arranque) el motor de celdas
// se CONGELA. Sin esto pasa lo siguiente, que se vio en la placa: al invalidar
// la rejilla el motor considera sucias las 840 celdas y se pone a pintarlas
// -vacias, azules- ENCIMA del logo, que desaparece en menos de un segundo.
// El motor no sabe que hay debajo; solo sabe que su rejilla no coincide con lo
// que cree que hay. Asi que hay que decirle explicitamente que no toque nada
// hasta que la fase del bitmap termine.
static bool s_bitmapHold = false;

static void cellFlush()
{
    if (s_bitmapHold) return;       // logo en pantalla: no pintar nada encima
    int budget = SCREEN_CELLS_PER_TICK;
    for (int n = 0; n < SCR_ROWS && budget > 0; n++) {
        const int r = (s_scanRow + n) % SCR_ROWS;
        if (!s_rowDirty[r]) continue;
        bool complete = true;
        for (int c = 0; c < SCR_COLS; c++) {
            if (s_buf[r][c].ch == s_shadow[r][c].ch &&
                s_buf[r][c].attr == s_shadow[r][c].attr) continue;
            if (budget <= 0) { complete = false; break; }
            cellBlit(r, c);
            s_shadow[r][c] = s_buf[r][c];
            budget--;
        }
        if (complete) s_rowDirty[r] = 0;
        s_scanRow = (r + 1) % SCR_ROWS;
    }
}

// ===========================================================================
//  BACKLIGHT (LEDC, activo ALTO, arranca apagado)
// ===========================================================================
// La API de LEDC cambio en Arduino-ESP32 3.x (ahora se direcciona por PIN, no
// por canal). Se soportan las dos porque el repo aun no fija version minima.
static void blApply(uint8_t pct)
{
    if (pct > 100) pct = 100;
    const uint32_t duty = (uint32_t)pct * 255 / 100;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(S3_LCD_BL, duty);
#else
    ledcWrite(SCR_BL_CHANNEL, duty);
#endif
}

static void blInit()
{
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(S3_LCD_BL, 5000, 8);          // 5 kHz, 8 bits: sin zumbido audible
#else
    ledcSetup(SCR_BL_CHANNEL, 5000, 8);
    ledcAttachPin(S3_LCD_BL, SCR_BL_CHANNEL);
#endif
    blApply(0);      // apagado ANTES de tocar el panel: nadie ve el ruido de encendido
}

// ===========================================================================
//  LOGO DE ARRANQUE
// ===========================================================================
// Pinta el logo de MSX Barcelona SALTANDOSE el motor de celdas: es un bitmap,
// no texto. Los datos vienen comprimidos con RLE (ver LogoMsxBcn.h y el script
// que lo genera); se descomprime al vuelo por tramos y se empuja al LCD sin
// buffer intermedio, que no hay 108 KB de RAM que malgastar en esto.
//
// Hay DOS trampas aqui, y las dos se pagan en pantalla:
//
//  1. Mientras el logo esta visible el motor de celdas tiene que estar QUIETO.
//     Si no, repinta su rejilla (vacia) encima y el logo se borra solo. Por eso
//     se levanta s_bitmapHold, que congela cellFlush() hasta que alguien llame
//     a screenInvalidate().
//  2. Al escribir directo en el LCD, s_shadow -lo que el motor CREE que hay en
//     pantalla- se queda mintiendo. Si despues el BASIC pide azul donde el
//     shadow ya dice azul, el motor se lo salta por optimizacion y el logo se
//     quedaria pegado. Por eso al descongelar hay que invalidar TODO.
//
// Resumen del contrato: screenDrawLogo() pinta y congela; screenInvalidate()
// descongela y fuerza un repintado completo. Quien muestra un bitmap es
// responsable de llamar a la segunda cuando termina.
void screenDrawLogo()
{
    if (!s_lcdOk) return;

    s_gfx->startWrite();
    s_gfx->writeAddrWindow(0, 0, LOGO_W, LOGO_H);
    for (uint32_t i = 0; i < LOGO_RUNS; i++) {
        const uint16_t n = LOGO_RLE[i][0];
        const uint16_t c = LOGO_RLE[i][1];
        s_gfx->writeRepeat(c, n);
    }
    s_gfx->endWrite();

    s_bitmapHold = true;        // el motor no toca nada hasta el invalidate
}

// Descongela el motor y marca TODA la rejilla como no dibujada, para que la
// siguiente pantalla se repinte entera sobre lo que hubiera (el logo).
void screenInvalidate()
{
    s_bitmapHold = false;
    // 0xFF en el caracter es un valor que el motor no genera nunca, asi que
    // ninguna celda puede coincidir con el shadow y todas se consideran sucias.
    memset(s_shadow, 0xFF, sizeof(s_shadow));
    memset(s_rowDirty, 1, sizeof(s_rowDirty));
}

// ===========================================================================
//  API PUBLICA
// ===========================================================================
bool screenSetup()
{
    memset(s_buf, 0, sizeof(s_buf));
    memset(s_shadow, 0, sizeof(s_shadow));
    memset(s_rowDirty, 0, sizeof(s_rowDirty));

    blInit();       // primero apagar: el panel arranca con ruido en la RAM

    // Los pines vienen de BoardS3.h, que es el unico sitio revisado. FSPI en
    // el S3 = SPI2_HOST. Sin MISO: el panel no lo cablea.
    s_bus = new Arduino_ESP32SPI(S3_LCD_DC, S3_LCD_CS, S3_LCD_SCLK, S3_LCD_MOSI,
                                 GFX_NOT_DEFINED, FSPI, true /* bus compartido */);
    // Medidas NATIVAS del panel (vertical): 170 ancho x 320 alto. La rotacion 1
    // las intercambia y deja los 320x170 utiles. El offset de columna 35 va en
    // COL_OFFSET1 y COL_OFFSET2 porque el panel esta centrado en la RAM de 240
    // columnas (35+170+35) y asi vale en las cuatro rotaciones.
    s_gfx = new Arduino_ST7789(s_bus, S3_LCD_RST, SCREEN_ROTATION, true /* IPS */,
                               S3_LCD_H /* 170 */, S3_LCD_W /* 320 */,
                               S3_LCD_XOFF, 0, S3_LCD_XOFF, 0);

    // OJO: begin() bloquea ~340 ms por los delays de reset que exige el
    // ST7789. Es la unica llamada bloqueante de todo el modulo y ocurre en
    // setup(), antes de que el MSX pueda hablar.
    if (!s_gfx->begin(SCREEN_SPI_HZ)) { s_lcdOk = false; return false; }
    s_lcdOk = true;

    s_gfx->fillScreen(SCR_PAL[PAL_MSX_BG]);   // fondo antes de encender la luz
    g_scr.tempC = temperatureRead();          // para no mostrar "TEMP 0C" el primer segundo
    scrBootReset();                            // la narrativa vuelve a la fase 1
    s_blTarget = 100;                          // el fundido lo hace screenTick()
    return true;
}

void screenTick()
{
    if (!s_lcdOk) return;
    const uint32_t now = millis();

    // Sensor interno del SoC (no el del FPGA). Es lento de leer: una vez/s.
    static uint32_t tSensor = 0;
    if (now - tSensor >= SCR_T_SENSOR) { tSensor = now; g_scr.tempC = temperatureRead(); }

    scrBootTick(now);   // la narrativa escribe en la rejilla...
    cellFlush();        // ...y aqui es donde de verdad baja al LCD

    // Fundido del backlight, solo DESPUES de que haya algo pintado: asi no se
    // ve nunca el contenido aleatorio de la RAM del panel al arrancar.
    if (s_blCur != s_blTarget && (now - s_tBl) >= SCR_T_BL_STEP) {
        s_tBl = now;
        s_blCur += (s_blCur < s_blTarget) ? 1 : -1;
        blApply(s_blCur);
    }
}

void screenSetWifi(const char *ssid, const char *ip, int rssi, bool ok)
{
    // Frontera del sistema: copiar acotado, nunca fiarse del largo ajeno.
    snprintf(g_scr.ssid, sizeof(g_scr.ssid), "%s", (ssid && *ssid) ? ssid : "");
    snprintf(g_scr.ip,   sizeof(g_scr.ip),   "%s", (ip && *ip) ? ip : "");
    g_scr.rssi = rssi;
    g_scr.wifiOk = ok;
    g_scr.wifiResolved = true;
}

void screenSetUsb(bool kbd, bool pad)
{
    g_scr.usbKbd = kbd; g_scr.usbPad = pad; g_scr.usbResolved = true;
}

void screenSetTurbo(bool on) { g_scr.turbo = on; }

void screenSetLauncher(uint8_t ver, uint16_t perdidos, uint32_t enviados,
                       uint8_t color, bool hold, uint8_t fase, uint8_t hidVer)
{
    g_scr.lnzOn = true;
    g_scr.lnzVer = ver;   g_scr.lnzPerdidos = perdidos;
    g_scr.lnzEnviados = enviados;
    g_scr.lnzColor = color; g_scr.lnzHold = hold;
    g_scr.lnzFase = fase; g_scr.lnzHidVer = hidVer;
}

void screenSetLauncherRaw(const uint8_t *rx8)
{
    for (int i = 0; i < 16; i++) g_scr.lnzRaw[i] = rx8[i];
}

void screenSetSd(uint16_t firma, bool ok, const uint8_t *ini4)
{
    g_scr.sdFirma = firma;
    g_scr.sdOk = ok;
    for (int i = 0; i < 4; i++) g_scr.sdIni[i] = ini4[i];
}

void screenSetSdDiag(uint8_t ver, bool hold, bool busy0, bool busy1, uint8_t intentos)
{
    g_scr.sdVer = ver; g_scr.sdHold = hold;
    g_scr.sdBusy0 = busy0; g_scr.sdBusy1 = busy1;
    g_scr.sdIntentos = intentos;
}

void screenSetFs(bool ok, uint8_t err, int n, const char prim[3][20],
                 uint32_t lba, uint8_t tipo, uint8_t nparts, const uint8_t *ini4,
                 uint32_t pmAntes, uint32_t pmDespues)
{
    for (int i = 0; i < 4; i++) g_scr.fsIni[i] = ini4 ? ini4[i] : 0;
    g_scr.pmAntes = pmAntes; g_scr.pmDespues = pmDespues;
    g_scr.fsOk = ok; g_scr.fsErr = err; g_scr.fsN = n;
    g_scr.fsLba = lba; g_scr.fsTipo = tipo; g_scr.fsNParts = nparts;
    for (int i = 0; i < 3; i++) {
        strncpy(g_scr.fsPrim[i], prim[i], sizeof(g_scr.fsPrim[i]) - 1);
        g_scr.fsPrim[i][sizeof(g_scr.fsPrim[i]) - 1] = 0;
    }
}

void screenSetTraffic(uint32_t rxBytesPerSec, uint32_t txBytesPerSec)
{
    g_scr.rxBps = rxBytesPerSec; g_scr.txBps = txBytesPerSec;
}

void screenSetClock(int hh, int mm, int ss)
{
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return;
    g_scr.clockBaseSec = hh * 3600 + mm * 60 + ss;
    g_scr.clockBaseMs = millis();
    g_scr.clockPushed = true;
}

// Quien decide que hacer con el fallo es la narrativa: depende de por que fase
// del arranque vaya. El driver solo pasa el recado.
void screenBootFailed(const char *motivo) { scrBootFail(motivo); }

void screenSetBacklight(uint8_t percent)
{
    s_blTarget = (percent > 100) ? 100 : percent;
}

bool screenBootDone() { return scrBootIsDos(); }
