/*
 * CompanionSpi.cpp - el bus SPI de verdad hacia la FPGA del MSXimus.
 *
 * Companion.cpp construye las tramas y es C++ puro (se prueba en el PC).
 * ESTE fichero es la otra mitad: la que toca hardware. Se separan a proposito,
 * igual que ScreenS3 (driver) y ScreenS3Boot (narrativa).
 *
 * SPI3_HOST, y no es indiferente: el SPI2 lo tiene la pantalla ST7789.
 *
 * BRING-UP SIN TOCAR LA PANTALLA
 * ------------------------------
 * El boton BOOT (GPIO0, ya previsto en BoardS3.h como boton de usuario tras
 * arrancar) manda una 'A' al MSX. Si aparece la letra, la cadena entera esta
 * probada: SPI -> mcu_spi -> hid.v -> keyboard_spi -> el OR de top.v -> matriz.
 * Es deliberado y no automatico: un envio periodico escribiria basura sola en
 * el MSX, que es justo lo que no se quiere durante una prueba.
 */
#include "Board.h"

#ifdef BOARD_S3

#include <Arduino.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include "Companion.h"
#include "ScreenS3.h"      // para enseñar el diagnostico del lanzador
#include "LauncherFs.h"    // peldano 3: FatFs sobre el puente de sectores

static spi_device_handle_t s_spi = nullptr;
static Companion           s_comp;

// Version que contesto la FPGA (0 = no contesto nadie). Publica para que la
// pantalla pueda pintarla el dia que se le anada un hueco.
uint8_t g_companionVersion = 0;

// ---------------------------------------------------------------------------
// Una trama = CS bajo de principio a fin. Se deja que el driver maneje el CS
// (spics_io_num) porque asi la trama es atomica: mcu_spi_new reinicia su
// contador de bytes con el CS, y soltarlo a mitad tira la trama entera.
// ---------------------------------------------------------------------------
static void spiXfer(const uint8_t *tx, uint8_t *rx, size_t n, void *)
{
    if (!s_spi || n == 0) return;
    spi_transaction_t t = {};
    t.length    = n * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    spi_device_polling_transmit(s_spi, &t);
}

bool companionSetup()
{
    spi_bus_config_t bus = {};
    bus.mosi_io_num     = S3_SPI_MOSI;
    bus.miso_io_num     = S3_SPI_MISO;
    bus.sclk_io_num     = S3_SPI_SCLK;
    bus.quadwp_io_num   = -1;
    bus.quadhd_io_num   = -1;
    bus.max_transfer_sz = 1024;
    if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

    spi_device_interface_config_t dev = {};
    dev.mode           = S3_SPI_MODE;      // 1: reposo bajo, dato en el flanco
    dev.clock_speed_hz = S3_SPI_HZ;        //    de subida, muestreo en el de bajada
    dev.spics_io_num   = S3_SPI_CS;
    dev.queue_size     = 2;
    if (spi_bus_add_device(SPI3_HOST, &dev, &s_spi) != ESP_OK) return false;

    pinMode(S3_SPI_IRQ, INPUT_PULLUP);     // sin FPGA -> en reposo, no dispara
    pinMode(S3_BTN_BOOT, INPUT_PULLUP);

    compBegin(&s_comp, spiXfer, nullptr);

    // Handshake: si la FPGA contesta su version, esta probado el enlace ENTERO
    // -- cableado, modo, orden de bytes y LAS DOS DIRECCIONES -- de una vez.
    uint8_t ver = 0, sub = 0;
    g_companionVersion = compStatus(&s_comp, &ver, &sub) ? ver : 0;
    return g_companionVersion != 0;
}

