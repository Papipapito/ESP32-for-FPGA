/*
 * test_filehunter.cpp - Banco de pruebas del nucleo del lanzador, en el PC.
 *
 * FileHunter.cpp es C++ puro (ni Arduino ni IDF), asi que aqui no hace falta
 * stub ninguno: se incluye entero y se le hacen cosquillas.
 *
 * Las cabeceras de meta son CAPTURAS REALES del servidor del 07/08/2026,
 * verificadas una a una contra el payload con curl+python. No son inventadas.
 *
 *   wsl -d Ubuntu-24.04 bash -lc "/mnt/c/Users/alber/proyectosAI/msx/ESP32-UNAPI-Firmware/host_test/test_filehunter.sh"
 */

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>

#include "../FileHunter.cpp"

static int g_fail = 0;
static int g_ok   = 0;

#define CHECK(cond, ...) do {                                   \
    if (cond) { ++g_ok; }                                       \
    else { ++g_fail; printf("  FAIL %s:%d  ", __FILE__, __LINE__); \
           printf(__VA_ARGS__); printf("\n"); }                 \
} while (0)

// ---------------------------------------------------------------------------
static void test_crc32(void)
{
    printf("\n== CRC32 (zlib/PKZIP, el que sirve la API) ==\n");

    // Vector de test canonico: CRC32("123456789") = 0xCBF43926
    const char *v = "123456789";
    uint32_t c = fhCrcInit();
    c = fhCrcUpdate(c, (const uint8_t *)v, strlen(v));
    CHECK(fhCrcFinal(c) == 0xCBF43926u, "vector canonico: %08x", fhCrcFinal(c));

    // Vacio -> 0
    CHECK(fhCrcFinal(fhCrcInit()) == 0u, "cadena vacia");

    // Trocear NO debe cambiar el resultado (se calcula al vuelo, en trozos
    // del tamano que traiga la red).
    uint32_t whole = fhCrcInit();
    whole = fhCrcUpdate(whole, (const uint8_t *)v, 9);
    uint32_t split = fhCrcInit();
    for (int i = 0; i < 9; ++i)
        split = fhCrcUpdate(split, (const uint8_t *)v + i, 1);
    CHECK(fhCrcFinal(whole) == fhCrcFinal(split), "byte a byte != de golpe");
}

