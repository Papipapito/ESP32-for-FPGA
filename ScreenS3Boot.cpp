/*
 * ScreenS3Boot.cpp - La NARRATIVA del arranque: el BASIC falso, la espera
 *                    real, el `call system` auto-tecleado y la consola
 *                    MSX-DOS viva.
 *
 * Aqui NO se toca un solo registro del LCD ni se sabe que pantalla hay debajo.
 * Todo lo que hace este fichero es escribir caracteres en la rejilla de 40x21
 * que expone el driver (ScreenS3.cpp) a traves de ScreenS3_internal.h. El
 * porque de estar partidos en dos esta razonado en ese header: es la frontera
 * que permite llevarse el driver entero al MSXimus cambiando cuatro pines y
 * retocar (o reescribir) la historia sin rozar el hardware.
 *
 * La IDEA del diseno -que la secuencia no es una animacion decorativa sino una
 * BARRA DE PROGRESO DISFRAZADA de MSX, y que cada fase corresponde a un hito
 * real del firmware- esta contada en ScreenS3.h. Aqui va el como.
 *
 * Copyright (c) 2026 - proyecto MSXnano. LGPL-2.1 o posterior, como el resto.
 */

#include <Arduino.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "ScreenS3.h"
#include "ScreenS3_internal.h"

// El reloj cae al de la configuracion UNAPI (offset GMT) si nadie empuja hora.
// Ponlo a 0 para portar ScreenS3 a un firmware sin UNAPI (p. ej. el MSXimus).
#ifndef SCREEN_USE_UNAPI_GLOBALS
  #define SCREEN_USE_UNAPI_GLOBALS 1
#endif
#if SCREEN_USE_UNAPI_GLOBALS
  #include "UNAPIESP.h"
  extern ESPConfig stDeviceConfiguration;   // .iGMT = offset horario en HORAS
#endif

// ===========================================================================
//  AJUSTES
// ===========================================================================
// -- Tiempos de la animacion (ms) ------------------------------------------
#define SCR_T_BANNER_CHAR     14       // tecleo del banner BASIC
#define SCR_T_BANNER_LINE     160      // respiro al terminar cada linea
#define SCR_T_CURSOR          500      // medio periodo de parpadeo del cursor
#define SCR_T_CMD_CHAR        80       // tecleo de `call system` (especificado)
#define SCR_T_ENTER           420      // pausa tras pulsar Enter
#define SCR_T_DOS_REFRESH     250      // refresco de los campos vivos del DOS
// Techo de la fase 2. NO es la duracion de la espera (esa la marca el hardware
// real): es la red de seguridad para que la pantalla no se quede colgada para
// siempre si un subsistema nunca contesta.
#define SCR_T_BOOT_TIMEOUT    45000

// Fase 0: logo de MSX Barcelona antes del BASIC. A 0 se salta y el arranque
// empieza directamente en la pantalla azul.
#ifndef SCREEN_BOOT_LOGO
  #define SCREEN_BOOT_LOGO 1
#endif
#define SCR_T_LOGO            2500     // cuanto se queda el logo en pantalla

// Franja de teclas de funcion al pie de la pantalla de BASIC. APAGADA: en la
// placa real se vio que durante el arranque no aporta (hora, WiFi y temperatura
// aun no existen) y ensucia una pantalla que gana estando limpia. La informacion
// sale igualmente despues, en la consola MSX-DOS. A 1 vuelve.
#ifndef SCREEN_BASIC_FKEY_STRIP
  #define SCREEN_BASIC_FKEY_STRIP 0
#endif

// Cursor: 0 = guion bajo (como en los mockups del diseno), 1 = bloque solido
// (que es lo que hace el MSX de verdad). Un cambio de un byte.
#ifndef SCREEN_CURSOR_BLOCK
  #define SCREEN_CURSOR_BLOCK 0
#endif
#define SCR_CURSOR_CH         '_'

