// ============================================================================
// TsxWeb.ino - "Cinta web" del MSXimus Z (18/09/2026): el catalogo de tsx.eslamejor.com
// servido al menu con el formato del File-Hunter, sin tocar la maquinaria UNAPI.
//
//  - El menu (bios/src/menu_main.asm, consulta tipo 4 = "tsx") resuelve el nombre magico
//    "tsx.msximus": el handler de TCPIP_DNS_Q lo contesta con 127.0.0.1 (tsxWebDnsHook) y el
//    TCP_OPEN de siempre abre un socket por el loopback de lwIP contra este servidor (puerto 80).
//  - Un hilo aparte (tsxWebTask, FreeRTOS) atiende cada peticion como lo haria
//    api.file-hunter.com/MSXnano.php (en un hilo porque la descarga es HTTPS bloqueante y
//    loop() tiene que seguir atendiendo al MSX):
//      GET /MSXnano.php?base=1BA0&type=tsx&msx=&char=<busqueda>            -> registros 7 B + nombres
//      GET /MSXnano.php?base=1BA0&type=tsx&msx=&char=<busqueda>&download=N -> "size:..,load:X,crc:..,name:..tsx\n" + fichero
//  - La lista sale de tsxcat.h (generada en el PC por esp32/tools/tsxcat_gen.py: TOSEC.NAME,
//    tamano, crc32 y TSX.LOAD); la web solo ve la descarga de /tsx-files/<TOSEC.NAME>.tsx.
//    Cuando tsx.eslamejor nos de un listado propio, cambia el generador (o se baja aqui), no el menu.
// ============================================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "tsxcat.h"

#define TSXWEB_HOST     "tsx.eslamejor.com"
#define TSXWEB_MAGIC    "tsx.msximus"
#define TSXWEB_PORT     80
#define TSXWEB_UA       "MSXimus/1.2 (cinta web)"
#define TSXWEB_MAXLIST  115           // FH_MAXENT del menu (tope de la lista)
#define TSXWEB_NAMEMAX  73            // como los nombres del API de File-Hunter en la lista
#define TSXWEB_FHBASE   0x1BA0        // "base=1BA0": los punteros de los registros (el menu no los usa)

static WiFiServer*   g_tsxSrv  = NULL;
static volatile bool g_tsxBusy = false;

// el DNS_Q del UNAPI llama aqui antes de preguntar a internet
bool tsxWebDnsHook(const char* name) {
  bool hit = strcmp(name, TSXWEB_MAGIC) == 0;
  if (hit) Serial.println("[tsxweb] DNS " TSXWEB_MAGIC " -> 127.0.0.1");
  return hit;
}

// ---- UTF-8 -> ASCII de la pantalla del MSX (acentos latinos; el resto '?') ----
static char tsxFoldChar(uint32_t cp) {
  if (cp < 0x80) return (char)cp;
  static const char tbl[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  if (cp >= 0xC0 && cp <= 0xFF) return tbl[cp - 0xC0];
  return '?';
}
static void tsxFold(const char* in, char* out, int cap) {
  int o = 0; const uint8_t* p = (const uint8_t*)in;
  while (*p && o < cap - 1) {
    uint32_t cp; int n;
    if (*p < 0x80) { cp = *p; n = 1; }
    else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 2; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 3; }
    else { cp = *p & 0x07; n = 4; }
    for (int i = 1; i < n; i++) { if ((p[i] & 0xC0) != 0x80) { n = i; break; } cp = (cp << 6) | (p[i] & 0x3F); }
    out[o++] = tsxFoldChar(cp);
    p += n;
  }
  out[o] = 0;
}
static void tsxLower(char* s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32; }

// "char=" del GET: '+' = espacio, %XX
static void tsxQueryDecode(const char* p, char* out, int cap) {
  int o = 0;
  while (*p && *p != '&' && *p != ' ' && *p != '\r' && *p != '\n' && o < cap - 1) {
    char c = *p++;
    if (c == '+') c = ' ';
    else if (c == '%' && isxdigit((int)p[0]) && isxdigit((int)p[1])) {
      char h[3] = {p[0], p[1], 0}; c = (char)strtol(h, NULL, 16); p += 2;
    }
    out[o++] = c;
  }
  out[o] = 0;
}
static String tsxUrlEncode(const char* s) {
  String r; char h[4];
  for (const uint8_t* p = (const uint8_t*)s; *p; p++) {
    uint8_t c = *p;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') r += (char)c;
    else { snprintf(h, sizeof h, "%%%02X", c); r += h; }
  }
  return r;
}
static char tsxLoadChar(uint8_t k) { return k == 0 ? 'R' : k == 1 ? 'B' : k == 2 ? 'C' : '?'; }

