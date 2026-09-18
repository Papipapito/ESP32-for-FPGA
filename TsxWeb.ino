// ============================================================================
// TsxWeb.ino - "Cinta web" del MSXimus Z (18/09/2026): el catalogo de tsx.eslamejor.com
// servido al menu con el formato del File-Hunter, sin tocar la maquinaria UNAPI.
//
//  - El menu (bios/src/menu_main.asm, consulta tipo 4 = "tsx") resuelve el nombre magico
//    "tsx.msximus": el handler de TCPIP_DNS_Q lo contesta con 127.0.0.1 (tsxWebDnsHook) y el
//    TCP_OPEN a esa IP, en vez de un socket, crea una CONEXION VIRTUAL (TsxVirtualClient, una
//    subclase de WiFiClient): el UNAPI la usa como cualquier otra (write = la peticion GET,
//    available/read = la respuesta, connected = hasta que se ha leido todo).
//      GET /MSXnano.php?base=1BA0&type=tsx&msx=&char=<busqueda>            -> registros 7 B + nombres
//      GET /MSXnano.php?base=1BA0&type=tsx&msx=&char=<busqueda>&download=N -> "size:..,load:X,crc:..,name:..tsx\n" + fichero
//  - La lista sale de tsxcat.h (generada en el PC por esp32/tools/tsxcat_gen.py: TOSEC.NAME,
//    tamano, crc32 y TSX.LOAD); la web solo ve la descarga de /tsx-files/<TOSEC.NAME>.tsx, que
//    un hilo aparte (tsxDlTask) baja por HTTPS a un buffer en la PSRAM mientras el menu va
//    leyendo lo que ya hay. Sin sockets de por medio: la primera version usaba un WiFiServer
//    local por el loopback de lwIP y con el MSX real (que escribe cada sector en la SD mientras
//    recibe, ~2 KB/s con paradas) el fichero llegaba con el CRC mal.
//    Cuando tsx.eslamejor nos de un listado propio, cambia el generador (o se baja aqui), no el menu.
// ============================================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "tsxcat.h"

#define TSXWEB_HOST     "tsx.eslamejor.com"
#define TSXWEB_MAGIC    "tsx.msximus"
#define TSXWEB_UA       "MSXimus/1.2 (cinta web)"
#define TSXWEB_MAXLIST  115           // FH_MAXENT del menu (tope de la lista)
#define TSXWEB_NAMEMAX  73            // como los nombres del API de File-Hunter en la lista
#define TSXWEB_FHBASE   0x1BA0        // "base=1BA0": los punteros de los registros (el menu no los usa)

// el DNS_Q del UNAPI llama aqui antes de preguntar a internet
bool tsxWebDnsHook(const char* name) {
  bool hit = strcmp(name, TSXWEB_MAGIC) == 0;
  if (hit) Serial.println("[tsxweb] DNS " TSXWEB_MAGIC " -> 127.0.0.1");
  return hit;
}
bool tsxWebIsMagic(IPAddress ip) { return ip == IPAddress(127, 0, 0, 1); }

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
static uint32_t tsxCrc32(const uint8_t* p, uint32_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < n; i++) { crc ^= p[i]; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u))); }
  return ~crc;
}

// ---- la descarga HTTPS: un hilo llena g_dl.buf (PSRAM); solo una a la vez ----
struct TsxDl {
  volatile bool running, done, ok, abort;
  uint8_t* buf; uint32_t size; volatile uint32_t got;
  uint32_t crc; unsigned long t0;
};
static TsxDl g_dl = {false, false, false, false, NULL, 0, 0, 0, 0};

