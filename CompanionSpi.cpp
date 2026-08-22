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

void companionTask()
{
    if (!s_spi) return;

    // Prueba manual: BOOT manda una 'A'. Indice 22 de la matriz del MSX
    // (fila 2 = ' ` , . / _ A B; la A es el bit 6 -> 2*8+6). Si aparece la
    // letra en el MSX, el enlace esta cerrado de punta a punta.
    static bool     prev = true;         // pulsado = 0 (pull-up)
    static uint32_t t_up = 0;
    static bool     pend = false;
    bool now = digitalRead(S3_BTN_BOOT);

    if (prev && !now) {                  // flanco de pulsacion
        compKey(&s_comp, 22, true);
        t_up = millis() + 60;            // 60 ms pulsada: el MSX lo ve seguro
        pend = true;
    }
    prev = now;

    if (pend && (int32_t)(millis() - t_up) >= 0) {
        compKey(&s_comp, 22, false);
        pend = false;
    }
}

#endif // BOARD_S3
