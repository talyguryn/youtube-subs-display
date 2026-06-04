#include "display.h"

#include "app_config.h"

// Display pins (Wemos ESP-12F GPIO)
static const int PIN_SEGMENT_A = D2;   // GPIO4
static const int PIN_SEGMENT_B = D1;   // GPIO5
static const int PIN_SEGMENT_C = D6;   // GPIO12
static const int PIN_SEGMENT_D = D7;   // GPIO13
static const int PIN_SEGMENT_E = D5;   // GPIO14
static const int PIN_SEGMENT_F = D8;   // GPIO15
static const int PIN_SEGMENT_G = D0;   // GPIO16

// Digit select pins
static const int PIN_DIGIT_1 = D4;     // GPIO2  - thousands
static const int PIN_DIGIT_2 = 3;      // GPIO3  - hundreds (RX)
static const int PIN_DIGIT_3 = 1;      // GPIO1  - tens (TX)
static const int PIN_DIGIT_4 = D3;     // GPIO0  - ones

// 7-segment table (bits: dp g f e d c b a)
static const byte SEGMENT_CODES[10] = {
  0b00111111,
  0b00000110,
  0b01011011,
  0b01001111,
  0b01100110,
  0b01101101,
  0b01111101,
  0b00000111,
  0b01111111,
  0b01101111
};

// Drum transition lookup tables (digit -> code), tuned manually.
static const byte ROTATE_UP_CODES[10] = {98, 2, 97, 67, 3, 67, 99, 2, 99, 67};
static const byte ROTATE_DOWN_CODES[10] = {84, 4, 76, 76, 28, 88, 88, 68, 92, 92};

static const byte SEGMENT_CODE_BLANK = 0b00000000;
static const byte SEGMENT_CODE_E = 0b01111001;
static const byte SEGMENT_CODE_DASH = 0b01000000;

static byte gCodes[4] = {SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK};
static bool gVisible[4] = {false, false, false, false};

static int clampDisplayNumber(int number) {
  if (number < 0) return 0;
  if (number > MAX_DISPLAY_VALUE) return MAX_DISPLAY_VALUE;
  return number;
}

static void splitDigits(int number, int outDigits[4]) {
  outDigits[0] = number % 10;
  outDigits[1] = (number / 10) % 10;
  outDigits[2] = (number / 100) % 10;
  outDigits[3] = (number / 1000) % 10;
}

static int findMostSignificantDigitPos(const int digits[4]) {
  int startPos = 3;
  while (startPos > 0 && digits[startPos] == 0) {
    startPos--;
  }
  return startPos;
}

static int findVisibleStartPosForPair(const int leftDigits[4], const int rightDigits[4]) {
  int leftPos = findMostSignificantDigitPos(leftDigits);
  int rightPos = findMostSignificantDigitPos(rightDigits);
  return (leftPos > rightPos) ? leftPos : rightPos;
}

static void disableAllDigits() {
  digitalWrite(PIN_DIGIT_1, HIGH);
  digitalWrite(PIN_DIGIT_2, HIGH);
  digitalWrite(PIN_DIGIT_3, HIGH);
  digitalWrite(PIN_DIGIT_4, HIGH);
}

static void displaySegments(byte code) {
  digitalWrite(PIN_SEGMENT_A, (code & 0x01) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_B, (code & 0x02) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_C, (code & 0x04) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_D, (code & 0x08) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_E, (code & 0x10) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_F, (code & 0x20) ? HIGH : LOW);
  digitalWrite(PIN_SEGMENT_G, (code & 0x40) ? HIGH : LOW);
}

static void enableDigit(int position) {
  switch (position) {
    case 0: digitalWrite(PIN_DIGIT_4, LOW); break;
    case 1: digitalWrite(PIN_DIGIT_3, LOW); break;
    case 2: digitalWrite(PIN_DIGIT_2, LOW); break;
    case 3: digitalWrite(PIN_DIGIT_1, LOW); break;
  }
}

static void showRawCode(byte code, int position) {
  disableAllDigits();
  displaySegments(code);
  enableDigit(position);
}

static void setDisplayFrame(const byte codes[4], const bool visible[4]) {
  for (int i = 0; i < 4; i++) {
    gCodes[i] = codes[i];
    gVisible[i] = visible[i];
  }
}

