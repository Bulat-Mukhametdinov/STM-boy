/*
  SPDX-License-Identifier: MIT

  STM32 button debugger + ST7735S UI + display test + mini game + W25Q128 storage
  Arduino IDE

  Buttons:
    A = A3  -> PA3
    B = A2  -> PA2
    X = A1  -> PA1
    Y = A0  -> PA0

    Joystick:
      Up     = B5 -> PB5
      Left   = B6 -> PB6
      Right  = B7 -> PB7
      Center = B8 -> PB8
      Down   = B9 -> PB9

  Display ST7735S 128x160:
    BLK         = B10 -> PB10
    CS          = B0  -> PB0
    DC          = B1  -> PB1
    RST         = B2  -> PB2
    SDA (MOSI)  = A7  -> PA7
    SCL         = A5  -> PA5

  External Flash W25Q128:
    CS   = A4 -> PA4
    SCK  = A5 -> PA5
    MISO = A6 -> PA6
    MOSI = A7 -> PA7

  IMPORTANT:
    - TFT_CS is owned ONLY by Adafruit_ST7735 library.
    - This sketch NEVER toggles TFT_CS manually.
    - Flash code controls ONLY FLASH_CS.
*/

#include <SPI.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// -------------------- Settings --------------------
#define SERIAL_BAUD            115200
#define DEBOUNCE_MS            16
#define BUTTONS_ACTIVE_LOW     1
#define USE_INTERNAL_PULLUPS   1
#define VERBOSE_BUTTON_LOG     0

#define TEST_HOLD_MS           1200
#define GAME_HOLD_MS           1000
#define BENCH_HOLD_MS          1000

// -------------------- Screen --------------------
static const int16_t SCREEN_W = 160;
static const int16_t SCREEN_H = 128;

// -------------------- Pins --------------------
// Buttons
constexpr uint32_t BTN_A_PIN      = PA3;
constexpr uint32_t BTN_B_PIN      = PA2;
constexpr uint32_t BTN_X_PIN      = PA1;
constexpr uint32_t BTN_Y_PIN      = PA0;

constexpr uint32_t BTN_UP_PIN     = PB5;
constexpr uint32_t BTN_LEFT_PIN   = PB6;
constexpr uint32_t BTN_RIGHT_PIN  = PB7;
constexpr uint32_t BTN_CENTER_PIN = PB8;
constexpr uint32_t BTN_DOWN_PIN   = PB9;

// Display
constexpr uint32_t TFT_BLK = PB10;
constexpr uint32_t TFT_CS  = PB0;
constexpr uint32_t TFT_DC  = PB1;
constexpr uint32_t TFT_RST = PB2;

// External flash
constexpr uint32_t FLASH_CS = PA4;

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

// -------------------- Colors --------------------
const uint16_t COLOR_BG         = 0x0841;
const uint16_t COLOR_PANEL      = 0x10A2;
const uint16_t COLOR_IDLE_FILL  = 0x2104;
const uint16_t COLOR_IDLE_EDGE  = 0x7BEF;
const uint16_t COLOR_HINT       = 0xBDF7;

// -------------------- Modes --------------------
enum AppMode : uint8_t {
  MODE_HOME = 0,
  MODE_DISPLAY_TEST,
  MODE_GAME,
  MODE_BENCH_MENU,
  MODE_BENCH_RT,
  MODE_BENCH_LCD
};

AppMode appMode = MODE_HOME;

// -------------------- UI Shapes --------------------
enum ShapeType : uint8_t {
  SHAPE_CIRCLE,
  SHAPE_ROUNDRECT
};

struct Button {
  const char* name;
  char label;
  uint32_t pin;

  ShapeType shape;
  int16_t x;
  int16_t y;
  int16_t a;
  int16_t b;

  uint16_t activeColor;

  bool stableState;
  bool lastRawState;
  uint32_t lastChangeTime;
};

// Order matters
Button buttons[] = {
  {"JOY_UP",    'U', BTN_UP_PIN,     SHAPE_ROUNDRECT, 28, 28, 24, 16, ST77XX_CYAN,   false, false, 0},
  {"JOY_LEFT",  'L', BTN_LEFT_PIN,   SHAPE_ROUNDRECT, 10, 56, 24, 16, ST77XX_CYAN,   false, false, 0},
  {"JOY_RIGHT", 'R', BTN_RIGHT_PIN,  SHAPE_ROUNDRECT, 46, 56, 24, 16, ST77XX_CYAN,   false, false, 0},
  {"JOY_C",     'C', BTN_CENTER_PIN, SHAPE_CIRCLE,    40, 64, 11,  0, ST77XX_CYAN,   false, false, 0},
  {"JOY_DOWN",  'D', BTN_DOWN_PIN,   SHAPE_ROUNDRECT, 28, 84, 24, 16, ST77XX_CYAN,   false, false, 0},

  {"X",         'X', BTN_X_PIN,      SHAPE_CIRCLE,   120, 40, 11,  0, ST77XX_BLUE,   false, false, 0},
  {"Y",         'Y', BTN_Y_PIN,      SHAPE_CIRCLE,    96, 64, 11,  0, ST77XX_YELLOW, false, false, 0},
  {"A",         'A', BTN_A_PIN,      SHAPE_CIRCLE,   144, 64, 11,  0, ST77XX_GREEN,  false, false, 0},
  {"B",         'B', BTN_B_PIN,      SHAPE_CIRCLE,   120, 88, 11,  0, ST77XX_RED,    false, false, 0}
};

const size_t BUTTON_COUNT = sizeof(buttons) / sizeof(buttons[0]);

enum ButtonIndex : uint8_t {
  IDX_JOY_UP = 0,
  IDX_JOY_LEFT,
  IDX_JOY_RIGHT,
  IDX_JOY_C,
  IDX_JOY_DOWN,
  IDX_X,
  IDX_Y,
  IDX_A,
  IDX_B
};

// -------------------- Hold Triggers --------------------
uint32_t joyCenterHoldStart = 0;
bool joyCenterHoldLatched = false;

uint32_t comboABHoldStart = 0;
bool comboABHoldLatched = false;

uint32_t comboXYHoldStart = 0;
bool comboXYHoldLatched = false;

// -------------------- Display test --------------------
int8_t displayTestPage = 0;
const int8_t DISPLAY_TEST_PAGE_COUNT = 9;

// -------------------- Benchmark menu --------------------
uint8_t benchMenuSelection = 0; // 0 = RT, 1 = LCD

// -------------------- Game --------------------
const int16_t GAME_FIELD_TOP    = 18;
const int16_t GAME_FIELD_BOTTOM = 127;
const int16_t GAME_FIELD_LEFT   = 2;
const int16_t GAME_FIELD_RIGHT  = 157;

const int16_t PLAYER_W = 18;
const int16_t PLAYER_H = 6;
const int16_t PLAYER_Y = 116;

const int16_t OBST_W = 12;
const int16_t OBST_H = 8;

int16_t gamePlayerX = 0;
int16_t gameObsX = 0;
int16_t gameObsY = 0;

bool gameOver = false;
bool gameScoreStored = false;
uint16_t gameScore = 0;
uint16_t gamePrevScore = 65535;

uint32_t gameLastStepMs = 0;
uint32_t gameLastMoveRepeatMs = 0;

const uint16_t GAME_STEP_MS = 45;
const uint16_t GAME_MOVE_REPEAT_MS = 65;

// -------------------- RT Benchmark --------------------
const int16_t BENCH_HDR_H = 16;
const int16_t BENCH_VIEW_X = 4;
const int16_t BENCH_VIEW_Y = 18;
const int16_t BENCH_VIEW_W = 152;
const int16_t BENCH_VIEW_H = 92;

const uint8_t BENCH_SCALE_MIN = 2;
const uint8_t BENCH_SCALE_MAX = 6;

const int16_t BENCH_MAX_RW = BENCH_VIEW_W / BENCH_SCALE_MIN; // 76
const int16_t BENCH_MAX_RH = BENCH_VIEW_H / BENCH_SCALE_MIN; // 46

struct BenchVec3 {
  float x;
  float y;
  float z;
};

enum BenchStage : uint8_t {
  BENCH_STAGE_RENDER = 0,
  BENCH_STAGE_PRESENT
};

BenchStage benchStage = BENCH_STAGE_RENDER;

float benchAngleY = 0.0f;
float benchAngleX = 0.35f;

float benchSinY = 0.0f;
float benchCosY = 1.0f;
float benchSinX = 0.0f;
float benchCosX = 1.0f;

BenchVec3 benchCameraRo      = {0.0f, 0.0f, -3.2f};
BenchVec3 benchCameraRoLocal = {0.0f, 0.0f, -3.2f};
BenchVec3 benchLightDir      = {-0.493f, 0.699f, -0.520f};

uint32_t benchLastFpsMs = 0;
uint16_t benchFrameCount = 0;
uint16_t benchFps = 0;

bool benchNeedFullRedraw = true;
bool benchClearViewportOnNextPresent = true;

uint8_t benchRenderScale = 3;
float   benchRotationSpeed = 0.045f;
float   benchFov = 1.10f;
bool    benchAdaptiveQuality = true;
bool    benchWireOverlay = true;

uint32_t benchFrameStartMs = 0;
uint16_t benchLastFrameMs = 0;
uint8_t  benchAdaptiveCooldown = 0;

int16_t benchFrameW = 0;
int16_t benchFrameH = 0;
int16_t benchScaledW = 0;
int16_t benchScaledH = 0;
int16_t benchScreenX = 0;
int16_t benchScreenY = 0;

float benchInvFrameW = 1.0f;
float benchInvFrameH = 1.0f;
float benchAspectScale = 1.0f;

int16_t benchRenderRow = 0;
int16_t benchPresentRow = 0;

const uint8_t BENCH_RENDER_ROWS_PER_SLICE = 1;
const uint8_t BENCH_PRESENT_LINES_PER_SLICE = 2;

