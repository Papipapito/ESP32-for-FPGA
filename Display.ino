/*
 * Display.ino - Pantalla de estado (WiFi + Sistema + Turbo) para MSXimus/MSXnano en la
 * Waveshare ESP32-C6-LCD-1.3 (ST7789V2 240x240 SPI). Modulo AÑADIDO al firmware
 * UNAPI de ducasp. Arduino concatena los .ino: llamar displaySetup() en setup()
 * y displayTask() en loop() del .ino principal.
 *
 * Libreria: "GFX Library for Arduino" (moononournation) -> Arduino_GFX.
 * PINES LCD: SCLK=7 MOSI=6 CS=14 DC=15 RST=21 BL=22 (no tocan UART 16/17).
 *
 * TURBO: lee un GPIO que el FPGA pone a 1 en modo turbo (pin del FPGA -> TURBO_PIN).
 *        Mientras no este cableado, INPUT_PULLDOWN lo lee 0 -> muestra "Normal".
 */

#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <time.h>
#include "UNAPIESP.h"     // ESPConfig, FIRMWARETYPE (guarda de inclusion)

#define LCD_SCK   7
#define LCD_MOSI  6
#define LCD_CS    14
#define LCD_DC    15
#define LCD_RST   21
#define LCD_BL    22

// Titulo del LCD. NEUTRO a proposito ("MSX" a secas, decision Albert 27/07):
// este firmware es COMUN al MSXimus y al MSXnano — la version del core no es
// cosa del ESP. Quien quiera personalizarlo: cambiar este define.
#define DEVICE_NAME    "MSX"     // 04/09: vuelve a ser NEUTRO. Se puso "MSXimus"
                                 // el 31/07, cuando cada maquina tenia su rama;
                                 // ahora hay UNA sola y sirve a las dos.
#define TURBO_PIN 3       // GPIO libre del C6 (header) cableado al pin de turbo del FPGA

// Colores RGB565
#define COL_BLACK    0x0000
#define COL_WHITE    0xFFFF
#define COL_RED      0xF800
#define COL_GREEN    0x07E0
#define COL_CYAN     0x07FF
#define COL_YELLOW   0xFFE0
#define COL_ORANGE   0xFD20
#define COL_DGREEN   0x03E0
#define COL_DGREY    0x2104
#define COL_DARKGREY 0x7BEF

// Globales del firmware de ducasp (definidas en el .ino principal)
extern const char chVer[4];
extern bool bSNTPOK;
extern ESPConfig stDeviceConfiguration;      // .iGMT = offset horario en HORAS (lo setea el menu WiFi)
extern volatile uint32_t g_lastUartMs;       // ultima actividad del enlace MSX<->ESP

static Arduino_DataBus *lcd_bus = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
// ROTACION 2 = la 0 girada 180 grados. El modulo del MSXimus 60K monta MEJOR AL
// REVES en la caja (Albert, 31/07).
//
// ⚠️ EL ULTIMO 80 NO ES OPCIONAL, Y ES EL ERROR CLASICO DE ESTOS PANELES.
// El ST7789 tiene 240x320 de RAM y este panel solo usa 240 de esas 320 filas.
// En rotacion 0 la ventana visible son las filas 0..239; al girar 180 grados el
// controlador invierte y pasa a ser 80..319. Sin compensarlo, TODO el contenido
// sube 80 px: se pierden el titulo y el bloque de WiFi por arriba, y por abajo
// se ve RAM sin inicializar (basura). Medido en placa el 31/07.
// Por eso va en ROW_OFFSET2 (el ultimo parametro), que es el offset de fila que
// la libreria aplica SOLO a las rotaciones invertidas (2 y 3) — el de la
// rotacion 0 se queda a 0 y esa sigue igual que siempre.
// Orden de los cuatro: col_offset1, row_offset1, col_offset2, row_offset2.
//
// ⛔ MI RAZONAMIENTO ANTERIOR ERA FALSO, queda escrito para que no se repita:
// "el panel es cuadrado 240x240, luego la geometria es identica en las cuatro
// rotaciones". Lo cuadrado es el PANEL, no la RAM del controlador.
static Arduino_GFX *gfx = new Arduino_ST7789(lcd_bus, LCD_RST, 2, true, 240, 240, 0, 0, 0, 80);
static bool lcd_ok = false;