// ==========================================================================
//  PELDANO 1 del lanzador: pintar la tele con el MSX PARADO
//
//  QUE DEMUESTRA, Y POR QUE ES ESTA PRUEBA Y NO OTRA
//  --------------------------------------------------------------------------
//  No monta ningun modo de pantalla a proposito. El color de FONDO del VDP
//  (R#7) se ve en toda la tele aunque la pantalla este en blanking, y se pone
//  con dos bytes. Si ademas VA CAMBIANDO, un acierto por casualidad queda
//  descartado: una tele que late con el Z80 en reset solo puede venir de la
//  cadena entera funcionando —
//
//      S3 -> SPI -> mcu_spi_new -> launcher_svc -> FIFO 27/86 MHz ->
//      mux del bus -> registros del V9968 -> msx2hdmi_v9968 -> HDMI
//
//  Y de paso prueba la RETENCION: si el Z80 no estuviera parado, la BIOS
//  reprogramaria el VDP y se llevaria los colores por delante.
//
//  SI NO PASA NADA, mirar en este orden:
//    - version del lanzador = 0  -> el destino OSD no contesta: bitstream viejo
//    - la tele arranca el MSX     -> lnz_hold no llega a esp_boot_ok
//    - colores fijos, no cambian  -> llega el primero y se atasca la FIFO
//    - PERDIDOS > 0               -> el VDP no traga al ritmo del S3
// ==========================================================================
// ============================ BISECCION =================================
// Soltar el mando NO devuelve la vida al MSX, y hold SI pasa de 1 a 0 (medido
// en placa). O sea que el Z80 sale de reset y aun asi no arranca. Descartados
// por lectura del RTL: el refresco de la SDRAM (autonomo con el Z80 parado,
// memory.v:219) y el sd_init pegado (a STANDBY solo se llega por reset).
//
// El lanzador hace CUATRO cosas mientras retiene. Con LNZ_TOCAR_VDP a 0 se
// quitan las tres que tocan el VDP y queda solo retener + leer la SD:
//   arranca el MSX  -> la culpa es de lo que le hacemos al VDP
//   sigue colgado   -> la culpa es de la retencion o de la SD
// Una prueba, media busqueda menos.
// Idea de Albert: quiza lo que impide arrancar no es parar el Z80, sino que le
// quitemos la SD. En mi protocolo TOMAR hace LAS DOS COSAS (retener + duenno de
// la tarjeta), asi que no se pueden pedir por separado -- una conflacion mia al
// disenarlo. Con LNZ_TOCAR_SD a 0 el lanzador toma y suelta SIN TOCAR NADA:
//   arranca -> la culpa es de lo que hacemos con la SD (idea de Albert)
//   cuelga  -> la culpa es del propio corte del Z80 (memory.v sirve en bucle
//              abierto y una transaccion en vuelo se pierde EN SILENCIO)
#define LNZ_TOCAR_SD        1        // (0 para bisecar: sin tocar la SD)
#define LNZ_TOCAR_VDP       1        // (0 para bisecar: sin tocar el VDP)
// PRUEBA DE CONTROL (idea de Albert, y un fallo de metodo mio: nunca la hice).
// Con esto a 0 el lanzador NO CORRE EN ABSOLUTO: ni toma el mando, ni toca la
// SD, ni el VDP. Si el MSX aun asi no arranca, el problema no es nada de lo que
// llevamos depurando -- es la propia .fs v31i, y todo lo demas era ruido.
// Construir el lanzador sin verificar antes que la build base arranca sola fue
// saltarse la linea base.
#define LNZ_PELDANO1        1        // (0 = prueba de control: lanzador apagado)
#define LNZ_P1_MS_COLOR     700      // cuanto dura cada color
#define LNZ_P1_MS_ESPERA  20000      // margen para que la FPGA acabe de cargar
#if LNZ_TOCAR_VDP
#define LNZ_P1_MS_TOTAL  300000      // tope de seguridad si nadie pulsa BOOT
#else
#define LNZ_P1_MS_TOTAL    8000      // sin colores que mirar, se suelta solo
#endif

#if LNZ_PELDANO1
// Colores del MSX bien separados entre si: si sale otro, es que el dato se
// esta corrompiendo, no que "casi funciona".
static const uint8_t s_p1_colores[] = { 6, 3, 5, 15, 8, 12 };
static const char   *s_p1_nombres[] = { "rojo", "verde", "azul",
                                        "blanco", "rojo claro", "verde osc" };