// ===========================================================================
//  ESTADO
// ===========================================================================
// Lo de aqui abajo es SOLO de la historia: por donde va el tecleo y en que
// fase estamos. Lo que empuja el firmware (WiFi, USB, turbo, reloj, cursor)
// vive en g_scr, compartido con el driver.
//
// Fases del arranque. Ver SCREEN_DESIGN.md para el diagrama completo.
enum ScrState {
    ST_INIT = 0,    // LCD listo, nada pintado
    ST_LOGO,        // fase 0: logo de MSX Barcelona (bitmap)
    ST_BASIC_TYPE,  // fase 1: tecleando el banner de BASIC
    ST_WAIT,        // fase 2: cursor parpadeando, esperando WiFi + USB REALES
    ST_TYPE_CMD,    // fase 3: tecleando `call system`
    ST_ENTER,       // fase 3: pausa tras Enter
    ST_DOS,         // fase 4: consola MSX-DOS viva
    ST_FAILED       // algo fallo: nos quedamos en BASIC informando
};
static ScrState s_state = ST_INIT;
static uint32_t s_tNext = 0;        // proximo vencimiento de la animacion
static uint32_t s_tEnterWait = 0;   // instante en que empezo la fase 2
static uint8_t  s_bLine = 0, s_bCol = 0;   // avance del tecleo del banner
static uint8_t  s_cmdCol = 0;              // avance del tecleo de `call system`

static char     s_failReason[SCR_COLS + 1] = "";
static bool     s_failPending = false;   // fallo notificado antes de tener pantalla

// ===========================================================================
//  FORMATEADORES
// ===========================================================================
static void fmtRate(char *buf, size_t n, uint32_t bps)
{
    // Sin coma flotante: printf de floats en el ESP32 arrastra codigo y tiempo.
    if (bps < 1000)          snprintf(buf, n, "%luB/s", (unsigned long)bps);
    else if (bps < 1000000)  snprintf(buf, n, "%lu.%luK/s",
                                      (unsigned long)(bps / 1000), (unsigned long)((bps % 1000) / 100));
    else                     snprintf(buf, n, "%lu.%luM/s",
                                      (unsigned long)(bps / 1000000), (unsigned long)((bps % 1000000) / 100000));
}

// RSSI -> 0..5 barras. Cortes habituales de WiFi en 2,4 GHz.
static int rssiBars(int rssi)
{
    if (rssi >= -50) return 5;
    if (rssi >= -60) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -75) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

static void fmtBar(char *buf, size_t n, int bars)
{
    char t[8];
    int i = 0;
    t[i++] = '[';
    for (int b = 0; b < 5; b++) t[i++] = (b < bars) ? '#' : '-';
    t[i++] = ']';
    t[i] = 0;
    snprintf(buf, n, "%s", t);
}

// Devuelve true si hay hora valida. hh/mm/ss solo se tocan en ese caso.
static bool clockNow(int *hh, int *mm, int *ss)
{
    if (g_scr.clockPushed) {
        const uint32_t elapsed = (millis() - g_scr.clockBaseMs) / 1000u;
        const int32_t t = (g_scr.clockBaseSec + (int32_t)elapsed) % 86400;
        *hh = t / 3600; *mm = (t % 3600) / 60; *ss = t % 60;
        return true;
    }
#if SCREEN_USE_UNAPI_GLOBALS
    // Igual que Display.ino: epoch > 2023-11 significa que SNTP ya sincronizo.
    time_t tnow = time(nullptr);
    if (tnow > 1700000000) {
        time_t t = tnow + (time_t)stDeviceConfiguration.iGMT * 3600;   // a hora LOCAL
        struct tm tmv;
        gmtime_r(&t, &tmv);
        *hh = tmv.tm_hour; *mm = tmv.tm_min; *ss = tmv.tm_sec;
        return true;
    }
#endif
    return false;
}

static void fmtClock(char *buf, size_t n)
{
    int hh, mm, ss;
    if (clockNow(&hh, &mm, &ss)) snprintf(buf, n, "%02d:%02d:%02d", hh, mm, ss);
    else                         snprintf(buf, n, "--:--:--");
}

static const char *usbText()
{
    if (g_scr.usbKbd && g_scr.usbPad) return "teclado + mando";
    if (g_scr.usbKbd)                 return "teclado";
    if (g_scr.usbPad)                 return "mando";
    return g_scr.usbResolved ? "(nada enchufado)" : "buscando...";
}

// ===========================================================================
//  CURSOR
// ===========================================================================
// Parpadeo libre derivado de millis(): sin estado propio y siempre en fase
// aunque cambiemos de pantalla.
static void cursorDraw(uint32_t now, uint8_t attr)
{
    const bool on = ((now / SCR_T_CURSOR) & 1) == 0;
#if SCREEN_CURSOR_BLOCK
    cellSet(g_scr.curRow, g_scr.curCol, ' ', on ? ATTR_INVERT(attr) : attr);
#else
    cellSet(g_scr.curRow, g_scr.curCol, on ? SCR_CURSOR_CH : ' ', attr);
#endif
}

