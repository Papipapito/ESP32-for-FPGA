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
#include "Companion.h"

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
#define LNZ_P1_MS_TOTAL   12000      // y cuando se suelta para que arranque el MSX

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
bool     g_p1_reteniendo = false;

const char *lnzPeldano1Nombre() { return s_p1_nombres[g_p1_color]; }

static void lnzPeldano1()
{
    static uint8_t  fase   = 0;   // 0 = sin empezar, 1 = pintando, 2 = acabado
    static uint32_t t_sig  = 0;
    static uint32_t t_fin  = 0;
    static uint8_t  idx    = 0;

    if (fase == 2) return;

    if (fase == 0) {
        // Se pregunta ANTES de tomar el mando: si el bitstream no lleva el
        // lanzador, mejor no retener un Z80 que luego no sabremos soltar.
        bool lleno = false;
        if (!compLnzStatus(&s_comp, &g_p1_version, &lleno, &g_p1_perdidos)) {
            g_p1_version = 0;
            fase = 2;                       // sin lanzador: MSX normal
            return;
        }
        compSdTake(&s_comp);                // retiene el Z80
        g_p1_reteniendo = true;
        t_fin = millis() + LNZ_P1_MS_TOTAL;
        t_sig = millis();
        fase  = 1;
    }

    if ((int32_t)(millis() - t_fin) >= 0) {
        compSdRelease(&s_comp);             // suelta: el MSX arranca
        g_p1_reteniendo = false;
        fase = 2;
        return;
    }

    if ((int32_t)(millis() - t_sig) >= 0) {
        idx = (uint8_t)((idx + 1) % (sizeof(s_p1_colores) / sizeof(s_p1_colores[0])));
        g_p1_color = idx;
        // R#7 = {color de texto, color de FONDO}. Con la pantalla en blanking
        // el fondo ocupa la tele entera.
        compVdpReg(&s_comp, 7, s_p1_colores[idx]);
        bool lleno = false;
        compLnzStatus(&s_comp, &g_p1_version, &lleno, &g_p1_perdidos);
        t_sig = millis() + LNZ_P1_MS_COLOR;
    }
}
#endif // LNZ_PELDANO1

void companionTask()
{
    if (!s_spi) return;

#if LNZ_PELDANO1
    lnzPeldano1();
#endif

    // Prueba manual: BOOT manda una 'A'. El vector keyboard[] del FPGA se
    // indexa por USAGE HID (usb_keyboard_msx.vhd:95 "standard HID codes"), NO
    // por celda de matriz: usage 4 = A. (El 22 de antes era 0x16 = S.)
    // Si aparece la letra en el MSX, el enlace esta cerrado de punta a punta.
    static bool     prev = true;         // pulsado = 0 (pull-up)
    static uint32_t t_up = 0;
    static bool     pend = false;
    bool now = digitalRead(S3_BTN_BOOT);

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