// Estado visible desde la pantalla del S3 (ScreenS3), para no depender de
// tener la tele delante mientras se depura.
uint8_t  g_p1_color    = 0;      // indice del color que se acaba de mandar
uint8_t  g_p1_version  = 0;      // version que devuelve launcher_svc
uint16_t g_p1_perdidos = 0;      // bytes que el VDP no ha podido tragar
uint32_t g_p1_enviados = 0;      // bytes que el S3 ha puesto en el cable
uint8_t  g_p1_fase     = 0;      // 0 sin empezar, 1 pintando, 2 acabado, 9 sin lanzador
// Peldano 2: lo que devolvio el sector 0 de la SD
uint16_t g_p2_firma    = 0;      // bytes 510-511, deben ser 55AA en FAT16
bool     g_p2_ok       = false;
uint8_t  g_p2_ini[4]   = {0,0,0,0};
uint8_t  g_p2_ver      = 0;      // version que devuelve sdc_bridge
bool     g_p2_busy0    = false;  // la tarjeta ya estaba ocupada al empezar
bool     g_p2_busy1    = false;  // ...y se puso ocupada al pedirle el sector
bool     g_p2_hold     = false;  // el puente cree que tenemos el mando
uint8_t  g_p2_intentos = 0;
// MEDIDA del protocolo de lectura: 32 muestras de `ocupado` a 1 ms, antes de
// pedir nada y justo despues de pedir el sector 0. Se llega aqui despues de
// TRES intentos de arreglar la lectura razonando sobre tiempos sin medirlos.
uint32_t g_pm_antes = 0;
uint32_t g_pm_despues = 0;
// Peldano 3: FatFs
bool     g_p3_montado  = false;
uint8_t  g_p3_err      = 0;
int      g_p3_n        = 0;
char     g_p3_prim[3][20] = {{0},{0},{0}};
bool     g_p1_reteniendo = false;

const char *lnzPeldano1Nombre() { return s_p1_nombres[g_p1_color]; }