static void fmtUptime(char *buf, uint32_t ms)
{
    uint32_t s = ms / 1000;
    sprintf(buf, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600),
            (unsigned long)((s % 3600) / 60), (unsigned long)(s % 60));
}

// ---------------------------------------------------------------------------
// ARRANQUE: el logotipo MSX armandose desde los dos lados
//
// Reproduce el arranque clasico del MSX2: dos copias del logo entran desde los
// lados opuestos, SE CRUZAN, y lo que va quedando blanco es la zona donde
// COINCIDEN. Ese detalle es el que lo hace reconocible -- si solo se deslizaran
// hasta juntarse no seria lo mismo. Al cuajar, el logo entero queda blanco.
//
// NO BLOQUEA, y esto no es negociable: displaySetup() se llama desde el setup()
// del firmware UNAPI, asi que pararse aqui es dejar al MSX sin atender.
// displaySetup() pinta el primer fotograma y apunta la hora; displayTask() va
// avanzando la animacion desde loop() y retira el logo cuando vence el tiempo.
//
// El logo va como MASCARA de 1 bit (LogoMsx.h, 1,3 KB) y no como bitmap RGB565
// (el LogoMsximus.h de antes eran 115 KB). Ademas de ocupar 85 veces menos, es
// lo que permite pintarlo DOS veces y colorear el solape: con un bitmap fijo la
// animacion no se puede hacer.
// Se regenera con: python tools/make_logo_msx.py
// ---------------------------------------------------------------------------
#include "LogoMsx.h"

#define COL_MSX_BLUE  0x001C      // RGB(1,0,228): el azul del arranque original
#define COL_LOGO_DIM  0x5AD7      // RGB(90,90,190): las copias mientras viajan

#define LOGO_BOX_W    224         // caja negra; proporcion 3.11:1 del original
#define LOGO_BOX_H    72
#define LOGO_ANIM_MS  900         // lo que tarda en armarse
#define LOGO_MS       3000        // cuanto se ve en total, animacion incluida
#define LOGO_FPS_MS   25          // no repintar mas rapido que esto

static const int16_t LOGO_BX = (240 - LOGO_BOX_W) / 2;
static const int16_t LOGO_BY = (240 - LOGO_BOX_H) / 2;
static const int16_t LOGO_OX = (LOGO_BOX_W - LOGO_MSX_W) / 2;   // dentro de la caja
static const int16_t LOGO_OY = (LOGO_BOX_H - LOGO_MSX_H) / 2;

static uint32_t t_logo    = 0;
static uint32_t t_logo_fr = 0;   // ultimo repintado (limita los fps)
static int16_t  logo_off  = -1;  // desplazamiento actual; 0 = ya cuajado
static bool     logo_vivo = false;

// Pinta la caja con las dos copias desplazadas -off y +off. Fila a fila con un
// buffer de 224 pixeles (448 B): montar la caja entera serian 32 KB de RAM para
// no ganar nada, el SPI del panel se traga 72 ventanas por fotograma de sobra.
static void logoFrame(int16_t off)
{
    static uint16_t fila[LOGO_BOX_W];
    for (int16_t y = 0; y < LOGO_BOX_H; y++) {
        // La caja negra VIAJA CON CADA COPIA: al principio son dos pestanas en
        // los bordes y van creciendo al converger. Pintar la caja entera desde
        // el primer fotograma se ve mal -- en el original la caja es el fondo
        // de cada mitad, no un marco fijo.
        for (int16_t x = 0; x < LOGO_BOX_W; x++) fila[x] = COL_MSX_BLUE;
        for (int16_t x = 0; x < LOGO_BOX_W - off; x++)   fila[x] = COL_BLACK;
        for (int16_t x = (off < 0 ? 0 : off); x < LOGO_BOX_W; x++) fila[x] = COL_BLACK;
        int16_t my = y - LOGO_OY;
        if (my >= 0 && my < LOGO_MSX_H) {
            const uint8_t *m = LOGO_MSX + my * LOGO_MSX_BPR;
            for (int16_t x = 0; x < LOGO_MSX_W; x++) {      // copia de la izquierda
                if (!(m[x >> 3] & (0x80 >> (x & 7)))) continue;
                int16_t xi = LOGO_OX + x - off;
                if (xi >= 0 && xi < LOGO_BOX_W) fila[xi] = COL_LOGO_DIM;
            }
            for (int16_t x = 0; x < LOGO_MSX_W; x++) {      // ...y la de la derecha:
                if (!(m[x >> 3] & (0x80 >> (x & 7)))) continue;
                int16_t xd = LOGO_OX + x + off;             // donde pisa a la otra,
                if (xd >= 0 && xd < LOGO_BOX_W)             // BLANCO
                    fila[xd] = (fila[xd] == COL_LOGO_DIM) ? COL_WHITE : COL_LOGO_DIM;
            }
        }
        gfx->draw16bitRGBBitmap(LOGO_BX, LOGO_BY + y, fila, LOGO_BOX_W, 1);
    }
}