const uint16_t BENCH_TARGET_FRAME_MS_FAST = 95;
const uint16_t BENCH_TARGET_FRAME_MS_SLOW = 165;

const float BENCH_CUBE_HALF = 0.82f;
const float BENCH_FLOOR_Y   = -0.98f;

uint16_t benchFrameBuf[BENCH_MAX_RW * BENCH_MAX_RH];
uint16_t benchLineBuf[BENCH_VIEW_W];

// -------------------- LCD Benchmark --------------------
const int16_t LCD_BENCH_HDR_H = 16;
const int16_t LCD_BENCH_FIELD_TOP = LCD_BENCH_HDR_H;
const int16_t LCD_BENCH_FIELD_BOTTOM = SCREEN_H - 1;
const int16_t LCD_BENCH_FIELD_LEFT = 0;
const int16_t LCD_BENCH_FIELD_RIGHT = SCREEN_W - 1;

const uint8_t LCD_BENCH_MAX_BALLS = 6;

struct LCDBall {
  int16_t x;
  int16_t y;
  int16_t prevX;
  int16_t prevY;
  int8_t  vx;
  int8_t  vy;
  uint16_t color;
};

LCDBall lcdBalls[LCD_BENCH_MAX_BALLS];
uint8_t lcdBenchBallCount = 1;
uint8_t lcdBenchBallRadius = 8;
bool lcdBenchFullClear = false;

uint32_t lcdBenchLastFpsMs = 0;
uint16_t lcdBenchFrameCount = 0;
uint16_t lcdBenchFps = 0;
uint16_t lcdBenchMaxFps = 0;
uint32_t lcdBenchLastHudMs = 0;
bool lcdBenchNeedHud = true;

// ===================== W25Q128 FLASH =====================
constexpr uint32_t FLASH_TOTAL_SIZE  = 16UL * 1024UL * 1024UL;
constexpr uint32_t FLASH_SECTOR_SIZE = 4096UL;
constexpr uint32_t FLASH_PAGE_SIZE   = 256UL;

constexpr uint32_t FLASH_CFG_ADDR    = 0x000000UL;
constexpr uint32_t FLASH_LOG_START   = 0x001000UL;
constexpr uint32_t FLASH_LOG_SIZE    = 256UL * 1024UL;
constexpr uint32_t FLASH_LOG_END     = FLASH_LOG_START + FLASH_LOG_SIZE;

constexpr uint32_t FLASH_CFG_MAGIC    = 0x464C4347UL; // 'FLCG'
constexpr uint32_t FLASH_RECORD_MAGIC = 0xA55A5AA5UL;

enum LogType : uint8_t {
  LOG_BOOT   = 1,
  LOG_BUTTON = 2,
  LOG_SCORE  = 3,
  LOG_NOTE   = 4
};

struct FlashConfig {
  uint32_t magic;
  uint32_t bootCount;
  uint32_t highScore;
  uint32_t reserved;
};

struct LogRecord {
  uint32_t magic;
  uint32_t ms;
  uint8_t  type;
  uint8_t  p1;
  uint8_t  p2;
  uint8_t  p3;
  uint32_t value;
};

FlashConfig gFlashCfg = {0, 0, 0, 0};
uint32_t gFlashLogWritePtr = FLASH_LOG_START;
bool gFlashReady = false;

SPISettings flashSpiSettings(4000000, MSBFIRST, SPI_MODE0);

// -------------------- Helpers --------------------
bool readPressed(uint32_t pin) {
#if BUTTONS_ACTIVE_LOW
  return digitalRead(pin) == LOW;
#else
  return digitalRead(pin) == HIGH;
#endif
}

bool isPressed(uint8_t idx) {
  return buttons[idx].stableState;
}

void printButtonState(const Button& btn) {
#if VERBOSE_BUTTON_LOG
  Serial.print("[BTN] ");
  Serial.print(btn.name);
  Serial.print(" -> ");
  Serial.println(btn.stableState ? "PRESSED" : "RELEASED");
#else
  (void)btn;
#endif
}

void printAllStates() {
  Serial.println("------ BUTTON STATES ------");
  for (size_t i = 0; i < BUTTON_COUNT; i++) {
    Serial.print(buttons[i].name);
    Serial.print(": ");
    Serial.println(buttons[i].stableState ? "PRESSED" : "RELEASED");
  }
  Serial.println("---------------------------");
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  s - print all button states");
  Serial.println("  r - redraw current screen");
  Serial.println("  t - enter display test");
  Serial.println("  g - enter game");
  Serial.println("  b - enter benchmark menu");
  Serial.println("  f - flash info");
  Serial.println("  L - dump recent flash logs");
  Serial.println("  E - erase flash logs");
  Serial.println("  j - read JEDEC ID once");
  Serial.println("  h - help");
}

uint16_t labelColorFor(const Button& btn, bool pressed) {
  if (!pressed) return ST77XX_WHITE;

  if (btn.activeColor == ST77XX_YELLOW ||
      btn.activeColor == ST77XX_GREEN  ||
      btn.activeColor == ST77XX_CYAN) {
    return ST77XX_BLACK;
  }

  return ST77XX_WHITE;
}

void drawCenteredChar(int16_t cx, int16_t cy, char c, uint16_t color, uint8_t textSize = 2) {
  tft.setTextSize(textSize);
  tft.setTextColor(color);
  tft.setCursor(cx - (3 * textSize), cy - (4 * textSize));
  tft.write(c);
}

void drawButtonVisual(const Button& btn) {
  const bool pressed = btn.stableState;
  const uint16_t fillColor = pressed ? btn.activeColor : COLOR_IDLE_FILL;
  const uint16_t edgeColor = pressed ? ST77XX_WHITE : COLOR_IDLE_EDGE;
  const uint16_t textColor = labelColorFor(btn, pressed);

  if (btn.shape == SHAPE_CIRCLE) {
    tft.fillCircle(btn.x, btn.y, btn.a, fillColor);
    tft.drawCircle(btn.x, btn.y, btn.a, edgeColor);
    tft.drawCircle(btn.x, btn.y, btn.a + 1, edgeColor);
    drawCenteredChar(btn.x, btn.y, btn.label, textColor, 2);
  } else {
    tft.fillRoundRect(btn.x, btn.y, btn.a, btn.b, 4, fillColor);
    tft.drawRoundRect(btn.x, btn.y, btn.a, btn.b, 4, edgeColor);
    tft.drawRoundRect(btn.x - 1, btn.y - 1, btn.a + 2, btn.b + 2, 5, edgeColor);
    drawCenteredChar(btn.x + btn.a / 2, btn.y + btn.b / 2, btn.label, textColor, 2);
  }
}

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// -------------------- Benchmark math helpers --------------------
static inline float benchClamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline float benchAbs(float v) {
  return (v < 0.0f) ? -v : v;
}

static inline int benchFastFloor(float x) {
  int i = (int)x;
  return (x < (float)i) ? (i - 1) : i;
}

static inline BenchVec3 benchMake(float x, float y, float z) {
  BenchVec3 r = {x, y, z};
  return r;
}