static void lnzPeldano1()
{
    static uint8_t  fase   = 0;   // 0 = sin empezar, 1 = pintando, 2 = acabado
    static uint32_t t_sig  = 0;
    static uint32_t t_fin  = 0;
    static uint8_t  idx    = 0;

    // EL DIAGNOSTICO VA LO PRIMERO Y SIEMPRE. En la version anterior estaba al
    // final, y el camino de "el lanzador no contesta" hacia return antes de
    // llegar: el fallo mas importante era justo el que NO se pintaba, y "no
    // contesta" se veia igual que "no se ha ejecutado". Una sonda que se apaga
    // cuando hay averia no es una sonda.
    g_p1_fase = fase;

    // El estado del puente se sigue mirando DESPUES de soltar. Si al pulsar
    // BOOT el MSX no arranca, esto dice si es porque hold no se ha limpiado
    // (el Z80 sigue en reset) o porque se limpio y el cuelgue esta mas alla.
    // Sin esto habria que adivinar, y adivinar aqui cuesta una campana.
    {
        static uint32_t t_sd = 0;
        if ((int32_t)(millis() - t_sd) >= 0) {
            bool b = false;
            compSdStatus(&s_comp, &g_p2_ver, &b, &g_p2_hold);
            g_p2_busy0 = b;      // en vivo: ahora es "ocupada AHORA"
            t_sd = millis() + 500;
        }
    }

    screenSetLauncher(g_p1_version, g_p1_perdidos, g_p1_enviados,
                      s_p1_colores[g_p1_color], g_p1_reteniendo,
                      g_p1_fase, g_companionVersion);
    screenSetSd(g_p2_firma, g_p2_ok, g_p2_ini);
    screenSetSdDiag(g_p2_ver, g_p2_hold, g_p2_busy0, g_p2_busy1, g_p2_intentos);
    screenSetFs(g_p3_montado, g_p3_err, g_p3_n, g_p3_prim,
                lfsLbaBase(), lfsTipoPart(), lfsNumParts(), lfsPrimeros4(),
                g_pm_antes, g_pm_despues);

    // Sonda cruda de MISO, una vez por segundo. Se pregunta al destino HID
    // (comando 0 = estado) porque de ese SI sabemos que el lado de ida
    // funciona: el boton BOOT mete una tecla en el MSX. Si aun asi no vuelve
    // nada, el problema esta en la vuelta y en nada mas.
    {
        static uint32_t t_probe = 0;
        if ((int32_t)(millis() - t_probe) >= 0) {
            uint8_t rx8[8];
            compProbe(&s_comp, COMP_TGT_HID, COMP_HID_STATUS, rx8);

            // ---- LA PRUEBA DEL PULL-UP -------------------------------
            // mcu_spi_new.v deja spi_io_dout a 0 con CS en reposo. Con un
            // pull-up puesto en nuestro lado:
            //   lee 0 -> la FPGA CONDUCE el pin: la salida vive y el dato es 0
            //   lee 1 -> la FPGA NO conduce: el fallo es el pin/banco/rutado
            // Distingue "responde cero" de "no responde", que mirando el bus
            // se ven exactamente igual. Lo mismo con IRQ#, la otra salida de
            // la FPGA en ese banco, que tampoco se ha ejercitado jamas.
            gpio_pullup_en((gpio_num_t)S3_SPI_MISO);
            gpio_pullup_en((gpio_num_t)S3_SPI_IRQ);
            delayMicroseconds(50);
            rx8[6] = (uint8_t)(0xE0 | (gpio_get_level((gpio_num_t)S3_SPI_MISO) ? 1 : 0));
            rx8[7] = (uint8_t)(0xF0 | (gpio_get_level((gpio_num_t)S3_SPI_IRQ)  ? 1 : 0));

            screenSetLauncherRaw(rx8);
            t_probe = millis() + 1000;
        }
    }

    if (fase == 2) {
        // SOLTAR se repite cada segundo. Es idempotente (sdc_bridge solo hace
        // hold <= 0) y elimina de la lista de sospechosos "la orden no llego".
        // Si con esto hold sigue a 1, el problema NO es el mensaje.
        static uint32_t t_rel = 0;
        if ((int32_t)(millis() - t_rel) >= 0) {
            compSdRelease(&s_comp);
            t_rel = millis() + 1000;
        }
        return;
    }

    if (fase == 0) {
        // SE REINTENTA. La version anterior preguntaba UNA vez y si no le
        // contestaban se rendia para siempre -- y en arranque frio no contesta
        // nadie todavia: la FPGA esta cargando 20 MB de bitstream desde la
        // flash mientras el S3 ya lleva rato despierto. De ahi que funcionara
        // al reiniciar solo el S3 (la FPGA llevaba rato lista) y nunca en frio.
        // Un handshake contra algo que arranca mas lento SIEMPRE se reintenta.
        static uint32_t t_limite = 0;
        if (t_limite == 0) t_limite = millis() + LNZ_P1_MS_ESPERA;

        bool lleno = false;
        if (!compLnzStatus(&s_comp, &g_p1_version, &lleno, &g_p1_perdidos)) {
            g_p1_version = 0;
            if ((int32_t)(millis() - t_limite) < 0) return;   // seguir esperando
            fase = 2;                       // se acabo el margen: MSX normal
            g_p1_fase = 9;                  // ...y que se VEA que fue por esto
            return;
        }
        compSdTake(&s_comp);                // retiene el Z80
        g_p1_reteniendo = true;

        // APAGAR LA PANTALLA (R#1 bit6 = 0). Sin esto la prueba no vale: el
        // color de fondo solo ocupa la tele ENTERA cuando el VDP esta en
        // blanking. Si la BIOS ya arranco y habilito la pantalla -- que es lo
        // que pasa, porque el S3 tarda mas en arrancar que los 3 s del
        // temporizador -- R#7 solo pinta el BORDE y lo que se ve es lo que
        // dejo la BIOS. (23/08: exactamente lo que ocurrio en la placa.)
#if LNZ_TOCAR_VDP
        compVdpReg(&s_comp, 1, 0x00);
#endif

        // ---- LA PRUEBA QUE DECIDE -------------------------------------
        // 1024 bytes de golpe contra una cola de 256. Lo que se mire despues
        // no es el color de la tele -- que puede enganar de mil formas -- sino
        // PERDIDOS:
        //   ~768 perdidos -> la cola NO se vacia: el lado del VDP esta parado
        //                    (owns que no llega, o un READY que nunca viene)
        //   ~0 perdidos   -> la cola SI se vacia: las escrituras LLEGAN al bus
        //                    del VDP, y entonces el fallo esta mas alla
        // Va al puerto 0 (datos de VRAM) porque garabatear VRAM con el Z80 en
        // reset es inofensivo: la BIOS la reinicializa al soltar.
#if LNZ_TOCAR_VDP
        {
            static uint8_t basura[128];
            for (int i = 0; i < 128; i++) basura[i] = (uint8_t)i;
            for (int k = 0; k < 8; k++)
                g_p1_enviados += compVdpBulk(&s_comp, 0, basura, sizeof(basura));
        }
#endif
        {
            bool ll = false;
            compLnzStatus(&s_comp, &g_p1_version, &ll, &g_p1_perdidos);
        }

#if LNZ_TOCAR_SD
        // ================= PELDANO 2: leer el sector 0 de la SD ==========
        // El sector 0 de una FAT16 acaba en 55 AA. Es la firma que no puede
        // salir por casualidad: si aparece en los bytes 510-511, el puente ha
        // leido la tarjeta DE VERDAD -- no es ruido, ni un buffer a ceros, ni
        // el eco de lo que mandamos.
        {
            // Estado ANTES de pedir nada. sdc_bridge solo lanza la lectura si
            // (hold && !leyendo && !rbusy): saber cual de los tres falla es la
            // diferencia entre arreglarlo y probar cosas.
            compSdStatus(&s_comp, &g_p2_ver, &g_p2_busy0, &g_p2_hold);

            // La tarjeta puede tardar en inicializarse (el sd_reader arranca
            // con la FPGA y el S3 llega pronto). Se REINTENTA: la leccion del
            // peldano 1 fue justo esta.
            static uint8_t sec[512];
            uint32_t tope_total = millis() + 8000;
            bool leido = false;
            while (!leido && (int32_t)(millis() - tope_total) < 0) {
                if (compSdLeerSector(&s_comp, 0, sec)) {
                    g_p2_busy1 = true;
                    if (sec[510] == 0x55 && sec[511] == 0xAA) leido = true;
                }
                if (!leido) delay(200);
                g_p2_intentos++;
            }
            g_p2_firma = (uint16_t)((sec[510] << 8) | sec[511]);
            g_p2_ok    = (sec[510] == 0x55 && sec[511] == 0xAA);
            // los cuatro primeros bytes tambien dicen mucho: un sector de
            // arranque real empieza por un salto (EB xx 90 o E9)
            for (int i = 0; i < 4; i++) g_p2_ini[i] = sec[i];
        }

        // ============ MEDIDA del protocolo de lectura ====================
        {
            for (int i = 0; i < 32; i++) {
                g_pm_antes = (g_pm_antes << 1) | (compSdBusy(&s_comp) ? 1 : 0);
                delay(1);
            }
            compSdRead(&s_comp, 0);
            for (int i = 0; i < 32; i++) {
                g_pm_despues = (g_pm_despues << 1) | (compSdBusy(&s_comp) ? 1 : 0);
                delay(1);
            }
        }

        // ============ PELDANO 3: FatFs y listar la raiz =================
        // Se monta DESPUES de tomar el mando (el puente ignora peticiones de
        // sector si la SD no es nuestra) y con la tarjeta ya despierta, que es
        // lo que acaba de demostrar la firma 55AA.
        g_p3_montado = lfsMount(&s_comp);
        g_p3_err = lfsUltimoError();
        if (g_p3_montado) {
            static LfsEntrada ent[24];
            g_p3_n = lfsListar("/", ent, 24);
            for (int i = 0; i < 3 && i < g_p3_n; i++) {
                snprintf(g_p3_prim[i], sizeof(g_p3_prim[i]), "%s%s",
                         ent[i].carpeta ? "/" : "", ent[i].nombre);
            }
        }
#endif // LNZ_TOCAR_SD

        t_fin = millis() + LNZ_P1_MS_TOTAL;
        t_sig = millis();
        fase  = 1;
    }

    // Se suelta al pulsar BOOT (idea de Albert): asi el lanzador se queda
    // quieto y se puede mirar con calma, que es justo lo que hara el menu de
    // verdad -- esperar al usuario. El tope por tiempo se queda de red de
    // seguridad por si el boton no responde.
    bool boot_ahora = (digitalRead(S3_BTN_BOOT) == LOW);
    if (boot_ahora || (int32_t)(millis() - t_fin) >= 0) {
#if LNZ_TOCAR_SD
        // Desmontar ANTES de soltar: a partir de aqui la tarjeta es del MSX y
        // no debe quedar ni un descriptor nuestro apuntandola.
        lfsUnmount();
#endif
        compSdRelease(&s_comp);             // suelta: el MSX arranca
        g_p1_reteniendo = false;
        fase = 2;
        return;
    }

    if ((int32_t)(millis() - t_sig) >= 0) {
        g_p1_color = idx;
#if LNZ_TOCAR_VDP
        // R#7 = {color de texto, color de FONDO}. Con la pantalla en blanking
        // el fondo ocupa la tele entera.
        compVdpReg(&s_comp, 7, s_p1_colores[idx]);
#endif
        bool lleno = false;
        compLnzStatus(&s_comp, &g_p1_version, &lleno, &g_p1_perdidos);
        g_p1_enviados += 2;                 // el registro son dos bytes
        t_sig = millis() + LNZ_P1_MS_COLOR;
        idx = (uint8_t)((idx + 1) % (sizeof(s_p1_colores) / sizeof(s_p1_colores[0])));
    }

}
#endif // LNZ_PELDANO1

