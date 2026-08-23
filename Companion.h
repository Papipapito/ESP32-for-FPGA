/*
 * Companion.h - enlace SPI del ESP32-S3 con la FPGA del MSXimus.
 *
 * C++ puro: NI Arduino NI ESP-IDF, igual que FileHunter. Todo lo que hay aqui
 * es construccion de tramas, asi que se prueba en el PC sin placa. Lo que
 * depende del hardware (el bus SPI de verdad) entra por un callback.
 *
 * ================== POR QUE ESTE PROTOCOLO Y NO OTRO ==================
 *
 * La FPGA YA TIENE un companion SPI implementado y ya esta instanciado en
 * top.v: es el de TangCore (nand2mario), pensado para el BL616 de a bordo.
 * Ese BL616 nunca llego a llevar firmware, asi que el bloque esta ahi
 * OCIOSO, esperando un maestro. O sea que el S3 no necesita un protocolo
 * nuevo: necesita hablar EL QUE YA HAY.
 *
 * Modulos de la FPGA implicados:
 *   fpga/src/usb/mcu_spi_new.v   el transporte (SPI -> strobes por destino)
 *   fpga/src/usb/hid.v           teclado, raton y joystick
 *   fpga/src/usb/fpga_companion.v el pegamento, ya instanciado en top.v:5333
 *
 * ================== EL PROTOCOLO, TAL COMO ES ==================
 *
 * SPI MODO 1 (CPOL=0, CPHA=1): reposo BAJO, el dato se pone en el flanco de
 * SUBIDA y se muestrea en el de BAJADA (mcu_spi_new.v:26-28). Full duplex.
 *
 * Una trama = CS abajo, N bytes, CS arriba. El PRIMER byte es el DESTINO;
 * el segundo es el COMANDO; los siguientes son la carga.
 *
 *   [destino][comando][b0][b1][b2]...
 *
 * ⚠️ EL CS NO SE PUEDE SOLTAR ENTRE MEDIAS. mcu_spi_new reinicia su contador
 * con el CS (spi_cnt <= 0 en el posedge de spi_io_ss), asi que soltarlo a
 * mitad tira la trama y la siguiente se lee corrida. Por eso el callback
 * recibe la trama ENTERA y no byte a byte.
 *
 * Destinos (mcu_spi_new.v:74-77):  0 SYS   1 HID   2 OSD   3 SDC
 *
 * Comandos del HID (hid.v:84-110):
 *   0 estado     devuelve version (1) y subversion (0)
 *   1 teclado    [tecla]  bit7 = 1 SOLTAR, 0 PULSAR; bits 6-0 = indice 0..127
 *   2 raton      [botones][dx][dy]  dx/dy son deltas CON SIGNO, se SUMAN a un
 *                acumulador de la FPGA; botones en los bits 1-0
 *   3 joystick   [dispositivo][bits]   dispositivo 0 o 1
 *
 * ⚠️ El teclado va TECLA A TECLA, no por informe. Cada trama enciende o apaga
 * UN bit del vector keyboard[127:0] de la FPGA. Quien tenga un informe HID de
 * 8 bytes tiene que convertirlo a altas y bajas el mismo: la FPGA no compara
 * con el informe anterior.
 */
#ifndef _COMPANION_H
#define _COMPANION_H

#include <stdint.h>
#include <stddef.h>

// destinos del transporte
#define COMP_TGT_SYS  0
#define COMP_TGT_HID  1
#define COMP_TGT_OSD  2
#define COMP_TGT_SDC  3

// comandos del destino HID
#define COMP_HID_STATUS    0
#define COMP_HID_KEYBOARD  1
#define COMP_HID_MOUSE     2
#define COMP_HID_JOYSTICK  3

// comandos del destino OSD = el lanzador pinta por el VDP (launcher_svc.v)
#define COMP_LNZ_STATUS    0
#define COMP_LNZ_WRITE     1    // [puerto][dato]        una escritura suelta
#define COMP_LNZ_BULK      2    // [puerto][d0][d1]...   todo al mismo puerto
#define COMP_LNZ_KEYS      3    // 16 bytes = vector de teclado USB del FPGA