static void renderFrameOnce() {
  for (int pos = 0; pos < 4; pos++) {
    if (gVisible[pos] && gCodes[pos] != SEGMENT_CODE_BLANK) {
      showRawCode(gCodes[pos], pos);
    } else {
      disableAllDigits();
      displaySegments(SEGMENT_CODE_BLANK);
    }
    delayMicroseconds(DISPLAY_TICK_US);
  }
  disableAllDigits();
}

static int cyclesFromMs(int ms) {
  if (ms <= 0) {
    return 1;
  }
  const unsigned long frameUs = 4UL * static_cast<unsigned long>(DISPLAY_TICK_US);
  const unsigned long totalUs = static_cast<unsigned long>(ms) * 1000UL;
  int cycles = static_cast<int>(totalUs / frameUs);
  return (cycles < 1) ? 1 : cycles;
}

void waitFrameCycles(int cycles) {
  if (cycles < 1) {
    cycles = 1;
  }

  for (int i = 0; i < cycles; i++) {
    renderFrameOnce();
    delay(0);
  }
}

void setDisplayBlank() {
  const byte codes[4] = {SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK};
  const bool visible[4] = {false, false, false, false};
  setDisplayFrame(codes, visible);
}

void setDisplayNumberFrame(int number) {
  number = clampDisplayNumber(number);

  int digits[4];
  splitDigits(number, digits);
  int startPos = findMostSignificantDigitPos(digits);

  byte codes[4];
  bool visible[4];
  for (int pos = 0; pos < 4; pos++) {
    if (pos <= startPos) {
      codes[pos] = SEGMENT_CODES[digits[pos]];
      visible[pos] = true;
    } else {
      codes[pos] = SEGMENT_CODE_BLANK;
      visible[pos] = false;
    }
  }

  setDisplayFrame(codes, visible);
}

void clearDigits() {
  setDisplayBlank();
  waitFrameCycles(1);
}

void displayNumberWithoutLeadingZeros(int number) {
  setDisplayNumberFrame(number);
  waitFrameCycles(1);
}

void displayErrorCodeFrame(FetchError errorCode) {
  int code = static_cast<int>(errorCode);
  if (code < 0) code = 0;
  if (code > 99) code = 99;

  int tens = (code / 10) % 10;
  int ones = code % 10;

  const byte symbolsByPosition[4] = {
    SEGMENT_CODES[ones],
    SEGMENT_CODES[tens],
    SEGMENT_CODE_E,
    SEGMENT_CODE_BLANK
  };
  const bool visibleByPosition[4] = {true, true, true, false};
  setDisplayFrame(symbolsByPosition, visibleByPosition);
  waitFrameCycles(DISPLAY_REFRESH_FRAMES);
}

void animateRunningDash() {
  const int holdCycles = cyclesFromMs(150);
  byte codes[4] = {SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK};
  bool visible[4] = {false, false, false, false};

  for (int pos = 0; pos < 4; pos++) {
    for (int i = 0; i < 4; i++) {
      codes[i] = SEGMENT_CODE_BLANK;
      visible[i] = false;
    }
    codes[pos] = SEGMENT_CODE_DASH;
    visible[pos] = true;
    setDisplayFrame(codes, visible);
    waitFrameCycles(holdCycles);
  }

  for (int pos = 3; pos >= 1; pos--) {
    for (int i = 0; i < 4; i++) {
      codes[i] = SEGMENT_CODE_BLANK;
      visible[i] = false;
    }
    codes[pos] = SEGMENT_CODE_DASH;
    visible[pos] = true;
    setDisplayFrame(codes, visible);
    waitFrameCycles(holdCycles);
  }

  setDisplayBlank();
}

void showWifiConnectingFrame() {
  // One non-blocking-ish animation step used by WiFi connect loop.
  static int dashPos = 0;

  byte codes[4] = {SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK, SEGMENT_CODE_BLANK};
  bool visible[4] = {false, false, false, false};

  codes[dashPos] = SEGMENT_CODE_DASH;
  visible[dashPos] = true;
  setDisplayFrame(codes, visible);
  waitFrameCycles(cyclesFromMs(100));

  dashPos = (dashPos + 1) % 4;
}

static byte getRotatedFrameCode(int digit, bool rotateUp) {
  if (digit < 0 || digit > 9) {
    digit = 0;
  }

  return rotateUp ? ROTATE_UP_CODES[digit] : ROTATE_DOWN_CODES[digit];
}

