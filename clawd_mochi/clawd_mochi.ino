/*
 * ╔══════════════════════════════════════════════════════════════╗
 *   CLAWD MOCHI PLUS — Enhanced Desk Companion (15 expressions;
 *   "s" squeeze eyes are a separate always-on view, not an expression)
 *   ESP32-C3 Super Mini + ST7789 1.54" 240×240 SPI
 *
 *   Wiring:
 *     SDA → GPIO 10  (hardware SPI MOSI)
 *     SCL → GPIO 8   (hardware SPI SCK)
 *     RST → GPIO 2
 *     DC  → GPIO 1
 *     CS  → GPIO 4
 *     BL  → GPIO 3
 *     VCC → 3V3
 *     GND → GND
 *
 *   WiFi Hotspot (Default out of the box):
 *     SSID: "ClaWD-Mochi"  pw: clawd1234  → http://192.168.4.1
 *   (Optional STA mode: see ENABLE_HOME_WIFI in code below)
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <math.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_system.h>

// ── Optional Home-WiFi (STA) Configuration ────────────────────
// Uncomment the lines below to connect to your local WiFi network.
// If commented out (default), Clawd Mochi starts the AP hotspot automatically:
//   SSID: "ClaWD-Mochi"  Password: "clawd1234"  IP: 192.168.4.1
// #define ENABLE_HOME_WIFI
#ifdef ENABLE_HOME_WIFI
  #define WIFI_SSID   "Your-WiFi-SSID"
  #define WIFI_PASS   "Your-WiFi-Password"
  #define SCREEN_IP   192,168,1,200
  #define SCREEN_GW   192,168,1,1
  #define SCREEN_MASK 255,255,255,0
  #define SCREEN_DNS  192,168,1,1
#endif

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS  4
#define TFT_DC  1
#define TFT_RST 2
#define TFT_BLK 3

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ── WiFi ──────────────────────────────────────────────────────
const char* AP_SSID = "ClaWD-Mochi";
const char* AP_PASS = "clawd1234";
WebServer server(80);

// STA link state: true after a successful home-WiFi connect at boot.
// Once STA came up we never fall back to AP — reconnect watchdog only.
bool     staMode          = false;
uint32_t lastWifiCheckMs  = 0;
#define STA_CONNECT_TIMEOUT_MS  15000UL  // boot: give STA ~15 s
#define WIFI_WATCHDOG_MS        10000UL  // loop: reconnect check interval

// ── Display ───────────────────────────────────────────────────
#define DISP_W 240
#define DISP_H 240

// ── Eye geometry (runtime-adjustable via /eye) ────────────────
int16_t eyeW   = 30;
int16_t eyeH   = 60;
int16_t eyeGap = 120;
int16_t eyeOx  = 0;    // horizontal offset
int16_t eyeOy  = 40;   // vertical offset upward (subtracted from centre)

// ── Colours ───────────────────────────────────────────────────
uint16_t C_ORANGE, C_DARKBG, C_MUTED, C_GREEN, C_RED;
#define C_WHITE ST77XX_WHITE
#define C_BLACK ST77XX_BLACK

// ── State ─────────────────────────────────────────────────────
#define VIEW_EYES_NORMAL 0
#define VIEW_EYES_SQUISH 1
#define VIEW_CODE        2
#define VIEW_DRAW        3
#define VIEW_INFO        4

enum Expr : uint8_t {
  EXPR_NORMAL    = 0,
  EXPR_SQUISH    = 1,
  EXPR_HAPPY     = 2,   // k=h  ^ ^
  EXPR_SLEEPY    = 3,   // k=z  bars + zZ
  EXPR_SURPRISED = 4,   // k=o  big eyes + shake
  EXPR_WINK      = 5,   // k=k  left normal, right bar
  EXPR_DIZZY     = 6,   // k=x  x x
  EXPR_ANGRY     = 7,   // k=g  inner brows cut
  EXPR_HEART     = 9,   // k=l  heart eyes
  EXPR_THINK     = 10,  // k=t  wave bars (borrowed from upstream thinking)
  EXPR_EXECUTING = 11,  // k=e  scan shrink rect (borrowed from upstream)
  EXPR_CRYING    = 13,
  EXPR_SHOCKED   = 14,
  EXPR_COOL      = 15,
  EXPR_EMBARRASSED = 18,
  EXPR_DELICIOUS = 19
};

uint8_t  currentView  = VIEW_EYES_NORMAL;
uint8_t  currentExpr  = EXPR_NORMAL;
bool     busy         = false;
bool     backlightOn  = true;
uint8_t  animSpeed    = 1;   // 1=slow(default) 2=normal 3=fast
bool     napping      = false;
bool     bootRequestSeen = false;

uint16_t animBgColor  = 0;   // background for eye/logo animations
uint16_t drawBgColor  = 0;   // background for canvas

// ── Tuning constants ──────────────────────────────────────────
#define WIFI_INFO_MS        8000UL  // boot info page duration
#define EXPR_HOLD_MS        5000UL  // temp expression auto-restore
#define EXPR_THINK_HOLD_MS  6000UL
#define EXPR_WAVE_MS          140   // think wave frame period
#define EXPR_SCAN_MS          200   // executing scan frame period
#define IDLE_BLINK_MIN_MS   2500UL
#define IDLE_BLINK_MAX_MS   6000UL
#define IDLE_LOOK_MIN_MS    6000UL
#define IDLE_LOOK_MAX_MS   15000UL
#define IDLE_SQUINT_MIN_MS 30000UL
#define IDLE_SQUINT_MAX_MS 60000UL
#define SLEEP_AFTER_MS    300000UL  // nap after 5 min without activity
#define IDLE_PAN_PX           12    // idle look-left/right amplitude

// ── Idle / expression engine state ────────────────────────────
bool     idleEnabled  = true;
uint32_t lastActivityMs = 0;
uint32_t nextBlinkAt = 0, nextLookAt = 0, nextSquintAt = 0;
uint32_t exprEndAt   = 0;
uint8_t  exprFrame   = 0;
uint32_t nextFrameAt = 0;

#define IDLE_NONE      0
#define IDLE_LOOK_OUT  1
#define IDLE_LOOK_DWELL 2
#define IDLE_LOOK_BACK 3
#define IDLE_SQUINT    4
uint8_t  idlePhase = IDLE_NONE;
int16_t  lookTarget = 0, lookCur = 0;
uint8_t  lookSteps = 0, lookStep = 0;
uint32_t idleWaitUntil = 0;

bool     blinkFxOn = false;
uint8_t  blinkFxLeft = 0;    // toggle-steps remaining
bool     blinkFxClosed = false;
uint32_t blinkFxNext = 0;

Preferences prefs;

// ── Terminal ──────────────────────────────────────────────────
#define TERM_COLS      15
#define TERM_ROWS       8
#define TERM_CHAR_W    12
#define TERM_CHAR_H    20
#define TERM_PAD_X      8
#define TERM_PAD_Y     18

bool    termMode    = false;
String  termLines[TERM_ROWS];
uint8_t termRow     = 0;
uint8_t termCol     = 0;

// ── Logo data ─────────────────────────────────────────────────
#define LOGO_CX 120
#define LOGO_CY 105

#define LOGO_TRI_COUNT 162
static const int16_t LOGO_TRIS[][6] PROGMEM = {
  {120,105,65,134,100,114},{120,105,100,114,101,113},{120,105,101,113,100,112},
  {120,105,100,112,99,112},{120,105,99,112,93,111},{120,105,93,111,73,111},
  {120,105,73,111,55,110},{120,105,55,110,38,109},{120,105,38,109,34,108},
  {120,105,34,108,30,103},{120,105,30,103,30,100},{120,105,30,100,34,98},
  {120,105,34,98,39,98},{120,105,39,98,50,99},{120,105,50,99,67,100},
  {120,105,67,100,80,101},{120,105,80,101,98,103},{120,105,98,103,101,103},
  {120,105,101,103,101,102},{120,105,101,102,100,101},{120,105,100,101,100,100},
  {120,105,100,100,82,88},{120,105,82,88,63,76},{120,105,63,76,53,69},
  {120,105,53,69,48,65},{120,105,48,65,45,61},{120,105,45,61,44,54},
  {120,105,44,54,49,49},{120,105,49,49,55,49},{120,105,55,49,57,49},
  {120,105,57,49,64,55},{120,105,64,55,78,66},{120,105,78,66,96,79},
  {120,105,96,79,99,81},{120,105,99,81,100,81},{120,105,100,81,100,80},
  {120,105,100,80,99,78},{120,105,99,78,89,60},{120,105,89,60,78,41},
  {120,105,78,41,73,34},{120,105,73,34,72,29},{120,105,72,29,72,28},
  {120,105,72,28,72,27},{120,105,72,27,71,26},{120,105,71,26,71,25},
  {120,105,71,25,71,24},{120,105,71,24,77,16},{120,105,77,16,80,15},
  {120,105,80,15,87,16},{120,105,87,16,91,19},{120,105,91,19,95,29},
  {120,105,95,29,103,46},{120,105,103,46,114,68},{120,105,114,68,118,75},
  {120,105,118,75,119,81},{120,105,119,81,120,83},{120,105,120,83,121,83},
  {120,105,121,83,121,82},{120,105,121,82,122,69},{120,105,122,69,124,54},
  {120,105,124,54,126,34},{120,105,126,34,126,28},{120,105,126,28,129,21},
  {120,105,129,21,135,18},{120,105,135,18,139,20},{120,105,139,20,143,25},
  {120,105,143,25,142,28},{120,105,142,28,140,42},{120,105,140,42,136,64},
  {120,105,136,64,133,78},{120,105,133,78,135,78},{120,105,135,78,136,76},
  {120,105,136,76,144,67},{120,105,144,67,156,51},{120,105,156,51,162,45},
  {120,105,162,45,168,38},{120,105,168,38,172,35},{120,105,172,35,180,35},
  {120,105,180,35,185,43},{120,105,185,43,183,52},{120,105,183,52,175,62},
  {120,105,175,62,168,71},{120,105,168,71,159,83},{120,105,159,83,153,94},
  {120,105,153,94,154,94},{120,105,154,94,155,94},{120,105,155,94,176,90},
  {120,105,176,90,188,88},{120,105,188,88,201,85},{120,105,201,85,208,88},
  {120,105,208,88,208,91},{120,105,208,91,206,97},{120,105,206,97,191,101},
  {120,105,191,101,174,104},{120,105,174,104,148,110},{120,105,148,110,148,111},
  {120,105,148,111,148,111},{120,105,148,111,160,112},{120,105,160,112,165,112},
  {120,105,165,112,177,112},{120,105,177,112,200,114},{120,105,200,114,205,118},
  {120,105,205,118,209,123},{120,105,209,123,208,126},{120,105,208,126,199,131},
  {120,105,199,131,187,128},{120,105,187,128,159,121},{120,105,159,121,149,119},
  {120,105,149,119,147,119},{120,105,147,119,147,120},{120,105,147,120,156,128},
  {120,105,156,128,170,141},{120,105,170,141,189,158},{120,105,189,158,190,163},
  {120,105,190,163,188,166},{120,105,188,166,185,166},{120,105,185,166,169,153},
  {120,105,169,153,162,148},{120,105,162,148,148,136},{120,105,148,136,147,136},
  {120,105,147,136,147,137},{120,105,147,137,150,142},{120,105,150,142,168,168},
  {120,105,168,168,169,176},{120,105,169,176,168,179},{120,105,168,179,163,180},
  {120,105,163,180,158,179},{120,105,158,179,148,165},{120,105,148,165,137,149},
  {120,105,137,149,129,134},{120,105,129,134,128,135},{120,105,128,135,123,189},
  {120,105,123,189,120,192},{120,105,120,192,115,194},{120,105,115,194,110,191},
  {120,105,110,191,108,185},{120,105,108,185,110,174},{120,105,110,174,113,160},
  {120,105,113,160,116,148},{120,105,116,148,118,134},{120,105,118,134,119,129},
  {120,105,119,129,119,129},{120,105,119,129,118,129},{120,105,118,129,107,144},
  {120,105,107,144,91,166},{120,105,91,166,78,180},{120,105,78,180,75,181},
  {120,105,75,181,70,178},{120,105,70,178,70,173},{120,105,70,173,73,169},
  {120,105,73,169,91,146},{120,105,91,146,102,132},{120,105,102,132,109,124},
  {120,105,109,124,109,123},{120,105,109,123,108,123},{120,105,108,123,61,153},
  {120,105,61,153,52,155},{120,105,52,155,49,151},{120,105,49,151,49,146},
  {120,105,49,146,51,144},{120,105,51,144,65,134},{120,105,65,134,65,134},
};

#define LOGO_SEG_COUNT 162
static const int16_t LOGO_SEGS[][4] PROGMEM = {
  {65,134,100,114},{100,114,101,113},{101,113,100,112},{100,112,99,112},
  {99,112,93,111},{93,111,73,111},{73,111,55,110},{55,110,38,109},
  {38,109,34,108},{34,108,30,103},{30,103,30,100},{30,100,34,98},
  {34,98,39,98},{39,98,50,99},{50,99,67,100},{67,100,80,101},
  {80,101,98,103},{98,103,101,103},{101,103,101,102},{101,102,100,101},
  {100,101,100,100},{100,100,82,88},{82,88,63,76},{63,76,53,69},
  {53,69,48,65},{48,65,45,61},{45,61,44,54},{44,54,49,49},
  {49,49,55,49},{55,49,57,49},{57,49,64,55},{64,55,78,66},
  {78,66,96,79},{96,79,99,81},{99,81,100,81},{100,81,100,80},
  {100,80,99,78},{99,78,89,60},{89,60,78,41},{78,41,73,34},
  {73,34,72,29},{72,29,72,28},{72,28,72,27},{72,27,71,26},
  {71,26,71,25},{71,25,71,24},{71,24,77,16},{77,16,80,15},
  {80,15,87,16},{87,16,91,19},{91,19,95,29},{95,29,103,46},
  {103,46,114,68},{114,68,118,75},{118,75,119,81},{119,81,120,83},
  {120,83,121,83},{121,83,121,82},{121,82,122,69},{122,69,124,54},
  {124,54,126,34},{126,34,126,28},{126,28,129,21},{129,21,135,18},
  {135,18,139,20},{139,20,143,25},{143,25,142,28},{142,28,140,42},
  {140,42,136,64},{136,64,133,78},{133,78,135,78},{135,78,136,76},
  {136,76,144,67},{144,67,156,51},{156,51,162,45},{162,45,168,38},
  {168,38,172,35},{172,35,180,35},{180,35,185,43},{185,43,183,52},
  {183,52,175,62},{175,62,168,71},{168,71,159,83},{159,83,153,94},
  {153,94,154,94},{154,94,155,94},{155,94,176,90},{176,90,188,88},
  {188,88,201,85},{201,85,208,88},{208,88,208,91},{208,91,206,97},
  {206,97,191,101},{191,101,174,104},{174,104,148,110},{148,110,148,111},
  {148,111,148,111},{148,111,160,112},{160,112,165,112},{165,112,177,112},
  {177,112,200,114},{200,114,205,118},{205,118,209,123},{209,123,208,126},
  {208,126,199,131},{199,131,187,128},{187,128,159,121},{159,121,149,119},
  {149,119,147,119},{147,119,147,120},{147,120,156,128},{156,128,170,141},
  {170,141,189,158},{189,158,190,163},{190,163,188,166},{188,166,185,166},
  {185,166,169,153},{169,153,162,148},{162,148,148,136},{148,136,147,136},
  {147,136,147,137},{147,137,150,142},{150,142,168,168},{168,168,169,176},
  {169,176,168,179},{168,179,163,180},{163,180,158,179},{158,179,148,165},
  {148,165,137,149},{137,149,129,134},{129,134,128,135},{128,135,123,189},
  {123,189,120,192},{120,192,115,194},{115,194,110,191},{110,191,108,185},
  {108,185,110,174},{110,174,113,160},{113,160,116,148},{116,148,118,134},
  {118,134,119,129},{119,129,119,129},{119,129,118,129},{118,129,107,144},
  {107,144,91,166},{91,166,78,180},{78,180,75,181},{75,181,70,178},
  {70,178,70,173},{70,173,73,169},{73,169,91,146},{91,146,102,132},
  {102,132,109,124},{109,124,109,123},{109,123,108,123},{108,123,61,153},
  {61,153,52,155},{52,155,49,151},{49,151,49,146},{49,146,51,144},
  {51,144,65,134},{65,134,65,134},
};

// ═════════════════════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════════════════════

int speedMs(int ms) {
  if (animSpeed == 3) return ms / 2;
  if (animSpeed == 1) return ms * 2;
  return ms;
}

uint16_t hexToRgb565(String hex) {
  hex.replace("#", "");
  if (hex.length() != 6) return C_WHITE;
  long v = strtol(hex.c_str(), nullptr, 16);
  return tft.color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void setBacklight(bool on) {
  backlightOn = on;
  digitalWrite(TFT_BLK, on ? HIGH : LOW);
}

void initColours() {
  // C_ORANGE = tft.color565(170, 72, 28);
  C_ORANGE = tft.color565(218, 17, 0);
  C_DARKBG = tft.color565(10,  12,  16);
  C_MUTED  = tft.color565(90,  88,  86);
  C_GREEN  = tft.color565(80, 220, 130);
  C_RED    = tft.color565(255, 96, 96);
  animBgColor = C_ORANGE;
  drawBgColor = C_ORANGE;
}

// ═════════════════════════════════════════════════════════════
//  LOGO
// ═════════════════════════════════════════════════════════════

void drawLogoFilled(uint16_t bg, uint16_t fg) {
  tft.fillScreen(bg);
  for (uint16_t i = 0; i < LOGO_TRI_COUNT; i++) {
    tft.fillTriangle(
      pgm_read_word(&LOGO_TRIS[i][0]), pgm_read_word(&LOGO_TRIS[i][1]),
      pgm_read_word(&LOGO_TRIS[i][2]), pgm_read_word(&LOGO_TRIS[i][3]),
      pgm_read_word(&LOGO_TRIS[i][4]), pgm_read_word(&LOGO_TRIS[i][5]),
      fg);
  }
  tft.setTextColor(fg); tft.setTextSize(2);
  tft.setCursor(LOGO_CX - 54, 210); tft.print("Anthropic");
  tft.setCursor(LOGO_CX - 53, 210); tft.print("Anthropic");
}

// ═════════════════════════════════════════════════════════════
//  VIEWS
// ═════════════════════════════════════════════════════════════

// Eye helpers — use runtime eye geometry variables
inline int16_t eyeLX(int16_t ox) {
  return (DISP_W - (eyeW * 2 + eyeGap)) / 2 + eyeOx + ox;
}
inline int16_t eyeRX(int16_t ox) { return eyeLX(ox) + eyeW + eyeGap; }
inline int16_t eyeY()            { return (DISP_H - eyeH) / 2 - eyeOy; }
inline int16_t eyeCY()           { return eyeY() + eyeH / 2; }

void drawNormalEyesAt(int16_t ox, int16_t oyShift, bool blink) {
  const int16_t lx = eyeLX(ox), rx = eyeRX(ox), ey = eyeY() - oyShift;
  if (!blink) {
    tft.fillRect(lx, ey, eyeW, eyeH, C_BLACK);
    tft.fillRect(rx, ey, eyeW, eyeH, C_BLACK);
  } else {
    tft.fillRect(lx, ey + eyeH / 2 - 3, eyeW, 6, C_BLACK);
    tft.fillRect(rx, ey + eyeH / 2 - 3, eyeW, 6, C_BLACK);
  }
}

void drawNormalEyes(int16_t ox = 0, bool blink = false) {
  tft.fillScreen(animBgColor);
  drawNormalEyesAt(ox, 0, blink);
}

void drawChevron(int16_t cx, int16_t cy, int16_t arm, int16_t reach,
                 uint8_t thk, bool rightFacing, uint16_t col) {
  for (int8_t t = -(int8_t)thk; t <= (int8_t)thk; t++) {
    if (rightFacing) {
      tft.drawLine(cx - reach/2, cy - arm + t, cx + reach/2, cy + t,      col);
      tft.drawLine(cx + reach/2, cy + t,       cx - reach/2, cy + arm + t, col);
    } else {
      tft.drawLine(cx + reach/2, cy - arm + t, cx - reach/2, cy + t,      col);
      tft.drawLine(cx - reach/2, cy + t,       cx + reach/2, cy + arm + t, col);
    }
  }
}

void drawSquishEyes(bool closed = false) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  const int16_t arm   = eyeH / 2;
  const int16_t reach = eyeW / 2;
  const int16_t lcx   = lx + eyeW / 2;
  const int16_t rcx   = rx + eyeW / 2;
  if (!closed) {
    drawChevron(lcx, cy, arm, reach, 10, true,  C_BLACK);
    drawChevron(rcx, cy, arm, reach, 10, false, C_BLACK);
  } else {
    tft.fillRect(lx, cy - 5, eyeW, 10, C_BLACK);
    tft.fillRect(rx, cy - 5, eyeW, 10, C_BLACK);
  }
}

void drawCodeView() {
  termMode = false;
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0,          DISP_W, 4, C_ORANGE);
  tft.fillRect(0, DISP_H - 4, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_ORANGE); tft.setTextSize(4);
  tft.setCursor((DISP_W - 144) / 2, DISP_H / 2 - 52); tft.print("Claude");
  tft.setTextColor(C_WHITE);  tft.setTextSize(4);
  tft.setCursor((DISP_W - 96) / 2,  DISP_H / 2 + 8);  tft.print("Code");
  tft.fillRect((DISP_W - 96) / 2, DISP_H / 2 + 52, 96, 3, C_ORANGE);
}

// Info page reflects the active mode (STA home WiFi / AP hotspot)
void drawInfoPage() {
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_WHITE);  tft.setTextSize(2);
#ifdef ENABLE_HOME_WIFI
  if (staMode) {
    String ssid = String(WIFI_SSID);
    if (ssid.length() > 14) ssid = ssid.substring(0, 14);
    tft.setCursor(12, 16);  tft.print("WiFi: " + ssid);
    tft.setTextColor(C_MUTED);  tft.setTextSize(1);
    tft.setCursor(12, 44);  tft.print("mode: STA (home network)");
    tft.setTextColor(C_WHITE);  tft.setTextSize(2);
    tft.setCursor(12, 68);  tft.print("IP address:");
    tft.setTextColor(C_ORANGE); tft.setTextSize(2);
    tft.setCursor(12, 94);  tft.print(WiFi.localIP().toString());
    tft.setTextColor(C_MUTED);  tft.setTextSize(1);
    tft.setCursor(12, 124); tft.print("press any button to start");
    return;
  }
#endif
  tft.setCursor(12, 16);  tft.print("WiFi: ClaWD-Mochi");
  tft.setTextColor(C_MUTED);  tft.setTextSize(1);
  tft.setCursor(12, 44);  tft.print("password: clawd1234");
  tft.setTextColor(C_WHITE);  tft.setTextSize(2);
  tft.setCursor(12, 68);  tft.print("Open browser:");
  tft.setTextColor(C_ORANGE); tft.setTextSize(2);
  tft.setCursor(12, 94);  tft.print("192.168.4.1");
  tft.setTextColor(C_MUTED);  tft.setTextSize(1);
  tft.setCursor(12, 124); tft.print("press any button to start");
}

// Transient page while the STA attempt runs at boot (max ~15 s)
void drawWifiConnectPage() {
  tft.fillScreen(C_DARKBG);
  tft.fillRect(0, 0, DISP_W, 4, C_ORANGE);
  tft.setTextColor(C_WHITE);  tft.setTextSize(2);
  tft.setCursor(12, 64);  tft.print("WiFi: connecting");
  tft.setTextColor(C_MUTED);  tft.setTextSize(1);
  tft.setCursor(12, 92);  tft.print("to home network...");
}

// ═════════════════════════════════════════════════════════════
//  TERMINAL
// ═════════════════════════════════════════════════════════════

void termClear() {
  for (uint8_t i = 0; i < TERM_ROWS; i++) termLines[i] = "";
  termRow = 0; termCol = 0;
}

void termDrawHeader() {
  tft.fillRect(0, 0, DISP_W, TERM_PAD_Y + 1, C_DARKBG);
  tft.setTextColor(C_ORANGE); tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, 4); tft.print("clawd@mochi terminal");
  tft.drawFastHLine(0, TERM_PAD_Y, DISP_W, C_ORANGE);
}

// Prefix "clawd:~$ " in green, drawn only when the row has content
void termDrawPrefix(int16_t yy) {
  tft.setTextColor(C_GREEN); tft.setTextSize(1);
  tft.setCursor(TERM_PAD_X, yy + 6);
  tft.print("clawd:~$ ");
}

#define PREFIX_PX 54   // 9 chars × 6px = 54px at textSize 1

void termDrawLine(uint8_t r) {
  const int16_t yy = TERM_PAD_Y + 4 + r * TERM_CHAR_H;
  tft.fillRect(0, yy, DISP_W, TERM_CHAR_H, C_DARKBG);
  // show prefix only on the currently active (cursor) line
  if (r == termRow) termDrawPrefix(yy);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(TERM_PAD_X + PREFIX_PX, yy + 1);
  tft.print(termLines[r]);
  if (r == termRow) {
    const int16_t cx = TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W;
    tft.fillRect(cx, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  }
}

void termDrawLastChar() {
  if (termCol == 0) return;
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  const uint8_t prev  = termCol - 1;
  // erase prev cell (had cursor block)
  tft.fillRect(baseX + prev * TERM_CHAR_W, yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(baseX + prev * TERM_CHAR_W, yy + 1);
  tft.print(termLines[termRow][prev]);
  // new cursor
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
}

void termDrawBackspace() {
  const int16_t yy    = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
  const int16_t baseX = TERM_PAD_X + PREFIX_PX;
  // erase deleted char + old cursor
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W * 2, TERM_CHAR_H - 1, C_DARKBG);
  // new cursor
  tft.fillRect(baseX + termCol * TERM_CHAR_W, yy + 1, TERM_CHAR_W - 2, TERM_CHAR_H - 2, C_GREEN);
  // if line now empty, erase the prefix too
  if (termLines[termRow].length() == 0) {
    tft.fillRect(0, yy, TERM_PAD_X + PREFIX_PX, TERM_CHAR_H, C_DARKBG);
  }
}

void termFullRedraw() {
  tft.fillScreen(C_DARKBG);
  termDrawHeader();
  for (uint8_t r = 0; r < TERM_ROWS; r++) termDrawLine(r);
}

void termScroll() {
  for (uint8_t i = 0; i < TERM_ROWS - 1; i++) termLines[i] = termLines[i + 1];
  termLines[TERM_ROWS - 1] = "";
  termRow = TERM_ROWS - 1;
  termFullRedraw();
}

void termAddChar(char c) {
  if (c == '\n' || c == '\r') {
    const int16_t yy = TERM_PAD_Y + 4 + termRow * TERM_CHAR_H;
    // erase cursor on current row
    tft.fillRect(TERM_PAD_X + PREFIX_PX + termCol * TERM_CHAR_W,
                 yy + 1, TERM_CHAR_W, TERM_CHAR_H - 1, C_DARKBG);
    termRow++; termCol = 0;
    if (termRow >= TERM_ROWS) { termScroll(); return; }
    termDrawLine(termRow);  // draws prefix on new line
  } else if (c == '\b' || c == 127) {
    if (termCol > 0) {
      termCol--;
      termLines[termRow].remove(termLines[termRow].length() - 1);
      termDrawBackspace();
    }
  } else if (c >= 32 && c < 127) {
    if (termCol >= TERM_COLS) {
      termRow++; termCol = 0;
      if (termRow >= TERM_ROWS) { termScroll(); return; }
    }
    // draw prefix on first char of this line
    if (termCol == 0) termDrawPrefix(TERM_PAD_Y + 4 + termRow * TERM_CHAR_H);
    termLines[termRow] += c;
    termCol++;
    termDrawLastChar();
  }
}

// ═════════════════════════════════════════════════════════════
//  ANIMATIONS
// ═════════════════════════════════════════════════════════════

void animNormalEyes() {
  busy = true;
  const int16_t offs[] = {-16, 16, -16, 16, 0};
  for (uint8_t i = 0; i < 5; i++) { drawNormalEyes(offs[i]); delay(speedMs(80)); }
  drawNormalEyes(0, true);  delay(speedMs(100));
  drawNormalEyes(0, false); delay(speedMs(70));
  drawNormalEyes(0, true);  delay(speedMs(70));
  drawNormalEyes(0, false);
  busy = false;
  resetIdleTimers();
}

void animSquishEyes() {
  busy = true;
  for (uint8_t i = 0; i < 3; i++) {
    drawSquishEyes(false); delay(speedMs(160));
    drawSquishEyes(true);  delay(speedMs(100));
  }
  drawSquishEyes(false);
  busy = false;
  resetIdleTimers();
}

void animLogoReveal() {
  busy = true;
  tft.fillScreen(animBgColor);
  for (uint16_t i = 0; i < LOGO_SEG_COUNT; i++) {
    int16_t x1 = pgm_read_word(&LOGO_SEGS[i][0]);
    int16_t y1 = pgm_read_word(&LOGO_SEGS[i][1]);
    int16_t x2 = pgm_read_word(&LOGO_SEGS[i][2]);
    int16_t y2 = pgm_read_word(&LOGO_SEGS[i][3]);
    tft.drawLine(x1, y1, x2, y2, C_WHITE);
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, C_WHITE);
    if (i % 4 == 0) { server.handleClient(); delay(speedMs(8)); }
  }
  drawLogoFilled(animBgColor, C_WHITE);
  delay(1500);
  busy = false;
  resetIdleTimers();
}

// ═════════════════════════════════════════════════════════════
//  EYE PARAMS / NVS
// ═════════════════════════════════════════════════════════════

void clampEyeParams() {
  eyeW   = constrain(eyeW,   10, 80);
  eyeH   = constrain(eyeH,   20, 120);
  eyeGap = constrain(eyeGap, 20, 180);
  // keep both eyes on screen: 2*w + gap <= 236
  if (2 * eyeW + eyeGap > 236) eyeGap = 236 - 2 * eyeW;
  // horizontal margin, minus reserve for idle pan (±12)
  const int16_t mx = (DISP_W - (2 * eyeW + eyeGap)) / 2;
  const int16_t xmax = (int16_t)min(40, max(0, (int)mx - 12));
  eyeOx = constrain(eyeOx, -xmax, xmax);
  // vertical: leave 24px reserve (squint overhang, tear, think shift)
  const int16_t oyMax = (int16_t)min((long)80, (long)((DISP_H - eyeH) / 2 - 24));
  const int16_t oyMin = (int16_t)max((long)-60, (long)-(108 - eyeH / 2));
  eyeOy = constrain(eyeOy, oyMin, oyMax);
}

void saveEyeParams() {
  prefs.begin("mochi", false);
  prefs.putShort("ew", eyeW);
  prefs.putShort("eh", eyeH);
  prefs.putShort("eg", eyeGap);
  prefs.putShort("ex", eyeOx);
  prefs.putShort("ey", eyeOy);
  prefs.end();
}

void loadEyeParams() {
  prefs.begin("mochi", true);
  eyeW   = prefs.getShort("ew", 30);
  eyeH   = prefs.getShort("eh", 60);
  eyeGap = prefs.getShort("eg", 120);
  eyeOx  = prefs.getShort("ex", 0);
  eyeOy  = prefs.getShort("ey", 40);
  idleEnabled = prefs.getBool("idle", true);
  prefs.end();
  clampEyeParams();
}

// ═════════════════════════════════════════════════════════════
//  IDLE ENGINE — helpers
// ═════════════════════════════════════════════════════════════

uint32_t randRange(uint32_t lo, uint32_t hi) {
  return lo + esp_random() % (hi - lo);
}

void resetIdleTimers() {
  const uint32_t now = millis();
  lastActivityMs = now;
  nextBlinkAt  = now + randRange(IDLE_BLINK_MIN_MS,  IDLE_BLINK_MAX_MS);
  nextLookAt   = now + randRange(IDLE_LOOK_MIN_MS,   IDLE_LOOK_MAX_MS);
  nextSquintAt = now + randRange(IDLE_SQUINT_MIN_MS, IDLE_SQUINT_MAX_MS);
}

// Erase only the horizontal band the eyes live in (no full fillScreen).
void eyeBand(int16_t &by, int16_t &bh) {
  const int16_t ey = eyeY();
  by = max((int16_t)0, (int16_t)(ey - 14));
  bh = min((int16_t)(DISP_H - by), (int16_t)(eyeH + 28));
}

void drawEyesBand(int16_t ox, bool blink) {
  int16_t by, bh;
  eyeBand(by, bh);
  tft.fillRect(0, by, DISP_W, bh, animBgColor);
  drawNormalEyesAt(ox, 0, blink);
}

// Internal idle-only squint (><) — band redraw, no state change
void drawSquintBand() {
  int16_t by, bh;
  eyeBand(by, bh);
  tft.fillRect(0, by, DISP_W, bh, animBgColor);
  const int16_t arm = eyeH / 2, reach = eyeW / 2;
  drawChevron(eyeLX(0) + eyeW / 2, eyeCY(), arm, reach, 10, true,  C_BLACK);
  drawChevron(eyeRX(0) + eyeW / 2, eyeCY(), arm, reach, 10, false, C_BLACK);
}

void cancelBlinkFx() { blinkFxOn = false; blinkFxLeft = 0; }

void startBlinkFx(uint8_t blinks) {
  if (busy || termMode || napping) return;
  if (currentView != VIEW_EYES_NORMAL || currentExpr != EXPR_NORMAL) return;
  blinkFxOn = true;
  blinkFxLeft = blinks * 2;
  blinkFxClosed = false;
  blinkFxNext = millis();
}

// Non-blocking one-shot blink, safe to call every loop()
void blinkFxTick() {
  if (!blinkFxOn) return;
  if (busy || termMode || napping ||
      currentView != VIEW_EYES_NORMAL || currentExpr != EXPR_NORMAL) {
    cancelBlinkFx();
    return;
  }
  const uint32_t now = millis();
  if ((int32_t)(now - blinkFxNext) < 0) return;
  if (!blinkFxClosed) {
    blinkFxClosed = true;
    drawEyesBand(0, true);
    blinkFxNext = now + 60;
  } else {
    blinkFxClosed = false;
    blinkFxLeft--;
    drawEyesBand(0, false);
    if (blinkFxLeft == 0) blinkFxOn = false;
    else blinkFxNext = now + 60;
  }
}

void noteActivity() {
  bootRequestSeen = true;
  resetIdleTimers();
  if (napping && !busy) {
    napping = false;
    if (currentView == VIEW_EYES_NORMAL) {
      currentExpr = EXPR_NORMAL;
      drawNormalEyes();
      startBlinkFx(1);
    }
  }
}

// ═════════════════════════════════════════════════════════════
//  EXPRESSIONS — all geometry scales with runtime eye params
//  (w=eyeW h=eyeH lx/rx=eye left edges ey=eye top cy=eye centre)
// ═════════════════════════════════════════════════════════════

// happy ^ ^ — upward chevron per eye
void drawHappyEye(int16_t cx, int16_t cy, int16_t hw, int16_t hh, int16_t thk) {
  for (int16_t t = -thk; t <= thk; t++) {
    tft.drawLine(cx - hw + t, cy + hh, cx + t, cy - hh, C_BLACK);
    tft.drawLine(cx + t, cy - hh, cx + hw + t, cy + hh, C_BLACK);
  }
}

void drawExprHappy() {
  tft.fillScreen(animBgColor);
  const int16_t hw = eyeH * 45 / 100;
  const int16_t hh = eyeH * 45 / 100;
  const int16_t thk = max((int16_t)1, (int16_t)(eyeW / 6));
  drawHappyEye(eyeLX(0) + eyeW / 2, eyeCY(), hw, hh, thk);
  drawHappyEye(eyeRX(0) + eyeW / 2, eyeCY(), hw, hh, thk);
}

// 以线段画字，避免固定字号让 zZ 脱离运行时眼睛比例。
void drawSleepZ(int16_t x, int16_t y, int16_t w, int16_t h, int16_t thk) {
  for (int16_t t = 0; t < thk; t++) {
    tft.drawLine(x, y + t, x + w, y + t, C_WHITE);
    tft.drawLine(x + w, y + t, x, y + h + t, C_WHITE);
    tft.drawLine(x, y + h + t, x + w, y + h + t, C_WHITE);
  }
}

void drawExprSleepy(uint8_t frame) {
  tft.fillScreen(animBgColor);
  const int16_t barH = max((int16_t)1, (int16_t)(eyeH / 4));
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY(), ey = eyeY();
  tft.fillRect(lx, cy, eyeW, barH, C_BLACK);
  tft.fillRect(rx, cy, eyeW, barH, C_BLACK);
  const int16_t scale = (frame & 1) ? 120 : 100;
  const int16_t rise = (frame & 1) * eyeH / 10;
  const int16_t thk = max((int16_t)1, (int16_t)(eyeW / 15));
  drawSleepZ(rx + eyeW / 4, ey - eyeH / 3 - rise,
             eyeW * 2 * scale / 500, eyeH * scale / 500, thk);
  drawSleepZ(rx + eyeW * 5 / 6, ey - eyeH * 19 / 30 - rise,
             eyeW * 3 * scale / 500, eyeH * 3 * scale / 1000, thk);
}

// surprised — eyes scaled 1.3x around original centres (ox = shake offset)
void drawExprSurprised(int16_t ox) {
  tft.fillScreen(animBgColor);
  const int16_t nw = eyeW * 13 / 10, nh = eyeH * 13 / 10;
  const int16_t lcx = eyeLX(ox) + eyeW / 2, rcx = eyeRX(ox) + eyeW / 2;
  const int16_t cy = eyeCY();
  tft.fillRect(lcx - nw / 2, cy - nh / 2, nw, nh, C_BLACK);
  tft.fillRect(rcx - nw / 2, cy - nh / 2, nw, nh, C_BLACK);
}

// 单眼闭合的静态表情，闭眼厚度沿用基准的 8/60 比例。
void drawExprWink() {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY(), cy = eyeCY();
  const int16_t h = max((int16_t)1, (int16_t)(eyeH * 4 / 30));
  tft.fillRect(lx, ey, eyeW, eyeH, C_BLACK);
  tft.fillRect(rx, cy - h / 2, eyeW, h, C_BLACK);
}

// dizzy x x — thick crossed diagonals (ox = sway offset)
void drawExprDizzy(int16_t ox) {
  tft.fillScreen(animBgColor);
  const int16_t s = eyeH * 6 / 10;
  const int16_t thk = max((int16_t)1, (int16_t)(eyeW / 6));
  const int16_t lcx = eyeLX(ox) + eyeW / 2, rcx = eyeRX(ox) + eyeW / 2;
  const int16_t cy = eyeCY();
  for (int16_t t = -thk; t <= thk; t++) {
    tft.drawLine(lcx - s / 2 + t, cy - s / 2, lcx + s / 2 + t, cy + s / 2, C_BLACK);
    tft.drawLine(lcx + s / 2 + t, cy - s / 2, lcx - s / 2 + t, cy + s / 2, C_BLACK);
    tft.drawLine(rcx - s / 2 + t, cy - s / 2, rcx + s / 2 + t, cy + s / 2, C_BLACK);
    tft.drawLine(rcx + s / 2 + t, cy - s / 2, rcx - s / 2 + t, cy + s / 2, C_BLACK);
  }
}

// angry — normal eyes, inner top corners cut by bg-colour triangles
void drawExprAngry() {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY();
  const int16_t cut = eyeH * 45 / 100;
  tft.fillRect(lx, ey, eyeW, eyeH, C_BLACK);
  tft.fillRect(rx, ey, eyeW, eyeH, C_BLACK);
  tft.fillTriangle(lx + eyeW, ey, lx + eyeW, ey + cut, lx + eyeW - cut, ey, animBgColor);
  tft.fillTriangle(rx, ey, rx + cut, ey, rx, ey + cut, animBgColor);
}

void drawExprDrop(int16_t cx, int16_t cy, int16_t r, int16_t tip) {
  tft.fillCircle(cx, cy, r, C_WHITE);
  tft.fillTriangle(cx - r, cy, cx + r, cy, cx, cy - tip, C_WHITE);
}

void drawExprCrying(uint8_t frame) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY();
  const int16_t cut = eyeH * 45 / 100;
  tft.fillRect(lx, ey, eyeW, eyeH, C_BLACK);
  tft.fillRect(rx, ey, eyeW, eyeH, C_BLACK);
  tft.fillTriangle(lx, ey, lx + cut, ey, lx, ey + cut, animBgColor);
  tft.fillTriangle(rx + eyeW, ey, rx + eyeW - cut, ey, rx + eyeW, ey + cut, animBgColor);
  const uint8_t f = frame % 4;
  drawExprDrop(lx + eyeW / 2, ey + eyeH + (8 + f * 5) * eyeH / 60,
               max((int16_t)1, (int16_t)((4 - f) * eyeW / 30)),
               max((int16_t)1, (int16_t)((4 - f) * eyeH / 30)));
  const uint8_t rightFrame = (frame + 2) % 4;
  drawExprDrop(rx + eyeW / 2, ey + eyeH + (8 + rightFrame * 5) * eyeH / 60,
               max((int16_t)1, (int16_t)((4 - rightFrame) * eyeW / 30)),
               max((int16_t)1, (int16_t)((4 - rightFrame) * eyeH / 30)));
}

// heart eyes — two circles + triangle per eye, width ~1.6*w
void drawExprHeart(uint8_t frame) {
  tft.fillScreen(animBgColor);
  const int16_t scale = (frame & 1) ? 110 : 90;
  const int16_t r = max((int16_t)1, (int16_t)((eyeW * 4 * scale + 500) / 1000));
  // 独立缩放心尖，避免窄眼参数下圆半径取整后两帧完全相同。
  const int16_t tip = max((int16_t)1, (int16_t)((eyeW * 8 * scale + 500) / 1000));
  const int16_t join = max((int16_t)1, (int16_t)(r / 6));
  const int16_t lcx = eyeLX(0) + eyeW / 2, rcx = eyeRX(0) + eyeW / 2;
  const int16_t cy = eyeCY();
  const int16_t cxs[2] = {lcx, rcx};
  for (uint8_t i = 0; i < 2; i++) {
    const int16_t cx = cxs[i];
    tft.fillCircle(cx - r, cy - r / 2, r, C_BLACK);
    tft.fillCircle(cx + r, cy - r / 2, r, C_BLACK);
    tft.fillTriangle(cx - 2 * r, cy - r / 2 + join, cx + 2 * r, cy - r / 2 + join,
                     cx, cy + tip, C_BLACK);
  }
}

// think — wave bars (borrowed from upstream clawd_mochi "thinking", scaled to
// runtime eye params; upstream pixel values are ratios of EYE_W=30 / EYE_H=60)
void drawExprWave(uint8_t frame) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY();
  static const int8_t INS_L[4][3] = {{0, 8, 16}, {6, 0, 10}, {12, 4, 0}, {8, 0, 12}};
  static const int8_t INS_R[4][3] = {{16, 8, 0}, {10, 0, 6}, {0, 4, 12}, {12, 0, 8}};
  static const int8_t TOP[3] = {6, 22, 38};   // band tops, /60 of eyeH
  static const int8_t BHT[3] = {14, 14, 12};  // band heights, /60 of eyeH
  const uint8_t f = frame & 3;
  for (uint8_t b = 0; b < 3; b++) {
    const int16_t by = ey + TOP[b] * eyeH / 60;
    const int16_t bh = BHT[b] * eyeH / 60;
    const int16_t il = INS_L[f][b] * eyeW / 30;
    const int16_t ir = INS_R[f][b] * eyeW / 30;
    tft.fillRect(lx + il, by, eyeW - il, bh, C_BLACK);
    tft.fillRect(rx + ir, by, eyeW - ir, bh, C_BLACK);
  }
}

// executing — shrink-scan rect (borrowed from upstream "executing"): left eye
// erodes from its left edge, right eye from its right edge
void drawExprExecuting(uint8_t frame) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), ey = eyeY();
  const int16_t inset = (frame % 3) * 3 * eyeW / 30;
  const int16_t top   = (frame % 2) * 4 * eyeH / 60;
  tft.fillRect(lx + inset, ey + top, eyeW - inset, eyeH - top, C_BLACK);
  tft.fillRect(rx, ey + top, eyeW - inset, eyeH - top, C_BLACK);
}

void drawExprShocked(uint8_t frame) {
  tft.fillScreen(animBgColor);
  const int16_t nw = eyeW * 8 / 5, nh = eyeH * 8 / 5;
  const int16_t cy = eyeCY() + ((frame & 1) ? -1 : 1) * eyeH / 12;
  const int16_t r = max((int16_t)1, (int16_t)(eyeW / 5));
  tft.fillRoundRect(eyeLX(0) + eyeW / 2 - nw / 2, cy - nh / 2, nw, nh, r, C_BLACK);
  tft.fillRoundRect(eyeRX(0) + eyeW / 2 - nw / 2, cy - nh / 2, nw, nh, r, C_BLACK);
}

void drawExprCool(uint8_t frame) {
  tft.fillScreen(animBgColor);
  const int16_t lx = eyeLX(0), rx = eyeRX(0), cy = eyeCY();
  const int16_t lensW = eyeW * 4 / 3, lensH = eyeH / 2;
  tft.fillRect(lx + eyeW, cy - eyeH / 18, eyeGap,
               max((int16_t)1, (int16_t)(eyeH / 9)), C_BLACK);
  const int16_t xs[2] = {(int16_t)(lx - eyeW / 6), (int16_t)(rx - eyeW / 6)};
  for (uint8_t i = 0; i < 2; i++) {
    tft.fillRoundRect(xs[i], cy - lensH / 2, lensW, lensH, eyeW / 6, C_BLACK);
    const int16_t glint = xs[i] + eyeW / 3 + (frame & 1) * eyeW / 4;
    const int16_t thk = max((int16_t)1, (int16_t)(eyeW / 15));
    for (int16_t t = 0; t < thk; t++) {
      tft.drawLine(glint + t, cy + lensH / 3,
                    glint + eyeW / 3 + t, cy - lensH / 3, C_WHITE);
    }
  }
}

void drawExprEmbarrassed(uint8_t frame) {
  tft.fillScreen(animBgColor);
  const int16_t glance = eyeW * ((frame & 1) ? 1 : 2) / 5;
  const int16_t ey = eyeY(), cy = ey + eyeH + eyeH / 12;
  const int16_t xs[2] = {eyeLX(0), eyeRX(0)};
  const int16_t thk = max((int16_t)1, (int16_t)(eyeW / 10));
  for (uint8_t i = 0; i < 2; i++) {
    tft.fillRect(xs[i] + glance, ey + eyeH / 8, eyeW, eyeH * 3 / 4, C_BLACK);
    for (uint8_t line = 0; line < 2; line++) {
      const int16_t x = xs[i] + eyeW / 4 + line * eyeW / 3;
      for (int16_t t = 0; t < thk; t++) {
        tft.drawLine(x + t, cy + eyeH / 10, x + eyeW / 6 + t, cy, C_RED);
      }
    }
  }
}

void drawExprDelicious(uint8_t frame) {
  tft.fillScreen(animBgColor);
  const int16_t cy = eyeCY();
  tft.fillRect(eyeLX(0), cy, eyeW, eyeH / 2, C_BLACK);
  tft.fillRect(eyeRX(0), cy, eyeW, eyeH / 2, C_BLACK);
  const int16_t mx = (eyeLX(0) + eyeRX(0) + eyeW) / 2;
  const int16_t my = eyeY() + eyeH * 9 / 10;
  tft.fillRect(mx - eyeW / 4, my, eyeW / 2,
               max((int16_t)1, (int16_t)(eyeH / 15)), C_BLACK);
  const uint8_t f = frame % 4;
  drawExprDrop(mx + eyeW / 4, my + (8 + f * 5) * eyeH / 60,
               max((int16_t)1, (int16_t)((4 - f) * eyeW / 30)),
               max((int16_t)1, (int16_t)((4 - f) * eyeH / 30)));
}

uint8_t exprFrameCount(uint8_t e) {
  switch (e) {
    case EXPR_THINK: case EXPR_CRYING:
    case EXPR_DELICIOUS: return 4;
    case EXPR_EXECUTING: return 3;
    case EXPR_SLEEPY: case EXPR_HEART:
    case EXPR_SHOCKED: case EXPR_COOL:
    case EXPR_EMBARRASSED: return 2;
    default: return 0;
  }
}

// 帧间隔按各表情的动作节奏设置，避免整屏重绘挤占 HTTP 处理。
uint16_t exprFramePeriod(uint8_t e) {
  switch (e) {
    case EXPR_THINK: return EXPR_WAVE_MS;
    case EXPR_EXECUTING: return EXPR_SCAN_MS;
    case EXPR_SHOCKED: return 90;
    case EXPR_HEART: return 250;
    case EXPR_SLEEPY: return 600;
    case EXPR_COOL: return 350;
    case EXPR_EMBARRASSED: return 400;
    default: return 200;
  }
}

void drawCurrentExpr() {
  switch (currentExpr) {
    case EXPR_NORMAL:    drawNormalEyes();      break;
    case EXPR_SQUISH:    drawSquishEyes();      break;
    case EXPR_HAPPY:     drawExprHappy();       break;
    case EXPR_SLEEPY:    drawExprSleepy(exprFrame); break;
    case EXPR_SURPRISED: drawExprSurprised(0);  break;
    case EXPR_WINK:      drawExprWink();          break;
    case EXPR_DIZZY:     drawExprDizzy(0);      break;
    case EXPR_ANGRY:     drawExprAngry();       break;
    case EXPR_HEART:     drawExprHeart(exprFrame); break;
    case EXPR_THINK:     drawExprWave(exprFrame);      break;
    case EXPR_EXECUTING: drawExprExecuting(exprFrame); break;
    case EXPR_CRYING:    drawExprCrying(exprFrame); break;
    case EXPR_SHOCKED:   drawExprShocked(exprFrame); break;
    case EXPR_COOL:      drawExprCool(exprFrame); break;
    case EXPR_EMBARRASSED: drawExprEmbarrassed(exprFrame); break;
    case EXPR_DELICIOUS: drawExprDelicious(exprFrame); break;
  }
}

void startExpr(uint8_t e) {
  cancelBlinkFx();
  idlePhase = IDLE_NONE;
  currentView = VIEW_EYES_NORMAL;
  currentExpr = e;
  exprFrame = 0;
  busy = true;
  switch (e) {
    case EXPR_SURPRISED:
      for (uint8_t i = 0; i < 4; i++) {
        drawExprSurprised((i & 1) ? -6 : 6);
        delay(60);
      }
      drawExprSurprised(0);
      break;
    case EXPR_DIZZY:
      for (uint8_t i = 0; i < 4; i++) {
        drawExprDizzy((i & 1) ? -4 : 4);
        delay(60);
      }
      drawExprDizzy(0);
      break;
    default:
      drawCurrentExpr();
      break;
  }
  busy = false;
  nextFrameAt = millis() + exprFramePeriod(e);
  exprEndAt = millis() + (e == EXPR_THINK ? EXPR_THINK_HOLD_MS : EXPR_HOLD_MS);
  resetIdleTimers();
}

void exprTick() {
  if (busy || termMode) return;
  if (currentView != VIEW_EYES_NORMAL) return;
  const uint32_t now = millis();
  const uint8_t frames = exprFrameCount(currentExpr);
  if (frames > 0 && (int32_t)(now - nextFrameAt) >= 0) {
    exprFrame = (exprFrame + 1) % frames;
    drawCurrentExpr();
    nextFrameAt = now + exprFramePeriod(currentExpr);
  }
  const bool isTemp = currentExpr >= EXPR_HAPPY;
  if (isTemp && !napping && (int32_t)(now - exprEndAt) >= 0) {
    currentExpr = EXPR_NORMAL;
    drawNormalEyes();
    startBlinkFx(1);
    resetIdleTimers();
  }
}

// ═════════════════════════════════════════════════════════════
//  IDLE ENGINE — non-blocking stepper (no delay / no handleClient)
// ═════════════════════════════════════════════════════════════

void startNap() {
  napping = true;
  startExpr(EXPR_SLEEPY);
}

void startIdleBlink() {
  // 15% chance of a double blink
  startBlinkFx((esp_random() % 100) < 15 ? 2 : 1);
  nextBlinkAt = millis() + randRange(IDLE_BLINK_MIN_MS, IDLE_BLINK_MAX_MS);
}

void startIdleLook() {
  const int16_t mx = (DISP_W - (2 * eyeW + eyeGap)) / 2;
  const int16_t pan = (int16_t)min((int16_t)IDLE_PAN_PX, mx);
  if (pan < 2) {
    nextLookAt = millis() + randRange(IDLE_LOOK_MIN_MS, IDLE_LOOK_MAX_MS);
    return;
  }
  lookTarget = (esp_random() & 1) ? pan : -pan;
  lookCur = 0;
  lookStep = 0;
  lookSteps = 2 + (esp_random() % 2);   // 2-3 steps
  idlePhase = IDLE_LOOK_OUT;
  idleWaitUntil = millis();
}

void idleTick() {
  if (!idleEnabled || busy || termMode || napping || !backlightOn) {
    if (idlePhase != IDLE_NONE) { idlePhase = IDLE_NONE; cancelBlinkFx(); }
    return;
  }
  if (currentView != VIEW_EYES_NORMAL || currentExpr != EXPR_NORMAL) {
    if (idlePhase != IDLE_NONE) { idlePhase = IDLE_NONE; cancelBlinkFx(); }
    return;
  }
  const uint32_t now = millis();
  if (idlePhase == IDLE_NONE) {
    if ((int32_t)(now - lastActivityMs) >= (int32_t)SLEEP_AFTER_MS) {
      startNap();
      return;
    }
    if (blinkFxOn) return;  // let the blink finish before new effects
    if ((int32_t)(now - nextBlinkAt)  >= 0) { startIdleBlink(); return; }
    if ((int32_t)(now - nextLookAt)   >= 0) { startIdleLook();  return; }
    if ((int32_t)(now - nextSquintAt) >= 0) {
      drawSquintBand();
      idlePhase = IDLE_SQUINT;
      idleWaitUntil = now + 1500;
      return;
    }
    return;
  }
  switch (idlePhase) {
    case IDLE_LOOK_OUT:
    case IDLE_LOOK_BACK: {
      if ((int32_t)(now - idleWaitUntil) < 0) return;
      lookStep++;
      const int16_t from = (idlePhase == IDLE_LOOK_OUT) ? 0 : lookTarget;
      const int16_t to   = (idlePhase == IDLE_LOOK_OUT) ? lookTarget : 0;
      lookCur = from + (to - from) * (int32_t)lookStep / lookSteps;
      drawEyesBand(lookCur, false);
      if (lookStep >= lookSteps) {
        if (idlePhase == IDLE_LOOK_OUT) {
          idlePhase = IDLE_LOOK_DWELL;
          idleWaitUntil = now + randRange(600, 1200);
        } else {
          idlePhase = IDLE_NONE;
          lookCur = 0;
          nextLookAt = now + randRange(IDLE_LOOK_MIN_MS, IDLE_LOOK_MAX_MS);
        }
      } else {
        idleWaitUntil = now + 80;
      }
      break;
    }
    case IDLE_LOOK_DWELL:
      if ((int32_t)(now - idleWaitUntil) >= 0) {
        idlePhase = IDLE_LOOK_BACK;
        lookStep = 0;
        idleWaitUntil = now;
      }
      break;
    case IDLE_SQUINT:
      if ((int32_t)(now - idleWaitUntil) >= 0) {
        drawEyesBand(0, false);
        idlePhase = IDLE_NONE;
        nextSquintAt = now + randRange(IDLE_SQUINT_MIN_MS, IDLE_SQUINT_MAX_MS);
      }
      break;
    default:
      idlePhase = IDLE_NONE;
      break;
  }
}

// ═════════════════════════════════════════════════════════════
//  WEB PAGE
// ═════════════════════════════════════════════════════════════
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Clawd Mochi</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
body{background:#1c1c20;font-family:'Courier New',monospace;color:#e8e4dc;
  display:flex;flex-direction:column;align-items:center;
  padding:20px 14px 52px;gap:14px;min-height:100vh}

.hdr{text-align:center;padding:2px 0 4px}
.mascot{font-size:15px;color:#c96a3e;line-height:1.3;font-weight:bold;
  font-family:'Courier New',monospace;display:block;letter-spacing:1px}
.sitename{font-size:10px;color:#5a5048;margin-top:8px;letter-spacing:3px}

.sec{width:100%;max-width:390px;font-size:10px;color:#8a8278;
  letter-spacing:2px;font-weight:bold;padding:0 2px}

/* Busy bar */
.busy{width:100%;max-width:390px;height:2px;background:#2e2a28;
  border-radius:1px;overflow:hidden;opacity:0;transition:opacity .2s}
