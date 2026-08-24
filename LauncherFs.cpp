#include "Board.h"
#ifdef BOARD_S3

#include <Arduino.h>
#include <string.h>
#include "LauncherFs.h"

extern "C" {
#include "ff.h"
#include "diskio.h"
#include "diskio_impl.h"
}

// El companion por el que salen las peticiones de sector. FatFs llama al
// diskio sin contexto, asi que no queda otra que guardarlo aparte.
static Companion *s_comp = nullptr;
static BYTE       s_pdrv = 0xFF;
static FATFS      s_fs;
static bool       s_montado = false;
static uint8_t    s_err = 0;

// 🚨 DESPLAZAMIENTO DE PARTICION. La tabla la leemos NOSOTROS y se la sumamos a
// cada sector, asi que FatFs ve un disco que empieza justo en el volumen: no
// llega a ver el MBR y no hay que pelearse con FF_MULTI_PARTITION (que en el
// SDK viene a 1 y espera una tabla VolToPart de la aplicacion).
//
// Hace falta de verdad: la SD de Albert da FR_NO_FILESYSTEM (13) y su sector 0
// empieza por EB FE 90 -- que PARECE el salto de un sector de arranque, pero
// EB FE es "salta a ti mismo", el relleno tipico de un MBR que no arranca nada.
// FatFs lo tomaba por un volumen, no encontraba la firma FAT y se rendia.
static uint32_t   s_lba0 = 0;      // primer sector del volumen
static uint8_t    s_tipo = 0;      // tipo de particion detectado (0 = sin MBR)
static uint8_t    s_nparts = 0;

uint32_t lfsLbaBase()  { return s_lba0; }
uint8_t  lfsTipoPart() { return s_tipo; }
uint8_t  lfsNumParts() { return s_nparts; }

uint8_t lfsUltimoError() { return s_err; }

// Los 4 primeros bytes que devolvio la lectura del sector 0 EN EL MONTAJE.
// Se guardan para poder compararlos con los del peldano 2: si difieren, es
// que las dos lecturas del mismo sector no dan lo mismo, y eso es del codigo.
static uint8_t s_ini[4] = {0,0,0,0};
const uint8_t *lfsPrimeros4() { return s_ini; }

// Las 4 entradas de la tabla tal cual (tipo + LBA), y lo que hay DE VERDAD en
// el sector al que apunta la elegida. Sin esto no se puede distinguir "la
// particion es correcta y su arranque no es FAT" de "lo que leo como tabla es
// basura porque el sector 0 ES el volumen".
static uint8_t  s_ptipo[4] = {0,0,0,0};
static uint32_t s_plba[4]  = {0,0,0,0};
static uint8_t  s_vbr[8]   = {0,0,0,0,0,0,0,0};   // 0x36..0x3D: el "FAT16   "
static uint8_t  s_vbr0[2]  = {0,0};               // los 2 primeros del VBR
// El BPB: la geometria de verdad. Es lo que FatFs valida para aceptar el
// volumen, asi que es el unico sitio donde se puede ver POR QUE lo rechaza.
// Mirar el nombre en 0x36 no sirve: muchos formateadores de MSX lo dejan vacio.
static uint16_t s_bps = 0;      // 0x0B bytes por sector
static uint8_t  s_spc = 0;      // 0x0D sectores por clluster
static uint16_t s_rsv = 0;      // 0x0E sectores reservados
static uint8_t  s_nfat = 0;     // 0x10 numero de FATs
static uint16_t s_root = 0;     // 0x11 entradas del directorio raiz
static uint16_t s_fsz = 0;      // 0x16 sectores por FAT
static uint32_t s_tot = 0;      // 0x13 (16b) o 0x20 (32b) sectores totales
uint16_t lfsBps()  { return s_bps; }
uint8_t  lfsSpc()  { return s_spc; }
uint16_t lfsRsv()  { return s_rsv; }
uint8_t  lfsNfat() { return s_nfat; }
uint16_t lfsRoot() { return s_root; }
uint16_t lfsFsz()  { return s_fsz; }
uint32_t lfsTot()  { return s_tot; }
const uint8_t  *lfsPTipos()  { return s_ptipo; }
const uint32_t *lfsPLbas()   { return s_plba; }
const uint8_t  *lfsVbrFat()  { return s_vbr; }
const uint8_t  *lfsVbrIni()  { return s_vbr0; }