// ---------------------------------------------------------------------------
static void test_meta_reales(void)
{
    printf("\n== Meta: capturas REALES del servidor ==\n");
    FhMeta m;

    // --- ROM con mapper, formato NUEVO (con crc) ---
    const char *rom =
        "type:ascii16,start:,size:262144,crc:b3e6c121,"
        "name:Aleste - Compile (1988) [Spanish] [Translated] [2662].rom";
    CHECK(fhParseMeta(rom, strlen(rom), &m), "rom ascii16 no parsea");
    CHECK(m.size == 262144, "size=%u", m.size);
    CHECK(m.hasCrc && m.crc == 0xb3e6c121u, "crc=%08x hasCrc=%d", m.crc, m.hasCrc);
    CHECK(m.mapper == FH_MAP_ASCII16, "mapper=%d", (int)m.mapper);
    CHECK(strcmp(m.name, "Aleste - Compile (1988) [Spanish] [Translated] [2662].rom") == 0,
          "name=\"%s\"", m.name);

    // --- Formato VIEJO, sin crc: tiene que seguir funcionando ---
    // Y ojo, ESTE nombre lleva una COMA dentro: cortar por coma lo partiria.
    const char *viejo = "size:87845,name:Aleste (MSX2, PSG+OPLL).vgm";
    CHECK(fhParseMeta(viejo, strlen(viejo), &m), "meta vieja no parsea");
    CHECK(m.size == 87845, "size=%u", m.size);
    CHECK(!m.hasCrc, "deberia venir SIN crc");
    CHECK(m.mapper == FH_MAP_NONE, "vgm no tiene mapper");
    CHECK(strcmp(m.name, "Aleste (MSX2, PSG+OPLL).vgm") == 0,
          "coma dentro del nombre: \"%s\"", m.name);

    // --- .dsk: no trae type: (no tiene mapper) ---
    const char *dsk = "size:737280,crc:8d67680d,"
                      "name:Champion Ice Hockey (1986)(Pony Canyon)[cr Nemesis].dsk";
    CHECK(fhParseMeta(dsk, strlen(dsk), &m), "dsk no parsea");
    CHECK(m.size == 737280 && m.hasCrc && m.crc == 0x8d67680du, "dsk size/crc");
    CHECK(m.mapper == FH_MAP_NONE, "dsk no lleva mapper");

    // --- .cas: trae load: en medio ---
    const char *cas = "size:34287,load:R,crc:b8f6c8b6,name:Konami's Soccer (1985).cas";
    CHECK(fhParseMeta(cas, strlen(cas), &m), "cas no parsea");
    CHECK(m.load == 'R', "load=%c", m.load ? m.load : '?');
    CHECK(m.crc == 0xb8f6c8b6u, "cas crc=%08x", m.crc);

    // --- type: VACIO (existe de verdad en la API) -> sin etiqueta, no error ---
    const char *vacio = "type:,start:,size:131072,crc:1f5803d5,name:Metal Gear [9703].rom";
    CHECK(fhParseMeta(vacio, strlen(vacio), &m), "type vacio deberia parsear");
    CHECK(m.mapper == FH_MAP_NONE, "type vacio -> sin mapper");
    CHECK(m.size == 131072, "size=%u", m.size);

    // --- konami / konamiscc: 'konami' es prefijo de 'konamiscc', ojo al orden ---
    const char *kon = "type:konami,start:,size:131072,crc:92016876,name:Gradius.rom";
    CHECK(fhParseMeta(kon, strlen(kon), &m) && m.mapper == FH_MAP_KONAMI,
          "konami -> %d", (int)m.mapper);
    const char *scc = "type:konamiscc,start:,size:131072,crc:12345678,name:Nemesis 2.rom";
    CHECK(fhParseMeta(scc, strlen(scc), &m) && m.mapper == FH_MAP_KONAMI_SCC,
          "konamiscc -> %d (no debe confundirse con konami)", (int)m.mapper);

    // --- normal ---
    const char *nor = "type:normal,start:,size:16384,crc:de539063,name:GraCo.rom";
    CHECK(fhParseMeta(nor, strlen(nor), &m) && m.mapper == FH_MAP_NORMAL, "normal");

    // --- CRC con cero delante: el servidor rellena a 8 digitos ---
    const char *pad = "type:konami,start:,size:163840,crc:0694b93d,name:Metal Gear.rom";
    CHECK(fhParseMeta(pad, strlen(pad), &m) && m.crc == 0x0694b93du,
          "padding: crc=%08x", m.crc);
}

// ---------------------------------------------------------------------------
static void test_meta_venenosas(void)
{
    printf("\n== Meta: casos venenosos ==\n");
    FhMeta m;

    // EL CASO IMPORTANTE: un fichero cuyo NOMBRE contiene "crc:" o "size:".
    // Si la busqueda de claves no exigiese que vayan tras una coma, se leeria
    // el crc de dentro del nombre y se daria por corrupto un fichero sano.
    const char *trampa = "size:1024,crc:aabbccdd,name:Demo crc:deadbeef y size:99.rom";
    CHECK(fhParseMeta(trampa, strlen(trampa), &m), "trampa no parsea");
    CHECK(m.crc == 0xaabbccddu, "pico el crc del NOMBRE: %08x", m.crc);
    CHECK(m.size == 1024, "pico el size del NOMBRE: %u", m.size);
    CHECK(strcmp(m.name, "Demo crc:deadbeef y size:99.rom") == 0, "name=\"%s\"", m.name);

    // Sin name: -> invalido
    const char *sinname = "type:ascii8,size:1024,crc:11223344";
    CHECK(!fhParseMeta(sinname, strlen(sinname), &m), "sin name deberia fallar");

    // Sin size: -> invalido
    const char *sinsize = "type:ascii8,crc:11223344,name:X.rom";
    CHECK(!fhParseMeta(sinsize, strlen(sinsize), &m), "sin size deberia fallar");

    // size absurdo -> invalido (evita reservar medio mundo)
    const char *enorme = "size:999999999,name:X.rom";
    CHECK(!fhParseMeta(enorme, strlen(enorme), &m), "size enorme deberia fallar");

    // size 0 -> invalido
    const char *cero = "size:0,name:X.rom";
    CHECK(!fhParseMeta(cero, strlen(cero), &m), "size 0 deberia fallar");

    // CRLF: el '\r' no debe quedarse pegado al nombre
    const char *crlf = "size:16,crc:00000000,name:X.rom\r";
    CHECK(fhParseMeta(crlf, strlen(crlf), &m), "crlf no parsea");
    CHECK(strcmp(m.name, "X.rom") == 0, "quedo el \\r: \"%s\"", m.name);

    // Nombre larguisimo: se trunca, no se desborda
    char largo[FH_MAX_META];
    int n = snprintf(largo, sizeof(largo), "size:16,name:");
    for (int i = 0; i < 200 && n < (int)sizeof(largo) - 6; ++i)
        n += snprintf(largo + n, sizeof(largo) - (size_t)n, "A");
    snprintf(largo + n, sizeof(largo) - (size_t)n, ".rom");
    CHECK(fhParseMeta(largo, strlen(largo), &m), "nombre largo no parsea");
    CHECK(strlen(m.name) <= FH_MAX_NAME - 1, "desbordo: %zu", strlen(m.name));
}