// Desplazamiento segun el tiempo transcurrido: easing cubico (entra rapido y
// frena al cuajar) en enteros a escala 1024, sin coma flotante.
static int16_t logoOffset(uint32_t el)
{
    if (el >= LOGO_ANIM_MS) return 0;
    uint32_t t   = (el * 1024) / LOGO_ANIM_MS;        // 0..1024
    uint32_t inv = 1024 - t;
    uint32_t cub = (inv * inv / 1024) * inv / 1024;   // (1-t)^3
    return (int16_t)((cub * (LOGO_MSX_W + 16)) / 1024);
}

// El "marco" fijo de la pantalla de estado: titulo y lineas separadoras. Estaba
// dentro de displaySetup(); se saca aparte porque ahora hay que pintarlo DOS
// veces (o mejor dicho: mas tarde, cuando el logo se retira).
static void drawChrome()
{
    gfx->fillScreen(COL_BLACK);
    gfx->setTextColor(COL_CYAN);
    gfx->setTextSize(2);
    gfx->setCursor(6, 6);
    gfx->print(DEVICE_NAME);
    gfx->drawFastHLine(0, 28, 240, COL_DARKGREY);
    gfx->drawFastHLine(0, 108, 240, COL_DARKGREY);
    gfx->drawFastHLine(0, 152, 240, COL_DARKGREY);
}

void displaySetup()
{
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);
    pinMode(TURBO_PIN, INPUT_PULLDOWN);      // 0 si no esta cableado -> "Normal"

    if (!gfx->begin()) { lcd_ok = false; return; }
    lcd_ok = true;

    gfx->fillScreen(COL_MSX_BLUE);
    logo_off  = logoOffset(0);
    logoFrame(logo_off);
    t_logo    = millis();
    t_logo_fr = t_logo;
    logo_vivo = true;
}