// busca en el catalogo (subcadena, sin mayusculas/acentos); devuelve cuantos y sus indices
static int tsxSearch(const char* q, uint16_t* idx, int cap) {
  char ql[64], nm[200];
  tsxFold(q, ql, sizeof ql); tsxLower(ql);
  if (!ql[0]) return 0;
  int n = 0;
  for (uint32_t i = 0; i < TSXCAT_N && n < cap; i++) {
    tsxFold(tsxcat_names + tsxcat[i].nameOff, nm, sizeof nm); tsxLower(nm);
    if (strstr(nm, ql)) idx[n++] = (uint16_t)i;
  }
  return n;
}

static bool tsxWriteAll(WiFiClient* c, const uint8_t* p, size_t n) {
  while (n) {
    if (!c->connected()) return false;
    size_t w = c->write(p, n > 1024 ? 1024 : n);
    if (!w) { delay(2); if (!c->connected()) return false; continue; }
    p += w; n -= w;
  }
  return true;
}
static void tsxHttpHead(WiFiClient* c, int code, size_t len) {
  char h[160];
  snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: application/octet-stream\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
           code, code == 200 ? "OK" : "Error", (unsigned)len);
  tsxWriteAll(c, (const uint8_t*)h, strlen(h));
}

// ---- lista: registros {u32 ptr, u16 KB, u8 load} + u32 0 + nombres ASCIIZ ----
static void tsxSendList(WiFiClient* c, const char* q) {
  static uint16_t idx[TSXWEB_MAXLIST];
  int n = tsxSearch(q, idx, TSXWEB_MAXLIST);
  size_t body = (size_t)n * 7 + 4;
  char nm[TSXWEB_NAMEMAX + 1];
  for (int k = 0; k < n; k++) { tsxFold(tsxcat_names + tsxcat[idx[k]].nameOff, nm, sizeof nm); body += strlen(nm) + 1; }
  tsxHttpHead(c, 200, body);
  uint8_t rec[7]; uint32_t ptr = TSXWEB_FHBASE;
  for (int k = 0; k < n; k++) {
    const TsxCatEntry& e = tsxcat[idx[k]];
    uint32_t kb = (e.size + 1023) / 1024; if (kb > 65535) kb = 65535;
    rec[0] = ptr; rec[1] = ptr >> 8; rec[2] = ptr >> 16; rec[3] = ptr >> 24;
    rec[4] = kb; rec[5] = kb >> 8; rec[6] = (uint8_t)tsxLoadChar(e.load);
    if (!tsxWriteAll(c, rec, 7)) return;
    tsxFold(tsxcat_names + e.nameOff, nm, sizeof nm); ptr += strlen(nm) + 1;
  }
  uint8_t z[4] = {0, 0, 0, 0};
  if (!tsxWriteAll(c, z, 4)) return;
  for (int k = 0; k < n; k++) {
    tsxFold(tsxcat_names + tsxcat[idx[k]].nameOff, nm, sizeof nm);
    if (!tsxWriteAll(c, (const uint8_t*)nm, strlen(nm) + 1)) return;
  }
  Serial.printf("[tsxweb] lista '%s': %d cintas\n", q, n);
}