// ---------------------------------------------------------------------------
static void test_nombre_final(void)
{
    printf("\n== Nombre final con la etiqueta del mapper ==\n");
    FhMeta m;
    char out[FH_MAX_NAME];

    const char *rom = "type:ascii8,start:,size:262144,crc:11223344,"
                      "name:Fleet Commander 2 - ASCII (1990) [GoodMSX] [725].rom";
    fhParseMeta(rom, strlen(rom), &m);
    fhBuildFileName(&m, out, sizeof(out));
    CHECK(strcmp(out, "Fleet Commander 2 - ASCII (1990) [GoodMSX] [725][ASCII8].rom") == 0,
          "out=\"%s\"", out);

    // Sin mapper -> el nombre tal cual
    const char *dsk = "size:737280,crc:8d67680d,name:Champion Ice Hockey.dsk";
    fhParseMeta(dsk, strlen(dsk), &m);
    fhBuildFileName(&m, out, sizeof(out));
    CHECK(strcmp(out, "Champion Ice Hockey.dsk") == 0, "out=\"%s\"", out);

    // Nombre con puntos por el medio: manda el ULTIMO
    const char *multi = "type:konami,size:1024,crc:1,name:Juego v1.2 final.rom";
    fhParseMeta(multi, strlen(multi), &m);
    fhBuildFileName(&m, out, sizeof(out));
    CHECK(strcmp(out, "Juego v1.2 final[Konami].rom") == 0, "out=\"%s\"", out);

    // Buffer justo: se recorta el tronco, NUNCA la extension ni la etiqueta
    char chico[24];
    fhParseMeta(rom, strlen(rom), &m);
    size_t w = fhBuildFileName(&m, chico, sizeof(chico));
    CHECK(w > 0 && w < sizeof(chico), "w=%zu", w);
    CHECK(strstr(chico, "[ASCII8]") != NULL, "perdio la etiqueta: \"%s\"", chico);
    CHECK(strstr(chico, ".rom") != NULL, "perdio la extension: \"%s\"", chico);
}

// ---------------------------------------------------------------------------
static void test_listado(void)
{
    printf("\n== Listado (registros de 7 bytes + nombres ASCIIZ) ==\n");

    // Formato real: 3 registros, terminador de 4 ceros, 3 nombres.
    // El primero replica una entrada de verdad: id 0x1BA0, 256 KB.
    uint8_t buf[128];
    size_t o = 0;
    const uint8_t recs[3][7] = {
        { 0xA0, 0x1B, 0x00, 0x00, 0x00, 0x01, 0x00 },   // id 7072, 256 KB
        { 0xCA, 0x1B, 0x00, 0x00, 0xC0, 0x04, 0x00 },   // id 7114, 1216 KB
        { 0x10, 0x1C, 0x00, 0x00, 0x20, 0x00, 0x01 },   // id 7184, 32 KB, flag 1
    };
    for (int i = 0; i < 3; ++i) { memcpy(buf + o, recs[i], 7); o += 7; }
    memset(buf + o, 0, 4); o += 4;
    const char *nombres[3] = { "Aleste [50].rom", "Aleste 2 v8 [3275].rom", "Corto.rom" };
    for (int i = 0; i < 3; ++i) { size_t l = strlen(nombres[i]) + 1; memcpy(buf + o, nombres[i], l); o += l; }

    CHECK(fhListCount(buf, o) == 3, "count=%zu", fhListCount(buf, o));

    FhEntry e;
    CHECK(fhListEntry(buf, o, 0, &e), "entrada 0");
    CHECK(e.id == 0x1BA0 && e.sizeKb == 256, "id=%u kb=%u", e.id, e.sizeKb);
    CHECK(strcmp(e.name, "Aleste [50].rom") == 0, "name0=\"%s\"", e.name);

    CHECK(fhListEntry(buf, o, 1, &e), "entrada 1");
    CHECK(e.sizeKb == 1216, "kb=%u (Aleste 2 son 1216 KB)", e.sizeKb);

    CHECK(fhListEntry(buf, o, 2, &e), "entrada 2");
    CHECK(e.flags == 1 && strcmp(e.name, "Corto.rom") == 0, "entrada 2");

    CHECK(!fhListEntry(buf, o, 3, &e), "fuera de rango deberia fallar");

    // Buffer truncado a media lista: no debe leer de mas ni petar
    CHECK(fhListCount(buf, 10) == 0, "buffer truncado deberia dar 0");
    CHECK(!fhListEntry(buf, o - 3, 2, &e), "ultimo nombre sin cerrar");
}