// ---------------------------------------------------------------------------
//  diskio: las cinco funciones que FatFs necesita
// ---------------------------------------------------------------------------
static DSTATUS lfs_init(BYTE) { return 0; }
static DSTATUS lfs_status(BYTE) { return s_comp ? 0 : STA_NOINIT; }

static DRESULT lfs_read(BYTE, BYTE *buff, DWORD sector, UINT count)
{
    if (!s_comp) return RES_NOTRDY;
    for (UINT i = 0; i < count; i++) {
        // compSdLeerSector espera a que la lectura ARRANQUE antes de esperar a
        // que acabe, y reintenta si el puente la ignoro. Hacerlo a mano aqui
        // devolvia el buffer anterior sin dar error.
        if (!compSdLeerSector(s_comp, s_lba0 + (uint32_t)(sector + i),
                              buff + (size_t)i * 512))
            return RES_ERROR;
    }
    return RES_OK;
}

// 🚨 NO ESCRIBE. Ver la cabecera: el desastre de las descargas corruptas vino
// de un FAT propio que SI escribia. Aqui es imposible, no solo improbable.
static DRESULT lfs_write(BYTE, const BYTE *, DWORD, UINT) { return RES_WRPRT; }

static DRESULT lfs_ioctl(BYTE, BYTE cmd, void *buff)
{
    switch (cmd) {
    case CTRL_SYNC:         return RES_OK;          // nada que vaciar: no escribimos
    case GET_SECTOR_SIZE:   *(WORD *)buff  = 512;   return RES_OK;
    case GET_BLOCK_SIZE:    *(DWORD *)buff = 1;     return RES_OK;
    case GET_SECTOR_COUNT:
        // No preguntamos el tamano real a la tarjeta: para MONTAR un volumen
        // que ya existe, FatFs no lo usa (solo lo mira f_mkfs, y aqui formatear
        // esta descartado por diseno). Un valor grande evita que rechace
        // tarjetas legitimas por parecer pequenas.
        *(DWORD *)buff = 0x0FFFFFFF;
        return RES_OK;
    default:                return RES_PARERR;
    }
}

static const ff_diskio_impl_t s_impl = {
    .init    = &lfs_init,
    .status  = &lfs_status,
    .read    = &lfs_read,
    .write   = &lfs_write,
    .ioctl   = &lfs_ioctl,
};