static void cursorErase(uint8_t attr) { cellSet(g_scr.curRow, g_scr.curCol, ' ', attr); }

// ===========================================================================
//  PANTALLA 1: BASIC
// ===========================================================================
static const char *BASIC_BANNER[] = {
    SCREEN_MACHINE_NAME " BASIC version 2.0",
    "Copyright 1985 by Microsoft",
    SCREEN_BASIC_FREE " Bytes free",
    "",
    "Ok",
};
#define BASIC_BANNER_LINES ((int)(sizeof(BASIC_BANNER) / sizeof(BASIC_BANNER[0])))

// La franja de teclas de funcion: 5 huecos de 8 caracteres = 40 exactos.
// Contenido: hora, TURBO, WiFi, temperatura y version.
#if SCREEN_BASIC_FKEY_STRIP
// Solo se compilan si la franja esta activada (ver SCREEN_BASIC_FKEY_STRIP).
static void stripSlot(int slot, const char *txt)
{
    char pad[9];
    const int len = (int)strlen(txt);
    const int off = (len >= 8) ? 0 : (8 - len) / 2;      // centrado
    for (int i = 0; i < 8; i++) pad[i] = ' ';
    for (int i = 0; i < 8 - off && txt[i]; i++) pad[off + i] = txt[i];
    pad[8] = 0;
    cellField(SCR_ROWS - 1, slot * 8, pad, 8, ATTR_STRIP);
}

static void basicStripRender()
{
    char t[16];
    fmtClock(t, sizeof(t));                       stripSlot(0, t);
    stripSlot(1, g_scr.turbo ? "TURBO ON" : "TURBO --");
    stripSlot(2, g_scr.wifiResolved ? (g_scr.wifiOk ? "WiFi OK" : "WiFi --") : "WiFi...");
    snprintf(t, sizeof(t), "TEMP %dC", (int)(g_scr.tempC + 0.5f));  stripSlot(3, t);
    stripSlot(4, SCREEN_MACHINE_VER);
}
#endif // SCREEN_BASIC_FKEY_STRIP

static void basicScreenInit()
{
    cellClearAll(ATTR_BASIC);
#if SCREEN_BASIC_FKEY_STRIP
    cellRowFill(SCR_ROWS - 1, ' ', ATTR_STRIP);
#endif
    s_bLine = 0; s_bCol = 0;
    g_scr.curRow = 0; g_scr.curCol = 0;
}

// Salto de linea con scroll, como cualquier terminal. Existe para que la ruta
// de fallo (que imprime varias lineas extra) no pueda desbordar nunca la
// rejilla, ni siquiera tras varios reintentos de WiFi. El scroll en si lo hace
// el driver (cellScrollUp), que es quien puede leer la rejilla.
static void basicNewline()
{
    g_scr.curCol = 0;
    if (g_scr.curRow + 1 >= SCR_TEXT_ROWS) {
        cellScrollUp(0, SCR_TEXT_ROWS - 1, ATTR_BASIC);   // la franja ni se toca
    } else {
        g_scr.curRow++;
    }
}

static void basicPrintLine(const char *s)
{
    cursorErase(ATTR_BASIC);
    cellPuts(g_scr.curRow, 0, s, ATTR_BASIC);
    basicNewline();
}

// ===========================================================================
//  PANTALLA 2: CONSOLA MSX-DOS
// ===========================================================================
// Filas del bloque. Se deja la 0 en blanco como margen y de la 10 abajo queda
// negro: es como se ve una consola de verdad esperando ordenes.
#define DOS_R_TITLE   1
#define DOS_R_SEP1    2
#define DOS_R_WIFI    3
#define DOS_R_IP      4
#define DOS_R_LINK    5
#define DOS_R_CPU     6
#define DOS_R_USB     7
#define DOS_R_SEP2    8
#define DOS_R_PROMPT  9
// Filas 10..19 estaban libres: ahi va el diagnostico del lanzador.
#define DOS_R_LNZ1    11
#define DOS_R_LNZ2    12
#define DOS_R_LNZ3    13
#define DOS_C_VALUE   6        // las etiquetas ocupan las columnas 0..5

