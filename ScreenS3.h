/*
 * ScreenS3.h - API publica de la pantalla del companion MSXnano sobre
 *              ESP32-1732S019 (ESP32-S3-WROOM-1 N16R8 + LCD ST7789V 170x320).
 *
 * Este fichero es el CONTRATO con el resto del firmware. La implementacion va
 * partida en dos, y la frontera importa: es por donde se corta para portar al
 * MSXimus reaprovechando el driver entero.
 *   ScreenS3.cpp      EL DRIVER: LCD, backlight, fuente y motor de celdas.
 *                     Depende del hardware pero no sabe nada de MSX.
 *   ScreenS3Boot.cpp  LA NARRATIVA: el BASIC falso, la espera, `call system` y
 *                     la consola MSX-DOS. Solo escribe en la rejilla.
 * La costura entre ambos esta documentada en ScreenS3_internal.h.
 *
 * NO sustituye a Display.ino: aquel es el de la placa VIEJA (Waveshare
 * ESP32-C6-LCD-1.3, ST7789V2 240x240). Los dos pueden convivir en el arbol;
 * solo se compila uno u otro segun el #define de placa (ver ESP32BOARDS.h).
 *
 * ------------------------------------------------------------------------
 * IDEA DEL DISENO (el "por que", que es lo que se pierde al portar)
 * ------------------------------------------------------------------------
 * La secuencia de arranque NO es una animacion decorativa: es una BARRA DE
 * PROGRESO DISFRAZADA de MSX. Cada fase corresponde a un hito real del
 * firmware:
 *
 *   Fase 1  BASIC     el ESP ha arrancado y el LCD responde
 *   Fase 2  espera    dura EXACTAMENTE lo que tarden WiFi y USB de verdad
 *   Fase 3  "call system" auto-tecleado  =  todo listo, saltamos a DOS
 *   Fase 4  consola MSX-DOS viva
 *
 * Por eso la fase 2 no tiene duracion fija: se sale de ella cuando el resto
 * del firmware llama a screenSetWifi() y screenSetUsb(). Si el WiFi falla,
 * el `call system` NO se teclea y la pantalla se queda en BASIC informando
 * del fallo con estetica MSX. Esa es la gracia: el usuario ve el estado real
 * del aparato sin leer un solo log.
 *
 * ------------------------------------------------------------------------
 * INTEGRACION EN EL FIRMWARE (3 lineas)
 * ------------------------------------------------------------------------
 *   #include "ScreenS3.h"          // en ESP32-UNAPI-Firmware.ino
 *   setup() { ... screenSetup(); ... }
 *   loop()  { screenTick(); ... }  // NO bloquea, se auto-limita
 *
 * Y despues, desde donde corresponda:
 *   - al resolver el WiFi (conecte o no):  screenSetWifi(ssid, ip, rssi, ok)
 *   - al terminar de enumerar el USB:      screenSetUsb(kbd, pad)
 *   - cuando cambie el turbo:              screenSetTurbo(bool)
 *   - periodicamente con el trafico:       screenSetTraffic(rx, tx)
 *
 * REGLA DE ORO: screenTick() nunca hace delay() ni espera activa. El loop()
 * de este firmware tiene que atender el enlace UNAPI con el MSX a 859372 bps
 * y cualquier bloqueo se traduce en bytes perdidos. Todo va por maquina de
 * estados con millis() y el volcado al LCD esta limitado por tick.
 *
 * Copyright (c) 2026 - proyecto MSXnano. Se distribuye bajo la misma licencia
 * que el resto del firmware (LGPL-2.1 o posterior).
 */

#ifndef _SCREENS3_H
#define _SCREENS3_H

#include <stdint.h>

// ===========================================================================
//  IDENTIDAD DE LA MAQUINA (lo unico que hay que tocar para portar al MSXimus)
// ===========================================================================
// Display.ino (placa C6) usa a proposito un titulo NEUTRO ("MSX") porque aquel
// firmware es comun a MSXimus y MSXnano. Aqui es al reves: la pantalla CUENTA
// UNA HISTORIA concreta y necesita nombres concretos, asi que se parametriza.
// Para el MSXimus basta con cambiar estas cinco lineas:
//     SCREEN_MACHINE_NAME "MSXimus"  /  SCREEN_MACHINE_VER "v2.0"
//     SCREEN_FPGA_NAME    "TC60K GW5AT-60"
#ifndef SCREEN_MACHINE_NAME
  #define SCREEN_MACHINE_NAME   "MSXnano"
#endif
#ifndef SCREEN_MACHINE_VER
  #define SCREEN_MACHINE_VER    "v1.9"
#endif
#ifndef SCREEN_FPGA_NAME
  #define SCREEN_FPGA_NAME      "TN20K GW2AR-18"
#endif
// Reloj de CPU que se muestra en la linea CPU de la consola DOS.
// El MSXnano hace turbo Panasonic WSX a 5.37 MHz (ver TURBO.COM / v1.9).
#ifndef SCREEN_CPU_NORMAL
  #define SCREEN_CPU_NORMAL     "3.58MHz"
