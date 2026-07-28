/*
 * test_screens3.cpp - Banco de pruebas de ScreenS3 en el PC (WSL).
 *
 * No hay hardware que valga: se stubean Arduino.h y Arduino_GFX (stub_screen/)
 * con las MISMAS firmas que la libreria real, se incluyen los .cpp enteros
 * (driver + narrativa) para poder mirarles las tripas (s_buf, s_state) y se
 * hace correr la maquina de estados con un reloj falso.
 *
 * Lo que comprueba de verdad:
 *   1. Que compila contra las firmas reales de Arduino_GFX.
 *   2. La geometria: rotacion 1 -> 320x170 -> 40x21 celdas.
 *   3. Que la fase 2 dura lo que tarda el hardware y NI UN MS MAS: no avanza
 *      sola por mucho que pase el tiempo.
 *   4. Que si el WiFi falla NO se teclea `call system` (la regla del diseno).
 *   5. Que el motor de celdas solo vuelca lo que cambia (anti-parpadeo).
 *   6. El layout exacto, volcado como ASCII para revisarlo a ojo.
 *
 *   wsl -d Ubuntu-24.04 bash -lc "/mnt/c/.../host_test/test_screens3.sh"
 */

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cassert>

uint32_t g_fakeMillis = 0;
float    g_fakeTempC  = 48.0f;
int      g_blDuty     = 0;
long     g_blitCount  = 0;
long     g_fillScreenCount = 0;
unsigned long g_logoPixels = 0;   // pixeles empujados por el volcado del logo

// Se incluyen LOS DOS .cpp para tener acceso a los estaticos internos: s_buf y
// el motor de celdas viven en el driver, y s_state (la fase del arranque) en la
// narrativa. Juntos en una sola unidad de traduccion, los `static` de ambos
// quedan a la vista del test.
#include "../ScreenS3.cpp"
#include "../ScreenS3Boot.cpp"

static int g_fails = 0;

static void check(bool cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "OK  " : "FALLO", what);
    if (!cond) g_fails++;
}

// Avanza el reloj falso en pasos de 1 ms llamando a screenTick() en cada uno,
// que es justo lo que hace el loop() real (mas rapido, pero eso solo ayuda).
static void advance(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) { g_fakeMillis++; screenTick(); }
}

static void dump(const char *titulo)
{
    printf("\n    %s\n    +", titulo);
    for (int c = 0; c < SCR_COLS; c++) putchar('-');
    printf("+\n");
    for (int r = 0; r < SCR_ROWS; r++) {
        printf("    |");
        for (int c = 0; c < SCR_COLS; c++) {
            unsigned char ch = s_buf[r][c].ch;
            putchar((ch >= 0x20 && ch <= 0x7E) ? ch : ' ');
        }
        printf("|\n");
    }
    printf("    +");
    for (int c = 0; c < SCR_COLS; c++) putchar('-');
    printf("+\n");
}