static void dosScreenInit()
{
    cellClearAll(ATTR_DOS);
    cellPuts(DOS_R_WIFI, 0, "WIFI",  ATTR_DOS_DIM);
    cellPuts(DOS_R_IP,   0, "IP",    ATTR_DOS_DIM);
    cellPuts(DOS_R_LINK, 0, "LINK",  ATTR_DOS_DIM);
    cellPuts(DOS_R_CPU,  0, "CPU",   ATTR_DOS_DIM);
    cellPuts(DOS_R_USB,  0, "USB",   ATTR_DOS_DIM);
    cellRowFill(DOS_R_SEP1, '-', ATTR_DOS_DIM);
    cellRowFill(DOS_R_SEP2, '-', ATTR_DOS_DIM);
    cellPuts(DOS_R_PROMPT, 0, "A:\\>", ATTR_DOS);
    g_scr.curRow = DOS_R_PROMPT; g_scr.curCol = 4;
}

// Reescribe TODOS los campos vivos. Es barato porque solo toca RAM: el motor
// de celdas se encarga de que al LCD solo bajen los caracteres que cambiaron.
static void dosRender()
{
    // t: campo suelto (caudal, barra, reloj, temperatura). t2: linea compuesta.
    // El tamano de t esta acotado a 16 a proposito, para que el compilador
    // pueda demostrar que "RX %s" cabe en t2 y no avise de truncamiento.
    char t[16], t2[24];

    cellField(DOS_R_TITLE, 0, SCREEN_MACHINE_NAME " " SCREEN_MACHINE_VER, 20, ATTR_DOS);
    cellFieldRight(DOS_R_TITLE, SCREEN_FPGA_NAME, (int)strlen(SCREEN_FPGA_NAME), ATTR_DOS_DIM);

    cellField(DOS_R_WIFI, DOS_C_VALUE, g_scr.wifiOk ? g_scr.ssid : "(sin conexion)", 20, ATTR_DOS);
    fmtBar(t, sizeof(t), g_scr.wifiOk ? rssiBars(g_scr.rssi) : 0);
    cellFieldRight(DOS_R_WIFI, t, 7, ATTR_DOS_DIM);

    cellField(DOS_R_IP, DOS_C_VALUE, g_scr.wifiOk ? g_scr.ip : "-", 34, ATTR_DOS);

    fmtRate(t, sizeof(t), g_scr.rxBps);
    snprintf(t2, sizeof(t2), "RX %s", t);
    cellField(DOS_R_LINK, DOS_C_VALUE, t2, 11, ATTR_DOS);
    fmtRate(t, sizeof(t), g_scr.txBps);
    snprintf(t2, sizeof(t2), "TX %s", t);
    cellField(DOS_R_LINK, DOS_C_VALUE + 11, t2, 11, ATTR_DOS);

    cellField(DOS_R_CPU, DOS_C_VALUE, g_scr.turbo ? SCREEN_CPU_TURBO : SCREEN_CPU_NORMAL, 8, ATTR_DOS);
    cellField(DOS_R_CPU, DOS_C_VALUE + 9, g_scr.turbo ? "TURBO ON" : "TURBO --", 10,
              g_scr.turbo ? ATTR_DOS : ATTR_DOS_DIM);

    cellField(DOS_R_USB, DOS_C_VALUE, usbText(), 20, ATTR_DOS);
    snprintf(t, sizeof(t), "TEMP %dC", (int)(g_scr.tempC + 0.5f));
    cellFieldRight(DOS_R_USB, t, 8, ATTR_DOS_DIM);

    fmtClock(t, sizeof(t));
    cellFieldRight(DOS_R_PROMPT, t, 8, ATTR_DOS_DIM);

    // ---- diagnostico del lanzador -------------------------------------
    // Solo aparece cuando el lanzador ha corrido. PERDIDOS es el numero que
    // decide: si crece, la cola del VDP no se vacia; si se queda en cero tras
    // un chorro grande, las escrituras SI estan llegando al bus.
    if (g_scr.lnzOn) {
        // SPI = destino HID (lo que ya sabiamos que funcionaba).
        // LNZ = destino OSD. Separarlos dice si el enlace esta muerto entero
        // o si es solo el lanzador el que no contesta.
        // OJO: buffer propio. El `t` de esta funcion es de 16 bytes y cortaba
        // estas lineas a 15 caracteres. "SPI v0   LNZ NO CONTESTA   f9" se
        // quedaba en "SPI v0   LNZ NO", que es lo que se estuvo leyendo un
        // buen rato creyendo que era el mensaje entero.
        char lz[48];
        snprintf(lz, sizeof(lz), "SPI v%u  LNZ %s  f%u",
                 (unsigned)g_scr.lnzHidVer,
                 g_scr.lnzVer ? (g_scr.lnzHold ? "RETIENE" : "suelto")
                              : "NO-CONTESTA",
                 (unsigned)g_scr.lnzFase);
        cellField(DOS_R_LNZ1, 0, lz, 34, g_scr.lnzVer ? ATTR_DOS : ATTR_DOS_DIM);
        snprintf(lz, sizeof(lz), "c%u", (unsigned)g_scr.lnzColor);
        cellFieldRight(DOS_R_LNZ1, lz, 5, ATTR_DOS_DIM);

        snprintf(lz, sizeof(lz), "ENV %lu  PERD %u",
                 (unsigned long)g_scr.lnzEnviados, (unsigned)g_scr.lnzPerdidos);
        cellField(DOS_R_LNZ2, 0, lz, 30,
                  g_scr.lnzPerdidos ? ATTR_DOS : ATTR_DOS_DIM);

        // Los 8 bytes crudos de MISO. Todo 00 o todo FF = no vuelve nada (cable
        // o pin). Un 01 en algun sitio = si vuelve, y ese indice dice el desfase.
        // Buffer PROPIO: el `t` de esta funcion es de 16 bytes y cortaba la
        // linea en "MISO 00 00 00 0" -- 15 caracteres justos. Estuvimos un
        // buen rato razonando sobre los cuatro primeros bytes creyendo que
        // eran los ocho.
        snprintf(lz, sizeof(lz), "MISO %02X %02X %02X %02X %02X %02X %02X %02X",
                 g_scr.lnzRaw[0], g_scr.lnzRaw[1], g_scr.lnzRaw[2], g_scr.lnzRaw[3],
                 g_scr.lnzRaw[4], g_scr.lnzRaw[5], g_scr.lnzRaw[6], g_scr.lnzRaw[7]);
        cellField(DOS_R_LNZ3, 0, lz, 34, ATTR_DOS);
    }
}

