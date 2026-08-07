/*
 * FileHunter.h - Nucleo del lanzador File-Hunter. COMPARTIDO C6 / S3.
 *
 * C++ puro: NI Arduino NI ESP-IDF. Todo lo que hay aqui es logica que no
 * depende de la placa (parseo, CRC32, maquina de estados), asi que compila y
 * se prueba en el PC con host_test/test_filehunter.sh, igual que ScreenS3.
 *
 * Lo que SI depende de la placa (WiFi, pantalla, escritura en la SD) queda
 * fuera a proposito: entra por callbacks. Asi el mismo fichero vale para el
 * ESP32-C6 del MSXimus y para el ESP32-S3 del MSXnano sin un solo #ifdef.
 *
 * Formato de la API (verificado contra el servidor el 07/08/2026):
 *
 *   LISTADO   GET /MSXnano.php?base=1BA0&type=rom&msx=&char=<busqueda>
 *             -> cabecera binaria de N registros de 7 bytes + N nombres ASCIIZ
 *                bytes 0-3: id (LE32)   bytes 4-5: tamano en KB (LE16)   byte 6: flags
 *
 *   DESCARGA  ...&download=<indice>
 *             -> una linea de meta terminada en '\n', y detras el fichero crudo
 *                "type:ascii8,start:,size:262144,crc:1a2b3c4d,name:Juego.rom"
 *
 * REGLA DE ORO DEL META: 'name:' es SIEMPRE el ultimo campo, porque los nombres
 * llevan comas dentro y se leen hasta el final de la linea. El resto de campos
 * se buscan por clave y pueden venir en cualquier orden, o no venir:
 *   - .dsk y .vgm no traen 'type:' (no tienen mapper)
 *   - .cas trae ademas 'load:' (R = RUN"CAS:)
 *   - 'crc:' no existia antes del 07/08/2026 -> hay que tolerar su ausencia
 */

#ifndef _FILEHUNTER_H
#define _FILEHUNTER_H

#include <stdint.h>
#include <stddef.h>

#define FH_MAX_NAME     128     // nombre de fichero (la API llega a ~110)
#define FH_MAX_META     300     // linea de meta completa (real ~205)
#define FH_LIST_REC     7       // bytes por registro en la cabecera del listado

// Mapper declarado por la API. El orden importa: se usa como indice.
enum FhMapper {
    FH_MAP_NONE = 0,            // sin 'type:' (dsk/vgm) o desconocido -> sin etiqueta
    FH_MAP_ASCII8,
    FH_MAP_ASCII16,
    FH_MAP_KONAMI,
    FH_MAP_KONAMI_SCC,
    FH_MAP_NORMAL               // 'type:normal' = ROM lineal de 16/32K
};

// Resultado de parsear la linea de meta de una descarga.
struct FhMeta {
    uint32_t  size;             // bytes del payload (campo 'size:')
    uint32_t  crc;              // CRC32 del payload (campo 'crc:')
    bool      hasCrc;           // false = servidor antiguo, no se puede verificar
    FhMapper  mapper;
    char      load;             // 'R' de 'load:R' en los .cas; 0 si no viene
    char      name[FH_MAX_NAME];
};

// Una entrada del listado.
struct FhEntry {
    uint32_t id;
    uint16_t sizeKb;
    uint8_t  flags;
    const char *name;           // apunta DENTRO del buffer del listado
};

// ---------------------------------------------------------------------------
// CRC32 (el mismo de zlib/PKZIP = crc32() de PHP, que es el que sirve la API)
// ---------------------------------------------------------------------------
// Se calcula al vuelo mientras entra el fichero: fhCrcInit una vez, fhCrcUpdate
// por cada trozo recibido y fhCrcFinal al terminar. No hay tabla estatica: se
// construye en RAM la primera vez (1 KB) para no gastar flash.
uint32_t fhCrcInit(void);
uint32_t fhCrcUpdate(uint32_t crc, const uint8_t *data, size_t len);
uint32_t fhCrcFinal(uint32_t crc);

// ---------------------------------------------------------------------------
// Parseo
// ---------------------------------------------------------------------------

// Parsea la linea de meta (SIN el '\n'). Devuelve false si falta 'size:' o
// 'name:', o si el tamano no es creible. Tolera campos ausentes y desconocidos.
bool fhParseMeta(const char *line, size_t len, FhMeta *out);

// Etiqueta de mapper para incrustar en el nombre, "[ASCII8]" etc. Devuelve ""
// cuando no hay mapper que declarar (dsk, vgm, normal, desconocido).
const char *fhMapperTag(FhMapper m);

// Compone el nombre final con la etiqueta del mapper antes de la extension:
//   "Juego.rom" + FH_MAP_ASCII8 -> "Juego[ASCII8].rom"
// Trunca a dstSize-1 sin partir la extension. Devuelve la longitud escrita.
size_t fhBuildFileName(const FhMeta *meta, char *dst, size_t dstSize);

// Cuenta las entradas de un listado crudo. Devuelve 0 si el buffer no cuadra.
size_t fhListCount(const uint8_t *buf, size_t len);

// Extrae la entrada i-esima. false si i esta fuera de rango o el buffer miente.
bool fhListEntry(const uint8_t *buf, size_t len, size_t i, FhEntry *out);

// Monta la ruta de la peticion (sin host). Para listar, download < 0.
// Devuelve la longitud escrita, o 0 si no cabe.
size_t fhBuildPath(const char *type, const char *search, int download,
                   char *dst, size_t dstSize);

// ---------------------------------------------------------------------------
// Maquina de estados de la descarga
// ---------------------------------------------------------------------------
enum FhState {
    FH_IDLE = 0,
    FH_META,                    // acumulando la linea de meta hasta el '\n'
    FH_BODY,                    // volcando el payload y actualizando el CRC
    FH_DONE_OK,                 // tamano correcto y CRC correcto (o sin CRC)
    FH_ERR_META,                // meta ilegible
    FH_ERR_SIZE,                // llegaron mas o menos bytes de los anunciados
    FH_ERR_CRC                  // CRC32 no coincide -> el fichero esta podrido
};

// Sumidero del payload. Devuelve false para abortar (p.ej. la SD se llena).
typedef bool (*FhSink)(const uint8_t *data, size_t len, void *user);

struct FhDownload {
    FhState  state;
    FhMeta   meta;
    uint32_t received;          // bytes de payload procesados
    uint32_t crc;               // acumulador en curso
    uint32_t crcCalc;           // CRC final una vez cerrado
    size_t   metaLen;
    char     metaBuf[FH_MAX_META];
    FhSink   sink;
    void    *sinkUser;
};

void fhDownloadBegin(FhDownload *d, FhSink sink, void *user);

// Alimenta el flujo segun va llegando. Se puede llamar con trozos de cualquier
// tamano: la frontera entre meta y payload puede caer en medio de un trozo.
// Devuelve los bytes consumidos; el estado dice si hay que seguir o parar.
size_t fhDownloadFeed(FhDownload *d, const uint8_t *data, size_t len);

// Cierra y dictamina. Llamar cuando el servidor corta la conexion.
FhState fhDownloadEnd(FhDownload *d);

// true si merece la pena reintentar (fallo de integridad, no de logica).
bool fhShouldRetry(FhState s);

#endif // _FILEHUNTER_H