static void tsxDlTask(void* arg) {
  TsxDl* d = &g_dl;
  const TsxCatEntry& e = tsxcat[(uint16_t)(uintptr_t)arg];
  const char* name = tsxcat_names + e.nameOff;
  String url = String("https://" TSXWEB_HOST "/tsx-files/") + tsxUrlEncode(name) + ".tsx";
  bool ok = false; uint32_t got = 0;
  // El HTTPClient guarda un puntero al cliente TLS y su destructor lo usa: se destruye PRIMERO el HTTPClient
  // y DESPUES el WiFiClientSecure (al reves daba "Guru Meditation LoadProhibited" nada mas acabar).
  WiFiClientSecure* s = new WiFiClientSecure();
  HTTPClient* hp = s ? new HTTPClient() : NULL;
  if (hp) {
    if (!loadCACertForClient(s)) s->setInsecure();
    s->setTimeout(15000);
    hp->setUserAgent(TSXWEB_UA);
    hp->setTimeout(15000);
    if (hp->begin(*s, url)) {
      hp->addHeader("Referer", "https://" TSXWEB_HOST "/");
      int code = hp->GET();
      if (code == HTTP_CODE_OK) {
        WiFiClient* st = hp->getStreamPtr();
        unsigned long idle = millis(), tlog = millis();
        ok = (st != NULL);
        while (ok && got < d->size && !d->abort) {
          int av = st->available();
          if (av > 0) {
            if ((uint32_t)av > d->size - got) av = d->size - got;
            int r = st->read(d->buf + got, av);
            if (r > 0) { got += r; d->got = got; idle = millis(); }
          } else if (!hp->connected()) { ok = false; Serial.printf("[tsxweb] el servidor cerro con %u/%u\n", (unsigned)got, (unsigned)d->size); }
          else if (millis() - idle > 30000) { ok = false; Serial.println("[tsxweb] 30 s sin datos del servidor"); }
          else delay(2);
          if (millis() - tlog > 5000) { tlog = millis(); Serial.printf("[tsxweb] %lu ms: bajados %u de %u\n", millis() - d->t0, (unsigned)got, (unsigned)d->size); }
        }
      } else { Serial.printf("[tsxweb] %s -> HTTP %d\n", url.c_str(), code); }
      hp->end();
    }
  } else Serial.println("[tsxweb] sin memoria para el TLS");
  delete hp; delete s;
  uint32_t crc = (ok && got == d->size) ? tsxCrc32(d->buf, got) : 0;
  d->ok = ok && got == d->size && crc == e.crc && !d->abort;
  Serial.printf("[tsxweb] %s: %u/%u bytes en %lu ms, crc %08x (catalogo %08x): %s\n", name, (unsigned)got, (unsigned)d->size,
                millis() - d->t0, (unsigned)crc, (unsigned)e.crc, d->ok ? "BIEN" : d->abort ? "abortada por el menu" : "MALA");
  d->done = true;
  if (d->abort) { free(d->buf); d->buf = NULL; }   // nadie la va a leer
  d->running = false;
  vTaskDelete(NULL);
}
static bool tsxDlStart(uint16_t idx) {
  if (g_dl.running) { Serial.println("[tsxweb] ya hay una descarga en marcha"); return false; }
  if (g_dl.buf) { free(g_dl.buf); g_dl.buf = NULL; }
  const TsxCatEntry& e = tsxcat[idx];
  uint8_t* b = (uint8_t*)ps_malloc(e.size + 16);
  if (!b) b = (uint8_t*)malloc(e.size + 16);
  if (!b) { Serial.println("[tsxweb] sin memoria para el fichero"); return false; }
  g_dl.buf = b; g_dl.size = e.size; g_dl.got = 0; g_dl.crc = e.crc; g_dl.done = false; g_dl.ok = false; g_dl.abort = false;
  g_dl.t0 = millis(); g_dl.running = true;
  if (xTaskCreatePinnedToCore(tsxDlTask, "tsxdl", 20480, (void*)(uintptr_t)idx, 1, NULL, 1) != pdPASS) {
    g_dl.running = false; free(g_dl.buf); g_dl.buf = NULL; return false;
  }
  return true;
}