static inline BenchVec3 benchAdd(const BenchVec3& a, const BenchVec3& b) {
  return benchMake(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline BenchVec3 benchSub(const BenchVec3& a, const BenchVec3& b) {
  return benchMake(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline BenchVec3 benchMul(const BenchVec3& a, float k) {
  return benchMake(a.x * k, a.y * k, a.z * k);
}

static inline float benchDot(const BenchVec3& a, const BenchVec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline BenchVec3 benchNorm(const BenchVec3& v) {
  float d = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
  if (d < 0.00001f) return benchMake(0, 0, 0);
  float inv = 1.0f / d;
  return benchMake(v.x * inv, v.y * inv, v.z * inv);
}

static inline BenchVec3 benchReflect(const BenchVec3& v, const BenchVec3& n) {
  float k = 2.0f * benchDot(v, n);
  return benchSub(v, benchMul(n, k));
}

// world -> local cube space using precomputed sin/cos
static inline BenchVec3 benchRotateToLocal(const BenchVec3& p) {
  float x1 = p.x * benchCosY - p.z * benchSinY;
  float z1 = p.x * benchSinY + p.z * benchCosY;

  float y2 = p.y * benchCosX + z1 * benchSinX;
  float z2 = -p.y * benchSinX + z1 * benchCosX;

  return benchMake(x1, y2, z2);
}

// local -> world
static inline BenchVec3 benchRotateToWorld(const BenchVec3& p) {
  float y1 = p.y * benchCosX - p.z * benchSinX;
  float z1 = p.y * benchSinX + p.z * benchCosX;

  float x2 = p.x * benchCosY + z1 * benchSinY;
  float z2 = -p.x * benchSinY + z1 * benchCosY;

  return benchMake(x2, y1, z2);
}

void benchUpdateFrameConstants() {
  benchSinY = sinf(benchAngleY);
  benchCosY = cosf(benchAngleY);
  benchSinX = sinf(benchAngleX);
  benchCosX = cosf(benchAngleX);
  benchCameraRoLocal = benchRotateToLocal(benchCameraRo);
}

bool benchRayBoxLocal(
  const BenchVec3& ro,
  const BenchVec3& rd,
  float halfSize,
  float& tHit,
  BenchVec3& hitNormalLocal
) {
  const float minB = -halfSize;
  const float maxB =  halfSize;

  float tmin = -1e9f;
  float tmax =  1e9f;
  BenchVec3 nmin = benchMake(0, 0, 0);

  if (benchAbs(rd.x) < 0.00001f) {
    if (ro.x < minB || ro.x > maxB) return false;
  } else {
    float t1 = (minB - ro.x) / rd.x;
    float t2 = (maxB - ro.x) / rd.x;
    BenchVec3 n1 = benchMake(-1, 0, 0);
    BenchVec3 n2 = benchMake( 1, 0, 0);
    if (t1 > t2) {
      float tt = t1; t1 = t2; t2 = tt;
      BenchVec3 nt = n1; n1 = n2; n2 = nt;
    }
    if (t1 > tmin) {
      tmin = t1;
      nmin = n1;
    }
    if (t2 < tmax) tmax = t2;
    if (tmin > tmax) return false;
  }

  if (benchAbs(rd.y) < 0.00001f) {
    if (ro.y < minB || ro.y > maxB) return false;
  } else {
    float t1 = (minB - ro.y) / rd.y;
    float t2 = (maxB - ro.y) / rd.y;
    BenchVec3 n1 = benchMake(0, -1, 0);
    BenchVec3 n2 = benchMake(0,  1, 0);
    if (t1 > t2) {
      float tt = t1; t1 = t2; t2 = tt;
      BenchVec3 nt = n1; n1 = n2; n2 = nt;
    }
    if (t1 > tmin) {
      tmin = t1;
      nmin = n1;
    }
    if (t2 < tmax) tmax = t2;
    if (tmin > tmax) return false;
  }

  if (benchAbs(rd.z) < 0.00001f) {
    if (ro.z < minB || ro.z > maxB) return false;
  } else {
    float t1 = (minB - ro.z) / rd.z;
    float t2 = (maxB - ro.z) / rd.z;
    BenchVec3 n1 = benchMake(0, 0, -1);
    BenchVec3 n2 = benchMake(0, 0,  1);
    if (t1 > t2) {
      float tt = t1; t1 = t2; t2 = tt;
      BenchVec3 nt = n1; n1 = n2; n2 = nt;
    }
    if (t1 > tmin) {
      tmin = t1;
      nmin = n1;
    }
    if (t2 < tmax) tmax = t2;
    if (tmin > tmax) return false;
  }

  if (tmax < 0.01f) return false;

  tHit = (tmin >= 0.01f) ? tmin : tmax;
  hitNormalLocal = nmin;
  return true;
}

bool benchRayFloor(const BenchVec3& ro, const BenchVec3& rd, float planeY, float& tHit) {
  if (benchAbs(rd.y) < 0.00001f) return false;
  float t = (planeY - ro.y) / rd.y;
  if (t < 0.01f) return false;
  tHit = t;
  return true;
}

bool benchShadowCube(const BenchVec3& pointWorld, const BenchVec3& lightDirWorld) {
  BenchVec3 ro = benchAdd(pointWorld, benchMul(lightDirWorld, 0.03f));
  BenchVec3 roL = benchRotateToLocal(ro);
  BenchVec3 rdL = benchRotateToLocal(lightDirWorld);

  float tHit;
  BenchVec3 nHit;
  return benchRayBoxLocal(roL, rdL, BENCH_CUBE_HALF, tHit, nHit);
}

float benchCubeEdgeFactor(const BenchVec3& hitLocal, const BenchVec3& nLocal) {
  float hs = BENCH_CUBE_HALF;
  float edge = 0.0f;

  if (benchAbs(nLocal.x) > 0.5f) {
    float e1 = 1.0f - benchAbs(benchAbs(hitLocal.y) - hs) * 28.0f;
    float e2 = 1.0f - benchAbs(benchAbs(hitLocal.z) - hs) * 28.0f;
    edge = (e1 > e2) ? e1 : e2;
  } else if (benchAbs(nLocal.y) > 0.5f) {
    float e1 = 1.0f - benchAbs(benchAbs(hitLocal.x) - hs) * 28.0f;
    float e2 = 1.0f - benchAbs(benchAbs(hitLocal.z) - hs) * 28.0f;
    edge = (e1 > e2) ? e1 : e2;
  } else {
    float e1 = 1.0f - benchAbs(benchAbs(hitLocal.x) - hs) * 28.0f;
    float e2 = 1.0f - benchAbs(benchAbs(hitLocal.y) - hs) * 28.0f;
    edge = (e1 > e2) ? e1 : e2;
  }

  return benchClamp(edge, 0.0f, 1.0f);
}

uint16_t benchShadeCubeSurface(
  const BenchVec3& rdWorld,
  const BenchVec3& hitLocal,
  const BenchVec3& nLocal,
  bool reflected
) {
  BenchVec3 nWorld = benchNorm(benchRotateToWorld(nLocal));

  float diff = benchDot(nWorld, benchLightDir);
  if (diff < 0.0f) diff = 0.0f;

  float rim = 1.0f - benchClamp(-benchDot(rdWorld, nWorld), 0.0f, 1.0f);
  rim = rim * rim;

  uint8_t br = 40, bg = 110, bb = 180;

  if (benchAbs(nLocal.x) > 0.5f) {
    br = 170; bg = 80; bb = 70;
  } else if (benchAbs(nLocal.y) > 0.5f) {
    br = 70; bg = 170; bb = 90;
  } else {
    br = 50; bg = 120; bb = 185;
  }

  if (nLocal.x < -0.5f || nLocal.y < -0.5f || nLocal.z < -0.5f) {
    br = (uint8_t)(br * 0.82f);
    bg = (uint8_t)(bg * 0.82f);
    bb = (uint8_t)(bb * 0.82f);
  }

  float lit = 0.16f + diff * 0.76f + rim * 0.14f;
  lit = benchClamp(lit, 0.0f, 1.0f);

  float edge = benchCubeEdgeFactor(hitLocal, nLocal);

  int r = (int)(br * lit);
  int g = (int)(bg * lit);
  int b = (int)(bb * lit);

  if (benchWireOverlay) {
    int add = (int)(edge * 110.0f);
    r += add;
    g += add;
    b += add;
  }

  if (reflected) {
    r = (int)(r * 0.72f);
    g = (int)(g * 0.72f);
    b = (int)(b * 0.82f);
  }

  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;

  return rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b);
}

bool benchTraceReflectedCube(
  const BenchVec3& roWorld,
  const BenchVec3& rdWorld,
  uint16_t& outColor
) {
  BenchVec3 roL = benchRotateToLocal(roWorld);
  BenchVec3 rdL = benchRotateToLocal(rdWorld);

  float tHit;
  BenchVec3 nLocal;
  if (!benchRayBoxLocal(roL, rdL, BENCH_CUBE_HALF, tHit, nLocal)) return false;

  BenchVec3 hitLocal = benchAdd(roL, benchMul(rdL, tHit));
  outColor = benchShadeCubeSurface(rdWorld, hitLocal, nLocal, true);
  return true;
}

uint16_t benchMix565(uint16_t c1, uint16_t c2, float a) {
  a = benchClamp(a, 0.0f, 1.0f);

  uint8_t r1 = ((c1 >> 11) & 0x1F) << 3;
  uint8_t g1 = ((c1 >>  5) & 0x3F) << 2;
  uint8_t b1 = ((c1 >>  0) & 0x1F) << 3;

  uint8_t r2 = ((c2 >> 11) & 0x1F) << 3;
  uint8_t g2 = ((c2 >>  5) & 0x3F) << 2;
  uint8_t b2 = ((c2 >>  0) & 0x1F) << 3;

  uint8_t r = (uint8_t)(r1 * (1.0f - a) + r2 * a);
  uint8_t g = (uint8_t)(g1 * (1.0f - a) + g2 * a);
  uint8_t b = (uint8_t)(b1 * (1.0f - a) + b2 * a);

  return rgb565(r, g, b);
}

uint16_t benchTraceScenePixel(float sx, float sy) {
  BenchVec3 ro = benchCameraRo;
  BenchVec3 rd = benchNorm(benchMake(sx * benchFov, sy * benchFov, 1.75f));

  float sky = benchClamp(0.5f * (rd.y + 1.0f), 0.0f, 1.0f);
  uint16_t skyColor = rgb565(
    (uint8_t)(8  + 22 * sky),
    (uint8_t)(14 + 36 * sky),
    (uint8_t)(24 + 78 * sky)
  );

  BenchVec3 rdL = benchRotateToLocal(rd);

  float tCube = 1e9f;
  BenchVec3 nCubeLocal = benchMake(0, 0, 0);
  bool hitCube = benchRayBoxLocal(benchCameraRoLocal, rdL, BENCH_CUBE_HALF, tCube, nCubeLocal);

  float tFloor = 1e9f;
  bool hitFloor = benchRayFloor(ro, rd, BENCH_FLOOR_Y, tFloor);

  if (hitCube && (!hitFloor || tCube < tFloor)) {
    BenchVec3 hitLocal = benchAdd(benchCameraRoLocal, benchMul(rdL, tCube));
    return benchShadeCubeSurface(rd, hitLocal, nCubeLocal, false);
  }

  if (hitFloor) {
    BenchVec3 p = benchAdd(ro, benchMul(rd, tFloor));
    BenchVec3 n = benchMake(0.0f, 1.0f, 0.0f);

    float diff = benchDot(n, benchLightDir);
    if (diff < 0.0f) diff = 0.0f;

    int ix = benchFastFloor((p.x + 12.0f) * 2.2f);
    int iz = benchFastFloor((p.z + 12.0f) * 2.2f);
    bool checker = ((ix + iz) & 1) != 0;

    uint8_t base = checker ? 92 : 46;
    bool shadow = benchShadowCube(p, benchLightDir);

    float floorLight = 0.18f + diff * 0.70f;
    if (shadow) floorLight *= 0.34f;

    uint8_t fr = (uint8_t)benchClamp(base * 0.88f * floorLight, 0.0f, 255.0f);
    uint8_t fg = (uint8_t)benchClamp(base * 0.94f * floorLight, 0.0f, 255.0f);
    uint8_t fb = (uint8_t)benchClamp((base + 10) * 1.05f * floorLight, 0.0f, 255.0f);

    uint16_t floorColor = rgb565(fr, fg, fb);

    BenchVec3 rr = benchReflect(rd, n);
    BenchVec3 rro = benchAdd(p, benchMul(n, 0.03f));

    uint16_t reflCube;
    if (rr.y > 0.0f && benchTraceReflectedCube(rro, rr, reflCube)) {
      float fres = 1.0f - benchClamp(-benchDot(rd, n), 0.0f, 1.0f);
      fres = 0.12f + fres * fres * 0.34f;
      if (checker) fres *= 0.85f;
      floorColor = benchMix565(floorColor, reflCube, fres);
    }

    float fog = benchClamp((tFloor - 1.8f) / 5.5f, 0.0f, 1.0f);
    floorColor = benchMix565(floorColor, skyColor, fog * 0.45f);

    return floorColor;
  }

  return skyColor;
}

void benchResetFramePipeline(bool clearViewport) {
  benchFrameW = BENCH_VIEW_W / benchRenderScale;
  benchFrameH = BENCH_VIEW_H / benchRenderScale;

  if (benchFrameW < 1) benchFrameW = 1;
  if (benchFrameH < 1) benchFrameH = 1;

  benchScaledW = benchFrameW * benchRenderScale;
  benchScaledH = benchFrameH * benchRenderScale;

  benchScreenX = BENCH_VIEW_X + (BENCH_VIEW_W - benchScaledW) / 2;
  benchScreenY = BENCH_VIEW_Y + (BENCH_VIEW_H - benchScaledH) / 2;

  benchInvFrameW = 1.0f / (float)benchFrameW;
  benchInvFrameH = 1.0f / (float)benchFrameH;
  benchAspectScale = (float)benchScaledW / (float)benchScaledH;

  benchRenderRow = 0;
  benchPresentRow = 0;
  benchStage = BENCH_STAGE_RENDER;
  benchFrameStartMs = millis();

  if (clearViewport) {
    benchClearViewportOnNextPresent = true;
  }
}

bool benchAdaptiveQualityStep() {
  if (!benchAdaptiveQuality) return false;

  if (benchAdaptiveCooldown > 0) {
    benchAdaptiveCooldown--;
    return false;
  }

  if (benchLastFrameMs > BENCH_TARGET_FRAME_MS_SLOW) {
    if (benchRenderScale < BENCH_SCALE_MAX) {
      benchRenderScale++;
      benchAdaptiveCooldown = 6;
      benchResetFramePipeline(true);
      return true;
    }
    return false;
  }

  if (benchLastFrameMs < BENCH_TARGET_FRAME_MS_FAST) {
    if (benchRenderScale > BENCH_SCALE_MIN) {
      benchRenderScale--;
      benchAdaptiveCooldown = 6;
      benchResetFramePipeline(true);
      return true;
    }
  }

  return false;
}

void benchDrawStaticUI() {
  tft.fillScreen(ST77XX_BLACK);

  tft.fillRect(0, 0, SCREEN_W, BENCH_HDR_H, COLOR_PANEL);
  tft.drawFastHLine(0, BENCH_HDR_H, SCREEN_W, ST77XX_WHITE);

  tft.drawRect(BENCH_VIEW_X, BENCH_VIEW_Y, BENCH_VIEW_W, BENCH_VIEW_H, ST77XX_WHITE);

  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(6, 4);
  tft.print("RT CUBE+FLOOR");

  tft.setCursor(4, 113);
  tft.print("U/D Q");

  tft.setCursor(42, 113);
  tft.print("L/R SPD");

  tft.setCursor(92, 113);
  tft.print("A/B FOV");

  tft.setCursor(4, 121);
  tft.print("X AQ");

  tft.setCursor(38, 121);
  tft.print("Y WF");

  tft.setCursor(72, 121);
  tft.print("C MENU");

  benchNeedFullRedraw = false;
}

void benchDrawHud() {
  tft.fillRect(98, 2, 58, 12, COLOR_PANEL);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(98, 4);
  tft.print(benchFps);

  tft.setCursor(116, 4);
  tft.print("Q");
  tft.print((int)benchRenderScale);

  tft.setCursor(132, 4);
  tft.print(benchAdaptiveQuality ? "A" : "-");

  tft.setCursor(140, 4);
  tft.print(benchWireOverlay ? "W" : "-");

  tft.fillRect(108, 112, 48, 16, ST77XX_BLACK);
  tft.setCursor(108, 112);
  tft.print("F");
  tft.print(benchFov, 1);
}

void benchRenderRowsSlice() {
  uint8_t rowsDone = 0;

  while (benchRenderRow < benchFrameH && rowsDone < BENCH_RENDER_ROWS_PER_SLICE) {
    float sy = 1.0f - ((((float)benchRenderRow) + 0.5f) * benchInvFrameH) * 2.0f;

    for (int16_t x = 0; x < benchFrameW; x++) {
      float sx = (((((float)x) + 0.5f) * benchInvFrameW) * 2.0f - 1.0f) * benchAspectScale;
      benchFrameBuf[benchRenderRow * BENCH_MAX_RW + x] = benchTraceScenePixel(sx, sy);
    }

    benchRenderRow++;
    rowsDone++;
  }

  if (benchRenderRow >= benchFrameH) {
    benchStage = BENCH_STAGE_PRESENT;
    benchPresentRow = 0;
  }
}

void benchBuildDisplayLine(int16_t localDisplayY) {
  int16_t srcY = localDisplayY / benchRenderScale;
  if (srcY < 0) srcY = 0;
  if (srcY >= benchFrameH) srcY = benchFrameH - 1;

  uint16_t* srcRow = &benchFrameBuf[srcY * BENCH_MAX_RW];

  for (int16_t x = 0; x < benchScaledW; x++) {
    int16_t srcX = x / benchRenderScale;
    if (srcX < 0) srcX = 0;
    if (srcX >= benchFrameW) srcX = benchFrameW - 1;
    benchLineBuf[x] = srcRow[srcX];
  }
}

void benchPresentLinesSlice(uint32_t now) {
  if (benchClearViewportOnNextPresent) {
    tft.fillRect(BENCH_VIEW_X + 1, BENCH_VIEW_Y + 1, BENCH_VIEW_W - 2, BENCH_VIEW_H - 2, ST77XX_BLACK);
    tft.drawRect(BENCH_VIEW_X, BENCH_VIEW_Y, BENCH_VIEW_W, BENCH_VIEW_H, ST77XX_WHITE);
    benchClearViewportOnNextPresent = false;
  }

  uint8_t linesDone = 0;

  while (benchPresentRow < benchScaledH && linesDone < BENCH_PRESENT_LINES_PER_SLICE) {
    benchBuildDisplayLine(benchPresentRow);

    int16_t y = benchScreenY + benchPresentRow;

    tft.startWrite();
    tft.setAddrWindow(benchScreenX, y, benchScaledW, 1);
    for (int16_t x = 0; x < benchScaledW; x++) {
      tft.writeColor(benchLineBuf[x], 1);
    }
    tft.endWrite();

    benchPresentRow++;
    linesDone++;
  }

  if (benchPresentRow >= benchScaledH) {
    benchAngleY += benchRotationSpeed;
    benchAngleX += benchRotationSpeed * 0.43f;

    if (benchAngleY > 1000.0f) benchAngleY = 0.0f;
    if (benchAngleX > 1000.0f) benchAngleX = 0.0f;

    benchUpdateFrameConstants();

    benchFrameCount++;
    benchLastFrameMs = (uint16_t)(now - benchFrameStartMs);

    uint32_t dt = now - benchLastFpsMs;
    if (dt >= 500) {
      benchFps = (uint16_t)((benchFrameCount * 1000UL) / dt);
      benchFrameCount = 0;
      benchLastFpsMs = now;
    }

    bool aqChanged = benchAdaptiveQualityStep();
    benchDrawHud();

    if (!aqChanged) {
      benchResetFramePipeline(false);
    }
  }
}

void enterRTBenchmark() {
  appMode = MODE_BENCH_RT;

  benchAngleY = 0.0f;
  benchAngleX = 0.35f;
  benchUpdateFrameConstants();

  benchLastFpsMs = millis();
  benchFrameCount = 0;
  benchFps = 0;

  benchRenderScale = 3;
  benchRotationSpeed = 0.045f;
  benchFov = 1.10f;
  benchAdaptiveQuality = true;
  benchWireOverlay = true;

  benchLastFrameMs = 0;
  benchAdaptiveCooldown = 0;

  benchNeedFullRedraw = true;
  benchClearViewportOnNextPresent = true;
  benchResetFramePipeline(true);

  Serial.println("[MODE] RT Benchmark");
  flashAppendRecord(LOG_NOTE, 5, 0, 0, 0);

  benchDrawStaticUI();
  benchDrawHud();
}

void handleRTBenchmarkPress(uint8_t idx);

// -------------------- LCD benchmark helpers --------------------
void lcdBenchFillCircleWrite(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  if (r <= 0) return;

  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

  tft.writeFastHLine(x0 - r, y0, 2 * r + 1, color);

  while (x < y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }

    x++;
    ddF_x += 2;
    f += ddF_x;

    tft.writeFastHLine(x0 - x, y0 + y, 2 * x + 1, color);
    tft.writeFastHLine(x0 - x, y0 - y, 2 * x + 1, color);
    tft.writeFastHLine(x0 - y, y0 + x, 2 * y + 1, color);
    tft.writeFastHLine(x0 - y, y0 - x, 2 * y + 1, color);
  }
}

uint16_t lcdBenchColorFor(uint8_t idx) {
  static const uint16_t colors[] = {
    ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE,
    ST77XX_YELLOW, ST77XX_CYAN, ST77XX_MAGENTA
  };
  return colors[idx % 6];
}

void lcdBenchInitBalls() {
  int16_t fieldW = LCD_BENCH_FIELD_RIGHT - LCD_BENCH_FIELD_LEFT + 1;
  int16_t fieldH = LCD_BENCH_FIELD_BOTTOM - LCD_BENCH_FIELD_TOP + 1;

  for (uint8_t i = 0; i < lcdBenchBallCount; i++) {
    LCDBall& b = lcdBalls[i];

    int16_t stepX = fieldW / (lcdBenchBallCount + 1);
    int16_t cx = stepX * (i + 1);
    int16_t cy = LCD_BENCH_FIELD_TOP + (fieldH / 2) + ((i & 1) ? -10 : 10);

    if (cx < lcdBenchBallRadius) cx = lcdBenchBallRadius;
    if (cx > SCREEN_W - 1 - lcdBenchBallRadius) cx = SCREEN_W - 1 - lcdBenchBallRadius;
    if (cy < LCD_BENCH_FIELD_TOP + lcdBenchBallRadius) cy = LCD_BENCH_FIELD_TOP + lcdBenchBallRadius;
    if (cy > LCD_BENCH_FIELD_BOTTOM - lcdBenchBallRadius) cy = LCD_BENCH_FIELD_BOTTOM - lcdBenchBallRadius;

    b.x = cx;
    b.y = cy;
    b.prevX = cx;
    b.prevY = cy;

    // небольшая разница скоростей, но всё integer
    b.vx = (i & 1) ? (2 + (i % 3)) : -(2 + (i % 3));
    b.vy = (i & 1) ? -(2 + ((i + 1) % 2)) : (2 + ((i + 1) % 2));

    b.color = lcdBenchColorFor(i);
  }
}

void lcdBenchDrawStaticUI() {
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, SCREEN_W, LCD_BENCH_HDR_H, COLOR_PANEL);
  tft.drawFastHLine(0, LCD_BENCH_HDR_H, SCREEN_W, ST77XX_WHITE);

  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(4, 4);
  tft.print("LCD BENCH");

  lcdBenchNeedHud = true;
}

void lcdBenchDrawHud() {
  tft.fillRect(70, 0, 90, LCD_BENCH_HDR_H, COLOR_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(72, 4);
  tft.print(lcdBenchFps);

  tft.setCursor(92, 4);
  tft.print("M");
  tft.print(lcdBenchMaxFps);

  tft.setCursor(126, 4);
  tft.print(lcdBenchFullClear ? "F" : "D");

  tft.setCursor(136, 4);
  tft.print((int)lcdBenchBallCount);

  tft.setCursor(146, 4);
  tft.print((int)lcdBenchBallRadius);
}

void lcdBenchClearField() {
  tft.fillRect(0, LCD_BENCH_FIELD_TOP, SCREEN_W, SCREEN_H - LCD_BENCH_FIELD_TOP, ST77XX_BLACK);
}

void enterLCDBenchmark() {
  appMode = MODE_BENCH_LCD;

  lcdBenchBallCount = 1;
  lcdBenchBallRadius = 8;
  lcdBenchFullClear = false;

  lcdBenchLastFpsMs = millis();
  lcdBenchFrameCount = 0;
  lcdBenchFps = 0;
  lcdBenchMaxFps = 0;
  lcdBenchLastHudMs = 0;
  lcdBenchNeedHud = true;

  lcdBenchDrawStaticUI();
  lcdBenchInitBalls();
  lcdBenchDrawHud();

  Serial.println("[MODE] LCD Benchmark");
  flashAppendRecord(LOG_NOTE, 6, 0, 0, 0);
}

void lcdBenchResetStats() {
  lcdBenchLastFpsMs = millis();
  lcdBenchFrameCount = 0;
  lcdBenchFps = 0;
  lcdBenchMaxFps = 0;
  lcdBenchNeedHud = true;
}

void lcdBenchRebuildScene() {
  lcdBenchClearField();
  lcdBenchInitBalls();
  lcdBenchNeedHud = true;
}

void handleLCDBenchmarkPress(uint8_t idx) {
  if (idx == IDX_JOY_C) {
    appMode = MODE_BENCH_MENU;
    Serial.println("[MODE] Benchmark menu");
    flashAppendRecord(LOG_NOTE, 4, 0, 0, 0);
    return;
  }

  if (idx == IDX_JOY_LEFT) {
    if (lcdBenchBallCount > 1) {
      lcdBenchBallCount--;
      lcdBenchRebuildScene();
    }
    return;
  }

  if (idx == IDX_JOY_RIGHT) {
    if (lcdBenchBallCount < LCD_BENCH_MAX_BALLS) {
      lcdBenchBallCount++;
      lcdBenchRebuildScene();
    }
    return;
  }

  if (idx == IDX_JOY_UP) {
    if (lcdBenchBallRadius < 16) {
      lcdBenchBallRadius++;
      lcdBenchRebuildScene();
    }
    return;
  }

  if (idx == IDX_JOY_DOWN) {
    if (lcdBenchBallRadius > 4) {
      lcdBenchBallRadius--;
      lcdBenchRebuildScene();
    }
    return;
  }

  if (idx == IDX_A) {
    lcdBenchFullClear = !lcdBenchFullClear;
    lcdBenchClearField();
    lcdBenchNeedHud = true;
    return;
  }

  if (idx == IDX_B) {
    lcdBenchResetStats();
    return;
  }
}

void updateLCDBenchmark(uint32_t now) {
  if (appMode != MODE_BENCH_LCD) return;

  tft.startWrite();

  if (lcdBenchFullClear) {
    tft.writeFillRect(0, LCD_BENCH_FIELD_TOP, SCREEN_W, SCREEN_H - LCD_BENCH_FIELD_TOP, ST77XX_BLACK);
  } else {
    for (uint8_t i = 0; i < lcdBenchBallCount; i++) {
      LCDBall& b = lcdBalls[i];
      lcdBenchFillCircleWrite(b.prevX, b.prevY, lcdBenchBallRadius, ST77XX_BLACK);
    }
  }

  for (uint8_t i = 0; i < lcdBenchBallCount; i++) {
    LCDBall& b = lcdBalls[i];

    b.prevX = b.x;
    b.prevY = b.y;

    b.x += b.vx;
    b.y += b.vy;

    if (b.x - lcdBenchBallRadius < LCD_BENCH_FIELD_LEFT) {
      b.x = LCD_BENCH_FIELD_LEFT + lcdBenchBallRadius;
      b.vx = -b.vx;
    } else if (b.x + lcdBenchBallRadius > LCD_BENCH_FIELD_RIGHT) {
      b.x = LCD_BENCH_FIELD_RIGHT - lcdBenchBallRadius;
      b.vx = -b.vx;
    }

    if (b.y - lcdBenchBallRadius < LCD_BENCH_FIELD_TOP) {
      b.y = LCD_BENCH_FIELD_TOP + lcdBenchBallRadius;
      b.vy = -b.vy;
    } else if (b.y + lcdBenchBallRadius > LCD_BENCH_FIELD_BOTTOM) {
      b.y = LCD_BENCH_FIELD_BOTTOM - lcdBenchBallRadius;
      b.vy = -b.vy;
    }

    lcdBenchFillCircleWrite(b.x, b.y, lcdBenchBallRadius, b.color);
  }

  tft.endWrite();

  lcdBenchFrameCount++;
  uint32_t dt = now - lcdBenchLastFpsMs;
  if (dt >= 500) {
    lcdBenchFps = (uint16_t)((lcdBenchFrameCount * 1000UL) / dt);
    lcdBenchFrameCount = 0;
    lcdBenchLastFpsMs = now;
    if (lcdBenchFps > lcdBenchMaxFps) lcdBenchMaxFps = lcdBenchFps;
    lcdBenchNeedHud = true;
  }

  if (lcdBenchNeedHud || (now - lcdBenchLastHudMs >= 250)) {
    lcdBenchDrawHud();
    lcdBenchLastHudMs = now;
    lcdBenchNeedHud = false;
  }
}

// -------------------- Benchmark menu --------------------
void drawBenchmarkMenu() {
  tft.fillScreen(ST77XX_BLACK);

  tft.fillRect(0, 0, SCREEN_W, 16, COLOR_PANEL);
  tft.drawFastHLine(0, 16, SCREEN_W, ST77XX_WHITE);

  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(42, 4);
  tft.print("BENCHMARK MENU");

  const uint16_t selFill = 0x2A69;
  const uint16_t normFill = 0x18C3;

  // RT box
  tft.fillRoundRect(8, 28, 68, 66, 8, benchMenuSelection == 0 ? selFill : normFill);
  tft.drawRoundRect(8, 28, 68, 66, 8, ST77XX_WHITE);

  // LCD box
  tft.fillRoundRect(84, 28, 68, 66, 8, benchMenuSelection == 1 ? selFill : normFill);
  tft.drawRoundRect(84, 28, 68, 66, 8, ST77XX_WHITE);

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 40);
  tft.print("RT BENCH");

  tft.setCursor(14, 56);
  tft.print("MCU + GFX");

  tft.setCursor(11, 68);
  tft.print("raytrace");

  tft.setCursor(95, 40);
  tft.print("LCD BENCH");

  tft.setCursor(92, 56);
  tft.print("display");

  tft.setCursor(95, 68);
  tft.print("speed");

  tft.setTextColor(COLOR_HINT);
  tft.setCursor(8, 108);
  tft.print("L/R sel");

  tft.setCursor(54, 108);
  tft.print("A/C open");

  tft.setCursor(112, 108);
  tft.print("B home");
}

void enterBenchmarkMenu() {
  appMode = MODE_BENCH_MENU;
  Serial.println("[MODE] Benchmark menu");
  flashAppendRecord(LOG_NOTE, 4, 0, 0, 0);
  drawBenchmarkMenu();
}

void handleBenchmarkMenuPress(uint8_t idx) {
  if (idx == IDX_JOY_LEFT) {
    if (benchMenuSelection > 0) benchMenuSelection--;
    drawBenchmarkMenu();
    return;
  }

  if (idx == IDX_JOY_RIGHT) {
    if (benchMenuSelection < 1) benchMenuSelection++;
    drawBenchmarkMenu();
    return;
  }

  if (idx == IDX_A || idx == IDX_JOY_C) {
    if (benchMenuSelection == 0) {
      enterRTBenchmark();
    } else {
      enterLCDBenchmark();
    }
    return;
  }

  if (idx == IDX_B) {
    appMode = MODE_HOME;
    Serial.println("[MODE] Home");
    flashAppendRecord(LOG_NOTE, 2, 0, 0, 0);
    redrawHomeUI();
  }
}

// -------------------- RT benchmark input --------------------
void handleRTBenchmarkPress(uint8_t idx) {
  if (idx == IDX_JOY_C) {
    enterBenchmarkMenu();
    return;
  }

  if (idx == IDX_JOY_UP) {
    if (benchRenderScale > BENCH_SCALE_MIN) benchRenderScale--;
    benchResetFramePipeline(true);
    return;
  }

  if (idx == IDX_JOY_DOWN) {
    if (benchRenderScale < BENCH_SCALE_MAX) benchRenderScale++;
    benchResetFramePipeline(true);
    return;
  }

  if (idx == IDX_JOY_LEFT) {
    benchRotationSpeed -= 0.01f;
    if (benchRotationSpeed < 0.0f) benchRotationSpeed = 0.0f;
    return;
  }

  if (idx == IDX_JOY_RIGHT) {
    benchRotationSpeed += 0.01f;
    if (benchRotationSpeed > 0.20f) benchRotationSpeed = 0.20f;
    return;
  }

  if (idx == IDX_A) {
    benchFov -= 0.08f;
    if (benchFov < 0.75f) benchFov = 0.75f;
    benchResetFramePipeline(false);
    return;
  }

  if (idx == IDX_B) {
    benchFov += 0.08f;
    if (benchFov > 1.85f) benchFov = 1.85f;
    benchResetFramePipeline(false);
    return;
  }

  if (idx == IDX_X) {
    benchAdaptiveQuality = !benchAdaptiveQuality;
    return;
  }

  if (idx == IDX_Y) {
    benchWireOverlay = !benchWireOverlay;
    benchResetFramePipeline(false);
    return;
  }
}

void updateRTBenchmark(uint32_t now) {
  if (appMode != MODE_BENCH_RT) return;

  if (benchNeedFullRedraw) {
    benchDrawStaticUI();
    benchDrawHud();
    benchNeedFullRedraw = false;
  }

  if (benchStage == BENCH_STAGE_RENDER) {
    benchRenderRowsSlice();
  } else {
    benchPresentLinesSlice(now);
  }
}

// ===================== FLASH LOW LEVEL =====================
inline void flashSelect() {
  digitalWrite(FLASH_CS, LOW);
}

inline void flashDeselect() {
  digitalWrite(FLASH_CS, HIGH);
}

uint8_t flashXfer(uint8_t v) {
  return SPI.transfer(v);
}

void flashWriteEnable() {
  SPI.beginTransaction(flashSpiSettings);
  flashSelect();
  flashXfer(0x06);
  flashDeselect();
  SPI.endTransaction();
}

uint8_t flashReadStatus1() {
  uint8_t s;
  SPI.beginTransaction(flashSpiSettings);
  flashSelect();
  flashXfer(0x05);
  s = flashXfer(0xFF);
  flashDeselect();
  SPI.endTransaction();
  return s;
}

void flashWaitBusy() {
  while (flashReadStatus1() & 0x01) {
    delay(1);
  }
}

uint32_t flashReadJedecId() {
  uint32_t id = 0;
  SPI.beginTransaction(flashSpiSettings);
  flashSelect();
  flashXfer(0x9F);
  id |= ((uint32_t)flashXfer(0xFF)) << 16;
  id |= ((uint32_t)flashXfer(0xFF)) << 8;
  id |= ((uint32_t)flashXfer(0xFF));
  flashDeselect();
  SPI.endTransaction();
  return id;
}

void flashReadBytes(uint32_t addr, void* dst, size_t len) {
  uint8_t* p = (uint8_t*)dst;

  SPI.beginTransaction(flashSpiSettings);
  flashSelect();
  flashXfer(0x03);
  flashXfer((addr >> 16) & 0xFF);
  flashXfer((addr >> 8) & 0xFF);
  flashXfer((addr >> 0) & 0xFF);

  while (len--) {
    *p++ = flashXfer(0xFF);
  }

  flashDeselect();
  SPI.endTransaction();
}

void flashPageProgram(uint32_t addr, const uint8_t* src, size_t len) {
  if (len == 0 || len > FLASH_PAGE_SIZE) return;

  flashWriteEnable();

  SPI.beginTransaction(flashSpiSettings);
  flashSelect();
  flashXfer(0x02);
  flashXfer((addr >> 16) & 0xFF);
  flashXfer((addr >> 8) & 0xFF);
  flashXfer((addr >> 0) & 0xFF);

  for (size_t i = 0; i < len; i++) {
    flashXfer(src[i]);
  }

  flashDeselect();
  SPI.endTransaction();

  flashWaitBusy();
}

void flashWriteBytes(uint32_t addr, const void* src, size_t len) {
  const uint8_t* p = (const uint8_t*)src;

  while (len > 0) {
    uint32_t pageOff = addr % FLASH_PAGE_SIZE;
    uint32_t chunk = FLASH_PAGE_SIZE - pageOff;
    if (chunk > len) chunk = len;

    flashPageProgram(addr, p, chunk);

    addr += chunk;
    p += chunk;
    len -= chunk;
  }
}

void flashEraseSector4K(uint32_t addr) {
  flashWriteEnable();

  SPI.beginTransaction(flashSpiSettings);
  flashSelect();
  flashXfer(0x20);
  flashXfer((addr >> 16) & 0xFF);
  flashXfer((addr >> 8) & 0xFF);
  flashXfer((addr >> 0) & 0xFF);
  flashDeselect();
  SPI.endTransaction();

  flashWaitBusy();
}

bool flashRecordIsEmpty(uint32_t addr) {
  LogRecord r;
  flashReadBytes(addr, &r, sizeof(r));
  return r.magic == 0xFFFFFFFFUL;
}

// ===================== FLASH APP LAYER =====================
void flashLoadConfig() {
  flashReadBytes(FLASH_CFG_ADDR, &gFlashCfg, sizeof(gFlashCfg));

  if (gFlashCfg.magic != FLASH_CFG_MAGIC) {
    gFlashCfg.magic = FLASH_CFG_MAGIC;
    gFlashCfg.bootCount = 0;
    gFlashCfg.highScore = 0;
    gFlashCfg.reserved = 0;

    flashEraseSector4K(FLASH_CFG_ADDR);
    flashWriteBytes(FLASH_CFG_ADDR, &gFlashCfg, sizeof(gFlashCfg));
  }
}

void flashSaveConfig() {
  flashEraseSector4K(FLASH_CFG_ADDR);
  flashWriteBytes(FLASH_CFG_ADDR, &gFlashCfg, sizeof(gFlashCfg));
}

void flashFindLogWritePtr() {
  gFlashLogWritePtr = FLASH_LOG_START;

  while (gFlashLogWritePtr + sizeof(LogRecord) <= FLASH_LOG_END) {
    if (flashRecordIsEmpty(gFlashLogWritePtr)) {
      return;
    }
    gFlashLogWritePtr += sizeof(LogRecord);
  }

  for (uint32_t a = FLASH_LOG_START; a < FLASH_LOG_END; a += FLASH_SECTOR_SIZE) {
    flashEraseSector4K(a);
  }
  gFlashLogWritePtr = FLASH_LOG_START;
}

void flashAppendRecord(uint8_t type, uint8_t p1, uint8_t p2, uint8_t p3, uint32_t value) {
  if (!gFlashReady) return;

  if (gFlashLogWritePtr + sizeof(LogRecord) > FLASH_LOG_END) {
    for (uint32_t a = FLASH_LOG_START; a < FLASH_LOG_END; a += FLASH_SECTOR_SIZE) {
      flashEraseSector4K(a);
    }
    gFlashLogWritePtr = FLASH_LOG_START;
  }

  LogRecord r;
  r.magic = FLASH_RECORD_MAGIC;
  r.ms    = millis();
  r.type  = type;
  r.p1    = p1;
  r.p2    = p2;
  r.p3    = p3;
  r.value = value;

  flashWriteBytes(gFlashLogWritePtr, &r, sizeof(r));
  gFlashLogWritePtr += sizeof(r);
}

void flashEraseLogs() {
  if (!gFlashReady) return;

  for (uint32_t a = FLASH_LOG_START; a < FLASH_LOG_END; a += FLASH_SECTOR_SIZE) {
    flashEraseSector4K(a);
  }
  gFlashLogWritePtr = FLASH_LOG_START;
  Serial.println("[FLASH] logs erased");
}

void flashDumpRecentLogs(uint16_t maxCount = 32) {
  if (!gFlashReady) return;

  const uint32_t recSize = sizeof(LogRecord);
  const uint32_t used = (gFlashLogWritePtr - FLASH_LOG_START) / recSize;

  if (used == 0) {
    Serial.println("[FLASH] no logs");
    return;
  }

  uint32_t startIndex = 0;
  if (used > maxCount) startIndex = used - maxCount;

  Serial.println("----- FLASH LOG DUMP -----");

  for (uint32_t i = startIndex; i < used; i++) {
    LogRecord r;
    uint32_t addr = FLASH_LOG_START + i * recSize;
    flashReadBytes(addr, &r, sizeof(r));

    Serial.print("#");
    Serial.print(i);
    Serial.print(" t=");
    Serial.print(r.ms);
    Serial.print(" type=");
    Serial.print(r.type);
    Serial.print(" p1=");
    Serial.print(r.p1);
    Serial.print(" p2=");
    Serial.print(r.p2);
    Serial.print(" p3=");
    Serial.print(r.p3);
    Serial.print(" value=");
    Serial.println(r.value);
  }

  Serial.println("--------------------------");
}

void flashPrintInfo() {
  if (!gFlashReady) {
    Serial.println("[FLASH] not ready");
    return;
  }

  uint32_t id = flashReadJedecId();

  Serial.println("----- FLASH INFO -----");
  Serial.print("JEDEC ID: 0x");
  Serial.println(id, HEX);
  Serial.print("Boot count: ");
  Serial.println(gFlashCfg.bootCount);
  Serial.print("High score: ");
  Serial.println(gFlashCfg.highScore);
  Serial.print("Log write ptr: 0x");
  Serial.println(gFlashLogWritePtr, HEX);
  Serial.println("----------------------");
}

void flashReadJedecSmokeTest() {
  uint32_t id = flashReadJedecId();
  Serial.print("[FLASH] JEDEC ID = 0x");
  Serial.println(id, HEX);
}

void flashBeginApp() {
  pinMode(FLASH_CS, OUTPUT);
  flashDeselect();

  uint32_t id = flashReadJedecId();
  Serial.print("[FLASH] JEDEC ID = 0x");
  Serial.println(id, HEX);

  if (id == 0 || id == 0xFFFFFFUL) {
    Serial.println("[FLASH] not detected");
    gFlashReady = false;
    return;
  }

  gFlashReady = true;

  flashLoadConfig();
  gFlashCfg.bootCount++;
  flashSaveConfig();

  flashFindLogWritePtr();
  flashAppendRecord(LOG_BOOT, 0, 0, 0, gFlashCfg.bootCount);
}

// -------------------- HOME UI --------------------
void drawHomeStaticUI() {
  tft.fillScreen(COLOR_BG);

  tft.fillRoundRect(4, 4, 72, 120, 8, COLOR_PANEL);
  tft.drawRoundRect(4, 4, 72, 120, 8, ST77XX_WHITE);

  tft.fillRoundRect(84, 4, 72, 120, 8, COLOR_PANEL);
  tft.drawRoundRect(84, 4, 72, 120, 8, ST77XX_WHITE);

  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(16, 10);
  tft.print("JOYSTICK");

  tft.setCursor(103, 10);
  tft.print("BUTTONS");

  tft.setTextColor(COLOR_HINT);
  tft.setCursor(8, 102);
  tft.print("C hold:test");

  tft.setCursor(8, 114);
  tft.print("X+Y:bench");

  tft.setCursor(92, 102);
  tft.print("A+B:game");

  tft.setCursor(92, 114);
  tft.print("HS:");
  tft.setTextColor(ST77XX_WHITE);
  tft.print(gFlashCfg.highScore);
}

void redrawHomeUI() {
  drawHomeStaticUI();
  for (size_t i = 0; i < BUTTON_COUNT; i++) {
    drawButtonVisual(buttons[i]);
  }
}

// -------------------- DISPLAY TEST --------------------
void drawCheckerboard(uint8_t cell) {
  for (int16_t y = 0; y < SCREEN_H; y += cell) {
    for (int16_t x = 0; x < SCREEN_W; x += cell) {
      bool w = (((x / cell) + (y / cell)) & 1) == 0;
      tft.fillRect(x, y, cell, cell, w ? ST77XX_WHITE : ST77XX_BLACK);
    }
  }
}

void drawColorBars() {
  const uint16_t bars[] = {
    ST77XX_WHITE, ST77XX_YELLOW, ST77XX_CYAN, ST77XX_GREEN,
    ST77XX_MAGENTA, ST77XX_RED, ST77XX_BLUE, ST77XX_BLACK
  };
  const uint8_t count = sizeof(bars) / sizeof(bars[0]);
  const int16_t barW = SCREEN_W / count;

  for (uint8_t i = 0; i < count; i++) {
    int16_t x = i * barW;
    int16_t w = (i == count - 1) ? (SCREEN_W - x) : barW;
    tft.fillRect(x, 0, w, SCREEN_H, bars[i]);
  }
}

void drawGradients() {
  for (int16_t x = 0; x < SCREEN_W; x++) {
    uint8_t v = map(x, 0, SCREEN_W - 1, 0, 255);
    uint16_t gray  = tft.color565(v, v, v);
    uint16_t red   = tft.color565(v, 0, 0);
    uint16_t green = tft.color565(0, v, 0);
    uint16_t blue  = tft.color565(0, 0, v);

    tft.drawFastVLine(x, 0, 32, red);
    tft.drawFastVLine(x, 32, 32, green);
    tft.drawFastVLine(x, 64, 32, blue);
    tft.drawFastVLine(x, 96, 32, gray);
  }
}

void drawDisplayTestInfoPage() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(28, 10);
  tft.print("DISPLAY TEST");

  tft.setCursor(8, 28);
  tft.print("RIGHT/A  - next");

  tft.setCursor(8, 40);
  tft.print("LEFT/Y   - prev");

  tft.setCursor(8, 52);
  tft.print("CENTER   - exit");

  tft.setCursor(8, 72);
  tft.print("Pages:");

  tft.setCursor(8, 84);
  tft.print("1 White / 2 Black");

  tft.setCursor(8, 96);
  tft.print("3 Red 4 Green 5 Blue");

  tft.setCursor(8, 108);
  tft.print("6 Bars 7 Chess 8 Grad");
}

void drawDisplayTestPage(int8_t page) {
  displayTestPage = page;
  if (displayTestPage < 0) displayTestPage = DISPLAY_TEST_PAGE_COUNT - 1;
  if (displayTestPage >= DISPLAY_TEST_PAGE_COUNT) displayTestPage = 0;

  Serial.print("[TEST] Page ");
  Serial.println(displayTestPage);

  switch (displayTestPage) {
    case 0: drawDisplayTestInfoPage(); break;
    case 1: tft.fillScreen(ST77XX_WHITE); break;
    case 2: tft.fillScreen(ST77XX_BLACK); break;
    case 3: tft.fillScreen(ST77XX_RED); break;
    case 4: tft.fillScreen(ST77XX_GREEN); break;
    case 5: tft.fillScreen(ST77XX_BLUE); break;
    case 6: drawColorBars(); break;
    case 7: drawCheckerboard(4); break;
    case 8: drawGradients(); break;
  }
}

void enterDisplayTest() {
  appMode = MODE_DISPLAY_TEST;
  Serial.println("[MODE] Display test");
  flashAppendRecord(LOG_NOTE, 1, 0, 0, 0);
  drawDisplayTestPage(0);
}

void handleDisplayTestPress(uint8_t idx) {
  if (idx == IDX_JOY_C) {
    appMode = MODE_HOME;
    Serial.println("[MODE] Home");
    flashAppendRecord(LOG_NOTE, 2, 0, 0, 0);
    redrawHomeUI();
    return;
  }

  if (idx == IDX_JOY_RIGHT || idx == IDX_A) {
    drawDisplayTestPage(displayTestPage + 1);
    return;
  }

  if (idx == IDX_JOY_LEFT || idx == IDX_Y) {
    drawDisplayTestPage(displayTestPage - 1);
    return;
  }
}

// -------------------- GAME --------------------
bool rectsIntersect(
  int16_t x1, int16_t y1, int16_t w1, int16_t h1,
  int16_t x2, int16_t y2, int16_t w2, int16_t h2
) {
  return !(x1 + w1 <= x2 || x2 + w2 <= x1 || y1 + h1 <= y2 || y2 + h2 <= y1);
}

void gameDrawHud() {
  tft.fillRect(0, 0, SCREEN_W, 16, ST77XX_BLACK);
  tft.drawFastHLine(0, 16, SCREEN_W, ST77XX_WHITE);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(4, 4);
  tft.print("DODGE");

  tft.setCursor(54, 4);
  tft.print("Score:");
  tft.print(gameScore);

  tft.setCursor(108, 4);
  tft.print("C:exit");
}

void gameDrawField() {
  tft.fillScreen(ST77XX_BLACK);
  gameDrawHud();
  tft.drawRect(GAME_FIELD_LEFT, GAME_FIELD_TOP,
               GAME_FIELD_RIGHT - GAME_FIELD_LEFT + 1,
               GAME_FIELD_BOTTOM - GAME_FIELD_TOP + 1,
               ST77XX_WHITE);
}

void gameDrawPlayer(int16_t x, uint16_t color) {
  tft.fillRoundRect(x, PLAYER_Y, PLAYER_W, PLAYER_H, 2, color);
}

void gameDrawObstacle(int16_t x, int16_t y, uint16_t color) {
  tft.fillRect(x, y, OBST_W, OBST_H, color);
}

void gameUpdateScoreIfNeeded() {
  if (gameScore != gamePrevScore) {
    gamePrevScore = gameScore;
    tft.fillRect(90, 4, 18, 8, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(90, 4);
    tft.print(gameScore);
  }
}

void gameSpawnObstacle() {
  gameObsX = random(GAME_FIELD_LEFT + 2, GAME_FIELD_RIGHT - OBST_W - 1);
  gameObsY = GAME_FIELD_TOP + 4;
  gameDrawObstacle(gameObsX, gameObsY, ST77XX_RED);
}

void gameShowGameOver() {
  tft.fillRoundRect(28, 40, 104, 44, 6, ST77XX_BLACK);
  tft.drawRoundRect(28, 40, 104, 44, 6, ST77XX_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_RED);
  tft.setCursor(40, 48);
  tft.print("LOSE");

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(42, 70);
  tft.print("A: restart");
}

void gameReset() {
  gameOver = false;
  gameScoreStored = false;
  gameScore = 0;
  gamePrevScore = 65535;

  gamePlayerX = (SCREEN_W - PLAYER_W) / 2;
  gameObsX = 20;
  gameObsY = GAME_FIELD_TOP + 4;

  gameLastStepMs = millis();
  gameLastMoveRepeatMs = millis();

  gameDrawField();
  gameUpdateScoreIfNeeded();
  gameDrawPlayer(gamePlayerX, ST77XX_GREEN);
  gameSpawnObstacle();

  Serial.println("[GAME] Start");
}

void enterGame() {
  appMode = MODE_GAME;
  Serial.println("[MODE] Game");
  flashAppendRecord(LOG_NOTE, 3, 0, 0, 0);
  randomSeed(micros());
  gameReset();
}

void exitToHome() {
  appMode = MODE_HOME;
  Serial.println("[MODE] Home");
  flashAppendRecord(LOG_NOTE, 2, 0, 0, 0);
  redrawHomeUI();
}

void gameMovePlayer(int8_t dx) {
  if (gameOver) return;

  int16_t newX = gamePlayerX + dx;
  if (newX < GAME_FIELD_LEFT + 2) newX = GAME_FIELD_LEFT + 2;
  if (newX > GAME_FIELD_RIGHT - PLAYER_W - 1) newX = GAME_FIELD_RIGHT - PLAYER_W - 1;

  if (newX != gamePlayerX) {
    gameDrawPlayer(gamePlayerX, ST77XX_BLACK);
    gamePlayerX = newX;
    gameDrawPlayer(gamePlayerX, ST77XX_GREEN);
  }
}

void handleGamePress(uint8_t idx) {
  if (idx == IDX_JOY_C) {
    exitToHome();
    return;
  }

  if (idx == IDX_JOY_LEFT) {
    gameMovePlayer(-6);
    return;
  }

  if (idx == IDX_JOY_RIGHT) {
    gameMovePlayer(6);
    return;
  }

  if (idx == IDX_A && gameOver) {
    gameReset();
    return;
  }
}

void updateGame(uint32_t now) {
  if (appMode != MODE_GAME) return;

  if (!gameOver && (now - gameLastMoveRepeatMs >= GAME_MOVE_REPEAT_MS)) {
    gameLastMoveRepeatMs = now;

    if (isPressed(IDX_JOY_LEFT) && !isPressed(IDX_JOY_RIGHT)) {
      gameMovePlayer(-4);
    } else if (isPressed(IDX_JOY_RIGHT) && !isPressed(IDX_JOY_LEFT)) {
      gameMovePlayer(4);
    }
  }

  if (gameOver) return;

  if (now - gameLastStepMs < GAME_STEP_MS) return;
  gameLastStepMs = now;

  gameDrawObstacle(gameObsX, gameObsY, ST77XX_BLACK);

  int16_t speed = 2 + (gameScore / 5);
  if (speed > 8) speed = 8;

  gameObsY += speed;

  if (rectsIntersect(gamePlayerX, PLAYER_Y, PLAYER_W, PLAYER_H,
                     gameObsX, gameObsY, OBST_W, OBST_H)) {
    gameDrawObstacle(gameObsX, gameObsY, ST77XX_RED);
    gameOver = true;

    if (!gameScoreStored) {
      gameScoreStored = true;
      flashAppendRecord(LOG_SCORE, 0, 0, 0, gameScore);

      if (gFlashReady && gameScore > gFlashCfg.highScore) {
        gFlashCfg.highScore = gameScore;
        flashSaveConfig();
        Serial.print("[FLASH] New high score: ");
        Serial.println(gFlashCfg.highScore);
      }
    }

    Serial.print("[GAME] Over. Score=");
    Serial.println(gameScore);
    gameShowGameOver();
    return;
  }

  if (gameObsY > (GAME_FIELD_BOTTOM - OBST_H - 1)) {
    gameScore++;
    gameUpdateScoreIfNeeded();
    gameSpawnObstacle();
  } else {
    gameDrawObstacle(gameObsX, gameObsY, ST77XX_RED);
  }
}

// -------------------- Setup --------------------
void setupButtons() {
  for (size_t i = 0; i < BUTTON_COUNT; i++) {
#if USE_INTERNAL_PULLUPS
    pinMode(buttons[i].pin, INPUT_PULLUP);
#else
    pinMode(buttons[i].pin, INPUT);
#endif

    bool state = readPressed(buttons[i].pin);
    buttons[i].lastRawState   = state;
    buttons[i].stableState    = state;
    buttons[i].lastChangeTime = millis();
  }
}

void setupDisplay() {
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);

  pinMode(FLASH_CS, OUTPUT);
  flashDeselect();

  SPI.begin();

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  redrawHomeUI();
}

// -------------------- Event Handling --------------------
void handleStableButtonChange(uint8_t idx) {
  printButtonState(buttons[idx]);
  flashAppendRecord(LOG_BUTTON, idx, 0, 0, buttons[idx].stableState ? 1 : 0);

  if (appMode == MODE_HOME) {
    drawButtonVisual(buttons[idx]);
  }

  if (!buttons[idx].stableState) {
    return;
  }

  switch (appMode) {
    case MODE_HOME:
      break;
    case MODE_DISPLAY_TEST:
      handleDisplayTestPress(idx);
      break;
    case MODE_GAME:
      handleGamePress(idx);
      break;
    case MODE_BENCH_MENU:
      handleBenchmarkMenuPress(idx);
      break;
    case MODE_BENCH_RT:
      handleRTBenchmarkPress(idx);
      break;
    case MODE_BENCH_LCD:
      handleLCDBenchmarkPress(idx);
      break;
  }
}

void scanButtons(uint32_t now) {
  for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
    bool raw = readPressed(buttons[i].pin);

    if (raw != buttons[i].lastRawState) {
      buttons[i].lastRawState = raw;
      buttons[i].lastChangeTime = now;
    }

    if ((now - buttons[i].lastChangeTime) >= DEBOUNCE_MS) {
      if (buttons[i].stableState != buttons[i].lastRawState) {
        buttons[i].stableState = buttons[i].lastRawState;
        handleStableButtonChange(i);
      }
    }
  }
}