// comandos del destino SDC = el lanzador lee la SD (sdc_bridge.v)
#define COMP_SDC_STATUS    0
#define COMP_SDC_TAKE      1    // toma la SD Y RETIENE el Z80
#define COMP_SDC_RELEASE   2    // suelta: el MSX arranca
#define COMP_SDC_READ      3    // [lba0..lba3] little endian
#define COMP_SDC_DATA      4    // saca los 512 bytes por MISO

// Puertos del VDP = bus_address[2:0] (v9968_cpu_glue.v:98). En el MSX de toda
// la vida son #98..#9B; aqui solo van los dos bits bajos.
#define VDP_PORT_VRAM      0    // #98 datos de VRAM
#define VDP_PORT_REG       1    // #99 registro / direccion
#define VDP_PORT_PAL       2    // #9A paleta
#define VDP_PORT_IREG      3    // #9B registro indirecto

#define COMP_MAX_FRAME  24      // la mas larga es LEER TECLAS: 2 + 16 + relleno

// Envia una trama COMPLETA por SPI en modo 1, con CS bajo de principio a fin.
// rx puede ser NULL si no interesa lo que devuelve la FPGA.
typedef void (*CompXfer)(const uint8_t *tx, uint8_t *rx, size_t n, void *user);

typedef struct {
    CompXfer xfer;
    void    *user;
} Companion;

void compBegin(Companion *c, CompXfer xfer, void *user);

// --- construccion de tramas (separada del envio: asi se puede probar) ------
size_t compBuildKey     (uint8_t *dst, uint8_t codigo, bool pulsada);
size_t compBuildMouse   (uint8_t *dst, int8_t dx, int8_t dy, uint8_t botones);
size_t compBuildJoystick(uint8_t *dst, uint8_t dispositivo, uint8_t bits);
size_t compBuildStatus  (uint8_t *dst);

// --- lanzador: pintar por el VDP -----------------------------------------
// Una escritura suelta a un puerto del VDP.
size_t compBuildVdpWrite(uint8_t *dst, uint8_t puerto, uint8_t dato);
// Un REGISTRO del VDP son DOS escrituras al puerto #99: primero el dato y
// luego 0x80|registro. Van en una sola trama de volcado para que no se pueda
// colar nada por medio y dejar el VDP a medias.
size_t compBuildVdpReg  (uint8_t *dst, uint8_t registro, uint8_t valor);
size_t compBuildLnzStatus(uint8_t *dst);
size_t compBuildLnzKeys  (uint8_t *dst);

// --- lanzador: gobierno de la SD y del Z80 --------------------------------
size_t compBuildSdTake   (uint8_t *dst);
size_t compBuildSdRelease(uint8_t *dst);
size_t compBuildSdRead   (uint8_t *dst, uint32_t lba);

// --- envio ----------------------------------------------------------------
void compKey     (Companion *c, uint8_t codigo, bool pulsada);
void compMouse   (Companion *c, int8_t dx, int8_t dy, uint8_t botones);
void compJoystick(Companion *c, uint8_t dispositivo, uint8_t bits);
// Devuelve true y rellena version/subversion si la FPGA contesta.
bool compStatus  (Companion *c, uint8_t *version, uint8_t *subversion);

// --- lanzador: envio ------------------------------------------------------
void compVdpWrite (Companion *c, uint8_t puerto, uint8_t dato);
void compVdpReg   (Companion *c, uint8_t registro, uint8_t valor);
// version del launcher_svc + si la cola esta llena + bytes PERDIDOS. Los
// perdidos importan: el SPI no se puede frenar, asi que si el VDP no traga al
// ritmo del S3 se pierden bytes y este contador es la unica forma de saberlo.
bool compLnzStatus(Companion *c, uint8_t *version, bool *lleno, uint16_t *perdidos);
// 16 bytes indexados por USAGE HID (NO por celda de matriz: ver compBuildKey).
bool compLnzKeys  (Companion *c, uint8_t *destino16);

void compSdTake   (Companion *c);      // toma la SD y retiene el Z80
void compSdRelease(Companion *c);      // suelta: el MSX arranca

#endif // _COMPANION_H

// ---- lado hardware (CompanionSpi.cpp, solo S3) ---------------------------
#ifdef BOARD_S3
bool companionSetup();      // arranca SPI3_HOST y hace el handshake de version
void companionTask();       // llamar desde loop(): prueba manual con BOOT
extern uint8_t g_companionVersion;   // 0 = la FPGA no contesta
#endif