// ---------------------------------------------------------------------------
bool lfsMount(Companion *c)
{
    if (s_montado) return true;
    if (!c) return false;
    s_comp = c;
    s_err  = 0;

    if (s_pdrv == 0xFF && ff_diskio_get_drive(&s_pdrv) != ESP_OK) {
        s_pdrv = 0xFF;
        s_err  = 0xF0;                  // no hay hueco de unidad libre
        return false;
    }
    ff_diskio_register(s_pdrv, &s_impl);

    // ---- localizar el volumen ANTES de montar --------------------------
    s_lba0 = 0; s_tipo = 0; s_nparts = 0;
    {
        static uint8_t sec0[512];
        if (!compSdLeerSector(c, 0, sec0)) {
            s_err = 0xF1;                       // no se pudo leer el sector 0
            ff_diskio_unregister(s_pdrv);
            return false;
        }
        for (int k = 0; k < 4; k++) s_ini[k] = sec0[k];
        if (sec0[510] != 0x55 || sec0[511] != 0xAA) {
            s_err = 0xF2;                       // ni MBR ni VBR: nada que montar
            ff_diskio_unregister(s_pdrv);
            return false;
        }
        // Tabla de particiones: 4 entradas de 16 bytes desde 0x1BE. Se coge la
        // PRIMERA de tipo FAT. Los tipos son los de toda la vida: 01/04/06 FAT12
        // y FAT16, 0B/0C FAT32, 0E FAT16 LBA.
        for (int e = 0; e < 4; e++) {
            const uint8_t *pe = &sec0[0x1BE + e * 16];
            uint8_t tipo = pe[4];
            if (tipo == 0x00) continue;
            s_nparts++;
            bool esFat = (tipo == 0x01 || tipo == 0x04 || tipo == 0x06 ||
                          tipo == 0x0B || tipo == 0x0C || tipo == 0x0E);
            s_ptipo[e] = tipo;
            s_plba[e]   = (uint32_t)pe[8] | ((uint32_t)pe[9] << 8) |
                          ((uint32_t)pe[10] << 16) | ((uint32_t)pe[11] << 24);
            if (esFat && s_tipo == 0) {
                s_tipo = tipo;
                s_lba0 = (uint32_t)pe[8] | ((uint32_t)pe[9] << 8) |
                         ((uint32_t)pe[10] << 16) | ((uint32_t)pe[11] << 24);
            }
        }
        // Sin tabla util: o el sector 0 ES el volumen (tarjeta sin particionar)
        // o no hay nada. Se deja el desplazamiento a 0 y que decida FatFs.

        // Y AHORA SE MIRA lo que hay donde apuntamos, en vez de suponerlo. Un
        // sector de arranque FAT lleva "FAT12   "/"FAT16   " en 0x36 (o
        // "FAT32   " en 0x52). Si ahi no hay eso, la particion elegida no
        // sirve -- y si el sector 0 SI lo lleva, es que la tabla era basura.
        {
            static uint8_t vbr[512];
            if (compSdLeerSector(c, s_lba0, vbr)) {
                // Se guardan 4 bytes del principio Y los 2 del final. La
                // comparacion con el sector 0 es lo unico que distingue "este
                // sector esta vacio" de "me han devuelto otra vez el sector 0":
                // un MBR de Nextor tambien tiene ceros en las posiciones del BPB.
                s_vbr0[0] = vbr[0]; s_vbr0[1] = vbr[1];
                s_vbr[0] = vbr[2];   s_vbr[1] = vbr[3];
                s_vbr[2] = vbr[510]; s_vbr[3] = vbr[511];
                s_vbr[4] = vbr[0x1BE]; s_vbr[5] = vbr[0x1BE + 4];
                s_bps  = (uint16_t)vbr[0x0B] | ((uint16_t)vbr[0x0C] << 8);
                s_spc  = vbr[0x0D];
                s_rsv  = (uint16_t)vbr[0x0E] | ((uint16_t)vbr[0x0F] << 8);
                s_nfat = vbr[0x10];
                s_root = (uint16_t)vbr[0x11] | ((uint16_t)vbr[0x12] << 8);
                s_fsz  = (uint16_t)vbr[0x16] | ((uint16_t)vbr[0x17] << 8);
                s_tot  = (uint32_t)vbr[0x13] | ((uint32_t)vbr[0x14] << 8);
                if (s_tot == 0)
                    s_tot = (uint32_t)vbr[0x20] | ((uint32_t)vbr[0x21] << 8) |
                            ((uint32_t)vbr[0x22] << 16) | ((uint32_t)vbr[0x23] << 24);
            }
        }
    }

    char ruta[4] = { (char)('0' + s_pdrv), ':', 0, 0 };
    FRESULT r = f_mount(&s_fs, ruta, 1);   // 1 = montar YA, no perezoso
    if (r != FR_OK) {
        s_err = (uint8_t)r;
        ff_diskio_unregister(s_pdrv);
        return false;
    }
    s_montado = true;
    return true;
}

void lfsUnmount()
{
    if (!s_montado) return;
    char ruta[4] = { (char)('0' + s_pdrv), ':', 0, 0 };
    f_mount(NULL, ruta, 0);
    ff_diskio_unregister(s_pdrv);
    s_montado = false;
    s_comp = nullptr;
}

int lfsListar(const char *ruta, LfsEntrada *dst, int max)
{
    if (!s_montado || !dst || max <= 0) return 0;

    char camino[80];
    snprintf(camino, sizeof(camino), "%c:/%s",
             (char)('0' + s_pdrv), (ruta && ruta[0] == '/') ? ruta + 1 : (ruta ? ruta : ""));

    // FF_DIR, no DIR: FatFs lo renombro para no chocar con el DIR de
    // <dirent.h> de POSIX, que en el ESP32 tambien esta en el ambito.
    FF_DIR dir;
    FRESULT r = f_opendir(&dir, camino);
    if (r != FR_OK) { s_err = (uint8_t)r; return 0; }

    int n = 0;
    FILINFO fno;
    while (n < max) {
        r = f_readdir(&dir, &fno);
        if (r != FR_OK || fno.fname[0] == 0) break;   // fname vacio = se acabo
        if (fno.fattrib & AM_SYS) continue;           // fuera lo del sistema
        strncpy(dst[n].nombre, fno.fname, sizeof(dst[n].nombre) - 1);
        dst[n].nombre[sizeof(dst[n].nombre) - 1] = 0;
        dst[n].tam     = (uint32_t)fno.fsize;
        dst[n].carpeta = (fno.fattrib & AM_DIR) != 0;
        n++;
    }
    f_closedir(&dir);
    return n;
}

#endif // BOARD_S3