void updateHomeHoldActions(uint32_t now) {
  if (appMode != MODE_HOME) return;

  if (isPressed(IDX_JOY_C)) {
    if (joyCenterHoldStart == 0) {
      joyCenterHoldStart = now;
    } else if (!joyCenterHoldLatched && (now - joyCenterHoldStart >= TEST_HOLD_MS)) {
      joyCenterHoldLatched = true;
      enterDisplayTest();
      return;
    }
  } else {
    joyCenterHoldStart = 0;
    joyCenterHoldLatched = false;
  }

  if (isPressed(IDX_A) && isPressed(IDX_B)) {
    if (comboABHoldStart == 0) {
      comboABHoldStart = now;
    } else if (!comboABHoldLatched && (now - comboABHoldStart >= GAME_HOLD_MS)) {
      comboABHoldLatched = true;
      enterGame();
      return;
    }
  } else {
    comboABHoldStart = 0;
    comboABHoldLatched = false;
  }

  if (isPressed(IDX_X) && isPressed(IDX_Y)) {
    if (comboXYHoldStart == 0) {
      comboXYHoldStart = now;
    } else if (!comboXYHoldLatched && (now - comboXYHoldStart >= BENCH_HOLD_MS)) {
      comboXYHoldLatched = true;
      enterBenchmarkMenu();
      return;
    }
  } else {
    comboXYHoldStart = 0;
    comboXYHoldLatched = false;
  }
}

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == 's' || c == 'S') {
      printAllStates();

    } else if (c == 'r' || c == 'R') {
      if (appMode == MODE_HOME) {
        redrawHomeUI();
      } else if (appMode == MODE_DISPLAY_TEST) {
        drawDisplayTestPage(displayTestPage);
      } else if (appMode == MODE_GAME) {
        gameDrawField();
        gameUpdateScoreIfNeeded();
        gameDrawPlayer(gamePlayerX, ST77XX_GREEN);
        gameDrawObstacle(gameObsX, gameObsY, ST77XX_RED);
        if (gameOver) gameShowGameOver();
      } else if (appMode == MODE_BENCH_MENU) {
        drawBenchmarkMenu();
      } else if (appMode == MODE_BENCH_RT) {
        benchNeedFullRedraw = true;
        benchResetFramePipeline(true);
        benchDrawStaticUI();
        benchDrawHud();
      } else if (appMode == MODE_BENCH_LCD) {
        lcdBenchDrawStaticUI();
        lcdBenchClearField();
        lcdBenchDrawHud();
      }
      Serial.println("[DISP] Redraw done");

    } else if (c == 't' || c == 'T') {
      enterDisplayTest();

    } else if (c == 'g' || c == 'G') {
      enterGame();

    } else if (c == 'b' || c == 'B') {
      enterBenchmarkMenu();

    } else if (c == 'f' || c == 'F') {
      flashPrintInfo();

    } else if (c == 'L') {
      flashDumpRecentLogs(32);

    } else if (c == 'E') {
      flashEraseLogs();

    } else if (c == 'j' || c == 'J') {
      flashReadJedecSmokeTest();

    } else if (c == 'h' || c == 'H' || c == '?') {
      printHelp();
    }
  }
}

void serviceInputFast() {
  uint32_t now = millis();
  scanButtons(now);
  updateHomeHoldActions(now);
}

// -------------------- Arduino --------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  Serial.println();
  Serial.println("STM32 button debug + ST7735S UI + display test + game + W25Q128");
  Serial.println("UART: 115200");
  Serial.println("Buttons logic: ACTIVE LOW");
  printHelp();

  setupButtons();
  setupDisplay();
  flashBeginApp();
  redrawHomeUI();
  printAllStates();
}

void loop() {
  handleSerial();

  serviceInputFast();

  uint32_t now = millis();
  updateGame(now);
  updateRTBenchmark(now);
  updateLCDBenchmark(now);

  serviceInputFast();
}
