// diag.ino v2 - diagnostico del ESP32-1732S019 por pasos (18/09/2026)
// Pasos 1-3 sin radio (chip/PSRAM, GPIO del enlace, LCD). Paso 4 (WiFi a 2 dBm con el
// detector de brownout desactivado por API) solo tras 20 s o al pulsar BOOT (GPIO0).
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>

extern "C" void esp_brownout_disable(void);

#define LCD_SCK   12
#define LCD_MOSI  13
#define LCD_CS    10
#define LCD_DC    11
#define LCD_RST    1
#define LCD_BL    14
#define BTN_BOOT   0

static Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED, HSPI);
static Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 1, true, 170, 320, 35, 0, 35, 0);
int vivo = 0;
int wifiHecho = 0;
unsigned long t0 = 0;

bool pinTest(int p) {
  pinMode(p, INPUT_PULLUP);   delay(5); int up = digitalRead(p);
  pinMode(p, INPUT_PULLDOWN); delay(5); int dn = digitalRead(p);
  pinMode(p, OUTPUT); digitalWrite(p, HIGH); delay(2); int h = digitalRead(p);
  digitalWrite(p, LOW); delay(2); int l = digitalRead(p);
  pinMode(p, INPUT);
  bool ok = (up == 1 && dn == 0 && h == 1 && l == 0);
  const char* v = ok ? "OK" : (h == 0) ? "PEGADO A 0 (corto a masa?)" : (l == 1) ? "PEGADO A 1 (corto a 3V3?)" : "RARO (pull-ups no responden)";
  Serial.printf("   GPIO%d: pullup=%d pulldown=%d salida1->%d salida0->%d  %s\n", p, up, dn, h, l, v);
  return ok;
}

void pruebaWifi() {
  Serial.println("4. WiFi a 2 dBm con el detector de brownout DESACTIVADO (esp_brownout_disable)");
  Serial.println("   si se reinicia igualmente, el 3,3 V se hunde de verdad (no es solo el detector)");
  gfx->println("WiFi (BOD off)...");
  Serial.flush(); delay(300);
  esp_brownout_disable();
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.setTxPower(WIFI_POWER_2dBm);
  int n = WiFi.scanNetworks();
  Serial.printf("   SOBREVIVE: scan = %d redes\n", n);
  for (int i = 0; i < n && i < 5; i++) Serial.printf("   - %s  %d dBm\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  gfx->printf("WiFi OK: %d redes\n", n);
  Serial.printf("   temp tras WiFi %.1f C, heap %u\n", temperatureRead(), (unsigned)ESP.getFreeHeap());
  WiFi.mode(WIFI_OFF);
  wifiHecho = 1;
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_BOOT, INPUT_PULLUP);
  delay(800);
  Serial.println();
  Serial.println("=== DIAG v2 ESP32-1732S019 (sin WiFi hasta el paso 4) ===");
  Serial.printf("1. chip %s rev %d, %d nucleos, flash %u MB, PSRAM %u KB (%s), heap libre %u, temp %.1f C\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), (unsigned)(ESP.getFlashChipSize() / 1048576),
                (unsigned)(ESP.getPsramSize() / 1024), psramFound() ? "detectada" : "NO", (unsigned)ESP.getFreeHeap(), temperatureRead());
  int badPs = -1;
  if (psramFound()) {
    uint8_t* p = (uint8_t*)ps_malloc(1 << 20);
    if (p) {
      for (int i = 0; i < (1 << 20); i++) p[i] = (uint8_t)(i * 7);
      badPs = 0; for (int i = 0; i < (1 << 20); i++) if (p[i] != (uint8_t)(i * 7)) badPs++;
      Serial.printf("   PSRAM: 1 MB escrito y leido, %d errores\n", badPs);
      free(p);
    } else Serial.println("   PSRAM: ps_malloc(1 MB) FALLA");
  }
  Serial.println("2. GPIO del enlace (39 RX, 40 TX, 41 turbo), sin nada conectado:");
  bool g39 = pinTest(39), g40 = pinTest(40), g41 = pinTest(41);
  Serial.println("3. LCD: init + barras rojo/verde/azul + informe en pantalla");
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH);
  bool ok = gfx->begin();
  Serial.printf("   gfx->begin() = %d\n", ok);
  gfx->fillScreen(RGB565_RED); delay(300); gfx->fillScreen(RGB565_GREEN); delay(300); gfx->fillScreen(RGB565_BLUE); delay(300); gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE); gfx->setTextSize(2); gfx->setCursor(4, 8);
  gfx->println("DIAG 1732S019");
  gfx->printf("CPU/flash %uMB OK\n", (unsigned)(ESP.getFlashChipSize() / 1048576));
  gfx->printf("PSRAM %uMB %s\n", (unsigned)(ESP.getPsramSize() >> 20), badPs == 0 ? "OK" : "FALLA");
  gfx->printf("GPIO39 %s 40 %s\n", g39 ? "OK" : "MAL", g40 ? "OK" : "MAL");
  gfx->printf("GPIO41 %s  LCD OK\n", g41 ? "OK" : "MAL");
  gfx->println("WiFi en 20 s");
  gfx->println("(o pulsa BOOT)");
  Serial.printf("   temp tras LCD %.1f C, heap %u\n", temperatureRead(), (unsigned)ESP.getFreeHeap());
  Serial.println("   esperando 20 s (o BOOT) antes de arrancar el WiFi: mide ahora el 3V3 en reposo");
  t0 = millis();
}

void loop() {
  Serial.printf("vivo %d  temp %.1f C  heap %u  (WiFi %s)\n", vivo++, temperatureRead(), (unsigned)ESP.getFreeHeap(), wifiHecho ? "ya probado" : "pendiente");
  if (!wifiHecho && (millis() - t0 > 20000 || digitalRead(BTN_BOOT) == LOW)) pruebaWifi();
  delay(2000);
}