// ---- la conexion virtual ----
class TsxVirtualClient : public WiFiClient {
 public:
  TsxVirtualClient() : _hdr(NULL), _hlen(0), _hpos(0), _bpos(0), _dl(false), _open(true), _answered(false), _fin(0), _nav(0), _nrd(0) { reg(); Serial.println("[tsxweb] conexion virtual abierta"); }
  int connect(IPAddress ip, uint16_t port) override { return 1; }
  int connect(IPAddress ip, uint16_t port, int32_t t) override { return 1; }
  int connect(const char* h, uint16_t p) override { return 1; }
  int connect(const char* h, uint16_t p, int32_t t) override { return 1; }
  int fd() const override { return -1; }
  int availableForWrite() override { return 2048; }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t n) override {          // la peticion del menu
    if (!_open || _answered) return n;
    for (size_t i = 0; i < n && _req.length() < 700; i++) _req += (char)buf[i];
    if (_req.indexOf("\r\n\r\n") >= 0 || _req.indexOf("\n\n") >= 0) answer();
    return n;
  }
  int available() override {
    if (!_open) return 0;
    // PRUEBA 18/09: un socket real tarda cientos de microsegundos en contestar; la conexion virtual contesta en
    // microsegundos y el menu recibia bytes de mas/menos (CRC MAL, lista con un tamano corrupto): posible carrera
    // en el driver del MSX justo tras enviar la orden. Se retrasa la respuesta como lo haria un socket.
    delay(2);
    uint32_t a = _hlen - _hpos;
    if (_dl) a += g_dl.got - _bpos;
    if (_dl && (_nav < 8 || (_nav % 200) == 0)) Serial.printf("[tsxweb] available() -> %u (hdr %u/%u, bpos %u, got %u)\n", (unsigned)a, (unsigned)_hpos, (unsigned)_hlen, (unsigned)_bpos, (unsigned)g_dl.got);
    if (_dl) _nav++;
    return a > 32767 ? 32767 : (int)a;
  }
  int read() override { uint8_t b; return read(&b, 1) == 1 ? b : -1; }
  int read(uint8_t* buf, size_t n) override {
    if (!_open) return 0;
    size_t got = 0;
    if (_hpos < _hlen) { size_t k = _hlen - _hpos; if (k > n) k = n; memcpy(buf, _hdr + _hpos, k); _hpos += k; got += k; }
    if (_dl && got < n) {
      uint32_t avail = g_dl.got - _bpos; size_t k = n - got; if (k > avail) k = avail;
      if (k) { memcpy(buf + got, g_dl.buf + _bpos, k); _bpos += k; got += k; }
    }
    if (_dl && (_nrd < 12 || (_nrd % 64) == 0)) Serial.printf("[tsxweb] read(%u) -> %u (bpos %u)\n", (unsigned)n, (unsigned)got, (unsigned)_bpos);
    if (_dl) _nrd++;
    return (int)got;
  }
  int peek() override { if (_hpos < _hlen) return _hdr[_hpos]; if (_dl && _bpos < g_dl.got) return g_dl.buf[_bpos]; return -1; }
  void flush() override {}
  uint8_t connected() override {                                  // 0 = cerrada: cuando se ha entregado todo (o fallo)
    if (!_open) return 0;
    if (!_answered) return 1;
    if (_hpos < _hlen) return 1;
    if (!_dl) return 0;
    if (g_dl.done && !g_dl.ok) { if (!_fin) { _fin = millis(); Serial.println("[tsxweb] connected() -> 0: descarga mala"); } return 0; }
    if (_bpos < g_dl.size) return 1;
    if (!_fin) { _fin = millis(); Serial.println("[tsxweb] connected() -> 0: todo entregado"); }
    return 0;
  }
  bool finished() const { return _open && _answered && _fin && (millis() - _fin > 3000); }   // para el autocierre
  void stop() override {
    if (!_open) return;
    _open = false;
    if (_dl) { if (g_dl.running) g_dl.abort = true; else if (g_dl.buf) { free(g_dl.buf); g_dl.buf = NULL; } }
    if (_hdr) { free(_hdr); _hdr = NULL; }
    for (int i = 0; i < 4; i++) if (s_reg[i] == this) s_reg[i] = NULL;
    Serial.printf("[tsxweb] conexion virtual cerrada (%u bytes leidos)\n", (unsigned)(_hpos + _bpos));
  }
  static TsxVirtualClient* s_reg[4];                              // las conexiones virtuales vivas (para el autocierre)
  void reg() { for (int i = 0; i < 4; i++) if (!s_reg[i]) { s_reg[i] = this; return; } }
 private:
  void head(int code, size_t len) {                               // cabecera HTTP (el menu solo mira "2xx" y CRLFCRLF)
    char h[160];
    snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: application/octet-stream\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
             code, code == 200 ? "OK" : "Error", (unsigned)len);
    size_t hl = strlen(h);
    _hdr = (uint8_t*)malloc(hl + 1); memcpy(_hdr, h, hl); _hlen = hl; _hpos = 0;
  }
  void answer() {
    _answered = true;
    char q[64] = {0};
    int pq = _req.indexOf("char=");
    if (pq >= 0) tsxQueryDecode(_req.c_str() + pq + 5, q, sizeof q);
    int pd = _req.indexOf("download=");
    bool tsx = _req.indexOf("type=tsx") >= 0 && _req.startsWith("GET ");
    static uint16_t idx[TSXWEB_MAXLIST];
    if (!tsx) { head(404, 0); return; }
    int n = tsxSearch(q, idx, TSXWEB_MAXLIST);
    char nm[TSXWEB_NAMEMAX + 1];
    if (pd < 0) {                                                 // ---- lista: registros {u32 ptr, u16 KB, u8 load} + u32 0 + nombres ----
      size_t body = (size_t)n * 7 + 4;
      for (int k = 0; k < n; k++) { tsxFold(tsxcat_names + tsxcat[idx[k]].nameOff, nm, sizeof nm); body += strlen(nm) + 1; }
      head(200, body);
      uint8_t* all = (uint8_t*)realloc(_hdr, _hlen + body); if (!all) { return; } _hdr = all;
      uint8_t* p = _hdr + _hlen; uint32_t ptr = TSXWEB_FHBASE;
      for (int k = 0; k < n; k++) {
        const TsxCatEntry& e = tsxcat[idx[k]];
        uint32_t kb = (e.size + 1023) / 1024; if (kb > 65535) kb = 65535;
        p[0] = ptr; p[1] = ptr >> 8; p[2] = ptr >> 16; p[3] = ptr >> 24; p[4] = kb; p[5] = kb >> 8; p[6] = (uint8_t)tsxLoadChar(e.load); p += 7;
        tsxFold(tsxcat_names + e.nameOff, nm, sizeof nm); ptr += strlen(nm) + 1;
      }
      memset(p, 0, 4); p += 4;
      for (int k = 0; k < n; k++) { tsxFold(tsxcat_names + tsxcat[idx[k]].nameOff, nm, sizeof nm); size_t l = strlen(nm) + 1; memcpy(p, nm, l); p += l; }
      _hlen += body;
      Serial.printf("[tsxweb] lista '%s': %d cintas\n", q, n);
      return;
    }
    int item = atoi(_req.c_str() + pd + 9);                       // ---- descarga: meta + fichero (lo va llenando el hilo) ----
    if (item < 0 || item >= n) { head(404, 0); return; }
    const TsxCatEntry& e = tsxcat[idx[item]];
    if (!tsxDlStart(idx[item])) { head(503, 0); return; }
    char folded[160], meta[240];
    tsxFold(tsxcat_names + e.nameOff, folded, sizeof folded);
    snprintf(meta, sizeof meta, "size:%u,load:%c,crc:%08x,name:%s.tsx\n", (unsigned)e.size, tsxLoadChar(e.load), (unsigned)e.crc, folded);
    head(200, strlen(meta) + e.size);
    size_t ml = strlen(meta);
    uint8_t* all = (uint8_t*)realloc(_hdr, _hlen + ml); if (!all) return; _hdr = all;
    memcpy(_hdr + _hlen, meta, ml); _hlen += ml;
    _bpos = 0; _dl = true;
    Serial.printf("[tsxweb] descarga %d: %s (%u bytes)\n", item, folded, (unsigned)e.size);
  }
  String _req; uint8_t* _hdr; size_t _hlen, _hpos; uint32_t _bpos; bool _dl, _open, _answered;
  unsigned long _fin; uint32_t _nav, _nrd;
};
TsxVirtualClient* TsxVirtualClient::s_reg[4] = {NULL, NULL, NULL, NULL};

WiFiClient* tsxWebNewClient() { return new TsxVirtualClient(); }

// desde loop(): autocierre de las conexiones virtuales ya entregadas (si el menu no las cierra, ocuparian
// una de las 4 ranuras del UNAPI para siempre) y limpieza de una descarga que nadie recogio
void tsxWebPoll() {
  static unsigned long last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  for (int i = 0; i < 4; i++) {
    TsxVirtualClient* v = TsxVirtualClient::s_reg[i];
    if (!v || !v->finished()) continue;
    for (int c = 0; c < 4; c++) if (ClientList[c] == v) { Serial.printf("[tsxweb] autocierre de la conexion %d\n", c + 1); CloseTcpConnection(c + 1); btConnections[c] = CONN_CLOSED; break; }
  }
  if (!g_dl.running && g_dl.done && g_dl.buf && g_dl.abort) { free(g_dl.buf); g_dl.buf = NULL; }
}
