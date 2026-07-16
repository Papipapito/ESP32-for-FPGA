/*
 * Tape.ino - Streaming de cinta virtual (CVS1) al FPGA del MSXnano/MSXimus.
 * Modulo anadido al firmware UNAPI de ducasp (Arduino concatena los .ino).
 *
 * Cadena: .tsx (descargado o subido desde el MSX) -> tsx2cvs (conversor C++
 * verificado byte-exacto contra tsx2rom) -> UART1 -> FPGA pin 26 ->
 * tape_uart.v (FIFO 2KB) -> cas_stream.v (KCS) -> BIOS del MSX (RUN"CAS:").
 *
 * FLOW CONTROL: el FPGA saca RTR (pin 32, activo-alto = "sigue"). Baja al
 * llenarse el FIFO a 3/4 (1536/2047) -> quedan ~511 bytes de margen. Aqui
 * mandamos rafagas de <=64 bytes tras comprobar RTR; con el buffer TX del
 * ESP (~128B) el peor caso en vuelo es ~192 bytes < 511 -> IMPOSIBLE
 * desbordar si el cableado es correcto.
 *
 * PINES (defines): C6 -> TX1=GPIO20, RTR=GPIO23 (header de la C6-LCD-1.3;
 * NO usar 12/13 = USB-JTAG). S3 -> ajustar defines al montarlo.
 *
 * El disparo (PLAY) llegara de la fase de catalogo (comando UNAPI custom /
 * subida desde SD). Este modulo expone la API y no toca el parser de ducasp.
 */

#include "tsx2cvs.h"
#include <vector>

#ifndef TAPE_TX_PIN
#define TAPE_TX_PIN  20      // C6: GPIO20 -> FPGA pin 26 (datos CVS1)
#endif
#ifndef TAPE_RTR_PIN
#define TAPE_RTR_PIN 23      // C6: GPIO23 <- FPGA pin 32 (1 = enviar)
#endif
#define TAPE_BAUD    115200
#define TAPE_CHUNK   64      // rafaga maxima tras ver RTR alto

static std::vector<uint8_t> g_tapeStream;   // stream CVS1 en RAM
static volatile size_t      g_tapePos  = 0;
static volatile bool        g_tapeBusy = false;
static TaskHandle_t         g_tapeTask = nullptr;

// ---- estado para la pantalla (Display.ino puede consultarlos) ----
bool   tapeBusy()     { return g_tapeBusy; }
size_t tapeTotal()    { return g_tapeStream.size(); }
size_t tapeSent()     { return g_tapePos; }

static void tapeFeedTask(void*)
{
    while (g_tapePos < g_tapeStream.size()) {
        if (digitalRead(TAPE_RTR_PIN)) {
            size_t n = g_tapeStream.size() - g_tapePos;
            if (n > TAPE_CHUNK) n = TAPE_CHUNK;
            Serial1.write(g_tapeStream.data() + g_tapePos, n);
            Serial1.flush();               // esperar a que salga (no acumular
            g_tapePos += n;                // en vuelo mas alla del chunk)
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));  // FPGA lleno: la cinta consume a
        }                                  // ~110B/s, sobra con re-mirar cada 5ms
    }
    g_tapeStream.clear();
    g_tapeStream.shrink_to_fit();
    g_tapeBusy = false;
    g_tapeTask = nullptr;
    vTaskDelete(nullptr);
}

// Inicializar UART1 + RTR. Llamar una vez desde setup() (tras displaySetup()).
void tapeSetup()
{
    pinMode(TAPE_RTR_PIN, INPUT_PULLDOWN);   // sin cable -> 0 -> no envia
    Serial1.begin(TAPE_BAUD, SERIAL_8N1, -1 /*rx: no usado*/, TAPE_TX_PIN);
}

// Reproducir un .tsx/.cas que YA esta en memoria: convierte a CVS1 y arranca
// la tarea de streaming. Devuelve nullptr si OK o el mensaje de error.
const char* tapePlay(const uint8_t* tapefile, size_t len)
{
    if (g_tapeBusy) return "ya hay una cinta reproduciendose";
    const char* err = tsx2cvs(tapefile, len, g_tapeStream);
    if (err) { g_tapeStream.clear(); return err; }
    g_tapePos  = 0;
    g_tapeBusy = true;
    // prioridad baja: el enlace UNAPI (loop) manda; la cinta tiene 18s de buffer
    if (xTaskCreate(tapeFeedTask, "tapefeed", 3072, nullptr, 1, &g_tapeTask) != pdPASS) {
        g_tapeBusy = false;
        g_tapeStream.clear();
        return "sin memoria para la tarea de cinta";
    }
    return nullptr;
}

// Cancelar la reproduccion (p.ej. el usuario vuelve al menu).
void tapeStop()
{
    if (g_tapeTask) { vTaskDelete(g_tapeTask); g_tapeTask = nullptr; }
    g_tapeStream.clear();
    g_tapeStream.shrink_to_fit();
    g_tapePos  = 0;
    g_tapeBusy = false;
}