.busy.show{opacity:1}
.busy-i{height:100%;width:30%;background:#c96a3e;border-radius:1px;
  animation:sl 1s linear infinite}
@keyframes sl{0%{margin-left:-30%}100%{margin-left:100%}}

/* Controls */
.ctrl{display:flex;gap:8px;width:100%;max-width:390px}
.cbtn{flex:1;background:#252428;border:1.5px solid #38343a;border-radius:10px;
  color:#b8b4ac;font-family:'Courier New',monospace;font-size:11px;font-weight:bold;
  padding:12px 4px;cursor:pointer;text-align:center;transition:all .12s}
.cbtn:active:not(:disabled){transform:scale(.94)}
.cbtn:disabled{opacity:.3;cursor:default}
.cbtn.on{border-color:#c96a3e;color:#c96a3e;background:#201408}
.cbtn.dim{border-color:#2e2a28;color:#4a4540}

/* View grid */
.vgrid{display:grid;grid-template-columns:1fr 1fr;gap:8px;width:100%;max-width:390px}
.vbtn{background:#252428;border:1.5px solid #38343a;border-radius:12px;
  color:#d8d4cc;font-family:'Courier New',monospace;
  padding:14px 6px 10px;cursor:pointer;text-align:center;
  transition:all .12s;user-select:none}
.vbtn:active:not(:disabled){transform:scale(.94)}
.vbtn:disabled{opacity:.3;cursor:default}
.vbtn .ic{font-size:20px;display:block;margin-bottom:4px;line-height:1;color:#c96a3e}
.vbtn .nm{font-size:12px;font-weight:bold;color:#e8e4dc}
.vbtn .ht{font-size:9px;color:#8a8278;margin-top:3px}
.vbtn.active{border-color:#c96a3e;background:#201408}
.vbtn[data-v="1"].active{border-color:#c96a3e;background:#201408}
.vbtn[data-v="2"].active{border-color:#4a8acd;background:#0c1628}
.vbtn[data-v="3"].active{border-color:#38343a;background:#201c18}

/* Speed slider */
.speed-row{width:100%;max-width:390px;display:flex;align-items:center;gap:10px}
.sl{font-size:10px;color:#6a6058;white-space:nowrap;min-width:36px}
input[type=range]{flex:1;accent-color:#c96a3e;cursor:pointer;height:20px}
.sv{font-size:11px;color:#c96a3e;min-width:44px;text-align:right;font-weight:bold}

/* Expressions */
.egrid{display:grid;grid-template-columns:repeat(5,1fr);gap:7px;width:100%;max-width:390px}
.ebtn{background:#252428;border:1.5px solid #38343a;border-radius:10px;
  color:#d8d4cc;font-family:'Courier New',monospace;font-size:10px;font-weight:bold;
  padding:9px 2px 7px;cursor:pointer;text-align:center;transition:all .12s;
  user-select:none;line-height:1.4}
.ebtn .ic{font-size:13px;display:block;color:#c96a3e}
.ebtn:active:not(:disabled){transform:scale(.94)}
.ebtn:disabled{opacity:.3;cursor:default}
.ebtn.hi{border-color:#c96a3e;color:#c96a3e;background:#201408}

/* Eye size rows (reuses .sl/.sv) */
.erow{width:100%;max-width:390px;display:flex;align-items:center;gap:10px}

/* Terminal */
.twrap{width:100%;max-width:390px;display:none;flex-direction:column;gap:8px}
.twrap.open{display:flex}
.thdr{display:flex;justify-content:space-between;align-items:center}
.tttl{font-size:11px;color:#28b878;letter-spacing:1px;font-weight:bold}
.tx{background:#0c1e12;border:2px solid #1a4828;border-radius:9px;
  color:#28b878;font-family:'Courier New',monospace;font-size:13px;
  font-weight:bold;padding:10px 18px;cursor:pointer}
.tx:active{background:#081410}
.trow{display:flex;gap:6px}
.tin{flex:1;background:#0c1018;border:1.5px solid #1a2820;border-radius:9px;
  color:#40d880;font-family:'Courier New',monospace;font-size:15px;
  padding:11px;outline:none}
.tin::placeholder{color:#2a3828}
.tgo{background:#1a9060;border:none;border-radius:9px;color:#fff;
  font-family:'Courier New',monospace;font-size:22px;font-weight:bold;
  padding:11px 16px;cursor:pointer;min-width:52px}
.tgo:active{background:#0f6040}

/* Canvas */
.cwrap{width:100%;max-width:390px;background:#222028;border:1.5px solid #38343a;
  border-radius:12px;padding:12px;flex-direction:column;gap:10px;display:none}
.cwrap.open{display:flex}
.crow{display:flex;gap:8px}
.ci{display:flex;flex-direction:column;align-items:center;gap:4px;flex:1}
.cl{font-size:10px;color:#7a7068;letter-spacing:1px;font-weight:bold}
.cs{width:100%;height:38px;border-radius:7px;border:1.5px solid #38343a;cursor:pointer;padding:0}
.dacts{display:flex;gap:7px}
.db{flex:1;background:#1c1820;border:1.5px solid #38343a;border-radius:9px;
  color:#c0bab8;font-family:'Courier New',monospace;font-size:11px;
  font-weight:bold;padding:11px 4px;cursor:pointer;transition:all .12s}
.db:active{transform:scale(.95);background:#281838}
.db.hi{border-color:#c96a3e;color:#c96a3e}
canvas{width:100%;border-radius:8px;border:1.5px solid #38343a;
  touch-action:none;cursor:crosshair;display:block}

/* Toast */
.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);
  background:#252428;border:1.5px solid #38343a;border-radius:9px;
  font-size:12px;color:#d8d4cc;padding:7px 16px;opacity:0;
  transition:opacity .18s;pointer-events:none;white-space:nowrap;z-index:99}
.toast.show{opacity:1}
</style>
</head>
<body>

<div class="hdr">
  <span class="mascot">&#x2590;&#x259B;&#x2588;&#x2588;&#x2588;&#x259C;&#x258C;<br>&#x259C;&#x2588;&#x2588;&#x2588;&#x2588;&#x2588;&#x259B;<br>&#x2598;&#x2598;&nbsp;&#x259D;&#x259D;</span>
  <div class="sitename">CLAWD &middot; MOCHI &middot; CONTROLLER</div>
</div>

<div class="busy" id="busy"><div class="busy-i"></div></div>

<div class="sec">// controls</div>
<div class="ctrl">
  <button class="cbtn on" id="blBtn" onclick="toggleBL()">&#9728; display on</button>
  <button class="cbtn on" id="idleBtn" onclick="toggleIdle()">&#9728; idle on</button>
</div>

<div class="sec">// views</div>
<div class="vgrid">
  <button class="vbtn active" data-v="0" onclick="setView(0)">
    <span class="ic">&#9632; &#9632;</span>
    <span class="nm">Normal eyes</span>
    <span class="ht">wiggle + blink</span>
  </button>
  <button class="vbtn" data-v="1" onclick="setView(1)">
    <span class="ic">&gt; &lt;</span>
    <span class="nm">Squish eyes</span>
    <span class="ht">open / close</span>
  </button>
  <button class="vbtn" data-v="2" onclick="setView(2)">
    <span class="ic">{ }</span>
    <span class="nm">Claude Code</span>
    <span class="ht">opens terminal</span>
  </button>
  <button class="vbtn" data-v="3" onclick="toggleCanvas()">
    <span class="ic">&#11035;</span>
    <span class="nm">Canvas</span>
    <span class="ht">draw on display</span>
  </button>
</div>

<div class="sec">// speed</div>
<div class="speed-row">
  <span class="sl">slow</span>
  <input type="range" id="spd" min="1" max="3" value="1" step="1" oninput="setSpeed(this.value)">
  <span class="sv" id="spdV">slow</span>
</div>

<div class="sec">// expressions</div>
<div class="egrid">
  <button class="ebtn" onclick="sendExpr(this,'w','normal eyes')"><span class="ic">&#9632;&#9632;</span>normal</button>
  <button class="ebtn" onclick="sendExpr(this,'h','happy')"><span class="ic">^ ^</span>happy</button>
  <button class="ebtn" onclick="sendExpr(this,'z','sleepy')"><span class="ic">z Z</span>sleepy</button>
  <button class="ebtn" onclick="sendExpr(this,'o','surprised')"><span class="ic">O O</span>surprise</button>
  <button class="ebtn" onclick="sendExpr(this,'k','wink')"><span class="ic">&#9679;&#8211;</span>wink</button>
  <button class="ebtn" onclick="sendExpr(this,'x','dizzy')"><span class="ic">x x</span>dizzy</button>
  <button class="ebtn" onclick="sendExpr(this,'g','angry')"><span class="ic">\ /</span>angry</button>
  <button class="ebtn" onclick="sendExpr(this,'l','love')"><span class="ic">&#9829;&#9829;</span>love</button>
  <button class="ebtn" onclick="sendExpr(this,'t','thinking')"><span class="ic">&#8230;</span>think</button>
  <button class="ebtn" onclick="sendExpr(this,'e','executing')"><span class="ic">&#9654;&#9654;</span>executing</button>
  <button class="ebtn" onclick="sendExpr(this,'crying','crying')"><span class="ic">T T</span>crying</button>
  <button class="ebtn" onclick="sendExpr(this,'shocked','shocked')"><span class="ic">O ! O</span>shocked</button>
  <button class="ebtn" onclick="sendExpr(this,'cool','cool')"><span class="ic">&#9632;&#8212;&#9632;</span>cool</button>
  <button class="ebtn" onclick="sendExpr(this,'embarrassed','embarrassed')"><span class="ic">/ &#8226; /</span>embarrassed</button>
  <button class="ebtn" onclick="sendExpr(this,'delicious','delicious')"><span class="ic">&#8211; &#164;</span>delicious</button>
</div>

<div class="sec">// eye size</div>
<div class="erow"><span class="sl">width</span><input type="range" id="ew" min="10" max="80" value="30" oninput="onEyeInput()"><span class="sv" id="ewV">30</span></div>
<div class="erow"><span class="sl">height</span><input type="range" id="eh" min="20" max="120" value="60" oninput="onEyeInput()"><span class="sv" id="ehV">60</span></div>
<div class="erow"><span class="sl">gap</span><input type="range" id="egap" min="20" max="180" value="120" oninput="onEyeInput()"><span class="sv" id="egapV">120</span></div>
<div class="erow"><span class="sl">off X</span><input type="range" id="eox" min="-40" max="40" value="0" oninput="onEyeInput()"><span class="sv" id="eoxV">0</span></div>
<div class="erow"><span class="sl">off Y</span><input type="range" id="eoy" min="-60" max="80" value="40" oninput="onEyeInput()"><span class="sv" id="eoyV">40</span></div>
<div class="ctrl">
  <button class="cbtn" id="eyeSave" onclick="eyeSaveCfg()">save eyes</button>
  <button class="cbtn" id="eyeReset" onclick="eyeResetCfg()">reset eyes</button>
</div>

<div class="ctrl">
  <div class="ci" style="flex:1;display:flex;flex-direction:column;gap:4px;align-items:stretch">
    <span class="cl" style="font-size:10px;color:#8a8278;letter-spacing:1px;font-weight:bold;text-align:center">BACKGROUND</span>
    <input type="color" class="cs" id="bgCol" value="#da1100" oninput="onBgChange(this.value)">
  </div>
  <div class="ci" style="flex:1;display:flex;flex-direction:column;gap:4px;align-items:stretch">
    <span class="cl" style="font-size:10px;color:#8a8278;letter-spacing:1px;font-weight:bold;text-align:center">PEN COLOR</span>
    <input type="color" class="cs" id="penCol" value="#000000">
  </div>
</div>

<div class="sec">// terminal</div>
<div class="twrap" id="twrap">
  <div class="thdr">
    <span class="tttl">&#9658; clawd:~$</span>
    <button class="tx" onclick="closeTerm()">&#x2715; exit terminal</button>
  </div>
  <div class="trow">
    <input class="tin" id="tin" type="text" placeholder="type here..."
           autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false">
    <button class="tgo" onclick="termEnter()">&#8629;</button>
  </div>
</div>

<div class="cwrap" id="cwrap">
  <div class="dacts">
    <button class="db hi" onclick="clearAll()">&#11035; clear</button>
    <button class="db" style="border-color:#28b878;color:#28b878" onclick="toggleCanvas()">&#10003; done</button>
  </div>
  <canvas id="cvs" width="240" height="240"></canvas>
</div>

<div class="toast" id="toast"></div>

<script>
let activeView  = 0;
let termOpen    = false;
let canvasOpen  = false;
let blOn        = true;
let isBusy      = false;
let drawing     = false;
let lastX = 0, lastY = 0;
let tt;

const spdLabels = ['','slow','normal','fast'];

// ── Toast ──────────────────────────────────────────────────────
function toast(msg, ok=true) {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.style.borderColor = ok ? '#28b878' : '#c96a3e';
  el.classList.add('show');
  clearTimeout(tt);
  tt = setTimeout(() => el.classList.remove('show'), 1300);
}

// ── Busy ────────────────────────────────────────────────────────
function setBusy(b) {
  isBusy = b;
  document.getElementById('busy').classList.toggle('show', b);
  const locked = b || termOpen;
  document.querySelectorAll('.vbtn').forEach(el => {
    // when canvas open, keep canvas btn (data-v=3) active so user can exit
    el.disabled = canvasOpen ? parseInt(el.dataset.v) !== 3 : locked;
  });
  document.querySelectorAll('.lbtn').forEach(el => el.disabled = locked || canvasOpen);
  document.querySelectorAll('.cbtn').forEach(el => {
    if (el.id !== 'blBtn') el.disabled = locked;
  });
  document.querySelectorAll('.ebtn').forEach(el => el.disabled = locked || canvasOpen);
  ['ew','eh','egap','eox','eoy','eyeSave','eyeReset'].forEach(id => {
    const el = document.getElementById(id);
    if (el) el.disabled = locked || canvasOpen;
  });
}

// ── HTTP ────────────────────────────────────────────────────────
async function req(path) {
  try { const r = await fetch(path); return r.ok; }
  catch(e) { toast('no connection', false); return false; }
}

async function waitNotBusy() {
  for (let i = 0; i < 100; i++) {
    try {
      const r = await fetch('/state');
      const j = await r.json();
      if (!j.busy) return;
    } catch(e) {}
    await new Promise(r => setTimeout(r, 150));
  }
}

// ── Background colour ───────────────────────────────────────────
async function onBgChange(hex) {
  if (canvasOpen) {
    await req('/draw/clear?bg=' + encodeURIComponent(hex));
  } else {
    await req('/redraw?bg=' + encodeURIComponent(hex));
  }
  redrawCanvas(hex);
}

// ── Speed ───────────────────────────────────────────────────────
async function setSpeed(v) {
  document.getElementById('spdV').textContent = spdLabels[v];
  await req('/speed?v=' + v);
}

// ── Expressions ─────────────────────────────────────────────────
async function sendExpr(btn, k, name) {
  if (isBusy || termOpen || canvasOpen) return;
  btn.classList.add('hi');
  setTimeout(() => btn.classList.remove('hi'), 400);
  await req('/cmd?k=' + k);
  toast(name);
}

// ── Eye size (throttled: max 1 request in flight, >=120ms apart) ─
const EYE_DEFAULTS = {w:30, h:60, gap:120, ox:0, oy:40};
let eyeInFlight = false, eyeDirty = false, eyeTimer = null;

function eyeVals() {
  return {
    w:   +document.getElementById('ew').value,
    h:   +document.getElementById('eh').value,
    gap: +document.getElementById('egap').value,
    ox:  +document.getElementById('eox').value,
    oy:  +document.getElementById('eoy').value
  };
}
function updateEyeLabels() {
  const v = eyeVals();
  document.getElementById('ewV').textContent = v.w;
  document.getElementById('ehV').textContent = v.h;
  document.getElementById('egapV').textContent = v.gap;
  document.getElementById('eoxV').textContent = v.ox;
  document.getElementById('eoyV').textContent = v.oy;
}
function onEyeInput() {
  if (isBusy || termOpen || canvasOpen) return;
  updateEyeLabels();
  eyeDirty = true;
  if (!eyeInFlight && eyeTimer === null) eyeTimer = setTimeout(flushEye, 120);
}
async function flushEye() {
  eyeTimer = null;
  if (!eyeDirty || eyeInFlight) return;
  eyeDirty = false;
  eyeInFlight = true;
  const v = eyeVals();
  await req('/eye?w=' + v.w + '&h=' + v.h + '&gap=' + v.gap +
            '&ox=' + v.ox + '&oy=' + v.oy);
  eyeInFlight = false;
  if (eyeDirty && eyeTimer === null) eyeTimer = setTimeout(flushEye, 120);
}
async function eyeSaveCfg() {
  await req('/eye?save=1');
  toast('eye size saved');
}
function setEyeSliders(v) {
  document.getElementById('ew').value = v.w;
  document.getElementById('eh').value = v.h;
  document.getElementById('egap').value = v.gap;
  document.getElementById('eox').value = v.ox;
  document.getElementById('eoy').value = v.oy;
  updateEyeLabels();
}
async function eyeResetCfg() {
  await req('/eye?reset=1');
  setEyeSliders(EYE_DEFAULTS);
  toast('eye size reset');
}

// ── Idle animation toggle ───────────────────────────────────────
let idleOn = true;
async function toggleIdle() {
  idleOn = !idleOn;
  const ok = await req('/idle?on=' + (idleOn ? 1 : 0));
  if (!ok) { idleOn = !idleOn; return; }
  const b = document.getElementById('idleBtn');
  b.textContent = idleOn ? '\u25cf idle on' : '\u25cb idle off';
  b.classList.toggle('on', idleOn);
  b.classList.toggle('dim', !idleOn);
  toast(idleOn ? 'idle animation on' : 'idle animation off');
}

// ── Views ───────────────────────────────────────────────────────
async function setView(v) {
  if (isBusy || termOpen || canvasOpen) return;
  if (v === 3) { toggleCanvas(); return; }  // canvas button in grid
  const keys = ['w','s','d'];
  if (!await req('/cmd?k=' + keys[v])) return;
  activeView = v;
  document.querySelectorAll('.vbtn').forEach(b =>
    b.classList.toggle('active', parseInt(b.dataset.v) === v));
  if (v === 2) {
    termOpen = true;
    document.getElementById('twrap').classList.add('open');
    setBusy(false);   // re-run to apply termOpen lock
    setBusy(false);
    document.querySelectorAll('.vbtn,.lbtn').forEach(b => b.disabled = true);
    const cvb = document.getElementById('cvBtn'); if (cvb) cvb.disabled = true;
    document.getElementById('tin').focus();
    toast('terminal open');
    return;
  }
  setBusy(true);
  await waitNotBusy();
  setBusy(false);
}

// ── Logo animations (kept for startup, not exposed in UI) ──────

// ── Backlight ───────────────────────────────────────────────────
async function toggleBL() {
  blOn = !blOn;
  await req('/backlight?on=' + (blOn ? 1 : 0));
  const b = document.getElementById('blBtn');
  b.textContent = blOn ? '\u2600 display on' : '\u25cb display off';
  b.classList.toggle('on', blOn);
  b.classList.toggle('dim', !blOn);
}

// ── Canvas toggle ───────────────────────────────────────────────
async function toggleCanvas() {
  canvasOpen = !canvasOpen;
  document.getElementById('cwrap').classList.toggle('open', canvasOpen);
  const b = document.getElementById('cvBtn');
  if (b) { b.classList.toggle('on', canvasOpen); b.textContent = canvasOpen ? '\u2b1b canvas on' : '\u2b1b canvas'; }
  // highlight the canvas vbtn (data-v=3) in the grid
  document.querySelectorAll('.vbtn').forEach(btn =>
    btn.classList.toggle('active', canvasOpen && parseInt(btn.dataset.v) === 3));
  await req('/canvas?on=' + (canvasOpen ? 1 : 0));
  if (canvasOpen) {
    const bg = document.getElementById('bgCol').value;
    redrawCanvas(bg);
    await req('/draw/clear?bg=' + encodeURIComponent(bg));
    // lock all other buttons
    document.querySelectorAll('.vbtn,.lbtn,.ebtn').forEach(b => b.disabled = true);
    ['ew','eh','egap','eox','eoy','eyeSave','eyeReset','idleBtn'].forEach(id => {
      const el = document.getElementById(id); if (el) el.disabled = true;
    });
    toast('canvas active');
  } else {
    setBusy(false);   // re-evaluate locks
    toast('canvas off');
  }
}

// ── Terminal ────────────────────────────────────────────────────
const tin = document.getElementById('tin');
let lastVal = '';
tin.addEventListener('input', async () => {
  const cur = tin.value, prev = lastVal;
  if (cur.length > prev.length) {
    await req('/char?c=' + encodeURIComponent(cur[cur.length - 1]));
  } else if (cur.length < prev.length) {
    await req('/char?c=%08');
  }
  lastVal = cur;
});
async function termEnter() {
  await req('/char?c=%0A');
  tin.value = ''; lastVal = ''; tin.focus();
}
tin.addEventListener('keydown', e => {
  if (e.key === 'Enter') { e.preventDefault(); termEnter(); }
});
async function closeTerm() {
  await req('/cmd?k=q');
  termOpen = false;
  document.getElementById('twrap').classList.remove('open');
  setBusy(false);
  toast('terminal closed');
}

// ── Canvas drawing — send full stroke on finger lift ────────────
const cvs = document.getElementById('cvs');
const ctx = cvs.getContext('2d');
let strokePts = [];

function getPos(e) {
  const r = cvs.getBoundingClientRect();
  const sx = cvs.width / r.width, sy = cvs.height / r.height;
  const s = e.touches ? e.touches[0] : e;
  return { x: (s.clientX - r.left) * sx, y: (s.clientY - r.top) * sy };
}

function redrawCanvas(hex) {
  ctx.fillStyle = hex;
  ctx.fillRect(0, 0, cvs.width, cvs.height);
}

function startDraw(e) {
  e.preventDefault();
  drawing = true;
  strokePts = [];
  const p = getPos(e); lastX = p.x; lastY = p.y;
  strokePts.push({ x: Math.round(p.x), y: Math.round(p.y) });
  // draw dot on canvas preview only — no display send yet
  ctx.beginPath(); ctx.arc(p.x, p.y, 2, 0, Math.PI * 2);
  ctx.fillStyle = document.getElementById('penCol').value; ctx.fill();
}
function moveDraw(e) {
  if (!drawing) return; e.preventDefault();
  const p = getPos(e);
  ctx.beginPath(); ctx.moveTo(lastX, lastY); ctx.lineTo(p.x, p.y);
  ctx.strokeStyle = document.getElementById('penCol').value;
  ctx.lineWidth = 4; ctx.lineCap = 'round'; ctx.stroke();
  strokePts.push({ x: Math.round(p.x), y: Math.round(p.y) });
  lastX = p.x; lastY = p.y;
}
async function endDraw(e) {
  if (!drawing) return; drawing = false;
  if (!canvasOpen || strokePts.length < 1) return;
  const pen = document.getElementById('penCol').value.replace('#', '');
  const pts = strokePts.map(p => p.x + ',' + p.y).join(';');
  await req('/draw/stroke?pen=' + pen + '&pts=' + encodeURIComponent(pts));
  strokePts = [];
}

cvs.addEventListener('mousedown',  startDraw);
cvs.addEventListener('mousemove',  moveDraw);
cvs.addEventListener('mouseup',    endDraw);
cvs.addEventListener('mouseleave', endDraw);
cvs.addEventListener('touchstart', startDraw, {passive:false});
cvs.addEventListener('touchmove',  moveDraw,  {passive:false});
cvs.addEventListener('touchend',   endDraw);

// Clear = clear both web canvas and display
async function clearAll() {
  const bg = document.getElementById('bgCol').value;
  redrawCanvas(bg);
  await req('/draw/clear?bg=' + encodeURIComponent(bg));
  toast('cleared');
}

// Init: sync speed and backlight from ESP32, reset bg to default
(async () => {
  let bgHex = '#da1100';
  try {
    const r = await fetch('/state');
    const j = await r.json();
    // Sync speed
    const spd = j.speed || 1;
    document.getElementById('spd').value = spd;
    document.getElementById('spdV').textContent = spdLabels[spd];
    // Sync backlight
    if (j.bl === false) {
      blOn = false;
      const b = document.getElementById('blBtn');
      b.textContent = '\u25cb display off';
      b.classList.remove('on'); b.classList.add('dim');
    }
    // Sync eye size sliders
    if (j.eye) setEyeSliders(j.eye);
    // Sync idle toggle
    if (j.idle === false) {
      idleOn = false;
      const b = document.getElementById('idleBtn');
      b.textContent = '\u25cb idle off';
      b.classList.remove('on'); b.classList.add('dim');
    }
    if (j.bg) bgHex = j.bg;
  } catch(e) {}
  // Sync bg picker with the device's current animation background
  document.getElementById('bgCol').value = bgHex;
  redrawCanvas(bgHex);
})();
</script>
</body>
</html>
)rawhtml";

// ═════════════════════════════════════════════════════════════
//  WEB ROUTES
// ═════════════════════════════════════════════════════════════

void routeRoot() {
  noteActivity();
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.send_P(200, "text/html", INDEX_HTML);
}

void routeCmd() {
  noteActivity();
  if (busy) {  // no reentry during blocking animations (e.g. animLogoReveal)
    server.send(200, "application/json", "{\"busy\":1}");
    return;
  }
  if (!server.hasArg("k") || server.arg("k").isEmpty()) {
    server.send(400, "application/json", "{\"e\":1}"); return;
  }
  const String key = server.arg("k");
  char c;
  if      (key == "crying")      c = 'c';
  else if (key == "shocked")     c = 'b';
  else if (key == "cool")        c = 'v';
  else if (key == "embarrassed") c = 'm';
  else if (key == "delicious")   c = 'y';
  else                          c = key[0];

  if (termMode) {
    if (c == 'q') { termMode = false; drawCodeView(); }
    server.send(200, "application/json", "{\"ok\":1}"); return;
  }

  server.send(200, "application/json", "{\"ok\":1}");

  // Optional hold override (ms) for temp-expression keys, clamped 500..600000.
  // Applied AFTER startExpr() so it wins over the built-in 5/6 s timers.
  uint32_t holdMs = 0;
  if (server.hasArg("hold")) {
    const long h = server.arg("hold").toInt();
    holdMs = (uint32_t)constrain(h, 500L, 600000L);
  }

  switch (c) {
    case 'w': currentView = VIEW_EYES_NORMAL; currentExpr = EXPR_NORMAL; animNormalEyes(); break;
    case 's': currentView = VIEW_EYES_SQUISH; currentExpr = EXPR_SQUISH; animSquishEyes(); break;
    case 'd':
      currentView = VIEW_CODE; drawCodeView();
      termMode = true; termClear(); termFullRedraw(); break;
    case 'a':
      currentView = VIEW_EYES_NORMAL;
      currentExpr = EXPR_NORMAL;
      animLogoReveal();
      break;
    case 'i':
      currentView = VIEW_INFO; termMode = false;
      drawInfoPage();
      break;
    case 'h': startExpr(EXPR_HAPPY);     break;
    case 'z': startExpr(EXPR_SLEEPY);    break;
    case 'o': startExpr(EXPR_SURPRISED); break;
    case 'k': startExpr(EXPR_WINK);      break;
    case 'x': startExpr(EXPR_DIZZY);     break;
    case 'g': startExpr(EXPR_ANGRY);     break;
    case 'l': startExpr(EXPR_HEART);     break;
    case 't': startExpr(EXPR_THINK);     break;
    case 'e': startExpr(EXPR_EXECUTING); break;
    case 'c': startExpr(EXPR_CRYING);    break;
    case 'b': startExpr(EXPR_SHOCKED);   break;
    case 'v': startExpr(EXPR_COOL);      break;
    case 'm': startExpr(EXPR_EMBARRASSED); break;
    case 'y': startExpr(EXPR_DELICIOUS); break;
    case 'n':
      // Silent restore to normal eyes — reuses exprTick's recovery path.
      // No blocking animation; no-op outside the eye view (never yanks the
      // user out of terminal / canvas / squish views).
      if (currentView == VIEW_EYES_NORMAL && currentExpr != EXPR_NORMAL) {
        currentExpr = EXPR_NORMAL;
        drawNormalEyes();
        startBlinkFx(1);
        resetIdleTimers();
      }
      break;
  }

  if (holdMs > 0 && c && strchr("hzokxgltecbvmy", c)) {
    exprEndAt = millis() + holdMs;
  }
}

void routeChar() {
  noteActivity();
  if (!termMode) { server.send(200, "application/json", "{\"ok\":1}"); return; }
  const String val = server.arg("c");
  if (val.length() > 0) termAddChar(val[0]);
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeSpeed() {
  noteActivity();
  if (server.hasArg("v")) animSpeed = constrain(server.arg("v").toInt(), 1, 3);
  server.send(200, "application/json", "{\"ok\":1}");
}

// /eye — set eye geometry (w/h/gap/ox/oy), save=1 writes NVS, reset=1 restores defaults
void routeEye() {
  noteActivity();
  if (server.hasArg("reset") && server.arg("reset") == "1") {
    eyeW = 30; eyeH = 60; eyeGap = 120; eyeOx = 0; eyeOy = 40;
    clampEyeParams();
  }
  if (server.hasArg("w"))   eyeW   = server.arg("w").toInt();
  if (server.hasArg("h"))   eyeH   = server.arg("h").toInt();
  if (server.hasArg("gap")) eyeGap = server.arg("gap").toInt();
  if (server.hasArg("ox"))  eyeOx  = server.arg("ox").toInt();
  if (server.hasArg("oy"))  eyeOy  = server.arg("oy").toInt();
  clampEyeParams();
  if (server.hasArg("save") && server.arg("save") == "1") saveEyeParams();  // NVS writes only on explicit save
  const bool eyeView = (currentView == VIEW_EYES_NORMAL || currentView == VIEW_EYES_SQUISH);
  if (!busy && eyeView) drawCurrentExpr();
  server.send(200, "application/json", "{\"ok\":1}");
}

// /idle?on=1|0 — toggle idle animation (persisted to NVS immediately)
void routeIdle() {
  noteActivity();
  if (server.hasArg("on")) {
    idleEnabled = (server.arg("on") == "1");
    prefs.begin("mochi", false);
    prefs.putBool("idle", idleEnabled);
    prefs.end();
    if (idleEnabled) resetIdleTimers();
    else { idlePhase = IDLE_NONE; cancelBlinkFx(); }
  }
  String j = "{\"ok\":1,\"idle\":";
  j += idleEnabled ? "true" : "false";
  j += "}";
  server.send(200, "application/json", j);
}

// /redraw?bg=hex — set animBg and immediately redraw current view
void routeRedraw() {
  noteActivity();
  if (server.hasArg("bg")) {
    animBgColor = hexToRgb565(server.arg("bg"));
    drawBgColor = animBgColor;
  }
  switch (currentView) {
    case VIEW_EYES_NORMAL:
    case VIEW_EYES_SQUISH: drawCurrentExpr(); break;
    case VIEW_CODE:        drawCodeView();   break;
    case VIEW_INFO:        drawInfoPage();   break;
    case VIEW_DRAW:        tft.fillScreen(drawBgColor); break;
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeCanvas() {
  noteActivity();
  const bool on = server.hasArg("on") && server.arg("on") == "1";
  if (on) { currentView = VIEW_DRAW; tft.fillScreen(drawBgColor); }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeDrawClear() {
  noteActivity();
  const String bg = server.hasArg("bg") ? server.arg("bg") : "#aa4818";
  drawBgColor = hexToRgb565(bg);
  animBgColor = drawBgColor;  // keep in sync
  currentView = VIEW_DRAW; termMode = false;
  tft.fillScreen(drawBgColor);
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeDrawStroke() {
  noteActivity();
  if (!server.hasArg("pts") || !server.hasArg("pen")) {
    server.send(200, "application/json", "{\"ok\":1}"); return;
  }
  const uint16_t color = hexToRgb565(server.arg("pen"));
  const String   data  = server.arg("pts");
  currentView = VIEW_DRAW;

  struct Pt { int16_t x, y; };
  Pt prev = {-1, -1};
  int start = 0;
  while (start < (int)data.length()) {
    int semi = data.indexOf(';', start);
    if (semi == -1) semi = data.length();
    String entry = data.substring(start, semi);
    const int comma = entry.indexOf(',');
    if (comma > 0) {
      const int16_t x = entry.substring(0, comma).toInt();
      const int16_t y = entry.substring(comma + 1).toInt();
      if (prev.x >= 0) {
        tft.drawLine(prev.x, prev.y, x, y, color);
        tft.drawLine(prev.x + 1, prev.y, x + 1, y, color);
        tft.drawLine(prev.x, prev.y + 1, x, y + 1, color);
      } else {
        tft.fillCircle(x, y, 2, color);
      }
      prev = {x, y};
    }
    start = semi + 1;
  }
  server.send(200, "application/json", "{\"ok\":1}");
}

void routeBacklight() {
  noteActivity();
  setBacklight(server.hasArg("on") && server.arg("on") == "1");
  server.send(200, "application/json", "{\"ok\":1}");
}

// Convert RGB565 back to #RRGGBB for state endpoint
String rgb565ToHex(uint16_t c) {
  uint8_t r = ((c >> 11) & 0x1F) << 3;
  uint8_t g = ((c >> 5)  & 0x3F) << 2;
  uint8_t b = (c & 0x1F) << 3;
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
  return String(buf);
}

void routeState() {
  String j = "{\"view\":"; j += currentView;
  j += ",\"busy\":";   j += busy        ? "true" : "false";
  j += ",\"term\":";   j += termMode    ? "true" : "false";
  j += ",\"bl\":";     j += backlightOn ? "true" : "false";
  j += ",\"speed\":";  j += animSpeed;
  j += ",\"idle\":";   j += idleEnabled ? "true" : "false";
  j += ",\"expr\":";   j += currentExpr;
  j += ",\"eye\":{\"w\":";  j += eyeW;
  j += ",\"h\":";      j += eyeH;
  j += ",\"gap\":";    j += eyeGap;
  j += ",\"ox\":";     j += eyeOx;
  j += ",\"oy\":";     j += eyeOy;
  j += "}";
  // Animation background as #RRGGBB — the web controller syncs its picker with it
  j += ",\"bg\":\"";  j += rgb565ToHex(animBgColor);
  // Link diagnostics — appended fields only, existing keys untouched
  j += "\",\"mode\":\"";  j += staMode ? "sta" : "ap";
  j += "\",\"ip\":\"";  j += (staMode ? WiFi.localIP() : WiFi.softAPIP()).toString();
  j += "\"";
  if (staMode) {
    j += ",\"rssi\":";  j += String(WiFi.RSSI());
  }
  j += ",\"heap\":";    j += String((uint32_t)ESP.getFreeHeap());
  j += "}";
  server.send(200, "application/json", j);
}

void routeNotFound() { server.send(404, "text/plain", "not found"); }

// ═════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  // Powered from a charger there is no USB host draining CDC writes.
  // Core default (HWCDC, 3.3.12) blocks up to 100 ms per write when the
  // TX buffer fills — cap it to zero so a stray print can never stall loop().
  Serial.setTxTimeoutMs(0);

  pinMode(TFT_BLK, OUTPUT);
  setBacklight(true);

  SPI.begin(8, -1, 10, TFT_CS);   // SCK=8, MOSI=10
  tft.init(240, 240);
  tft.setSPISpeed(40000000);
  tft.setRotation(1);
  initColours();
  loadEyeParams();
  randomSeed(esp_random());

  // ── Boot splash ────────────────────────────────────────────
  tft.fillScreen(animBgColor);
  tft.setTextColor(C_WHITE); tft.setTextSize(3);
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 - 22); tft.print("Clawd");
  tft.setCursor(DISP_W / 2 - 54, DISP_H / 2 + 14); tft.print("Mochi");
  delay(1200);

  // ── Logo shown once at startup ─────────────────────────────
  animLogoReveal();

  // ── Start WiFi (STA if enabled, else AP hotspot) ────────────
#ifdef ENABLE_HOME_WIFI
  drawWifiConnectPage();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.config(IPAddress(SCREEN_IP), IPAddress(SCREEN_GW),
              IPAddress(SCREEN_MASK), IPAddress(SCREEN_DNS));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (int32_t)(millis() - wifiStart) < (int32_t)STA_CONNECT_TIMEOUT_MS) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    staMode = true;                 // stay STA forever: watchdog reconnects
    WiFi.setAutoReconnect(true);
  } else {
    WiFi.mode(WIFI_AP);             // fallback: hotspot behaviour
    WiFi.softAP(AP_SSID, AP_PASS);
    staMode = false;
  }
#else
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  staMode = false;
#endif
  lastWifiCheckMs = millis();

  // ── Register routes BEFORE the info page so it can be
  //    dismissed early by any incoming request ─────────────────
  server.on("/",            HTTP_GET, routeRoot);
  server.on("/cmd",         HTTP_GET, routeCmd);
  server.on("/char",        HTTP_GET, routeChar);
  server.on("/speed",       HTTP_GET, routeSpeed);
  server.on("/redraw",      HTTP_GET, routeRedraw);
  server.on("/eye",         HTTP_GET, routeEye);
  server.on("/idle",        HTTP_GET, routeIdle);
  server.on("/canvas",      HTTP_GET, routeCanvas);
  server.on("/draw/clear",  HTTP_GET, routeDrawClear);
  server.on("/draw/stroke", HTTP_GET, routeDrawStroke);
  server.on("/backlight",   HTTP_GET, routeBacklight);
  server.on("/state",       HTTP_GET, routeState);
  server.onNotFound(routeNotFound);
  server.begin();

  // ── WiFi info screen, ~8 s or until first request ──────────
  drawInfoPage();
  const uint32_t infoStart = millis();
  while ((int32_t)(millis() - infoStart) < (int32_t)WIFI_INFO_MS) {
    server.handleClient();
    if (bootRequestSeen) break;
    delay(10);
  }

  // ── Wake up: eyes view, one wiggle+blink, then idle takes over ──
  currentView = VIEW_EYES_NORMAL;
  currentExpr = EXPR_NORMAL;
  animNormalEyes();
}

// ═════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════

void loop() {
  // STA watchdog: non-blocking reconnect attempt every 10 s on drop.
  if (staMode &&
      (int32_t)(millis() - lastWifiCheckMs) >= (int32_t)WIFI_WATCHDOG_MS) {
    lastWifiCheckMs = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  }
  server.handleClient();
  exprTick();
  blinkFxTick();
  idleTick();
}