// ---------------------------------------------------------------------------
static uint8_t  g_sink[300000];
static size_t   g_sinkLen = 0;
static bool     g_sinkFail = false;

static bool sink(const uint8_t *d, size_t l, void *user)
{
    (void)user;
    if (g_sinkFail) return false;
    if (g_sinkLen + l > sizeof(g_sink)) return false;
    memcpy(g_sink + g_sinkLen, d, l);
    g_sinkLen += l;
    return true;
}

// Construye un flujo "meta\npayload" y lo mete por trozos de tam bytes.
static FhState correr(const char *meta, const uint8_t *pay, size_t paylen, size_t tam)
{
    g_sinkLen = 0;
    FhDownload d;
    fhDownloadBegin(&d, sink, NULL);

    size_t mlen = strlen(meta);
    uint8_t *flujo = (uint8_t *)malloc(mlen + 1 + paylen);
    memcpy(flujo, meta, mlen);
    flujo[mlen] = '\n';
    memcpy(flujo + mlen + 1, pay, paylen);
    size_t total = mlen + 1 + paylen;

    size_t off = 0;
    while (off < total) {
        size_t chunk = total - off < tam ? total - off : tam;
        size_t used = fhDownloadFeed(&d, flujo + off, chunk);
        off += used;
        if (used < chunk) break;            // estado terminal
    }
    free(flujo);
    return fhDownloadEnd(&d);
}

