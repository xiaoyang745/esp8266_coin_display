#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <EEPROM.h>

TFT_eSPI tft;
ESP8266WebServer server(80);
WiFiClient wifiClient;

#define TFT_W 240
#define TFT_H 240

#ifndef TFT_BL
#define TFT_BL 5
#endif

static const uint32_t REFRESH_MS = 15000;
static const uint32_t SYS_PUSH_MS = 10UL * 60UL * 1000UL;

uint32_t lastFetch = 0;
uint32_t lastSysPush = 0;

enum Mode { MODE_SINGLE, MODE_TRIPLE, MODE_HOLDINGS };
Mode currentMode = MODE_SINGLE;

struct Coin {
  char symbol[12];
  float price;
  float lastPrice;
  uint8_t decimals;
};

struct Holding {
  char symbol[12];
  float price;
  float lastPrice;
  float buyPrice;
  float amount;
  uint8_t decimals;
};

Coin singleCoin = {"BTCUSDT", 0, -1, 2};

Coin tripleCoins[3] = {
  {"BTCUSDT", 0, -1, 0},
  {"ETHUSDT", 0, -1, 2},
  {"FILUSDT", 0, -1, 3}
};

Holding holdings[3] = {
  {"BTCUSDT", 0, -1, 50000.0, 0.1, 2},
  {"ETHUSDT", 0, -1, 3000.0, 1.0, 2},
  {"SOLUSDT", 0, -1, 100.0, 10.0, 2}
};

static const int KCOUNT = 10;
float kO[KCOUNT], kH[KCOUNT], kL[KCOUNT], kC[KCOUNT];
bool kReady = false;

static const int TITLE_Y = 4;
static const int DIV1_Y  = 22;
static const int PRICE_Y = 26;
static const int DIV2_Y  = 56;
static const int CHART_TOP = 62;
static const int CHART_BOTTOM = 235;

struct AppConfig {
  uint32_t magic;
  char apiBase[96];
  char webhook[192];
};

static const uint32_t CFG_MAGIC = 0xC0A11CE6;
static AppConfig cfg;

static void cfgDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  strncpy(cfg.apiBase, "http://YOUR_API_HOST:8000", sizeof(cfg.apiBase) - 1);
  strncpy(cfg.webhook, "", sizeof(cfg.webhook) - 1);
}

static void cfgLoad() {
  EEPROM.begin(512);
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) {
    cfgDefaults();
    EEPROM.put(0, cfg);
    EEPROM.commit();
  }
}