static void drawCodesFrame(const byte codes[4], int startPos, int cycles) {
  bool visible[4] = {false, false, false, false};
  for (int pos = 0; pos < 4; pos++) {
    visible[pos] = (pos <= startPos) && (codes[pos] != SEGMENT_CODE_BLANK);
  }
  setDisplayFrame(codes, visible);
  waitFrameCycles(cycles);
}

static void animateDigitStepFrame(const int fromDigits[4], const int toDigits[4], int startPos, int frameCycles) {
  byte codes[4];
  int fromStartPos = findMostSignificantDigitPos(fromDigits);
  int toStartPos = findMostSignificantDigitPos(toDigits);

  // 1) old digit
  for (int pos = 0; pos < 4; pos++) {
    codes[pos] = (pos <= fromStartPos) ? SEGMENT_CODES[fromDigits[pos]] : SEGMENT_CODE_BLANK;
  }
  drawCodesFrame(codes, startPos, frameCycles);

  // 2) blank on changed places
  for (int pos = 0; pos < 4; pos++) {
    bool fromVisible = (pos <= fromStartPos);
    bool toVisible = (pos <= toStartPos);
    bool changed = fromVisible != toVisible || (fromVisible && toVisible && fromDigits[pos] != toDigits[pos]);

    if (!fromVisible && !toVisible) {
      codes[pos] = SEGMENT_CODE_BLANK;
    } else if (changed) {
      codes[pos] = SEGMENT_CODE_BLANK;
    } else {
      codes[pos] = fromVisible ? SEGMENT_CODES[fromDigits[pos]] : SEGMENT_CODE_BLANK;
    }
  }
  drawCodesFrame(codes, startPos, frameCycles);

  // 3) new digit enters from top (ROTATE_UP)
  for (int pos = 0; pos < 4; pos++) {
    bool fromVisible = (pos <= fromStartPos);
    bool toVisible = (pos <= toStartPos);
    bool changed = fromVisible != toVisible || (fromVisible && toVisible && fromDigits[pos] != toDigits[pos]);

    if (!fromVisible && !toVisible) {
      codes[pos] = SEGMENT_CODE_BLANK;
    } else if (changed) {
      codes[pos] = toVisible ? getRotatedFrameCode(toDigits[pos], true) : SEGMENT_CODE_BLANK;
    } else {
      codes[pos] = toVisible ? SEGMENT_CODES[toDigits[pos]] : SEGMENT_CODE_BLANK;
    }
  }
  drawCodesFrame(codes, startPos, frameCycles);

  // 4) new full digit
  for (int pos = 0; pos < 4; pos++) {
    codes[pos] = (pos <= toStartPos) ? SEGMENT_CODES[toDigits[pos]] : SEGMENT_CODE_BLANK;
  }
  drawCodesFrame(codes, startPos, frameCycles);
}

void animateNumberDrumTransition(int fromNumber, int toNumber) {
  fromNumber = clampDisplayNumber(fromNumber);
  toNumber = clampDisplayNumber(toNumber);

  if (fromNumber == toNumber) {
    return;
  }

  int fromDigits[4];
  int toDigits[4];
  splitDigits(fromNumber, fromDigits);
  splitDigits(toNumber, toDigits);

  int startPos = findVisibleStartPosForPair(fromDigits, toDigits);
  animateDigitStepFrame(fromDigits, toDigits, startPos, FETCH_ANIMATION_FRAME_CYCLES);
  yield();
}

void initDisplay() {
  pinMode(PIN_SEGMENT_A, OUTPUT);
  pinMode(PIN_SEGMENT_B, OUTPUT);
  pinMode(PIN_SEGMENT_C, OUTPUT);
  pinMode(PIN_SEGMENT_D, OUTPUT);
  pinMode(PIN_SEGMENT_E, OUTPUT);
  pinMode(PIN_SEGMENT_F, OUTPUT);
  pinMode(PIN_SEGMENT_G, OUTPUT);

  pinMode(PIN_DIGIT_1, OUTPUT);
  pinMode(PIN_DIGIT_2, OUTPUT);
  pinMode(PIN_DIGIT_3, OUTPUT);
  pinMode(PIN_DIGIT_4, OUTPUT);

  displaySegments(SEGMENT_CODE_BLANK);
  disableAllDigits();
  setDisplayBlank();
}