// ===========================================================================
//  MAQUINA DE ESTADOS DEL ARRANQUE
// ===========================================================================
static const char CMD_SYSTEM[] = "call system";

// El motivo se guarda aparte del salto de estado porque puede llegar ANTES de
// que la pantalla de BASIC exista (p. ej. un fallo detectado durante el
// tecleo del banner). En ese caso se marca como pendiente y se pinta al
// llegar a la fase 2, para no romper la animacion a media palabra.
static void failWith(const char *motivo)
{
    snprintf(s_failReason, sizeof(s_failReason), "%s", motivo ? motivo : "");
}

static void gotoFailed()
{
    // Estetica de error de MSX BASIC: el "?" delante es marca de la casa.
    basicPrintLine("?Device I/O error");
    if (s_failReason[0]) basicPrintLine(s_failReason);
    basicPrintLine("Ok");
    s_state = ST_FAILED;
}

void scrBootTick(uint32_t now)
{
    switch (s_state) {

    case ST_INIT:
        // Fase 0: el logo de MSX Barcelona. El MISMO original que usa el logo
        // de arranque del propio MSX, asi que la placa y la maquina ensenan la
        // misma marca. Es un bitmap: se pinta de una sola vez saltandose el
        // motor de celdas (el driver invalida la rejilla al terminar para que
        // el BASIC repinte encima sin dejar restos).
#if SCREEN_BOOT_LOGO
        screenDrawLogo();
        s_tNext = now + SCR_T_LOGO;
        s_state = ST_LOGO;
        break;

    case ST_LOGO:
        if ((int32_t)(now - s_tNext) < 0) break;
        // Se acabo el logo: descongelar el motor de celdas y forzar repintado
        // completo, que debajo hay un bitmap y no lo que la rejilla cree.
        screenInvalidate();
#endif
        basicScreenInit();
        s_tNext = now;
        s_state = ST_BASIC_TYPE;
        break;

    // -- Fase 1: el banner se escribe solo -------------------------------
    case ST_BASIC_TYPE:
        if ((int32_t)(now - s_tNext) < 0) break;
        {
            const char *line = BASIC_BANNER[s_bLine];
            if (line[s_bCol]) {
                cellSet(s_bLine, s_bCol, line[s_bCol], ATTR_BASIC);
                s_bCol++;
                s_tNext = now + SCR_T_BANNER_CHAR;
            } else {
                s_bLine++; s_bCol = 0;
                s_tNext = now + SCR_T_BANNER_LINE;
                if (s_bLine >= BASIC_BANNER_LINES) {
                    // El cursor queda en la linea siguiente al "Ok", que es
                    // exactamente donde se tecleara `call system`.
                    g_scr.curRow = BASIC_BANNER_LINES; g_scr.curCol = 0;
                    s_tEnterWait = now;
                    s_state = ST_WAIT;
                }
            }
        }
        break;

    // -- Fase 2: espera REAL. Aqui no hay temporizador que valga: se sale
    //    cuando WiFi y USB han contestado de verdad.
    case ST_WAIT:
        cursorDraw(now, ATTR_BASIC);
        if (s_failPending) {
            s_failPending = false;
            gotoFailed();
        } else if (g_scr.wifiResolved && g_scr.usbResolved) {
            if (g_scr.wifiOk) {
                cursorErase(ATTR_BASIC);
                s_cmdCol = 0;
                s_tNext = now + SCR_T_CMD_CHAR;
                s_state = ST_TYPE_CMD;
            } else {
                failWith("WIFI NOT READY - PULSA W EN EL MENU");
                gotoFailed();
            }
        } else if ((now - s_tEnterWait) > SCR_T_BOOT_TIMEOUT) {
            failWith(g_scr.wifiResolved ? "USB HOST TIMEOUT" : "WIFI TIMEOUT");
            gotoFailed();
        }
        break;

    // -- Fase 3: se teclea `call system` a 80 ms por letra ----------------
    case ST_TYPE_CMD:
        if ((int32_t)(now - s_tNext) < 0) { cursorDraw(now, ATTR_BASIC); break; }
        if (CMD_SYSTEM[s_cmdCol]) {
            cellSet(g_scr.curRow, g_scr.curCol, CMD_SYSTEM[s_cmdCol], ATTR_BASIC);
            s_cmdCol++; g_scr.curCol++;
            s_tNext = now + SCR_T_CMD_CHAR;
        } else {
            cursorErase(ATTR_BASIC);        // Enter
            s_tNext = now + SCR_T_ENTER;
            s_state = ST_ENTER;
        }
        break;

    case ST_ENTER:
        if ((int32_t)(now - s_tNext) < 0) break;
        dosScreenInit();
        s_state = ST_DOS;
        break;

    // -- Fase 4: consola viva --------------------------------------------
    case ST_DOS:
        {
            static uint32_t tRefresh = 0;
            if (now - tRefresh >= SCR_T_DOS_REFRESH) { tRefresh = now; dosRender(); }
            cursorDraw(now, ATTR_DOS);
        }
        break;

    // -- Fallo: nos quedamos en BASIC, pero la franja sigue VIVA para que
    //    se vea entrar el WiFi en cuanto entre. Si entra, se reanuda.
    case ST_FAILED:
        cursorDraw(now, ATTR_BASIC);
        if (g_scr.wifiOk && g_scr.usbResolved) {
            cursorErase(ATTR_BASIC);
            s_cmdCol = 0;
            s_tNext = now + SCR_T_CMD_CHAR;
            s_state = ST_TYPE_CMD;
        }
        break;
    }

    // Franja de teclas de funcion: APAGADA durante el arranque (decision de
    // Albert al verla en la placa, 28/07). Mientras carga no aporta nada -la
    // hora, el WiFi y la temperatura todavia no existen- y ensucia una pantalla
    // que se ve mejor limpia. Los datos ya salen luego en la consola MSX-DOS.
    // Ponerlo a 1 la devuelve.
#if SCREEN_BASIC_FKEY_STRIP
    if (s_state != ST_DOS && s_state != ST_INIT) {
        static uint32_t tStrip = 0;
        if (now - tStrip >= SCR_T_DOS_REFRESH) { tStrip = now; basicStripRender(); }
    }
#endif
}

// ===========================================================================
//  COSTURA CON EL DRIVER
// ===========================================================================
// Basta con volver a ST_INIT: la pantalla de BASIC la monta el propio
// scrBootTick() al procesar esa fase, ya con el reloj bueno en la mano.
void scrBootReset() { s_state = ST_INIT; }

void scrBootFail(const char *motivo)
{
    if (s_state == ST_DOS) return;              // ya arrancamos: no se vuelve atras
    failWith(motivo);
    if (s_state == ST_INIT || s_state == ST_BASIC_TYPE) {
        s_failPending = true;                   // se pintara al acabar el banner
        return;
    }
    gotoFailed();
}

bool scrBootIsDos() { return s_state == ST_DOS; }