// ---- descarga: meta como la de File-Hunter + el .tsx tal cual desde la web (HTTPS) ----
static void tsxSendFile(WiFiClient* c, const char* q, int item) {
  static uint16_t idx[TSXWEB_MAXLIST];
  int n = tsxSearch(q, idx, TSXWEB_MAXLIST);
  if (item < 0 || item >= n) { tsxHttpHead(c, 404, 0); return; }
  const TsxCatEntry& e = tsxcat[idx[item]];
  const char* name = tsxcat_names + e.nameOff;
  String url = String("https://" TSXWEB_HOST "/tsx-files/") + tsxUrlEncode(name) + ".tsx";
  WiFiClientSecure* s = new WiFiClientSecure();
  if (!s) { tsxHttpHead(c, 500, 0); return; }
  if (!loadCACertForClient(s)) s->setInsecure();
  s->setTimeout(15000);
  HTTPClient http;
  http.setUserAgent(TSXWEB_UA);
  http.setTimeout(15000);
  if (!http.begin(*s, url)) { delete s; tsxHttpHead(c, 502, 0); return; }
  http.addHeader("Referer", "https://" TSXWEB_HOST "/");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[tsxweb] %s -> HTTP %d\n", url.c_str(), code);
    http.end(); delete s; tsxHttpHead(c, 502, 0); return;
  }
  int remote = http.getSize();
  uint32_t size = (remote > 0) ? (uint32_t)remote : e.size;
  char folded[160], meta[240];
  tsxFold(name, folded, sizeof folded);
  snprintf(meta, sizeof meta, "size:%u,load:%c,crc:%08x,name:%s.tsx\n", (unsigned)size, tsxLoadChar(e.load), (unsigned)e.crc, folded);
  tsxHttpHead(c, 200, strlen(meta) + size);
  bool ok = tsxWriteAll(c, (const uint8_t*)meta, strlen(meta));
  WiFiClient* st = http.getStreamPtr();
  static uint8_t buf[1024];
  uint32_t left = size; unsigned long idle = millis();
  while (ok && left && http.connected()) {
    int av = st->available();
    if (av <= 0) { if (millis() - idle > 15000) { ok = false; break; } delay(1); continue; }
    if ((uint32_t)av > left) av = left;
    if (av > (int)sizeof buf) av = sizeof buf;
    int r = st->read(buf, av);
    if (r <= 0) { delay(1); continue; }
    idle = millis();
    ok = tsxWriteAll(c, buf, r);
    left -= r;
  }
  http.end(); delete s;
  Serial.printf("[tsxweb] %s: %u bytes, %s\n", folded, (unsigned)(size - left), ok && !left ? "OK" : "CORTADA");
}

static void tsxWebTask(void* arg) {
  WiFiClient* c = (WiFiClient*)arg;
  char req[640]; int rl = 0; unsigned long t0 = millis();
  while (c->connected() && millis() - t0 < 8000) {
    while (c->available() && rl < (int)sizeof(req) - 1) req[rl++] = (char)c->read();
    req[rl] = 0;
    if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n")) break;
    delay(5);
  }
  req[rl] = 0;
  char q[64] = {0};
  const char* pq = strstr(req, "char=");
  if (pq) tsxQueryDecode(pq + 5, q, sizeof q);
  const char* pd = strstr(req, "download=");
  if (strncmp(req, "GET ", 4) != 0 || !strstr(req, "type=tsx")) tsxHttpHead(c, 404, 0);
  else if (pd) tsxSendFile(c, q, atoi(pd + 9));
  else tsxSendList(c, q);
  c->flush();
  delay(50);
  c->stop();
  delete c;
  g_tsxBusy = false;
  vTaskDelete(NULL);
}

// desde loop(): arranca el servidor cuando hay WiFi y atiende una conexion cada vez
void tsxWebPoll() {
  if (!g_tsxSrv) {
    if (WiFi.status() != WL_CONNECTED) return;
    g_tsxSrv = new WiFiServer(TSXWEB_PORT);
    g_tsxSrv->begin();
    g_tsxSrv->setNoDelay(true);
    Serial.println("[tsxweb] servidor local en el puerto 80 (" TSXWEB_MAGIC " -> 127.0.0.1)");
    return;
  }
  if (g_tsxBusy) return;
  WiFiClient cl = g_tsxSrv->available();
  if (!cl) return;
  WiFiClient* pc = new WiFiClient(cl);
  Serial.printf("[tsxweb] conexion desde %s\n", cl.remoteIP().toString().c_str());
  g_tsxBusy = true;
  if (xTaskCreatePinnedToCore(tsxWebTask, "tsxweb", 20480, pc, 1, NULL, 1) != pdPASS) {
    pc->stop(); delete pc; g_tsxBusy = false;
  }
}
