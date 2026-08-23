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
#define LNZ_PELDANO1        1        // poner a 0 cuando pasemos al peldano 2
#define LNZ_P1_MS_COLOR     700      // cuanto dura cada color
#define LNZ_P1_MS_ESPERA  20000      // margen para que la FPGA acabe de cargar
#define LNZ_P1_MS_TOTAL  300000      // tope de seguridad si nadie pulsa BOOT

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
    screenSetLauncher(g_p1_version, g_p1_perdidos, g_p1_enviados,
                      s_p1_colores[g_p1_color], g_p1_reteniendo,
                      g_p1_fase, g_companionVersion);

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

    if (fase == 2) return;

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
        compVdpReg(&s_comp, 1, 0x00);

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
        {
            static uint8_t basura[128];
            for (int i = 0; i < 128; i++) basura[i] = (uint8_t)i;
            for (int k = 0; k < 8; k++)
                g_p1_enviados += compVdpBulk(&s_comp, 0, basura, sizeof(basura));
        }
        {
            bool ll = false;
            compLnzStatus(&s_comp, &g_p1_version, &ll, &g_p1_perdidos);
        }

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
        compSdRelease(&s_comp);             // suelta: el MSX arranca
        g_p1_reteniendo = false;
        fase = 2;
        return;
    }

    if ((int32_t)(millis() - t_sig) >= 0) {
        g_p1_color = idx;
        // R#7 = {color de texto, color de FONDO}. Con la pantalla en blanking
        // el fondo ocupa la tele entera.
        compVdpReg(&s_comp, 7, s_p1_colores[idx]);
        bool lleno = false;
        compLnzStatus(&s_comp, &g_p1_version, &lleno, &g_p1_perdidos);
        g_p1_enviados += 2;                 // el registro son dos bytes
        t_sig = millis() + LNZ_P1_MS_COLOR;
        idx = (uint8_t)((idx + 1) % (sizeof(s_p1_colores) / sizeof(s_p1_colores[0])));
    }

}
#endif // LNZ_PELDANO1

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
    if (g_p1_reteniendo) { prev = now; return; }

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