// ---------------------------------------------------------------------------
//  UNA sola forma de leer un sector, y sin adivinar tiempos.
//
//  Habia tres copias de esta secuencia y todas miraban `ocupado` JUSTO despues
//  de pedir el sector. sdc_bridge IGNORA la peticion si el lector esta ocupado,
//  y entonces `ocupado` sale 0, la espera acaba al instante y se devuelve el
//  BUFFER ANTERIOR sin dar error.
//
//  El primer arreglo -- exigir ver `ocupado` SUBIR -- fue peor: si la lectura
//  termina antes del primer sondeo, o si el estado ya venia ocupado de antes,
//  el flanco no se ve y se tiraba una lectura BUENA (error 241 en placa).
//
//  Asi que no se persigue ningun flanco. Se espera a que la tarjeta este
//  LIBRE, se pide, se espera a que vuelva a estarlo, y se lee. Sin suposiciones
//  sobre cuanto tarda nada.
// ---------------------------------------------------------------------------
static bool esperarLibre(Companion *c, uint32_t ms)
{
    uint32_t tope = millis() + ms;
    while ((int32_t)(millis() - tope) < 0) {
        if (!compSdBusy(c)) return true;
        delay(1);
    }
    return false;
}

bool compSdLeerSector(Companion *c, uint32_t lba, uint8_t *buf512)
{
    if (!c || !c->xfer || !buf512) return false;

    for (int intento = 0; intento < 3; intento++) {
        // 1) que la tarjeta este libre ANTES de pedir: si esta ocupada, el
        //    puente ignoraria la peticion en silencio.
        if (!esperarLibre(c, 1500)) continue;

        // 2) pedir
        compSdRead(c, lba);

        // 3) dejarle arrancar y esperar a que acabe. El margen inicial evita
        //    confundir "aun no ha empezado" con "ya ha terminado".
        delay(2);
        if (!esperarLibre(c, 1500)) continue;

        // 4) ahora el buffer es de ESTA lectura
        if (compSdSector(c, buf512)) return true;
    }
    return false;
}

