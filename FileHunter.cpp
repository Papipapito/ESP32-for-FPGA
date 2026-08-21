/*
 * FileHunter.cpp - Nucleo del lanzador File-Hunter. COMPARTIDO C6 / S3.
 * Ver FileHunter.h para el formato de la API y las reglas del meta.
 */

#include "FileHunter.h"
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// CRC32 (zlib/PKZIP, polinomio reflejado 0xEDB88320)
// ---------------------------------------------------------------------------
// La tabla se genera en RAM la primera vez: 1 KB de RAM a cambio de 1 KB de
// flash. En el Z80 esto era caro; aqui es gratis.
static uint32_t s_crcTable[256];
static bool     s_crcReady = false;

static void fhCrcBuildTable(void)
{
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crcTable[i] = c;
    }
    s_crcReady = true;
}

uint32_t fhCrcInit(void)
{
    if (!s_crcReady) fhCrcBuildTable();
    return 0xFFFFFFFFu;
}

uint32_t fhCrcUpdate(uint32_t crc, const uint8_t *data, size_t len)
{
    if (!s_crcReady) fhCrcBuildTable();
    while (len--)
        crc = s_crcTable[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32_t fhCrcFinal(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Utilidades de parseo
// ---------------------------------------------------------------------------

// Busca "clave:" dentro de [line, line+len) y devuelve el offset del valor,
// o -1. Solo acepta la clave al principio o justo detras de una coma, para no
// picar con un "crc:" que viniese DENTRO del nombre de un fichero.
static int fhFind(const char *line, size_t len, const char *key)
{
    size_t klen = strlen(key);
    for (size_t i = 0; i + klen <= len; ++i) {
        if (i != 0 && line[i - 1] != ',') continue;
        if (memcmp(line + i, key, klen) == 0) return (int)(i + klen);
    }
    return -1;
}

// Decimal sin signo desde p; para en el primer caracter no digito.
static bool fhParseDec(const char *p, const char *end, uint32_t *out)
{
    uint32_t v = 0;
    const char *start = p;
    while (p < end && *p >= '0' && *p <= '9') {
        if (v > 0x19999999u) return false;          // se saldria de 32 bits
        v = v * 10 + (uint32_t)(*p - '0');
        ++p;
    }
    if (p == start) return false;
    *out = v;
    return true;
}

// Hexadecimal de hasta 8 digitos. La API rellena con ceros a la izquierda
// desde el 07/08/2026, pero se aceptan menos digitos por si acaso.
static bool fhParseHex(const char *p, const char *end, uint32_t *out)
{
    uint32_t v = 0;
    int n = 0;
    while (p < end && n < 8) {
        char c = *p;
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
        ++p; ++n;
    }
    if (n == 0) return false;
    *out = v;
    return true;
}

// Compara el valor de un campo con un literal, hasta la coma o el fin.
static bool fhValueIs(const char *p, const char *end, const char *lit)
{
    size_t n = strlen(lit);
    if ((size_t)(end - p) < n) return false;
    if (memcmp(p, lit, n) != 0) return false;
    return (p + n == end) || p[n] == ',';
}

// ---------------------------------------------------------------------------
// Meta
// ---------------------------------------------------------------------------

bool fhParseMeta(const char *line, size_t len, FhMeta *out)
{
    if (!line || !out) return false;
    memset(out, 0, sizeof(*out));
    out->mapper = FH_MAP_NONE;

    const char *end = line + len;

    // --- size: obligatorio ---
    int p = fhFind(line, len, "size:");
    if (p < 0) return false;
    if (!fhParseDec(line + p, end, &out->size)) return false;
    if (out->size == 0 || out->size > (32u * 1024u * 1024u)) return false;

    // --- crc: opcional (no existia antes del 07/08/2026) ---
    p = fhFind(line, len, "crc:");
    if (p >= 0 && fhParseHex(line + p, end, &out->crc))
        out->hasCrc = true;

    // --- load: opcional, solo en .cas ---
    p = fhFind(line, len, "load:");
    if (p >= 0 && line + p < end && line[p] != ',')
        out->load = line[p];

    // --- type: opcional. Los .dsk y .vgm no lo traen: no tienen mapper ---
    p = fhFind(line, len, "type:");
    if (p >= 0) {
        const char *v = line + p;
        if      (fhValueIs(v, end, "ascii8"))     out->mapper = FH_MAP_ASCII8;
        else if (fhValueIs(v, end, "ascii16"))    out->mapper = FH_MAP_ASCII16;
        else if (fhValueIs(v, end, "konamiscc"))  out->mapper = FH_MAP_KONAMI_SCC;
        else if (fhValueIs(v, end, "konami"))     out->mapper = FH_MAP_KONAMI;
        else if (fhValueIs(v, end, "normal"))     out->mapper = FH_MAP_NORMAL;
        // vacio o desconocido -> FH_MAP_NONE, sin etiqueta. No es un error.
    }

    // --- name: obligatorio y SIEMPRE EL ULTIMO: se lee hasta el final ---
    // Los nombres llevan comas dentro ("Aleste (MSX2, PSG+OPLL).vgm"), asi que
    // cortar por coma romperia. Por eso el servidor tiene que dejarlo al final.
    p = fhFind(line, len, "name:");
    if (p < 0) return false;
    size_t nlen = len - (size_t)p;
    if (nlen == 0) return false;
    if (nlen > FH_MAX_NAME - 1) nlen = FH_MAX_NAME - 1;
    memcpy(out->name, line + p, nlen);
    out->name[nlen] = 0;

    // Un '\r' de un servidor que use CRLF no debe acabar en el nombre.
    while (nlen && (out->name[nlen - 1] == '\r' || out->name[nlen - 1] == '\n'))
        out->name[--nlen] = 0;

    return nlen != 0;
}

const char *fhMapperTag(FhMapper m)
{
    switch (m) {
        case FH_MAP_ASCII8:     return "[ASCII8]";
        case FH_MAP_ASCII16:    return "[ASCII16]";
        case FH_MAP_KONAMI:     return "[Konami]";
        case FH_MAP_KONAMI_SCC: return "[SCC]";
        default:                return "";
    }
}

size_t fhBuildFileName(const FhMeta *meta, char *dst, size_t dstSize)
{
    if (!meta || !dst || dstSize == 0) return 0;
    dst[0] = 0;

    const char *tag = fhMapperTag(meta->mapper);
    size_t nameLen = strlen(meta->name);
    size_t tagLen  = strlen(tag);

    // Sin etiqueta: copia directa (truncando si hace falta).
    if (tagLen == 0) {
        size_t n = nameLen < dstSize - 1 ? nameLen : dstSize - 1;
        memcpy(dst, meta->name, n);
        dst[n] = 0;
        return n;
    }

    // Ultimo punto = principio de la extension. Sin punto, la etiqueta al final.
    size_t dot = nameLen;
    for (size_t i = nameLen; i > 0; --i) {
        if (meta->name[i - 1] == '.') { dot = i - 1; break; }
    }
    size_t extLen = nameLen - dot;

    // Si no cabe entero, se recorta el TRONCO: la extension y la etiqueta son
    // lo que necesita el lanzador, el nombre bonito es lo prescindible.
    size_t stem = dot;
    if (stem + tagLen + extLen > dstSize - 1) {
        size_t room = dstSize - 1;
        if (tagLen + extLen >= room) return 0;      // ni con esas
        stem = room - tagLen - extLen;
    }

    size_t o = 0;
    memcpy(dst + o, meta->name, stem);       o += stem;
    memcpy(dst + o, tag, tagLen);            o += tagLen;
    memcpy(dst + o, meta->name + dot, extLen); o += extLen;
    dst[o] = 0;
    return o;
}

// ---------------------------------------------------------------------------
// Listado
// ---------------------------------------------------------------------------
// Cabecera de N registros de 7 bytes, un terminador de 4 bytes a cero, y luego
// N nombres ASCIIZ seguidos. N no viene declarado: se deduce buscando el
// terminador, y se comprueba que detras haya exactamente N nombres.

static bool fhListSplit(const uint8_t *buf, size_t len,
                        size_t *recCount, size_t *namesOff)
{
    if (!buf || len < 4) return false;
    for (size_t off = 0; off + 4 <= len; off += FH_LIST_REC) {
        if (buf[off] == 0 && buf[off+1] == 0 && buf[off+2] == 0 && buf[off+3] == 0) {
            *recCount = off / FH_LIST_REC;
            *namesOff = off + 4;
            return *namesOff <= len;
        }
    }
    return false;
}

size_t fhListCount(const uint8_t *buf, size_t len)
{
    size_t n, off;
    if (!fhListSplit(buf, len, &n, &off)) return 0;

    // Los nombres detras tienen que ser al menos tantos como registros.
    size_t seen = 0;
    for (size_t i = off; i < len; ++i)
        if (buf[i] == 0) ++seen;
    return seen >= n ? n : 0;
}

bool fhListEntry(const uint8_t *buf, size_t len, size_t i, FhEntry *out)
{
    if (!out) return false;
    size_t n, off;
    if (!fhListSplit(buf, len, &n, &off)) return false;
    if (i >= n) return false;

    const uint8_t *r = buf + i * FH_LIST_REC;
    out->id     = (uint32_t)r[0] | ((uint32_t)r[1] << 8)
                | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24);
    out->sizeKb = (uint16_t)((uint16_t)r[4] | ((uint16_t)r[5] << 8));
    out->flags  = r[6];

    // Saltar i nombres ASCIIZ.
    size_t p = off;
    for (size_t k = 0; k < i; ++k) {
        while (p < len && buf[p] != 0) ++p;
        if (p >= len) return false;
        ++p;
    }
    if (p >= len) return false;
    out->name = (const char *)(buf + p);

    // El ultimo nombre tambien tiene que estar cerrado.
    size_t q = p;
    while (q < len && buf[q] != 0) ++q;
    return q < len;
}

// ---------------------------------------------------------------------------
// URL
// ---------------------------------------------------------------------------

size_t fhBuildPath(const char *type, const char *search, int download,
                   char *dst, size_t dstSize)
{
    if (!type || !dst || dstSize == 0) return 0;
    dst[0] = 0;

    // El servidor espera la busqueda tal cual; se escapan solo los caracteres
    // que romperian la peticion. Los nombres MSX traen espacios y parentesis.
    char esc[128];
    size_t e = 0;
    if (search) {
        for (const char *s = search; *s && e + 3 < sizeof(esc); ++s) {
            unsigned char c = (unsigned char)*s;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
                esc[e++] = (char)c;
            } else {
                static const char hex[] = "0123456789ABCDEF";
                esc[e++] = '%';
                esc[e++] = hex[c >> 4];
                esc[e++] = hex[c & 0x0F];
            }
        }
    }
    esc[e] = 0;

    int n;
    if (download < 0)
        n = snprintf(dst, dstSize, "/MSXnano.php?base=1BA0&type=%s&msx=&char=%s",
                     type, esc);
    else
        n = snprintf(dst, dstSize, "/MSXnano.php?base=1BA0&type=%s&msx=&char=%s&download=%d",
                     type, esc, download);

    if (n < 0 || (size_t)n >= dstSize) { dst[0] = 0; return 0; }
    return (size_t)n;
}

// ---------------------------------------------------------------------------
// Maquina de estados de la descarga
// ---------------------------------------------------------------------------

void fhDownloadBegin(FhDownload *d, FhSink sink, void *user)
{
    if (!d) return;
    memset(d, 0, sizeof(*d));
    d->state    = FH_META;
    d->crc      = fhCrcInit();
    d->sink     = sink;
    d->sinkUser = user;
}

size_t fhDownloadFeed(FhDownload *d, const uint8_t *data, size_t len)
{
    if (!d || !data) return 0;
    size_t used = 0;

    while (used < len) {
        if (d->state == FH_META) {
            // Acumular hasta el '\n' que cierra la linea de meta.
            const uint8_t *nl = (const uint8_t *)memchr(data + used, '\n', len - used);
            size_t chunk = nl ? (size_t)(nl - (data + used)) : (len - used);

            if (d->metaLen + chunk > FH_MAX_META - 1) {
                d->state = FH_ERR_META;                 // meta absurdamente larga
                return used;
            }
            memcpy(d->metaBuf + d->metaLen, data + used, chunk);
            d->metaLen += chunk;
            used += chunk;

            if (!nl) return used;                       // seguimos esperando
            ++used;                                     // consumir el '\n'
            d->metaBuf[d->metaLen] = 0;

            if (!fhParseMeta(d->metaBuf, d->metaLen, &d->meta)) {
                d->state = FH_ERR_META;
                return used;
            }
            d->state = FH_BODY;
            continue;
        }

        if (d->state == FH_BODY) {
            size_t left  = d->meta.size - d->received;
            size_t avail = len - used;
            size_t chunk = avail < left ? avail : left;

            if (chunk == 0) {
                // Sobran bytes despues del payload anunciado.
                d->state = FH_ERR_SIZE;
                return used;
            }
            d->crc = fhCrcUpdate(d->crc, data + used, chunk);
            if (d->sink && !d->sink(data + used, chunk, d->sinkUser)) {
                d->state = FH_ERR_SIZE;                 // el sumidero abortó
                return used;
            }
            d->received += (uint32_t)chunk;
            used += chunk;
            continue;
        }

        break;                                          // estado terminal
    }
    return used;
}

FhState fhDownloadEnd(FhDownload *d)
{
    if (!d) return FH_ERR_META;
    if (d->state != FH_BODY) return d->state;           // ya venia mal

    if (d->received != d->meta.size) {
        d->state = FH_ERR_SIZE;                         // conexion cortada
        return d->state;
    }

    d->crcCalc = fhCrcFinal(d->crc);

    // Sin CRC (servidor antiguo) se da por bueno: es lo unico que se puede
    // hacer, pero el llamante puede mirar meta.hasCrc para avisar al usuario.
    if (d->meta.hasCrc && d->crcCalc != d->meta.crc) {
        d->state = FH_ERR_CRC;
        return d->state;
    }

    d->state = FH_DONE_OK;
    return d->state;
}

bool fhShouldRetry(FhState s)
{
    // El CRC malo y el corte de conexion son fallos de transporte: reintentar
    // tiene sentido. Un meta ilegible es un fallo de protocolo: no lo arregla
    // repetir la peticion.
    return s == FH_ERR_CRC || s == FH_ERR_SIZE;
}