// Busca un texto en cualquier fila del buffer logico.
static bool screenHas(const char *needle)
{
    char line[SCR_COLS + 1];
    for (int r = 0; r < SCR_ROWS; r++) {
        for (int c = 0; c < SCR_COLS; c++) line[c] = (char)s_buf[r][c].ch;
        line[SCR_COLS] = 0;
        if (strstr(line, needle)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
static void escenarioFeliz()
{
    printf("\n=== ESCENARIO 1: arranque normal (WiFi OK) ===\n");

    check(screenSetup(), "screenSetup() arranca");
    check(s_gfx->width() == 320 && s_gfx->height() == 170,
          "geometria en horizontal = 320x170");
    check(SCR_COLS == 40, "40 columnas exactas, como el SCREEN 0 del MSX");
    check(g_blDuty == 0, "el backlight arranca APAGADO");

    // -- Fase 0: el logo de MSX Barcelona -----------------------------------
    // La maquina de estados avanza en screenTick(), no en screenSetup(): hasta
    // el primer tick seguimos en ST_INIT.
    advance(1);
    check(s_state == ST_LOGO, "fase 0: arranca con el logo");
    // El RLE tiene que cubrir la pantalla EXACTA: si sobran o faltan pixeles,
    // la ventana de escritura del ST7789 queda descuadrada y el logo saldria
    // torcido o a medias. Es la comprobacion que no se puede hacer a ojo.
    check(g_logoPixels == (unsigned long)LOGO_W * LOGO_H,
          "el logo cubre exactamente 320x170 pixeles");
    // REGRESION (visto en la placa): el motor de celdas repintaba su rejilla
    // vacia ENCIMA del logo y lo borraba en menos de un segundo. Mientras dura
    // la fase del logo NO puede volcarse ni una celda al LCD.
    long blitsLogo = g_blitCount;
    advance(SCR_T_LOGO - 200);
    check(s_state == ST_LOGO, "el logo se mantiene sus segundos");
    check(g_blitCount == blitsLogo,
          "el motor de celdas NO pinta encima del logo (se borraba)");
    advance(200);
    check(s_state != ST_LOGO, "y despues da paso al BASIC");
    check(g_blitCount > blitsLogo,
          "al salir del logo si repinta (rejilla invalidada)");

    // -- Fase 1: el banner se teclea solo -----------------------------------
    advance(3000);
    check(s_state == ST_WAIT, "fase 1 termina y pasa a la espera");
    check(screenHas("MSXnano BASIC version 2.0"), "banner: linea de version");
    check(screenHas("Copyright 1985 by Microsoft"), "banner: copyright");
    check(screenHas("28815 Bytes free"), "banner: bytes libres");
    check(screenHas("Ok"), "banner: prompt Ok");
    check(g_blDuty > 200, "el backlight ya ha subido con el fundido");
    dump("Fase 1-2: BASIC + franja de teclas de funcion");

    // -- Fase 2: NO puede avanzar sola --------------------------------------
    long blitsAntes = g_blitCount;
    advance(10000);
    check(s_state == ST_WAIT,
          "tras 10 s SIN hardware listo sigue esperando (no es un delay fijo)");
    check(!screenHas("call system"), "no se ha tecleado `call system` todavia");
    // En reposo solo deberia repintar el cursor y los campos vivos de la franja.
    long blitsReposo = g_blitCount - blitsAntes;
    printf("       (celdas volcadas en 10 s de reposo: %ld)\n", blitsReposo);
    check(blitsReposo < 600, "en reposo apenas vuelca celdas (motor anti-parpadeo)");

    // -- El hardware contesta de verdad -------------------------------------
    screenSetUsb(true, true);
    check(s_state == ST_WAIT, "con solo el USB listo aun no arranca");
    advance(500);
    check(s_state == ST_WAIT, "sigue esperando al WiFi");

    screenSetWifi("MSX-BCN", "192.168.1.42", -58, true);
    advance(50);
    check(s_state == ST_TYPE_CMD, "con WiFi + USB listos empieza a teclear");

    // -- Fase 3: `call system` a 80 ms por letra ----------------------------
    advance(11 * 80 + 200);
    check(screenHas("call system"), "fase 3: se ha tecleado `call system`");
    advance(600);
    check(s_state == ST_DOS, "fase 4: ha saltado a la consola MSX-DOS");
    check(screenBootDone(), "screenBootDone() = true");

    // -- Fase 4: consola viva -----------------------------------------------
    screenSetTurbo(true);
    screenSetTraffic(1234, 412);
    screenSetClock(23, 47, 12);
    advance(400);
    dump("Fase 4: consola MSX-DOS");

    check(screenHas("MSXnano v1.9"), "DOS: nombre y version");
    check(screenHas("TN20K GW2AR-18"), "DOS: FPGA");
    check(screenHas("MSX-BCN"), "DOS: SSID");
    check(screenHas("192.168.1.42"), "DOS: IP");
    check(screenHas("[####-]"), "DOS: barra de cobertura (-58 dBm = 4/5)");
    check(screenHas("RX 1.2K/s"), "DOS: caudal RX formateado");
    check(screenHas("TX 412B/s"), "DOS: caudal TX formateado");
    check(screenHas("5.37MHz"), "DOS: reloj de CPU en turbo");
    check(screenHas("TURBO ON"), "DOS: indicador de turbo");
    check(screenHas("teclado + mando"), "DOS: USB enumerado");
    check(screenHas("TEMP 48C"), "DOS: temperatura INTERNA del ESP32-S3");
    check(screenHas("A:\\>"), "DOS: prompt");
    check(screenHas("23:47:1"), "DOS: reloj");

    // El reloj tiene que CORRER solo entre empujones.
    advance(5000);
    check(screenHas("23:47:17"), "el reloj avanza solo entre llamadas");

    // Un cambio pequeno no puede provocar un repintado masivo.
    blitsAntes = g_blitCount;
    advance(1000);
    long blits1s = g_blitCount - blitsAntes;
    printf("       (celdas volcadas en 1 s de consola viva: %ld)\n", blits1s);
    check(blits1s < 60, "consola viva: solo repinta reloj y cursor");
}

// ---------------------------------------------------------------------------
static void escenarioFallo()
{
    printf("\n=== ESCENARIO 2: sin WiFi (no debe hacer `call system`) ===\n");

    check(screenSetup(), "screenSetup() arranca");
    advance(SCR_T_LOGO + 3000);      // logo + tecleo del banner
    check(s_state == ST_WAIT, "fase 1 completada");

    screenSetUsb(true, false);
    screenSetWifi(nullptr, nullptr, 0, false);   // el WiFi se resolvio EN FALLO
    advance(100);

    check(s_state == ST_FAILED, "pasa al estado de fallo");
    check(!screenHas("call system"), "NO se teclea `call system` (regla del diseno)");
    check(screenHas("?Device I/O error"), "error con estetica de MSX BASIC");
    check(screenHas("WIFI NOT READY"), "explica el motivo en pantalla");
    dump("Fallo: se queda en BASIC y lo cuenta");

    advance(20000);
    check(s_state == ST_FAILED, "se queda ahi indefinidamente, sin saltar a DOS");
    // La franja de teclas va APAGADA durante el arranque (SCREEN_BASIC_FKEY_STRIP=0):
    // mientras carga no aporta -hora, WiFi y temperatura aun no existen- y la
    // pantalla gana estando limpia. Se comprueba justo lo contrario que antes:
    // que la ultima fila esta VACIA. Si algun dia se reactiva la franja, este
    // check hay que darle la vuelta.
#if SCREEN_BASIC_FKEY_STRIP
    check(screenHas("WiFi --"), "la franja de teclas refleja el WiFi caido");
#else
    check(!screenHas("WiFi --") && !screenHas("TURBO"),
          "sin franja de teclas durante el arranque (pantalla limpia)");
#endif

    // Si el WiFi entra mas tarde, el arranque se REANUDA.
    screenSetWifi("MSX-BCN", "192.168.1.42", -70, true);
    advance(11 * 80 + 1200);
    check(s_state == ST_DOS, "si el WiFi entra despues, el arranque se reanuda");
    // -70 dBm queda por debajo del corte de -67 (el umbral clasico de enlace
    // "bueno"), asi que le tocan 2 barras de 5. No es un fallo: es la escala.
    check(screenHas("[##---]"), "cobertura recalculada (-70 dBm = 2/5)");
    dump("Reanudado: consola MSX-DOS");
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    const bool soloFallo = (argc > 1 && strcmp(argv[1], "fallo") == 0);
    printf("=== test_screens3: ScreenS3.cpp en el PC ===\n");
    if (soloFallo) escenarioFallo();
    else           escenarioFeliz();

    printf("\n=== %s ===\n", g_fails == 0 ? "TODO OK" : "HAY FALLOS");
    return g_fails == 0 ? 0 : 1;
}