void companionTask()
{
    // El diagnostico va ANTES del corte por SPI muerto: si spiXfer no esta,
    // sus llamadas son no-op inofensivas, pero la pantalla sigue contando lo
    // que pasa. Al reves nos quedariamos otra vez sin saber nada.
#if LNZ_PELDANO1
    lnzPeldano1();
#endif

    if (!s_spi) return;

    // Prueba manual: BOOT manda una 'A'. El vector keyboard[] del FPGA se
    // indexa por USAGE HID (usb_keyboard_msx.vhd:95 "standard HID codes"), NO
    // por celda de matriz: usage 4 = A. (El 22 de antes era 0x16 = S.)
    // Si aparece la letra en el MSX, el enlace esta cerrado de punta a punta.
    static bool     prev = true;         // pulsado = 0 (pull-up)
    static uint32_t t_up = 0;
    static bool     pend = false;
    bool now = digitalRead(S3_BTN_BOOT);

    // Mientras el lanzador retiene la maquina, BOOT significa "suelta", no
    // "manda una tecla": si no, la pulsacion haria las dos cosas.
#if LNZ_PELDANO1
    if (g_p1_reteniendo) { prev = now; return; }
#endif

    if (prev && !now) {                  // flanco de pulsacion
        compKey(&s_comp, 4, true);
        t_up = millis() + 60;            // 60 ms pulsada: el MSX lo ve seguro
        pend = true;
    }
    prev = now;

    if (pend && (int32_t)(millis() - t_up) >= 0) {
        compKey(&s_comp, 4, false);
        pend = false;
    }
}

#endif // BOARD_S3