static void cfgSave() {
  cfg.magic = CFG_MAGIC;
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

static void formatPrice(char* out, size_t len, float v, uint8_t d) {
  if (d > 6) d = 6;
  char fmt[12];
  snprintf(fmt, sizeof(fmt), "$%%.%df", d);
  snprintf(out, len, fmt, v);
}

static void formatPL(char* out, size_t len, float v) {
  if (v >= 0) snprintf(out, len, "+$%.2f", v);
  else snprintf(out, len, "-$%.2f", -v);
}

static void formatPercent(char* out, size_t len, float pct) {
  if (pct >= 0) snprintf(out, len, "+%.2f%%", pct);
  else snprintf(out, len, "%.2f%%", pct);
}

static void drawDivider(int y) {
  tft.drawFastHLine(0, y, TFT_W, TFT_DARKGREY);
}

static void drawCountdown(uint32_t now) {
  uint32_t remain = (REFRESH_MS - (now - lastFetch)) / 1000;
  if ((int32_t)remain < 0) remain = 0;
  if (remain > 99) remain = 99;

  tft.fillRect(170, 0, 70, 18, TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(178, 2);
  tft.printf("T-%us", (unsigned int)remain);
}

static void normalizeSymbol(String &s) {
  s.trim();
  s.toUpperCase();
  if (!s.endsWith("USDT")) s += "USDT";
  if (s.length() > 11) s = s.substring(0, 11);
}

static void setCoinSymbol(Coin &c, const String &sym) {
  String s = sym;
  normalizeSymbol(s);
  s.toCharArray(c.symbol, sizeof(c.symbol));
  c.lastPrice = -1;
}

static void setHoldingSymbol(Holding &h, const String &sym) {
  String s = sym;
  normalizeSymbol(s);
  s.toCharArray(h.symbol, sizeof(h.symbol));
  h.lastPrice = -1;
}

static String formatUptime(uint32_t ms) {
  uint32_t s = ms / 1000UL;
  uint32_t m = s / 60UL; s %= 60UL;
  uint32_t h = m / 60UL; m %= 60UL;
  uint32_t d = h / 24UL; h %= 24UL;
  String out;
  if (d) out += String(d) + "d ";
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
           (unsigned long)h, (unsigned long)m, (unsigned long)s);
  out += buf;
  return out;
}

static String buildSysReportText() {
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : String("N/A");
  long rssi = WiFi.isConnected() ? WiFi.RSSI() : 0;

  uint32_t heap = ESP.getFreeHeap();
  uint32_t flash = ESP.getFlashChipSize();
  uint32_t sketch = ESP.getSketchSize();
  uint32_t freeSketch = ESP.getFreeSketchSpace();

  String msg;
  msg.reserve(256);
  msg += "ESP8266 status\n";
  msg += "• Uptime: " + formatUptime(millis()) + "\n";
  msg += "• FreeHeap: " + String(heap) + " B\n";

  #if defined(ARDUINO_ESP8266_MAJOR)
    msg += "• HeapFrag: " + String(ESP.getHeapFragmentation()) + " %\n";
    msg += "• MaxFreeBlock: " + String(ESP.getMaxFreeBlockSize()) + " B\n";
  #endif

  msg += "• Flash: " + String(flash) + " B\n";
  msg += "• Sketch: " + String(sketch) + " B\n";
  msg += "• FreeSketch: " + String(freeSketch) + " B\n";
  msg += "• WiFi: " + String(WiFi.SSID()) + "  RSSI " + String(rssi) + " dBm\n";
  msg += "• IP: " + ip + "\n";
  msg += "• ChipID: " + String(ESP.getChipId()) + "\n";
  return msg;
}

static bool postFeishuText(const String& text, String* outResp = nullptr, int* outCode = nullptr) {
  String hook = String(cfg.webhook);
  hook.trim();

  if (hook.length() == 0) {
    if (outResp) *outResp = "webhook not set";
    if (outCode) *outCode = -3;
    return false;
  }

  if (!WiFi.isConnected()) {
    if (outResp) *outResp = "wifi not connected";
    if (outCode) *outCode = -1;
    return false;
  }

  StaticJsonDocument<512> doc;
  doc["msg_type"] = "text";
  JsonObject content = doc.createNestedObject("content");
  content["text"] = text;

  String payload;
  serializeJson(doc, payload);

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  HTTPClient https;
  if (!https.begin(*client, hook)) {
    if (outResp) *outResp = "https.begin failed";
    if (outCode) *outCode = -2;
    return false;
  }

  https.addHeader("Content-Type", "application/json; charset=utf-8");
  int code = https.POST((uint8_t*)payload.c_str(), payload.length());
  String resp = https.getString();
  https.end();

  if (outResp) *outResp = resp;
  if (outCode) *outCode = code;
  return (code > 0 && code < 300);
}

static void handlePushSys() {
  String resp;
  int code = 0;
  bool ok = postFeishuText(buildSysReportText(), &resp, &code);

  StaticJsonDocument<512> out;
  out["ok"] = ok;
  out["http_code"] = code;
  out["resp"] = resp;

  String body;
  serializeJson(out, body);
  server.send(ok ? 200 : 500, "application/json; charset=utf-8", body);
}

static void handleSysJson() {
  StaticJsonDocument<512> out;
  out["uptime_ms"] = millis();
  out["uptime"] = formatUptime(millis());
  out["free_heap"] = ESP.getFreeHeap();

  #if defined(ARDUINO_ESP8266_MAJOR)
    out["heap_frag_pct"] = ESP.getHeapFragmentation();
    out["max_free_block"] = ESP.getMaxFreeBlockSize();
  #endif

  out["flash_size"] = ESP.getFlashChipSize();
  out["sketch_size"] = ESP.getSketchSize();
  out["free_sketch"] = ESP.getFreeSketchSpace();
  out["rssi"] = WiFi.isConnected() ? WiFi.RSSI() : 0;
  out["ssid"] = WiFi.isConnected() ? WiFi.SSID() : "";
  out["ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : "";
  out["chip_id"] = ESP.getChipId();

  String body;
  serializeJson(out, body);
  server.send(200, "application/json; charset=utf-8", body);
}

static bool apiReady() {
  String base = String(cfg.apiBase);
  base.trim();
  return base.length() > 0;
}

static bool fetchPrice(Coin& c) {
  if (!apiReady()) return false;

  char url[220];
  snprintf(url, sizeof(url), "%s/price?symbol=%s", cfg.apiBase, c.symbol);

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);

  if (!http.begin(wifiClient, url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;

  c.lastPrice = c.price;
  c.price = doc["price"].as<float>();
  return true;
}

static bool fetchHoldingPrice(Holding& h) {
  if (!apiReady()) return false;

  char url[220];
  snprintf(url, sizeof(url), "%s/price?symbol=%s", cfg.apiBase, h.symbol);

  HTTPClient http;
  http.setTimeout(5000);
  http.setReuse(false);

  if (!http.begin(wifiClient, url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;

  h.lastPrice = h.price;
  h.price = doc["price"].as<float>();
  return true;
}

static bool fetchKlines10(const char* symbol) {
  if (!apiReady()) { kReady = false; return false; }

  char url[260];
  snprintf(url, sizeof(url), "%s/klines?symbol=%s&interval=1h&limit=10", cfg.apiBase, symbol);

  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);

  if (!http.begin(wifiClient, url)) { kReady = false; return false; }

  int code = http.GET();
  if (code != 200) { http.end(); kReady = false; return false; }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();

  if (err) { kReady = false; return false; }

  JsonArray arr = doc.as<JsonArray>();
  int n = arr.size();
  if (n <= 0) { kReady = false; return false; }

  for (int i = 0; i < KCOUNT; i++) {
    int idx = (i < n) ? i : (n - 1);
    kO[i] = arr[idx][1].as<float>();
    kH[i] = arr[idx][2].as<float>();
    kL[i] = arr[idx][3].as<float>();
    kC[i] = arr[idx][4].as<float>();
    yield();
  }

  kReady = true;
  return true;
}

static void drawSingle() {
  tft.fillScreen(TFT_BLACK);

  tft.setTextFont(2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(6, TITLE_Y);
  tft.print(singleCoin.symbol);

  drawDivider(DIV1_Y);

  char pbuf[28];
  formatPrice(pbuf, sizeof(pbuf), singleCoin.price, singleCoin.decimals);

  uint16_t priceCol = TFT_WHITE;
  if (singleCoin.lastPrice >= 0) {
    priceCol = (singleCoin.price >= singleCoin.lastPrice) ? TFT_GREEN : TFT_RED;
  }

  tft.setTextFont(4);
  tft.setTextColor(priceCol, TFT_BLACK);
  tft.setCursor(8, PRICE_Y);
  tft.print(pbuf);

  drawDivider(DIV2_Y);

  if (!kReady) {
    tft.setTextFont(2);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.setCursor(8, CHART_TOP + 10);
    tft.print("kline not ready");
    return;
  }

  float ymin = kL[0], ymax = kH[0];
  for (int i = 1; i < KCOUNT; i++) {
    if (kL[i] < ymin) ymin = kL[i];
    if (kH[i] > ymax) ymax = kH[i];
  }
  float range = ymax - ymin;
  if (range < 0.0001f) range = 1.0f;
  ymin -= range * 0.06f;
  ymax += range * 0.06f;
  range = ymax - ymin;

  tft.fillRect(0, CHART_TOP, TFT_W, CHART_BOTTOM - CHART_TOP, TFT_BLACK);

  int chartH = (CHART_BOTTOM - CHART_TOP);
  int slotW = TFT_W / KCOUNT;
  int bodyW = slotW - 6;
  if (bodyW < 4) bodyW = 4;

  auto toY = [&](float v) -> int {
    float t = (ymax - v) / range;
    int y = CHART_TOP + (int)(t * (float)chartH);
    if (y < CHART_TOP) y = CHART_TOP;
    if (y > CHART_BOTTOM) y = CHART_BOTTOM;
    return y;
  };

  for (int i = 0; i < KCOUNT; i++) {
    int xCenter = i * slotW + slotW / 2;
    int xLeft   = xCenter - bodyW / 2;

    int yH = toY(kH[i]);
    int yL = toY(kL[i]);
    int yO = toY(kO[i]);
    int yC = toY(kC[i]);

    uint16_t col = (kC[i] >= kO[i]) ? TFT_GREEN : TFT_RED;

    tft.drawFastVLine(xCenter, yH, (yL - yH) + 1, TFT_LIGHTGREY);

    int top = min(yO, yC);
    int h   = abs(yO - yC);
    if (h < 2) h = 2;

    tft.fillRect(xLeft, top, bodyW, h, col);
    yield();
  }
}

static void drawTriple() {
  tft.fillScreen(TFT_BLACK);

  for (int i = 0; i < 3; i++) {
    int y = i * 80;

    drawDivider(y + 22);

    tft.setTextFont(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(6, y + 2);
    tft.print(tripleCoins[i].symbol);

    char buf[28];
    formatPrice(buf, sizeof(buf), tripleCoins[i].price, tripleCoins[i].decimals);

    uint16_t col = TFT_WHITE;
    if (tripleCoins[i].lastPrice >= 0) {
      col = (tripleCoins[i].price >= tripleCoins[i].lastPrice) ? TFT_GREEN : TFT_RED;
    }

    tft.setTextFont(4);
    tft.setTextColor(col, TFT_BLACK);
    tft.setCursor(8, y + 30);
    tft.print(buf);

    tft.setTextFont(2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(190, y + 34);
    tft.printf("d:%u", (unsigned int)tripleCoins[i].decimals);

    yield();
  }
}

static void drawHoldings() {
  tft.fillScreen(TFT_BLACK);

  for (int i = 0; i < 3; i++) {
    int y = i * 80;

    tft.setTextFont(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(4, y + 1);
    tft.print(holdings[i].symbol);

    drawDivider(y + 18);

    char priceBuf[28];
    formatPrice(priceBuf, sizeof(priceBuf), holdings[i].price, holdings[i].decimals);

    uint16_t priceCol = TFT_WHITE;
    if (holdings[i].lastPrice >= 0) {
      priceCol = (holdings[i].price >= holdings[i].lastPrice) ? TFT_GREEN : TFT_RED;
    }

    tft.setTextFont(4);
    tft.setTextColor(priceCol, TFT_BLACK);
    tft.setCursor(4, y + 22);
    tft.print(priceBuf);

    float costTotal = holdings[i].buyPrice * holdings[i].amount;
    float currentTotal = holdings[i].price * holdings[i].amount;
    float plUsdt = currentTotal - costTotal;
    float plPercent = (holdings[i].buyPrice > 0) ? (plUsdt / costTotal * 100.0f) : 0;

    uint16_t plCol = (plUsdt >= 0) ? TFT_GREEN : TFT_RED;

    tft.setTextFont(4);
    tft.setTextColor(plCol, TFT_BLACK);
    char plBuf[24];
    formatPL(plBuf, sizeof(plBuf), plUsdt);

    uint16_t w = tft.textWidth(plBuf);
    tft.setCursor(236 - w, y + 22);
    tft.print(plBuf);

    tft.setTextFont(2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(4, y + 54);
    char infoBuf[48];
    snprintf(infoBuf, sizeof(infoBuf), "%.3f@$%.2f", holdings[i].amount, holdings[i].buyPrice);
    tft.print(infoBuf);

    tft.setTextFont(2);
    tft.setTextColor(plCol, TFT_BLACK);
    char pctBuf[16];
    formatPercent(pctBuf, sizeof(pctBuf), plPercent);
    w = tft.textWidth(pctBuf);
    tft.setCursor(236 - w, y + 54);
    tft.print(pctBuf);

    yield();
  }
}

static const char HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP8266 Coin Display</title>
<style>
body{font-family:system-ui,-apple-system,Segoe UI,Roboto; padding:16px; max-width:620px;}
h2{margin:18px 0 8px;}
.row{display:flex; gap:8px; flex-wrap:wrap; align-items:center;}
input{padding:8px; font-size:16px; width:220px;}
input.num{width:120px;}
select{padding:8px; font-size:16px;}
button{padding:10px 12px; font-size:16px;}
small{color:#666;}
.card{padding:12px; border:1px solid #ddd; border-radius:10px; margin:10px 0;}
label{min-width:64px;}
.code{font-family:ui-monospace,Menlo,Consolas,monospace;}
</style>
</head>
<body>

<h2>Server config</h2>
<div class="card">
  <div class="row">
    <label>API Base</label>
    <input id="api" class="code" placeholder="http://host:8000" />
  </div>
  <div class="row" style="margin-top:8px;">
    <label>Webhook</label>
    <input id="wh" class="code" placeholder="https://open.feishu.cn/.../hook/xxxx" />
  </div>
  <div class="row" style="margin-top:10px;">
    <button onclick="saveCfg()">Save</button>
    <button onclick="loadCfg()">Reload</button>
    <button onclick="apiCall('/push')">Test push</button>
  </div>
  <p><small>API Base 用来取价格 / K线；Webhook 不填就不会推送。</small></p>
</div>

<h2>Mode</h2>
<div class="row">
  <button onclick="apiCall('/mode?m=single')">Single (Kline)</button>
  <button onclick="apiCall('/mode?m=triple')">Triple</button>
  <button onclick="apiCall('/mode?m=holdings')">Holdings P&amp;L</button>
</div>

<div class="card">
<h2>Single</h2>
<div class="row">
  <input id="ssym" placeholder="BTC / BTCUSDT" />
  <button onclick="setSingle()">Set</button>
</div>
<div class="row" style="margin-top:8px;">
  <button onclick="quickSingle('BTCUSDT')">BTC</button>
  <button onclick="quickSingle('ETHUSDT')">ETH</button>
  <button onclick="quickSingle('SOLUSDT')">SOL</button>
  <button onclick="quickSingle('BNBUSDT')">BNB</button>
  <button onclick="quickSingle('DOGEUSDT')">DOGE</button>
</div>
<div class="row" style="margin-top:10px;">
  <span>Decimals:</span>
  <select id="sd">
    <option>0</option><option>1</option><option selected>2</option><option>3</option><option>4</option><option>5</option><option>6</option>
  </select>
  <button onclick="setSingleDec()">Apply</button>
</div>
</div>

<div class="card">
<h2>Triple</h2>
<div class="row">
  <span>Coin1:</span><input id="t0" placeholder="BTC" value="BTC">
  <span>d:</span><select id="d0"><option selected>0</option><option>1</option><option>2</option><option>3</option><option>4</option><option>5</option><option>6</option></select>
</div>
<div class="row">
  <span>Coin2:</span><input id="t1" placeholder="ETH" value="ETH">
  <span>d:</span><select id="d1"><option>0</option><option>1</option><option selected>2</option><option>3</option><option>4</option><option>5</option><option>6</option></select>
</div>
<div class="row">
  <span>Coin3:</span><input id="t2" placeholder="FIL" value="FIL">
  <span>d:</span><select id="d2"><option>0</option><option>1</option><option>2</option><option selected>3</option><option>4</option><option>5</option><option>6</option></select>
</div>
<div class="row" style="margin-top:10px;">
  <button onclick="applyTriple()">Apply</button>
</div>
</div>

<div class="card">
<h2>Holdings P&amp;L</h2>
<div style="margin-bottom:12px;">
  <div class="row" style="margin-bottom:4px;">
    <label>Coin1</label><input id="h0sym" placeholder="BTC" value="BTC">
    <label>Buy</label><input id="h0buy" class="num" placeholder="50000" value="50000">
    <label>Amt</label><input id="h0amt" class="num" placeholder="0.1" value="0.1">
    <label>d</label><select id="h0d"><option>0</option><option>1</option><option selected>2</option><option>3</option><option>4</option><option>5</option><option>6</option></select>
  </div>
  <div class="row" style="margin-bottom:4px;">
    <label>Coin2</label><input id="h1sym" placeholder="ETH" value="ETH">
    <label>Buy</label><input id="h1buy" class="num" placeholder="3000" value="3000">
    <label>Amt</label><input id="h1amt" class="num" placeholder="1.0" value="1.0">
    <label>d</label><select id="h1d"><option>0</option><option>1</option><option selected>2</option><option>3</option><option>4</option><option>5</option><option>6</option></select>
  </div>
  <div class="row" style="margin-bottom:4px;">
    <label>Coin3</label><input id="h2sym" placeholder="SOL" value="SOL">
    <label>Buy</label><input id="h2buy" class="num" placeholder="100" value="100">
    <label>Amt</label><input id="h2amt" class="num" placeholder="10" value="10.0">
    <label>d</label><select id="h2d"><option>0</option><option>1</option><option selected>2</option><option>3</option><option>4</option><option>5</option><option>6</option></select>
  </div>
</div>
<div class="row">
  <button onclick="applyHoldings()">Apply</button>
</div>
<p><small>Buy 是买入均价(USDT)，Amt 是持仓数量。</small></p>
</div>

<p><small>屏幕右上角 T-xx 是下一次刷新倒计时（15 秒一轮）。</small></p>

<script>
function apiCall(u){ fetch(u).catch(console.error); }

function normSym(x){
  x = (x||"").trim().toUpperCase();
  if(!x) return "";
  if(!x.endsWith("USDT")) x += "USDT";
  return x;
}

async function loadCfg(){
  const r = await fetch("/cfg");
  const j = await r.json();
  api.value = j.api || "";
  wh.value = j.webhook || "";
}

async function saveCfg(){
  const u = "/cfg?api=" + encodeURIComponent(api.value.trim())
          + "&wh=" + encodeURIComponent(wh.value.trim());
  await fetch(u);
  await loadCfg();
}

function setSingle(){
  const s = normSym(ssym.value);
  if(!s) return;
  apiCall("/single?sym="+encodeURIComponent(s));
  ssym.value="";
}
function quickSingle(s){ apiCall("/single?sym="+encodeURIComponent(s)); }
function setSingleDec(){ apiCall("/singleDec?d="+encodeURIComponent(sd.value)); }

function applyTriple(){
  const c0 = normSym(t0.value), c1 = normSym(t1.value), c2 = normSym(t2.value);
  if(!c0||!c1||!c2) return;
  apiCall("/triple?c0="+encodeURIComponent(c0)+"&c1="+encodeURIComponent(c1)+"&c2="+encodeURIComponent(c2)
      +"&d0="+d0.value+"&d1="+d1.value+"&d2="+d2.value);
}

function applyHoldings(){
  const s0 = normSym(h0sym.value), s1 = normSym(h1sym.value), s2 = normSym(h2sym.value);
  if(!s0||!s1||!s2) return;

  apiCall("/holdings?s0="+encodeURIComponent(s0)+"&s1="+encodeURIComponent(s1)+"&s2="+encodeURIComponent(s2)
      +"&b0="+encodeURIComponent(h0buy.value)+"&b1="+encodeURIComponent(h1buy.value)+"&b2="+encodeURIComponent(h2buy.value)
      +"&a0="+encodeURIComponent(h0amt.value)+"&a1="+encodeURIComponent(h1amt.value)+"&a2="+encodeURIComponent(h2amt.value)
      +"&d0="+h0d.value+"&d1="+h1d.value+"&d2="+h2d.value);
}

loadCfg().catch(console.error);
</script>
</body>
</html>
)rawliteral";

static void handleRoot() {
  server.send_P(200, "text/html", HTML);
}

static void handleCfgGet() {
  StaticJsonDocument<512> out;
  out["api"] = String(cfg.apiBase);
  out["webhook"] = String(cfg.webhook);
  String body;
  serializeJson(out, body);
  server.send(200, "application/json; charset=utf-8", body);
}

static void handleCfgSet() {
  String api = server.arg("api");
  String wh  = server.arg("wh");
  api.trim();
  wh.trim();

  if (api.length() > 0) {
    api.toCharArray(cfg.apiBase, sizeof(cfg.apiBase));
  }
  if (wh.length() >= 0) {
    wh.toCharArray(cfg.webhook, sizeof(cfg.webhook));
  }

  cfgSave();
  server.send(200, "text/plain", "OK");
}

static void handleMode() {
  String m = server.arg("m");
  if (m == "single") currentMode = MODE_SINGLE;
  if (m == "triple") currentMode = MODE_TRIPLE;
  if (m == "holdings") currentMode = MODE_HOLDINGS;
  server.send(200, "text/plain", "OK");
}

static void handleSingle() {
  String s = server.arg("sym");
  if (s.length() < 2) { server.send(400, "text/plain", "BAD sym"); return; }

  normalizeSymbol(s);
  setCoinSymbol(singleCoin, s);
  kReady = false;

  currentMode = MODE_SINGLE;
  fetchPrice(singleCoin);
  fetchKlines10(singleCoin.symbol);
  drawSingle();

  server.send(200, "text/plain", "OK");
}

static void handleSingleDec() {
  int d = server.arg("d").toInt();
  if (d < 0) d = 0;
  if (d > 6) d = 6;
  singleCoin.decimals = (uint8_t)d;

  if (currentMode == MODE_SINGLE) drawSingle();
  server.send(200, "text/plain", "OK");
}

static void handleTripleConfig() {
  String c0 = server.arg("c0");
  String c1 = server.arg("c1");
  String c2 = server.arg("c2");

  if (c0.length() < 2 || c1.length() < 2 || c2.length() < 2) {
    server.send(400, "text/plain", "BAD c0/c1/c2");
    return;
  }

  normalizeSymbol(c0);
  normalizeSymbol(c1);
  normalizeSymbol(c2);

  setCoinSymbol(tripleCoins[0], c0);
  setCoinSymbol(tripleCoins[1], c1);
  setCoinSymbol(tripleCoins[2], c2);

  int d0 = server.arg("d0").toInt();
  int d1 = server.arg("d1").toInt();
  int d2 = server.arg("d2").toInt();

  if (d0 < 0) d0 = 0; if (d0 > 6) d0 = 6;
  if (d1 < 0) d1 = 0; if (d1 > 6) d1 = 6;
  if (d2 < 0) d2 = 0; if (d2 > 6) d2 = 6;

  tripleCoins[0].decimals = (uint8_t)d0;
  tripleCoins[1].decimals = (uint8_t)d1;
  tripleCoins[2].decimals = (uint8_t)d2;

  currentMode = MODE_TRIPLE;
  for (int i = 0; i < 3; i++) fetchPrice(tripleCoins[i]);
  drawTriple();

  server.send(200, "text/plain", "OK");
}

static void handleHoldingsConfig() {
  String s0 = server.arg("s0");
  String s1 = server.arg("s1");
  String s2 = server.arg("s2");

  if (s0.length() < 2 || s1.length() < 2 || s2.length() < 2) {
    server.send(400, "text/plain", "BAD symbols");
    return;
  }

  normalizeSymbol(s0);
  normalizeSymbol(s1);
  normalizeSymbol(s2);

  setHoldingSymbol(holdings[0], s0);
  setHoldingSymbol(holdings[1], s1);
  setHoldingSymbol(holdings[2], s2);

  holdings[0].buyPrice = server.arg("b0").toFloat();
  holdings[1].buyPrice = server.arg("b1").toFloat();
  holdings[2].buyPrice = server.arg("b2").toFloat();

  holdings[0].amount = server.arg("a0").toFloat();
  holdings[1].amount = server.arg("a1").toFloat();
  holdings[2].amount = server.arg("a2").toFloat();

  int d0 = server.arg("d0").toInt();
  int d1 = server.arg("d1").toInt();
  int d2 = server.arg("d2").toInt();

  if (d0 < 0) d0 = 0; if (d0 > 6) d0 = 6;
  if (d1 < 0) d1 = 0; if (d1 > 6) d1 = 6;
  if (d2 < 0) d2 = 0; if (d2 > 6) d2 = 6;

  holdings[0].decimals = (uint8_t)d0;
  holdings[1].decimals = (uint8_t)d1;
  holdings[2].decimals = (uint8_t)d2;

  currentMode = MODE_HOLDINGS;
  for (int i = 0; i < 3; i++) fetchHoldingPrice(holdings[i]);
  drawHoldings();

  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  delay(50);

  cfgLoad();

  tft.init();
  tft.setRotation(0);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  tft.fillScreen(TFT_BLACK);

  WiFiManager wm;
  wm.setConnectTimeout(15);
  wm.autoConnect("BTC_Display");

  server.on("/", handleRoot);
  server.on("/mode", handleMode);
  server.on("/single", handleSingle);
  server.on("/singleDec", handleSingleDec);
  server.on("/triple", handleTripleConfig);
  server.on("/holdings", handleHoldingsConfig);

  server.on("/cfg", HTTP_GET, handleCfgGet);
  server.on("/cfg", HTTP_POST, handleCfgSet);
  server.on("/cfg", HTTP_ANY, handleCfgSet);

  server.on("/push", handlePushSys);
  server.on("/sys", handleSysJson);

  server.begin();

  lastFetch = millis();
  lastSysPush = millis();

  fetchPrice(singleCoin);
  fetchKlines10(singleCoin.symbol);
  drawSingle();
}

void loop() {
  server.handleClient();

  uint32_t now = millis();
  drawCountdown(now);

  if (SYS_PUSH_MS > 0 && (now - lastSysPush >= SYS_PUSH_MS)) {
    lastSysPush = now;
    postFeishuText(buildSysReportText());
  }

  if (now - lastFetch >= REFRESH_MS) {
    lastFetch = now;

    if (currentMode == MODE_SINGLE) {
      fetchPrice(singleCoin);
      fetchKlines10(singleCoin.symbol);
      drawSingle();
    } else if (currentMode == MODE_TRIPLE) {
      for (int i = 0; i < 3; i++) fetchPrice(tripleCoins[i]);
      drawTriple();
    } else {
      for (int i = 0; i < 3; i++) fetchHoldingPrice(holdings[i]);
      drawHoldings();
    }
  }

  delay(2);
}