void displayTask()
{
    if (!lcd_ok) return;
    uint32_t now = millis();

    // Mientras el logo esta puesto no se pinta nada mas: se sale y ya. Cuando
    // vence el tiempo se borra, se pinta el marco y a partir de ahi la pantalla
    // de estado funciona como siempre (sus bloques se repintan solos porque
    // guardan el ultimo valor y ven que "ha cambiado" respecto a la pantalla en
    // blanco).
    if (logo_vivo) {
        // Mientras no haya cuajado (off != 0) se sigue repintando; logoOffset()
        // devuelve 0 en cuanto vence LOGO_ANIM_MS, asi que siempre converge
        // aunque un loop() largo se salte fotogramas.
        if (logo_off != 0 && now - t_logo_fr >= LOGO_FPS_MS) {
            t_logo_fr = now;
            int16_t off = logoOffset(now - t_logo);
            if (off != logo_off) { logo_off = off; logoFrame(off); }
        }
        if (now - t_logo < LOGO_MS) return;
        logo_vivo = false;
        drawChrome();
    }

    // Arranca SNTP en cuanto hay WiFi, para que el reloj de la pantalla salga sin
    // depender de que el MSX pida la hora (bSNTPOK solo se activa via peticion del MSX).
    static bool s_wasConn = false;
    bool s_conn = (WiFi.status() == WL_CONNECTED);
    if (s_conn && !s_wasConn) configTime(0, 0, "pool.ntp.org");
    s_wasConn = s_conn;

    // ---------- Indicador de actividad MSX<->ESP (punto arriba-dcha) ----------
    static uint32_t t_act = 0;
    static int8_t   act_last = -1;
    if (now - t_act >= 150) {
        t_act = now;
        int8_t act = (now - g_lastUartMs < 250) ? 1 : 0;   // trafico UART reciente
        if (act != act_last) {
            act_last = act;
            gfx->fillCircle(228, 13, 6, act ? COL_GREEN : COL_DGREY);
            gfx->drawCircle(228, 13, 6, COL_DARKGREY);
        }
    }

    // ---------- Bloque WiFi (size 2), repinta SOLO si cambia ----------
    static uint32_t t_wifi = 0;
    if (now - t_wifi >= 1000) {
        t_wifi = now;
        static int8_t last_conn = -1;
        static String last_ssid = "\x01";
        static long   last_rssi = 0x7fffffff;

        bool   conn = (WiFi.status() == WL_CONNECTED);
        String ssid = conn ? WiFi.SSID() : String("");
        long   rssi = conn ? WiFi.RSSI() : 0;

        if (!((int8_t)conn == last_conn && ssid == last_ssid && rssi == last_rssi)) {
            last_conn = conn; last_ssid = ssid; last_rssi = rssi;
            gfx->fillRect(0, 32, 240, 74, COL_BLACK);      // zona WiFi (y32..106)
            gfx->setTextSize(2);
            if (conn) {
                gfx->setTextColor(COL_GREEN); gfx->setCursor(6, 34); gfx->println("Conectado");
                gfx->setTextColor(COL_WHITE);
                gfx->setCursor(6, 58); gfx->print("SSID:"); gfx->println(ssid);
                gfx->setCursor(6, 82); gfx->print("RSSI:"); gfx->print(rssi); gfx->println("dBm");
            } else {
                gfx->setTextColor(COL_RED);    gfx->setCursor(6, 34); gfx->println("Sin WiFi");
                gfx->setTextColor(COL_DARKGREY); gfx->setCursor(6, 70); gfx->println("Pulsa W en el menu");
            }
        }
    }

    // ---------- Barra TURBO ----------
    // El FPGA sube TURBO_PIN en modo turbo. Sin cable, el pulldown lo lee 0
    // y pone "Normal": nunca destellos en frio.
    static uint32_t t_turbo = 0;
    static int8_t   last_turbo = -1;
    if (now - t_turbo >= 400) {
        t_turbo = now;
        {
        int8_t turbo = digitalRead(TURBO_PIN) ? 1 : 0;
        if (turbo != last_turbo) {
            last_turbo = turbo;
            if (turbo) {
                gfx->fillRect(6, 114, 228, 32, COL_ORANGE);
                gfx->setTextColor(COL_BLACK);
                gfx->setTextSize(2); gfx->setCursor(16, 122); gfx->print("TURBO 5.37MHz");
            } else {
                gfx->fillRect(6, 114, 228, 32, COL_DGREY);
                gfx->setTextColor(COL_DGREEN);
                gfx->setTextSize(2); gfx->setCursor(16, 122); gfx->print("Normal 3.58MHz");
            }
        }
        }
    }

    // ---------- Barra inferior (size 2): Uptime, Temp, Hora -- llena la zona bajo el turbo, cada 3s ----------
    static uint32_t t_sys = 0;
    if (now - t_sys >= 3000) {
        t_sys = now;
        char up[12]; fmtUptime(up, now);
        float temp = temperatureRead();

        gfx->fillRect(0, 156, 240, 84, COL_BLACK);   // zona bajo la barra de turbo (y156..240)
        gfx->setTextSize(2);                          // el doble de grande; 3 lineas repartidas
        gfx->setTextColor(COL_WHITE);
        gfx->setCursor(6, 160); gfx->print("Uptime "); gfx->println(up);
        gfx->setCursor(6, 190); gfx->print("Temp ");   gfx->print(temp, 0); gfx->println(" C");
        // Reloj: hora REAL del sistema (SNTP en background al conectar WiFi), sin depender de bSNTPOK.
        gfx->setCursor(6, 220);
        {
            time_t tnow = time(nullptr);
            if (tnow > 1700000000) {   // epoch > 2023-11 => SNTP ya sincronizo, hora valida
                time_t t = tnow + (time_t)stDeviceConfiguration.iGMT * 3600;   // hora LOCAL via iGMT
                struct tm tmv; gmtime_r(&t, &tmv);
                char hs[16]; strftime(hs, sizeof(hs), "%H:%M  %d/%m/%y", &tmv);   // size2: hora + fecha corta
                gfx->print(hs);
            } else {
                gfx->setTextColor(COL_DARKGREY);
                gfx->print((WiFi.status() == WL_CONNECTED) ? "Sincronizando" : "Sin WiFi");
            }
        }
    }
}