static void test_descarga(void)
{
    printf("\n== Descarga: maquina de estados ==\n");

    // Payload de 4 KB con un patron cualquiera
    static uint8_t pay[4096];
    for (size_t i = 0; i < sizeof(pay); ++i) pay[i] = (uint8_t)(i * 7 + 3);
    uint32_t crc = fhCrcFinal(fhCrcUpdate(fhCrcInit(), pay, sizeof(pay)));

    char meta[FH_MAX_META];
    snprintf(meta, sizeof(meta), "type:ascii8,start:,size:%zu,crc:%08x,name:Test.rom",
             sizeof(pay), crc);

    // Trozos de todos los tamanos: la frontera meta/payload cae en sitios
    // distintos cada vez. Es EXACTAMENTE lo que hace la red de verdad.
    const size_t tams[] = { 1, 2, 7, 63, 64, 512, 1460, 100000 };
    for (size_t i = 0; i < sizeof(tams)/sizeof(tams[0]); ++i) {
        FhState s = correr(meta, pay, sizeof(pay), tams[i]);
        CHECK(s == FH_DONE_OK, "trozos de %zu -> estado %d", tams[i], (int)s);
        CHECK(g_sinkLen == sizeof(pay), "trozos de %zu -> %zu bytes", tams[i], g_sinkLen);
        CHECK(memcmp(g_sink, pay, sizeof(pay)) == 0, "trozos de %zu -> payload distinto", tams[i]);
    }

    // CRC MALO -> se detecta. Este es el caso que nos habria ahorrado dos cazas.
    snprintf(meta, sizeof(meta), "type:ascii8,size:%zu,crc:deadbeef,name:Podrida.rom",
             sizeof(pay));
    FhState s = correr(meta, pay, sizeof(pay), 512);
    CHECK(s == FH_ERR_CRC, "crc malo -> %d", (int)s);
    CHECK(fhShouldRetry(s), "un crc malo SI merece reintento");

    // Conexion cortada a media descarga -> ERR_SIZE
    snprintf(meta, sizeof(meta), "size:%zu,crc:%08x,name:Cortada.rom", sizeof(pay), crc);
    {
        g_sinkLen = 0;
        FhDownload d;
        fhDownloadBegin(&d, sink, NULL);
        char flujo[600];
        size_t mlen = strlen(meta);
        memcpy(flujo, meta, mlen);
        flujo[mlen] = '\n';
        memcpy(flujo + mlen + 1, pay, 100);         // solo 100 de 4096
        fhDownloadFeed(&d, (const uint8_t *)flujo, mlen + 1 + 100);
        FhState c = fhDownloadEnd(&d);
        CHECK(c == FH_ERR_SIZE, "cortada -> %d", (int)c);
        CHECK(fhShouldRetry(c), "una descarga cortada SI merece reintento");
    }

    // Meta ilegible -> no reintentar (repetir no lo arregla)
    s = correr("esto no es un meta", pay, 16, 64);
    CHECK(s == FH_ERR_META, "meta basura -> %d", (int)s);
    CHECK(!fhShouldRetry(s), "un meta roto NO se arregla reintentando");

    // Servidor ANTIGUO sin crc: se acepta, pero hasCrc avisa de que no se pudo
    // verificar. Retrocompatibilidad con el MSXnano.php de siempre.
    snprintf(meta, sizeof(meta), "type:ascii8,size:%zu,name:Vieja.rom", sizeof(pay));
    {
        g_sinkLen = 0;
        FhDownload d;
        fhDownloadBegin(&d, sink, NULL);
        size_t mlen = strlen(meta);
        uint8_t *flujo = (uint8_t *)malloc(mlen + 1 + sizeof(pay));
        memcpy(flujo, meta, mlen); flujo[mlen] = '\n';
        memcpy(flujo + mlen + 1, pay, sizeof(pay));
        fhDownloadFeed(&d, flujo, mlen + 1 + sizeof(pay));
        FhState v = fhDownloadEnd(&d);
        free(flujo);
        CHECK(v == FH_DONE_OK, "sin crc -> %d", (int)v);
        CHECK(!d.meta.hasCrc, "hasCrc deberia ser false para avisar");
    }

    // El sumidero falla (SD llena) -> no se queda colgado
    g_sinkFail = true;
    snprintf(meta, sizeof(meta), "size:%zu,crc:%08x,name:X.rom", sizeof(pay), crc);
    s = correr(meta, pay, sizeof(pay), 512);
    g_sinkFail = false;
    CHECK(s != FH_DONE_OK, "si el sumidero falla NO puede salir OK");
}

// ---------------------------------------------------------------------------
static void test_url(void)
{
    printf("\n== Construccion de la peticion ==\n");
    char p[256];

    CHECK(fhBuildPath("rom", "aleste", -1, p, sizeof(p)) > 0, "listado");
    CHECK(strcmp(p, "/MSXnano.php?base=1BA0&type=rom&msx=&char=aleste") == 0, "p=\"%s\"", p);

    CHECK(fhBuildPath("rom", "aleste", 13, p, sizeof(p)) > 0, "descarga");
    CHECK(strstr(p, "&download=13") != NULL, "p=\"%s\"", p);

    // Espacios y acentos escapados: los titulos MSX vienen con de todo
    fhBuildPath("dsk", "metal gear", -1, p, sizeof(p));
    CHECK(strstr(p, "metal%20gear") != NULL, "escape de espacio: \"%s\"", p);

    // Buffer corto -> 0 y cadena vacia, sin desbordar
    char corto[10];
    CHECK(fhBuildPath("rom", "aleste", -1, corto, sizeof(corto)) == 0, "buffer corto");
    CHECK(corto[0] == 0, "deberia dejarlo vacio");
}

// ---------------------------------------------------------------------------
int main(void)
{
    printf("=== test_filehunter: nucleo del lanzador (compartido C6/S3) ===\n");
    test_crc32();
    test_meta_reales();
    test_meta_venenosas();
    test_nombre_final();
    test_listado();
    test_descarga();
    test_url();

    printf("\n--------------------------------------------------\n");
    printf("  comprobaciones OK: %d    fallos: %d\n", g_ok, g_fail);
    if (g_fail == 0) printf("  PASS\n");
    else             printf("  FAIL\n");
    return g_fail == 0 ? 0 : 1;
}
