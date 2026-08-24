// ============================================================================
//  LauncherFs.h — FatFs del ESP-IDF montado sobre el puente de sectores
//
//  POR QUE FatFs Y NO UN FAT PROPIO
//  --------------------------------------------------------------------------
//  Porque ya sabemos como acaba lo otro. El menu del MSX llevaba su propio FAT
//  escrito a mano y sus escrituras corrompieron tarjetas enteras durante dias:
//  un error de LECTURA se traducia en "no existe", y el menu respondia creando
//  el fichero encima de datos vivos. FatFs lleva decadas en produccion.
//
//  🚨 Y ADEMAS, AQUI ES DE SOLO LECTURA POR CONSTRUCCION
//  --------------------------------------------------------------------------
//  La funcion de escritura del diskio devuelve RES_WRPRT SIEMPRE. No es que no
//  la llamemos: es que NO PUEDE escribir. El lanzador solo necesita leer, y
//  hacer imposible la escritura vale mas que prometer tener cuidado -- que es
//  exactamente lo que se prometio la vez anterior.
// ============================================================================
#ifndef _LAUNCHER_FS_H
#define _LAUNCHER_FS_H

#include <stdint.h>
#include <stddef.h>
#include "Companion.h"

// Monta la tarjeta. Hay que tener el mando ANTES de llamar (compSdTake): el
// puente ignora las peticiones de sector si la SD no es nuestra.
bool lfsMount(Companion *c);
void lfsUnmount();

// Codigo del ultimo fallo de FatFs (FRESULT), para poder enseñarlo.
uint8_t lfsUltimoError();
// Que se encontro al mirar la tabla de particiones: sector inicial del
// volumen, tipo de particion (0 = la tarjeta no tiene MBR) y cuantas hay.
uint32_t lfsLbaBase();
uint8_t  lfsTipoPart();
uint8_t  lfsNumParts();
const uint8_t *lfsPrimeros4();   // los 4 primeros bytes del sector 0, tal cual
const uint8_t  *lfsPTipos();     // tipo de las 4 entradas de la tabla
const uint32_t *lfsPLbas();      // y su sector de inicio
const uint8_t  *lfsVbrFat();     // 8 bytes desde 0x36 del sector elegido
const uint8_t  *lfsVbrIni();     // sus 2 primeros bytes
// El BPB del volumen elegido: es lo que FatFs valida para aceptarlo.
uint16_t lfsBps(); uint8_t lfsSpc(); uint16_t lfsRsv();
uint8_t  lfsNfat(); uint16_t lfsRoot(); uint16_t lfsFsz(); uint32_t lfsTot();

typedef struct {
    char     nombre[64];
    uint32_t tam;
    bool     carpeta;
} LfsEntrada;

// Lista un directorio. Devuelve cuantas entradas ha puesto en `dst`.
// `ruta` vacia o "/" = raiz.
int lfsListar(const char *ruta, LfsEntrada *dst, int max);

#endif // _LAUNCHER_FS_H