#endif
#ifndef SCREEN_CPU_TURBO
  #define SCREEN_CPU_TURBO      "5.37MHz"
#endif
// Memoria libre que "declara" el BASIC falso. 28815 es el valor real de un
// MSX2 con 64K de RAM mapeada y DiskBASIC cargado: puro guino.
#ifndef SCREEN_BASIC_FREE
  #define SCREEN_BASIC_FREE     "28815"
#endif

// ===========================================================================
//  API PUBLICA
// ===========================================================================

// -- Ciclo de vida --------------------------------------------------------

// Inicializa LCD + backlight y arranca la fase 1 (BASIC).
// Llamar UNA vez desde setup(). Es la unica funcion que bloquea, y lo hace
// por culpa de la libreria: el reset hardware del ST7789 exige ~340 ms de
// delay() dentro de gfx->begin(). Como ocurre antes de que el MSX pueda
// hablar, no molesta a nadie.
// Devuelve false si el panel no responde (el resto de la API queda inerte
// pero sigue siendo segura de llamar).
bool screenSetup();

// Motor de la pantalla. Llamar en CADA vuelta de loop(), sin condiciones.
// No bloquea: se auto-limita por tiempo (animacion) y por numero de celdas
// volcadas al LCD por invocacion. Coste tipico por llamada: microsegundos;
// pico acotado (ver SCREEN_CELLS_PER_TICK en ScreenS3.cpp).
void screenTick();

// -- Setters de estado: alimentan la fase 2 y la consola DOS ---------------

// El subsistema WiFi ha TERMINADO de intentarlo (haya conectado o no).
//   ssid/ip : pueden ser NULL o "" si no hay conexion.
//   rssi    : dBm (negativo). Se ignora si ok==false.
//   ok      : true = conectado. Es la CONDICION que decide si se teclea
//             `call system` o si nos quedamos en BASIC con un error.
// Se puede volver a llamar tantas veces como se quiera: en la consola DOS
// refresca SSID/IP/cobertura en vivo, y si llega ok==true despues de un
// fallo, el arranque SE REANUDA (teclea `call system` y salta a DOS).
void screenSetWifi(const char *ssid, const char *ip, int rssi, bool ok);

// El USB host ha TERMINADO de enumerar. Que no haya nada enchufado NO es un
// fallo: kbd=false, pad=false es perfectamente valido y deja seguir el
// arranque. Lo que importa es que se haya llamado (= enumeracion resuelta).
void screenSetUsb(bool kbd, bool pad);

// Estado del turbo del core FPGA.
void screenSetTurbo(bool on);

// Diagnostico del lanzador: version que devuelve launcher_svc (0 = no
// contesta), bytes que el VDP no ha podido tragar, bytes enviados, color
// en curso y si estamos reteniendo el Z80.
void screenSetLauncher(uint8_t ver, uint16_t perdidos, uint32_t enviados,
                       uint8_t color, bool hold, uint8_t fase, uint8_t hidVer);
// Los 8 bytes crudos que devolvio la FPGA en la ultima sonda.
void screenSetLauncherRaw(const uint8_t *rx8);
// Peldano 2: firma del sector 0 de la SD (55AA en FAT16) y sus 4 primeros bytes.
void screenSetSd(uint16_t firma, bool ok, const uint8_t *ini4);
// Estado del puente de SD: version, si tenemos el mando, si la tarjeta estaba
// ocupada al empezar y si se puso ocupada al pedirle el sector.
void screenSetSdDiag(uint8_t ver, bool hold, bool busy0, bool busy1, uint8_t intentos);
// Peldano 3: FatFs montado, error, cuantas entradas y las tres primeras.
void screenSetFs(bool ok, uint8_t err, int n, const char prim[3][20],
                 uint32_t lba, uint8_t tipo, uint8_t nparts, const uint8_t *ini4,
                 uint32_t pmAntes, uint32_t pmDespues);

// Trafico del enlace, en BYTES POR SEGUNDO (ya promediado por quien llama).
void screenSetTraffic(uint32_t rxBytesPerSec, uint32_t txBytesPerSec);

// Hora local a mostrar. Llamarla cambia el reloj a modo "empujado" y
// desactiva para siempre el calculo interno via time()/SNTP.
// Si NUNCA se llama, ScreenS3 saca la hora el solo de time(nullptr) mas el
// offset GMT de la configuracion UNAPI (igual que hace Display.ino).
void screenSetClock(int hh, int mm, int ss);

// Aborta el arranque y deja la pantalla en BASIC mostrando el motivo con
// estetica de error de MSX BASIC. `motivo` se recorta a 40 columnas.
// Uso tipico: timeout de WiFi, fallo del USB host, particion corrupta...
void screenBootFailed(const char *motivo);

// -- Extras ---------------------------------------------------------------

// Brillo del backlight, 0..100 %. Arranca en 0 (apagado) y ScreenS3 hace un
// fundido de entrada solo cuando la primera pantalla ya esta pintada, para
// que no se vea el ruido de encendido del panel.
void screenSetBacklight(uint8_t percent);

// true en cuanto la consola MSX-DOS esta en pantalla (fin del arranque).
bool screenBootDone();

#endif // _SCREENS3_H
