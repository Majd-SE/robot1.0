#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>

#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>

// ============================================================
// WIFI
// ============================================================

#define WIFI_SSID       "Virus"
#define WIFI_PASSWORD   "orange2020"

// ============================================================
// ADAFRUIT IO
// ============================================================

#define AIO_SERVER      "io.adafruit.com"
#define AIO_PORT        8883
#define AIO_USERNAME    "majdalmajali"
#define AIO_KEY         "aio_QxOD848yybKidHuWiZw304CAH7GQ"

// ============================================================
// OLED
// ============================================================

#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1

#define OLED_SDA        4
#define OLED_SCL        5

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// ============================================================
// BUTTON
// ============================================================

#define BOOT_BUTTON 0

// ============================================================
// WEB SERVER
// ============================================================

WebServer server(80);

// ============================================================
// MQTT
// ============================================================

WiFiClientSecure secureClient;

Adafruit_MQTT_Client mqtt(
  &secureClient,
  AIO_SERVER,
  AIO_PORT,
  AIO_USERNAME,
  AIO_KEY
);

char commandTopic[120];
char statusTopic[120];

Adafruit_MQTT_Subscribe* robotCommand = nullptr;
Adafruit_MQTT_Publish* robotStatus = nullptr;

// ============================================================
// WEATHER HTTPS CLIENT
// ============================================================

WiFiClientSecure weatherClient;

// ============================================================
// MODES
// ============================================================

enum RobotMode {
  MODE_PET,
  MODE_STUDY,
  MODE_WORK,
  MODE_POMODORO,
  MODE_CLOCK,
  MODE_WEATHER,
  MODE_MESSAGE,
  MODE_STATUS,
  MODE_NIGHT,
  MODE_REMINDERS,
  MODE_TODO
};

RobotMode robotMode = MODE_PET;

// ============================================================
// EXPRESSIONS
//
// 1 NORMAL
// 2 HAPPY
// 3 LOVE
// 4 SAD
// 5 ANGRY
// 6 SURPRISED
// 7 SLEEPY
// ============================================================

enum Expression {
  EXP_NORMAL = 0,
  EXP_HAPPY,
  EXP_LOVE,
  EXP_SAD,
  EXP_ANGRY,
  EXP_SURPRISED,
  EXP_SLEEPY
};

Expression currentExpression = EXP_NORMAL;

// ============================================================
// PAGE
// ============================================================

int currentPage = 1;

// 1 = PET
// 2 = CLOCK
// 3 = WEATHER
// 4 = POMODORO
// 5 = MESSAGE
// 6 = STATUS
// 7 = REMINDERS
// 8 = TO-DO

const uint8_t MAX_REMINDERS = 8;

struct RobotReminder {
  String id;
  String title;
  String date;
  String time;
  bool complete = false;
  bool alerted = false;
};

RobotReminder reminders[MAX_REMINDERS];
uint8_t reminderCount = 0;
Preferences reminderPreferences;

const uint8_t MAX_TODO_ITEMS = 8;
struct TodoItem {
  String id;
  String title;
  bool complete = false;
};
TodoItem todoItems[MAX_TODO_ITEMS];
uint8_t todoCount = 0;
Preferences todoPreferences;

// ============================================================
// PET
// ============================================================

int happiness = 70;
int hunger = 20;

bool sleeping = false;

unsigned long lastHungerUpdate = 0;
unsigned long lastSleepActivity = 0;
unsigned long lastPetInteraction = 0;

const unsigned long HUNGER_INTERVAL =
  5UL * 60UL * 1000UL;

const int HUNGER_INCREASE = 1;

// ============================================================
// AUTO SLEEP
// ============================================================

const unsigned long AUTO_SLEEP_TIME =
  45UL * 60UL * 1000UL;

// ============================================================
// ADVICE
// ============================================================

unsigned long lastAdviceTime = 0;

const unsigned long ADVICE_INTERVAL =
  40UL * 60UL * 1000UL;

bool temporaryMessage = false;
bool todoAutoShowing = false;
int todoReturnPage = 1;
RobotMode todoReturnMode = MODE_PET;
unsigned long todoAutoStarted = 0;
unsigned long lastTodoAutoShow = 0;
const unsigned long TODO_AUTO_INTERVAL = 15UL * 60UL * 1000UL;
const unsigned long TODO_AUTO_DURATION = 15UL * 1000UL;

unsigned long temporaryMessageStart = 0;

const unsigned long TEMP_MESSAGE_TIME =
  7000;

int returnPageAfterMessage = 1;
RobotMode returnModeAfterMessage = MODE_PET;

String currentMessage = "";

// ============================================================
// AUTO INFORMATION
// ============================================================

unsigned long autoInfoStarted = 0;

int autoInfoStep = 0;

// 0 = PET
// 1 = CLOCK
// 2 = WEATHER

const unsigned long AUTO_INFO_INTERVAL =
  30UL * 60UL * 1000UL;

// ============================================================
// WEATHER
// ============================================================

float weatherTemperature = 0.0;
float weatherFeelsLike = 0.0;
float weatherHumidity = 0.0;
float weatherWind = 0.0;

int weatherCode = -1;

bool weatherValid = false;

String weatherDescription = "Loading...";

unsigned long lastWeatherUpdate = 0;

const unsigned long WEATHER_UPDATE_INTERVAL =
  10UL * 60UL * 1000UL;

// ============================================================
// POMODORO
// ============================================================

bool pomodoroRunning = false;
bool pomodoroPaused = false;

unsigned long pomodoroEndMillis = 0;

long pomodoroRemainingSeconds = 25 * 60;

int pomodoroMinutes = 25;

String pomodoroSession = "focus";

// ============================================================
// BUTTON
// ============================================================

bool lastButtonState = HIGH;

unsigned long buttonDownTime = 0;
unsigned long lastButtonRelease = 0;

int clickCount = 0;

const unsigned long LONG_PRESS_TIME = 900;
const unsigned long DOUBLE_CLICK_TIME = 450;

// ============================================================
// FACE ANIMATION
// ============================================================

bool blinking = false;

unsigned long blinkStart = 0;
unsigned long nextBlinkTime = 0;

const unsigned long BLINK_DURATION = 150;

// ============================================================
// STATUS
// ============================================================

unsigned long lastStatusPublish = 0;

const unsigned long STATUS_PUBLISH_INTERVAL =
  30000;

// ============================================================
// ADVICE LISTS
// ============================================================

const char* petAdvice[] = {
  "Drink some water!",
  "Take a short walk",
  "Stretch for a minute",
  "Rest your eyes",
  "Take a deep breath",
  "Fix your sitting position",
  "Stand up for a minute",
  "Don't forget to hydrate!"
};

const int PET_ADVICE_COUNT =
  sizeof(petAdvice) / sizeof(petAdvice[0]);

const char* studyAdvice[] = {
  "Take a small break",
  "Give your eyes a break",
  "Stretch your shoulders",
  "You've been focused for a while",
  "Walk around for a minute",
  "Don't forget to breathe"
};

const int STUDY_ADVICE_COUNT =
  sizeof(studyAdvice) / sizeof(studyAdvice[0]);

const char* workAdvice[] = {
  "You've been working for a while",
  "Take a quick break",
  "Stretch a little",
  "Drink some water",
  "Rest your eyes"
};

const int WORK_ADVICE_COUNT =
  sizeof(workAdvice) / sizeof(workAdvice[0]);

const char* nightAdvice[] = {
  "It's getting late...",
  "Maybe it's time to rest",
  "Your robot thinks you should sleep",
  "Take it easy tonight"
};

const int NIGHT_ADVICE_COUNT =
  sizeof(nightAdvice) / sizeof(nightAdvice[0]);

// ============================================================
// HUNGER MESSAGES
// ============================================================

const char* hungerMessages[] = {
  "I'm getting hungry...",
  "Can I have some food?",
  "I'm really hungry!",
  "Feed me please"
};

const int HUNGER_MESSAGE_COUNT =
  sizeof(hungerMessages) / sizeof(hungerMessages[0]);

int lastHungerMessage = -1;
int lastAdviceIndex = -1;

// ============================================================
// CENTER TEXT
// ============================================================

void centerText(
  const String& text,
  int y,
  int textSize = 1
) {

  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1;
  int16_t y1;
  uint16_t w;
  uint16_t h;

  display.getTextBounds(
    text,
    0,
    y,
    &x1,
    &y1,
    &w,
    &h
  );

  int x =
    (SCREEN_WIDTH - w) / 2;

  display.setCursor(
    x,
    y
  );

  display.print(text);
}

// ============================================================
// HEART
// ============================================================

void drawHeart(
  int x,
  int y,
  int size
) {

  display.fillCircle(
    x + size / 3,
    y + size / 3,
    size / 3,
    SSD1306_WHITE
  );

  display.fillCircle(
    x + size * 2 / 3,
    y + size / 3,
    size / 3,
    SSD1306_WHITE
  );

  display.fillTriangle(
    x,
    y + size / 3,
    x + size,
    y + size / 3,
    x + size / 2,
    y + size,
    SSD1306_WHITE
  );
}

// ============================================================
// HEART EYE
// ============================================================

void drawHeartEye(
  int x,
  int y,
  int size
) {

  int r = size / 3;

  // Left upper heart
  display.fillCircle(
    x + r,
    y + r,
    r,
    SSD1306_WHITE
  );

  // Right upper heart
  display.fillCircle(
    x + size - r,
    y + r,
    r,
    SSD1306_WHITE
  );

  // Lower point
  display.fillTriangle(
    x,
    y + r,
    x + size,
    y + r,
    x + size / 2,
    y + size,
    SSD1306_WHITE
  );
}

// ============================================================
// BLINK
// ============================================================

void updateBlink() {

  unsigned long now = millis();

  if (blinking) {

    if (
      now - blinkStart >=
      BLINK_DURATION
    ) {
      blinking = false;
    }

    return;
  }

  if (now >= nextBlinkTime) {

    blinking = true;

    blinkStart = now;

    nextBlinkTime =
      now + 3000 + (now % 4500);
  }
}

// ============================================================
// EYE
// ============================================================

void drawEye(
  int x,
  int y,
  int width,
  int height,
  int pupilX,
  int pupilY,
  bool closed
) {

  if (closed) {

    display.drawLine(
      x,
      y + height / 2,
      x + width,
      y + height / 2,
      SSD1306_WHITE
    );

    return;
  }

  display.fillRoundRect(
    x,
    y,
    width,
    height,
    8,
    SSD1306_WHITE
  );

  display.fillCircle(
    x + width / 2 + pupilX,
    y + height / 2 + pupilY,
    6,
    SSD1306_BLACK
  );
}

// ============================================================
// NORMAL FACE
// ============================================================

void drawNormalFace() {

  unsigned long t = millis();

  int lookX = 0;
  int lookY = 0;

  unsigned long phase =
    (t / 1800) % 4;

  if (phase == 1) {
    lookX = 3;
  }

  else if (phase == 2) {
    lookX = -3;
  }

  else if (phase == 3) {
    lookY = 2;
  }

  int bob =
    (int)(sin(t / 900.0) * 1.0);

  drawEye(
    17,
    16 + bob,
    38,
    28,
    lookX,
    lookY,
    blinking
  );

  drawEye(
    73,
    16 + bob,
    38,
    28,
    lookX,
    lookY,
    blinking
  );

  display.drawLine(
    20,
    11 + bob,
    48,
    9 + bob,
    SSD1306_WHITE
  );

  display.drawLine(
    80,
    9 + bob,
    108,
    11 + bob,
    SSD1306_WHITE
  );

  display.drawRoundRect(
    51,
    49,
    26,
    6,
    3,
    SSD1306_WHITE
  );
}

// ============================================================
// HAPPY FACE
// ============================================================

void drawHappyFace() {

  unsigned long t = millis();

  int bounce =
    (int)(sin(t / 260.0) * 2);

  drawEye(
    15,
    14 + bounce,
    40,
    29,
    2,
    2,
    false
  );

  drawEye(
    73,
    14 + bounce,
    40,
    29,
    2,
    2,
    false
  );

  display.drawLine(
    18,
    9 + bounce,
    27,
    5 + bounce,
    SSD1306_WHITE
  );

  display.drawLine(
    27,
    5 + bounce,
    36,
    7 + bounce,
    SSD1306_WHITE
  );

  display.drawLine(
    82,
    7 + bounce,
    91,
    5 + bounce,
    SSD1306_WHITE
  );

  display.drawLine(
    91,
    5 + bounce,
    110,
    9 + bounce,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    38,
    43,
    52,
    17,
    8,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    43,
    48,
    42,
    10,
    5,
    SSD1306_BLACK
  );

  display.fillRect(
    47,
    47,
    34,
    4,
    SSD1306_WHITE
  );

  display.drawPixel(
    36,
    50,
    SSD1306_WHITE
  );

  display.drawPixel(
    37,
    53,
    SSD1306_WHITE
  );

  display.drawPixel(
    91,
    50,
    SSD1306_WHITE
  );

  display.drawPixel(
    90,
    53,
    SSD1306_WHITE
  );

  display.drawPixel(
    8,
    47,
    SSD1306_WHITE
  );

  display.drawPixel(
    11,
    49,
    SSD1306_WHITE
  );

  display.drawPixel(
    14,
    47,
    SSD1306_WHITE
  );

  display.drawPixel(
    114,
    47,
    SSD1306_WHITE
  );

  display.drawPixel(
    117,
    49,
    SSD1306_WHITE
  );

  display.drawPixel(
    120,
    47,
    SSD1306_WHITE
  );
}

// ============================================================
// LOVE FACE - IMPROVED
// ============================================================

void drawLoveFace() {

  unsigned long t = millis();

  // حركة خفيفة للوجه
  int bounce =
    (int)(sin(t / 500.0) * 1);

  // نبض عيون الحب
  int pulse =
    (int)(sin(t / 220.0) * 2);

  // ==========================================================
  // HEART EYES
  // ==========================================================

  int eyeSize = 23 + pulse;

  // LEFT HEART EYE
  drawHeart(
    16,
    14 + bounce,
    eyeSize
  );

  // RIGHT HEART EYE
  drawHeart(
    89,
    14 + bounce,
    eyeSize
  );

  // ==========================================================
  // EYEBROWS
  // ==========================================================

  display.drawLine(
    18,
    9 + bounce,
    28,
    5 + bounce,
    SSD1306_WHITE
  );

  display.drawLine(
    28,
    5 + bounce,
    40,
    8 + bounce,
    SSD1306_WHITE
  );

  display.drawLine(
    88,
    8 + bounce,
    100,
    5 + bounce,
    SSD1306_WHITE
  );

  display.drawLine(
    100,
    5 + bounce,
    110,
    9 + bounce,
    SSD1306_WHITE
  );

  // ==========================================================
  // BIG HAPPY SMILE
  // ==========================================================

  display.drawLine(
    43,
    46,
    48,
    51,
    SSD1306_WHITE
  );

  display.drawLine(
    48,
    51,
    54,
    55,
    SSD1306_WHITE
  );

  display.drawLine(
    54,
    55,
    61,
    57,
    SSD1306_WHITE
  );

  display.drawLine(
    61,
    57,
    68,
    57,
    SSD1306_WHITE
  );

  display.drawLine(
    68,
    57,
    75,
    55,
    SSD1306_WHITE
  );

  display.drawLine(
    75,
    55,
    81,
    51,
    SSD1306_WHITE
  );

  display.drawLine(
    81,
    51,
    86,
    46,
    SSD1306_WHITE
  );

  // ==========================================================
  // CHEEKS
  // ==========================================================

  display.drawLine(
    28,
    48,
    32,
    46,
    SSD1306_WHITE
  );

  display.drawLine(
    32,
    46,
    36,
    48,
    SSD1306_WHITE
  );

  display.drawLine(
    92,
    48,
    96,
    46,
    SSD1306_WHITE
  );

  display.drawLine(
    96,
    46,
    100,
    48,
    SSD1306_WHITE
  );

  // ==========================================================
  // FLOATING HEARTS
  // ==========================================================

  int leftCycle =
    (t / 70) % 45;

  int rightCycle =
    (t / 90 + 20) % 45;

  int leftY =
    48 - leftCycle;

  int rightY =
    44 - rightCycle;

  drawHeart(
    2,
    leftY,
    8
  );

  drawHeart(
    112,
    rightY,
    9
  );

  // ==========================================================
  // SMALL HEART ABOVE HEAD
  // ==========================================================

  if (
    (t / 350) % 2 == 0
  ) {

    drawHeart(
      59,
      1,
      8
    );
  }

  // ==========================================================
  // LITTLE SPARKLES
  // ==========================================================

  if (
    (t / 250) % 2 == 0
  ) {

    display.drawPixel(
      10,
      22,
      SSD1306_WHITE
    );

    display.drawPixel(
      117,
      30,
      SSD1306_WHITE
    );

    display.drawPixel(
      106,
      14,
      SSD1306_WHITE
    );
  }
}
// ============================================================
// SAD FACE - IMPROVED
// ============================================================

void drawSadFace() {

  unsigned long t = millis();

  // حركة بسيطة وكأن الوجه يرتجف من الحزن
  int sadBob =
    (int)(sin(t / 700.0) * 1);

  // ==========================================================
  // SAD EYES
  // ==========================================================

  // LEFT EYE
  display.fillRoundRect(
    16,
    18 + sadBob,
    40,
    26,
    9,
    SSD1306_WHITE
  );

  // RIGHT EYE
  display.fillRoundRect(
    72,
    18 + sadBob,
    40,
    26,
    9,
    SSD1306_WHITE
  );

  // Pupils looking strongly DOWN
  display.fillCircle(
    36,
    39 + sadBob,
    6,
    SSD1306_BLACK
  );

  display.fillCircle(
    92,
    39 + sadBob,
    6,
    SSD1306_BLACK
  );

  // ==========================================================
  // VERY SAD EYEBROWS
  // ==========================================================

  // LEFT eyebrow
  display.drawLine(
    17,
    7,
    27,
    11,
    SSD1306_WHITE
  );

  display.drawLine(
    27,
    11,
    47,
    18,
    SSD1306_WHITE
  );

  // RIGHT eyebrow
  display.drawLine(
    81,
    18,
    101,
    11,
    SSD1306_WHITE
  );

  display.drawLine(
    101,
    11,
    111,
    7,
    SSD1306_WHITE
  );

  // ==========================================================
  // DEEP SAD MOUTH
  // ==========================================================

  display.drawLine(
    43,
    57,
    49,
    53,
    SSD1306_WHITE
  );

  display.drawLine(
    49,
    53,
    55,
    51,
    SSD1306_WHITE
  );

  display.drawLine(
    55,
    51,
    62,
    50,
    SSD1306_WHITE
  );

  display.drawLine(
    62,
    50,
    66,
    50,
    SSD1306_WHITE
  );

  display.drawLine(
    66,
    50,
    73,
    51,
    SSD1306_WHITE
  );

  display.drawLine(
    73,
    51,
    79,
    53,
    SSD1306_WHITE
  );

  display.drawLine(
    79,
    53,
    85,
    57,
    SSD1306_WHITE
  );

  // ==========================================================
  // LEFT TEAR
  // ==========================================================

  int tear1 =
    (t / 65) % 50;

  if (
    tear1 < 42
  ) {

    int y =
      40 + tear1;

    // tear body
    display.fillCircle(
      36,
      y,
      2,
      SSD1306_WHITE
    );

    // pointed top
    display.fillTriangle(
      33,
      y,
      39,
      y,
      36,
      y - 7,
      SSD1306_WHITE
    );
  }

  // ==========================================================
  // RIGHT TEAR
  // ==========================================================

  int tear2 =
    (t / 80 + 20) % 55;

  if (
    tear2 < 45
  ) {

    int y =
      40 + tear2;

    display.fillCircle(
      92,
      y,
      2,
      SSD1306_WHITE
    );

    display.fillTriangle(
      89,
      y,
      95,
      y,
      92,
      y - 7,
      SSD1306_WHITE
    );
  }

  // ==========================================================
  // EXTRA SMALL TEAR DROPS
  // ==========================================================

  if (
    (t / 180) % 3 == 0
  ) {

    display.fillCircle(
      31,
      46,
      1,
      SSD1306_WHITE
    );
  }

  if (
    (t / 220) % 4 == 0
  ) {

    display.fillCircle(
      97,
      47,
      1,
      SSD1306_WHITE
    );
  }
}
// ============================================================
// ANGRY FACE
// ============================================================

void drawAngryFace() {

  unsigned long t = millis();

  int shake =
    (int)(sin(t / 80.0) * 1.5);

  display.fillRoundRect(
    16 + shake,
    21,
    40,
    19,
    5,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    72 + shake,
    21,
    40,
    19,
    5,
    SSD1306_WHITE
  );

  display.fillCircle(
    36 + shake,
    30,
    6,
    SSD1306_BLACK
  );

  display.fillCircle(
    92 + shake,
    30,
    6,
    SSD1306_BLACK
  );

  display.drawLine(
    16,
    9,
    51,
    19,
    SSD1306_WHITE
  );

  display.drawLine(
    77,
    19,
    112,
    9,
    SSD1306_WHITE
  );

  display.drawLine(
    55,
    9,
    61,
    13,
    SSD1306_WHITE
  );

  display.drawLine(
    73,
    9,
    67,
    13,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    43,
    48,
    42,
    10,
    3,
    SSD1306_WHITE
  );

  display.drawLine(
    48,
    50,
    55,
    54,
    SSD1306_BLACK
  );

  display.drawLine(
    55,
    54,
    62,
    50,
    SSD1306_BLACK
  );

  display.drawLine(
    62,
    50,
    69,
    54,
    SSD1306_BLACK
  );

  display.drawLine(
    69,
    54,
    77,
    50,
    SSD1306_BLACK
  );
}

// ============================================================
// SURPRISED
// ============================================================

void drawSurprisedFace() {

  unsigned long t = millis();

  int bounce =
    (int)(sin(t / 170.0) * 2);

  int eyeRadius =
    13 +
    (int)(
      (sin(t / 260.0) + 1) * 1.5
    );

  display.drawLine(
    18,
    8,
    47,
    5,
    SSD1306_WHITE
  );

  display.drawLine(
    81,
    5,
    110,
    8,
    SSD1306_WHITE
  );

  display.fillCircle(
    36,
    27 + bounce,
    eyeRadius,
    SSD1306_WHITE
  );

  display.fillCircle(
    92,
    27 + bounce,
    eyeRadius,
    SSD1306_WHITE
  );

  display.fillCircle(
    36,
    27 + bounce,
    5,
    SSD1306_BLACK
  );

  display.fillCircle(
    92,
    27 + bounce,
    5,
    SSD1306_BLACK
  );

  display.fillCircle(
    64,
    51,
    10,
    SSD1306_WHITE
  );

  display.fillCircle(
    64,
    51,
    6,
    SSD1306_BLACK
  );
}

// ============================================================
// SLEEPY
// ============================================================

void drawSleepyFace() {

  unsigned long t = millis();

  int eyelid =
    24 +
    (int)(sin(t / 1000.0) * 2);

  display.fillRoundRect(
    17,
    eyelid,
    40,
    13,
    6,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    71,
    eyelid,
    40,
    13,
    6,
    SSD1306_WHITE
  );

  display.drawLine(
    20,
    eyelid + 6,
    54,
    eyelid + 6,
    SSD1306_BLACK
  );

  display.drawLine(
    74,
    eyelid + 6,
    108,
    eyelid + 6,
    SSD1306_BLACK
  );

  display.drawLine(
    53,
    51,
    59,
    49,
    SSD1306_WHITE
  );

  display.drawLine(
    59,
    49,
    65,
    51,
    SSD1306_WHITE
  );

  display.drawLine(
    65,
    51,
    71,
    49,
    SSD1306_WHITE
  );

  int zMove =
    (int)((t / 250) % 18);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(
    101,
    4 - zMove / 2
  );

  display.print("Z");

  display.setCursor(
    111,
    10 - zMove / 2
  );

  display.print("Z");

  display.setCursor(
    119,
    17 - zMove / 2
  );

  display.print("Z");
}

// ============================================================
// DRAW FACE
// ============================================================

void drawFace() {

  updateBlink();

  switch (currentExpression) {

    case EXP_NORMAL:
      drawNormalFace();
      break;

    case EXP_HAPPY:
      drawHappyFace();
      break;

    case EXP_LOVE:
      drawLoveFace();
      break;

    case EXP_SAD:
      drawSadFace();
      break;

    case EXP_ANGRY:
      drawAngryFace();
      break;

    case EXP_SURPRISED:
      drawSurprisedFace();
      break;

    case EXP_SLEEPY:
      drawSleepyFace();
      break;
  }
}

// ============================================================
// EXPRESSION
// ============================================================

void setExpression(
  Expression exp
) {

  currentExpression = exp;

  if (
    exp != EXP_SLEEPY
  ) {
    sleeping = false;
  }
}

// ============================================================
// NEXT EXPRESSION
// ============================================================

void nextExpression() {

  int next =
    ((int)currentExpression + 1) % 7;

  setExpression(
    (Expression)next
  );
}

// ============================================================
// TOUCH
// ============================================================

void touchInteraction() {

  lastPetInteraction = millis();
  lastSleepActivity = millis();

  if (sleeping) {

    sleeping = false;

    setExpression(
      EXP_NORMAL
    );
  }
}

// ============================================================
// FEED
// ============================================================

void feedRobot() {

  hunger -= 30;

  if (hunger < 0) {
    hunger = 0;
  }

  happiness += 15;

  if (happiness > 100) {
    happiness = 100;
  }

  sleeping = false;

  touchInteraction();

  robotMode = MODE_PET;
  currentPage = 1;

  setExpression(
    EXP_HAPPY
  );

  showTemporaryMessage(
    "Yummy! Thank you!",
    1,
    MODE_PET
  );
}

// ============================================================
// HUNGER
// ============================================================

void updateHunger() {

  unsigned long now = millis();

  if (
    now - lastHungerUpdate >=
    HUNGER_INTERVAL
  ) {

    lastHungerUpdate = now;

    hunger += HUNGER_INCREASE;

    if (hunger > 100) {
      hunger = 100;
    }
  }

  if (
    robotMode == MODE_PET &&
    !temporaryMessage &&
    !sleeping
  ) {

    if (hunger >= 90) {

      if (
        currentExpression != EXP_ANGRY
      ) {

        setExpression(
          EXP_ANGRY
        );
      }
    }

    else if (hunger >= 70) {

      if (
        currentExpression != EXP_SAD
      ) {

        setExpression(
          EXP_SAD
        );
      }
    }
  }
}

// ============================================================
// HUNGER MESSAGE
// ============================================================

void checkHungerMessage() {

  if (
    robotMode != MODE_PET ||
    temporaryMessage ||
    sleeping
  ) {
    return;
  }

  if (hunger < 70) {
    return;
  }

  static unsigned long lastWarning = 0;

  unsigned long now = millis();

  if (
    now - lastWarning <
    10UL * 60UL * 1000UL
  ) {
    return;
  }

  lastWarning = now;

  int index =
    (now / 600000) %
    HUNGER_MESSAGE_COUNT;

  if (
    index == lastHungerMessage
  ) {

    index =
      (index + 1) %
      HUNGER_MESSAGE_COUNT;
  }

  lastHungerMessage = index;

  if (hunger >= 90) {
    setExpression(EXP_ANGRY);
  }

  else {
    setExpression(EXP_SAD);
  }

  showTemporaryMessage(
    hungerMessages[index],
    1,
    MODE_PET
  );
}

// ============================================================
// TEMP MESSAGE
// ============================================================

void showTemporaryMessage(
  const String& msg,
  int returnPage,
  RobotMode returnMode
) {

  if (todoAutoShowing) {
    if (returnPage == 7 && returnMode == MODE_TODO) {
      returnPage = todoReturnPage;
      returnMode = todoReturnMode;
    }
    todoAutoShowing = false;
  }

  currentMessage = msg;

  temporaryMessage = true;

  temporaryMessageStart =
    millis();

  returnPageAfterMessage =
    returnPage;

  returnModeAfterMessage =
    returnMode;

  currentPage = 5;
}

// ============================================================
// TEMP MESSAGE UPDATE
// ============================================================

void updateTemporaryMessage() {

  if (!temporaryMessage) {
    return;
  }

  if (
    millis() -
    temporaryMessageStart >=
    TEMP_MESSAGE_TIME
  ) {

    temporaryMessage = false;

    currentPage =
      returnPageAfterMessage;

    robotMode =
      returnModeAfterMessage;

    currentMessage = "";
  }
}

// ============================================================
// AUTO SLEEP
// ============================================================

void updatePetSleep() {

  if (
    robotMode != MODE_PET ||
    temporaryMessage
  ) {
    return;
  }

  unsigned long now = millis();

  if (
    !sleeping &&
    now - lastSleepActivity >=
    AUTO_SLEEP_TIME
  ) {

    sleeping = true;

    setExpression(
      EXP_SLEEPY
    );
  }
}

// ============================================================
// CLOCK
// ============================================================

void drawClockPage() {

  struct tm timeinfo;

  if (
    !getLocalTime(&timeinfo)
  ) {

    centerText(
      "Clock unavailable",
      26
    );

    return;
  }

  int hour =
    timeinfo.tm_hour;

  String ampm;

  if (hour >= 12) {
    ampm = "PM";
  }

  else {
    ampm = "AM";
  }

  hour =
    hour % 12;

  if (hour == 0) {
    hour = 12;
  }

  char timeBuffer[10];

  snprintf(
    timeBuffer,
    sizeof(timeBuffer),
    "%02d:%02d",
    hour,
    timeinfo.tm_min
  );

  char dateBuffer[20];

  strftime(
    dateBuffer,
    sizeof(dateBuffer),
    "%d/%m/%Y",
    &timeinfo
  );

  centerText(
    String(timeBuffer),
    10,
    3
  );

  centerText(
    ampm,
    40,
    1
  );

  centerText(
    String(dateBuffer),
    54,
    1
  );
}

// ============================================================
// WEATHER CODE
// ============================================================

String weatherCodeToText(
  int code
) {

  if (code == 0)
    return "Clear";

  if (code == 1)
    return "Mostly clear";

  if (code == 2)
    return "Partly cloudy";

  if (code == 3)
    return "Cloudy";

  if (
    code == 45 ||
    code == 48
  )
    return "Fog";

  if (
    code >= 51 &&
    code <= 57
  )
    return "Drizzle";

  if (
    code >= 61 &&
    code <= 67
  )
    return "Rain";

  if (
    code >= 71 &&
    code <= 77
  )
    return "Snow";

  if (
    code >= 80 &&
    code <= 82
  )
    return "Rain showers";

  if (
    code >= 85 &&
    code <= 86
  )
    return "Snow showers";

  if (
    code >= 95 &&
    code <= 99
  )
    return "Thunderstorm";

  return "Unknown";
}

// ============================================================
// WEATHER UPDATE
// ============================================================

void updateWeather() {

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    Serial.println(
      "Weather: WiFi not connected"
    );

    return;
  }

  unsigned long now =
    millis();

  if (
    lastWeatherUpdate != 0 &&
    now - lastWeatherUpdate <
    WEATHER_UPDATE_INTERVAL
  ) {
    return;
  }

  Serial.println();
  Serial.println(
    "=============================="
  );
  Serial.println(
    "WEATHER UPDATE"
  );
  Serial.println(
    "=============================="
  );

  weatherClient.setInsecure();

  HTTPClient http;

  String url =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=31.9539"
    "&longitude=35.9106"
    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
    "&temperature_unit=celsius"
    "&wind_speed_unit=kmh"
    "&timezone=Asia%2FAmman";

  Serial.println(
    "Requesting:"
  );

  Serial.println(
    url
  );

  if (
    !http.begin(
      weatherClient,
      url
    )
  ) {

    Serial.println(
      "Weather HTTP begin FAILED"
    );

    return;
  }

  http.setTimeout(
    10000
  );

  int httpCode =
    http.GET();

  Serial.print(
    "HTTP code: "
  );

  Serial.println(
    httpCode
  );

  if (
    httpCode == HTTP_CODE_OK
  ) {

    String payload =
      http.getString();

    Serial.println(
      "Weather response:"
    );

    Serial.println(
      payload
    );

    JsonDocument doc;

    DeserializationError error =
      deserializeJson(
        doc,
        payload
      );

    if (error) {

      Serial.print(
        "JSON error: "
      );

      Serial.println(
        error.c_str()
      );

      http.end();

      return;
    }

    if (
      doc["current"].isNull()
    ) {

      Serial.println(
        "ERROR: current object missing"
      );

      http.end();

      return;
    }

    JsonObject current =
      doc["current"].as<JsonObject>();

    if (
      current["temperature_2m"].isNull()
    ) {

      Serial.println(
        "ERROR: temperature_2m missing"
      );

      http.end();

      return;
    }

    weatherTemperature =
      current["temperature_2m"].as<float>();

    weatherFeelsLike =
      current["apparent_temperature"].as<float>();

    weatherHumidity =
      current["relative_humidity_2m"].as<float>();

    weatherWind =
      current["wind_speed_10m"].as<float>();

    weatherCode =
      current["weather_code"].as<int>();

    weatherDescription =
      weatherCodeToText(
        weatherCode
      );

    weatherValid = true;

    Serial.print(
      "Temperature: "
    );

    Serial.print(
      weatherTemperature,
      1
    );

    Serial.println(
      " C"
    );

    Serial.print(
      "Feels like: "
    );

    Serial.print(
      weatherFeelsLike,
      1
    );

    Serial.println(
      " C"
    );

    Serial.print(
      "Humidity: "
    );

    Serial.print(
      weatherHumidity,
      0
    );

    Serial.println(
      " %"
    );

    Serial.print(
      "Wind: "
    );

    Serial.print(
      weatherWind,
      1
    );

    Serial.println(
      " km/h"
    );

    Serial.print(
      "Weather code: "
    );

    Serial.println(
      weatherCode
    );

    Serial.print(
      "Description: "
    );

    Serial.println(
      weatherDescription
    );

    Serial.println(
      "WEATHER SUCCESS"
    );

    lastWeatherUpdate =
      now;
  }

  else {

    Serial.print(
      "Weather HTTP FAILED: "
    );

    Serial.println(
      httpCode
    );

    String errorBody =
      http.getString();

    if (
      errorBody.length() > 0
    ) {

      Serial.println(
        "Server response:"
      );

      Serial.println(
        errorBody
      );
    }
  }

  http.end();

  Serial.println(
    "=============================="
  );
}

// ============================================================
// WEATHER PAGE - IMPROVED
// ============================================================

void drawWeatherPage() {

  centerText("AMMAN WEATHER", 0, 1);
  display.drawLine(4, 10, 123, 10, SSD1306_WHITE);

  if (!weatherValid) {
    centerText("Loading weather...", 26, 1);
    centerText("Please wait", 42, 1);
    return;
  }

  String condition = weatherDescription;
  if (condition.length() > 20)
    condition = condition.substring(0, 20);
  centerText(condition.c_str(), 13, 1);

  char tempBuffer[16];
  snprintf(tempBuffer, sizeof(tempBuffer), "%.1f C", weatherTemperature);
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  int16_t boundsX, boundsY;
  uint16_t boundsWidth, boundsHeight;
  display.getTextBounds(tempBuffer, 0, 0, &boundsX, &boundsY, &boundsWidth, &boundsHeight);
  display.setCursor((SCREEN_WIDTH - boundsWidth) / 2, 25);
  display.print(tempBuffer);

  display.setTextSize(1);
  display.drawLine(4, 44, 123, 44, SSD1306_WHITE);
  display.setCursor(4, 47);
  display.print("Feels ");
  display.print(weatherFeelsLike, 1);
  display.print("C");

  display.setCursor(78, 47);
  display.print("Hum ");
  display.print(weatherHumidity, 0);
  display.print("%");

  String windText = "Wind ";
  windText += String(weatherWind, 0);
  windText += " km/h";
  centerText(windText.c_str(), 56, 1);
}

void saveReminders() {
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  for (uint8_t i = 0; i < reminderCount; i++) {
    JsonObject item = list.add<JsonObject>();
    item["id"] = reminders[i].id;
    item["title"] = reminders[i].title;
    item["date"] = reminders[i].date;
    item["time"] = reminders[i].time;
    item["complete"] = reminders[i].complete;
    item["alerted"] = reminders[i].alerted;
  }
  String serialized;
  serializeJson(doc, serialized);
  reminderPreferences.putString("list", serialized);
}

void loadReminders() {
  reminderPreferences.begin("robot-reminders", false);
  String serialized = reminderPreferences.getString("list", "[]");
  JsonDocument doc;
  if (deserializeJson(doc, serialized))
    return;

  JsonArrayConst list = doc.as<JsonArrayConst>();
  reminderCount = 0;
  for (JsonObjectConst item : list) {
    if (reminderCount >= MAX_REMINDERS)
      break;
    reminders[reminderCount].id = item["id"] | "";
    reminders[reminderCount].title = item["title"] | "";
    reminders[reminderCount].date = item["date"] | "";
    reminders[reminderCount].time = item["time"] | "";
    reminders[reminderCount].complete = item["complete"] | false;
    reminders[reminderCount].alerted = item["alerted"] | false;
    if (reminders[reminderCount].title.length() > 0)
      reminderCount++;
  }
}

void drawReminderPage() {
  centerText("REMINDERS", 0, 1);
  display.drawLine(4, 10, 123, 10, SSD1306_WHITE);

  uint8_t activeCount = 0;
  for (uint8_t i = 0; i < reminderCount; i++)
    if (!reminders[i].complete) activeCount++;

  if (activeCount == 0) {
    centerText("No upcoming items", 25, 1);
    centerText("Add one on website", 42, 1);
    return;
  }

  uint8_t selected = (millis() / 5000UL) % activeCount;
  uint8_t index = 0;
  for (uint8_t i = 0; i < reminderCount; i++) {
    if (reminders[i].complete)
      continue;
    if (index++ != selected)
      continue;

    String title = reminders[i].title;
    if (title.length() > 20)
      title = title.substring(0, 20);
    centerText(title.c_str(), 16, 1);
    centerText(reminders[i].date.c_str(), 31, 1);
    centerText(reminders[i].time.c_str(), 43, 1);

    char countText[16];
    snprintf(countText, sizeof(countText), "%u of %u", (unsigned)(selected + 1), (unsigned)activeCount);
    centerText(countText, 55, 1);
    break;
  }
}

void updateReminderAlerts() {
  if (todoAutoShowing)
    return;
  if (reminderCount == 0)
    return;

  struct tm nowInfo;
  if (!getLocalTime(&nowInfo, 10))
    return;
  time_t now = mktime(&nowInfo);

  for (uint8_t i = 0; i < reminderCount; i++) {
    if (reminders[i].complete || reminders[i].alerted)
      continue;

    int year, month, day, hour, minute;
    if (sscanf(reminders[i].date.c_str(), "%d-%d-%d", &year, &month, &day) != 3 ||
        sscanf(reminders[i].time.c_str(), "%d:%d", &hour, &minute) != 2)
      continue;

    struct tm dueInfo = {};
    dueInfo.tm_year = year - 1900;
    dueInfo.tm_mon = month - 1;
    dueInfo.tm_mday = day;
    dueInfo.tm_hour = hour;
    dueInfo.tm_min = minute;
    dueInfo.tm_isdst = -1;
    time_t due = mktime(&dueInfo);
    if (due <= now) {
      reminders[i].alerted = true;
      saveReminders();
      robotMode = MODE_REMINDERS;
      currentPage = 6;
      touchInteraction();
      publishStatus();
      break;
    }
  }
}

void handleReminderCommand(JsonDocument& doc) {
  String operation = doc["operation"] | "";
  String id = doc["id"] | "";
  bool changed = false;

  if (operation == "add") {
    String title = doc["title"] | "";
    String date = doc["date"] | "";
    String time = doc["time"] | "";
    title.trim();
    if (id.length() == 0 || title.length() == 0 || date.length() != 10 || time.length() < 4)
      return;

    int index = -1;
    for (uint8_t i = 0; i < reminderCount; i++)
      if (reminders[i].id == id) index = i;
    if (index < 0 && reminderCount < MAX_REMINDERS)
      index = reminderCount++;
    if (index < 0)
      return;

    reminders[index].id = id;
    reminders[index].title = title.substring(0, 64);
    reminders[index].date = date;
    reminders[index].time = time.substring(0, 5);
    reminders[index].complete = false;
    reminders[index].alerted = false;
    changed = true;
  } else {
    for (uint8_t i = 0; i < reminderCount; i++) {
      if (reminders[i].id != id)
        continue;
      if (operation == "complete") {
        reminders[i].complete = true;
        changed = true;
      } else if (operation == "delete") {
        for (uint8_t j = i; j + 1 < reminderCount; j++)
          reminders[j] = reminders[j + 1];
        reminderCount--;
        changed = true;
      }
      break;
    }
  }

  if (!changed)
    return;
  saveReminders();
  robotMode = MODE_REMINDERS;
  currentPage = 6;
  touchInteraction();
  publishStatus();
}

void saveTodoItems() {
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  for (uint8_t i = 0; i < todoCount; i++) {
    JsonObject item = list.add<JsonObject>();
    item["id"] = todoItems[i].id;
    item["title"] = todoItems[i].title;
    item["complete"] = todoItems[i].complete;
  }
  String serialized;
  serializeJson(doc, serialized);
  todoPreferences.putString("list", serialized);
}

void loadTodoItems() {
  todoPreferences.begin("robot-todo", false);
  String serialized = todoPreferences.getString("list", "[]");
  JsonDocument doc;
  if (deserializeJson(doc, serialized))
    return;

  todoCount = 0;
  for (JsonObjectConst item : doc.as<JsonArrayConst>()) {
    if (todoCount >= MAX_TODO_ITEMS)
      break;
    todoItems[todoCount].id = item["id"] | "";
    todoItems[todoCount].title = item["title"] | "";
    todoItems[todoCount].complete = item["complete"] | false;
    if (todoItems[todoCount].title.length() > 0)
      todoCount++;
  }
}

uint8_t pendingTodoCount() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < todoCount; i++)
    if (!todoItems[i].complete) count++;
  return count;
}

void drawTodoPage() {
  centerText("TO-DO LIST", 0, 1);
  display.drawLine(4, 10, 123, 10, SSD1306_WHITE);

  uint8_t pending = pendingTodoCount();
  if (pending == 0) {
    centerText("No active tasks", 25, 1);
    centerText("Add tasks on website", 42, 1);
    return;
  }

  const uint8_t itemsPerPage = 4;
  uint8_t pageCount = (pending + itemsPerPage - 1) / itemsPerPage;
  uint8_t page = (millis() / 5000UL) % pageCount;
  uint8_t pendingIndex = 0;
  uint8_t row = 0;
  for (uint8_t i = 0; i < todoCount && row < itemsPerPage; i++) {
    if (todoItems[i].complete)
      continue;
    if (pendingIndex++ < page * itemsPerPage)
      continue;

    String title = todoItems[i].title;
    if (title.length() > 17)
      title = title.substring(0, 17);
    String line = String("[ ] ") + title;
    if (line.length() > 21)
      line = line.substring(0, 21);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(3, 14 + row * 10);
    display.print(line);
    row++;
  }

  char pageText[16];
  snprintf(pageText, sizeof(pageText), "%u/%u", (unsigned)(page + 1), (unsigned)pageCount);
  centerText(pageText, 56, 1);
}

void updateAutoTodoPage() {
  if (todoAutoShowing) {
    if (millis() - todoAutoStarted >= TODO_AUTO_DURATION) {
      todoAutoShowing = false;
      currentPage = todoReturnPage;
      robotMode = todoReturnMode;
      publishStatus();
    }
    return;
  }

  if (pendingTodoCount() == 0 || temporaryMessage ||
      millis() - lastTodoAutoShow < TODO_AUTO_INTERVAL)
    return;

  todoReturnPage = currentPage;
  todoReturnMode = robotMode;
  todoAutoShowing = true;
  todoAutoStarted = millis();
  lastTodoAutoShow = todoAutoStarted;
  currentPage = 7;
  robotMode = MODE_TODO;
  publishStatus();
}

void handleTodoCommand(JsonDocument& doc) {
  String operation = doc["operation"] | "";
  String id = doc["id"] | "";
  bool changed = false;

  if (operation == "add") {
    String title = doc["title"] | "";
    title.trim();
    if (id.length() == 0 || title.length() == 0)
      return;

    int index = -1;
    for (uint8_t i = 0; i < todoCount; i++)
      if (todoItems[i].id == id) index = i;
    if (index < 0 && todoCount < MAX_TODO_ITEMS)
      index = todoCount++;
    if (index < 0)
      return;

    todoItems[index].id = id;
    todoItems[index].title = title.substring(0, 64);
    todoItems[index].complete = false;
    changed = true;
  } else {
    for (uint8_t i = 0; i < todoCount; i++) {
      if (todoItems[i].id != id)
        continue;
      if (operation == "complete") {
        todoItems[i].complete = true;
        changed = true;
      } else if (operation == "delete") {
        for (uint8_t j = i; j + 1 < todoCount; j++)
          todoItems[j] = todoItems[j + 1];
        todoCount--;
        changed = true;
      }
      break;
    }
  }

  if (!changed)
    return;
  saveTodoItems();
  todoAutoShowing = false;
  robotMode = MODE_TODO;
  currentPage = 7;
  lastTodoAutoShow = millis();
  touchInteraction();
  publishStatus();
}

// ============================================================
// POMODORO
// ============================================================

void updatePomodoro() {

  if (
    !pomodoroRunning
  ) {
    return;
  }

  unsigned long now =
    millis();

  if (
    now >= pomodoroEndMillis
  ) {

    pomodoroRunning = false;
    pomodoroPaused = false;

    pomodoroRemainingSeconds = 0;

    setExpression(
      EXP_HAPPY
    );

    showTemporaryMessage(
      "Session complete!",
      4,
      MODE_POMODORO
    );

    publishStatus();
  }

  else {

    pomodoroRemainingSeconds =
      (
        pomodoroEndMillis - now
      ) / 1000;
  }
}

// ============================================================
// POMODORO PAGE
// ============================================================

void drawPomodoroPage() {

  centerText(
    "POMODORO",
    1,
    1
  );

  long total =
    pomodoroRemainingSeconds;

  if (total < 0) {
    total = 0;
  }

  int minutes =
    total / 60;

  int seconds =
    total % 60;

  char buffer[10];

  snprintf(
    buffer,
    sizeof(buffer),
    "%02d:%02d",
    minutes,
    seconds
  );

  centerText(
    String(buffer),
    17,
    3
  );

  if (
    pomodoroRunning
  ) {

    centerText(
      "RUNNING",
      52,
      1
    );
  }

  else if (
    pomodoroPaused
  ) {

    centerText(
      "PAUSED",
      52,
      1
    );
  }

  else {

    centerText(
      "READY",
      52,
      1
    );
  }
}

// ============================================================
// MESSAGE PAGE
// ============================================================

void drawMessagePage() {

  if (
    currentMessage.length() == 0
  ) {

    centerText(
      "No message",
      28
    );

    return;
  }

  display.setTextSize(1);

  String text =
    currentMessage;

  String line = "";

  int y = 8;

  for (
    int i = 0;
    i < text.length();
    i++
  ) {

    char c =
      text[i];

    if (
      c == ' ' &&
      line.length() >= 19
    ) {

      centerText(
        line,
        y
      );

      y += 12;

      line = "";
    }

    else {

      line += c;
    }

    if (y > 55) {
      break;
    }
  }

  if (
    line.length() > 0 &&
    y <= 55
  ) {

    centerText(
      line,
      y
    );
  }
}

// ============================================================
// STATUS PAGE
// ============================================================

void drawStatusPage() {

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  centerText(
    "ROBOT STATUS",
    0,
    1
  );

  display.drawLine(
    0,
    10,
    127,
    10,
    SSD1306_WHITE
  );

  display.setCursor(
    3,
    14
  );

  display.print(
    "HAPPY "
  );

  display.print(
    happiness
  );

  display.print(
    "%"
  );

  display.setCursor(
    68,
    14
  );

  display.print(
    "HUNGER "
  );

  display.print(
    hunger
  );

  display.print(
    "%"
  );

  display.setCursor(
    3,
    27
  );

  display.print(
    "MODE: "
  );

  switch (robotMode) {

    case MODE_PET:
      display.print("PET");
      break;

    case MODE_STUDY:
      display.print("STUDY");
      break;

    case MODE_WORK:
      display.print("WORK");
      break;

    case MODE_POMODORO:
      display.print("POMODORO");
      break;

    case MODE_CLOCK:
      display.print("CLOCK");
      break;

    case MODE_WEATHER:
      display.print("WEATHER");
      break;

    case MODE_MESSAGE:
      display.print("MESSAGE");
      break;

    case MODE_STATUS:
      display.print("STATUS");
      break;

    case MODE_REMINDERS:
      display.print("REMINDERS");
      break;

    case MODE_TODO:
      display.print("TO-DO");
      break;

    case MODE_NIGHT:
      display.print("NIGHT");
      break;
  }

  display.setCursor(
    3,
    39
  );

  display.print(
    "WIFI: "
  );

  if (
    WiFi.status() == WL_CONNECTED
  ) {

    display.print(
      "ONLINE"
    );
  }

  else {

    display.print(
      "OFFLINE"
    );
  }

  display.setCursor(
    72,
    39
  );

  display.print(
    sleeping
      ? "SLEEP"
      : "AWAKE"
  );

  display.setCursor(
    3,
    52
  );

  display.print(
    "IP "
  );

  if (
    WiFi.status() == WL_CONNECTED
  ) {

    String ip =
      WiFi.localIP().toString();

    display.print(
      ip
    );
  }

  else {

    display.print(
      "---"
    );
  }
}

// ============================================================
// SET MODE
// ============================================================

void setMode(
  RobotMode newMode
) {

  temporaryMessage = false;
  currentMessage = "";
  todoAutoShowing = false;

  robotMode =
    newMode;

  temporaryMessage =
    false;

  touchInteraction();

  switch (newMode) {

    case MODE_PET:

      currentPage = 1;

      autoInfoStarted =
        millis();

      autoInfoStep = 0;

      break;

    case MODE_STUDY:

      currentPage = 1;

      setExpression(
        EXP_NORMAL
      );

      break;

    case MODE_WORK:

      currentPage = 1;

      setExpression(
        EXP_NORMAL
      );

      break;

    case MODE_POMODORO:

      currentPage = 4;

      break;

    case MODE_CLOCK:

      currentPage = 2;

      break;

    case MODE_WEATHER:

      currentPage = 3;

      break;

    case MODE_MESSAGE:

      currentPage = 5;

      break;

    case MODE_STATUS:

      currentPage = 8;

      break;

    case MODE_REMINDERS:

      currentPage = 6;

      break;

    case MODE_TODO:

      currentPage = 7;

      break;

    case MODE_NIGHT:

      currentPage = 1;

      setExpression(
        EXP_SLEEPY
      );

      break;
  }

  publishStatus();
}

// ============================================================
// AUTO INFORMATION
// ============================================================

void updateAutoInformation() {

  if (
    temporaryMessage
  ) {
    return;
  }

  unsigned long now =
    millis();

  if (
    now - autoInfoStarted <
    AUTO_INFO_INTERVAL
  ) {
    return;
  }

  if (
    robotMode == MODE_PET &&
    autoInfoStep == 0
  ) {

    autoInfoStarted =
      now;

    autoInfoStep = 1;

    robotMode =
      MODE_CLOCK;

    currentPage = 2;

    publishStatus();

    return;
  }

  if (
    robotMode == MODE_CLOCK &&
    autoInfoStep == 1
  ) {

    autoInfoStarted =
      now;

    autoInfoStep = 2;

    robotMode =
      MODE_WEATHER;

    currentPage = 3;

    publishStatus();

    return;
  }

  if (
    robotMode == MODE_WEATHER &&
    autoInfoStep == 2
  ) {

    autoInfoStarted =
      now;

    autoInfoStep = 0;

    robotMode =
      MODE_PET;

    currentPage = 1;

    publishStatus();

    return;
  }
}

// ============================================================
// NIGHT
// ============================================================

bool isNightTime() {

  struct tm timeinfo;

  if (
    !getLocalTime(&timeinfo)
  ) {
    return false;
  }

  int hour =
    timeinfo.tm_hour;

  return (
    hour >= 23 ||
    hour < 7
  );
}

void updateAutomaticNightMode() {

  if (
    !isNightTime()
  ) {

    if (
      robotMode ==
      MODE_NIGHT
    ) {

      robotMode =
        MODE_PET;

      currentPage =
        1;

      setExpression(
        EXP_NORMAL
      );
    }

    return;
  }

  if (
    robotMode ==
    MODE_POMODORO
  ) {
    return;
  }

  if (
    robotMode == MODE_PET ||
    robotMode == MODE_STUDY ||
    robotMode == MODE_WORK
  ) {

    robotMode =
      MODE_NIGHT;

    currentPage =
      1;

    setExpression(
      EXP_SLEEPY
    );
  }
}

// ============================================================
// ADVICE
// ============================================================

void showAdvice() {

  if (
    temporaryMessage
  ) {
    return;
  }

  const char** list =
    nullptr;

  int count = 0;

  if (
    robotMode ==
    MODE_PET
  ) {

    list = petAdvice;
    count = PET_ADVICE_COUNT;
  }

  else if (
    robotMode ==
    MODE_STUDY
  ) {

    list = studyAdvice;
    count = STUDY_ADVICE_COUNT;
  }

  else if (
    robotMode ==
    MODE_WORK
  ) {

    list = workAdvice;
    count = WORK_ADVICE_COUNT;
  }

  else if (
    robotMode ==
    MODE_NIGHT
  ) {

    list = nightAdvice;
    count = NIGHT_ADVICE_COUNT;
  }

  else {
    return;
  }

  int index =
    (millis() / 1000) %
    count;

  if (
    index ==
    lastAdviceIndex
  ) {

    index =
      (index + 1) %
      count;
  }

  lastAdviceIndex =
    index;

  RobotMode oldMode =
    robotMode;

  int oldPage =
    currentPage;

  currentMessage =
    list[index];

  temporaryMessage =
    true;

  temporaryMessageStart =
    millis();

  returnModeAfterMessage =
    oldMode;

  returnPageAfterMessage =
    oldPage;

  currentPage = 5;
}

void updateAdvice() {

  if (
    robotMode ==
    MODE_POMODORO
  ) {
    return;
  }

  if (
    robotMode != MODE_PET &&
    robotMode != MODE_STUDY &&
    robotMode != MODE_WORK &&
    robotMode != MODE_NIGHT
  ) {
    return;
  }

  unsigned long now =
    millis();

  if (
    now - lastAdviceTime >=
    ADVICE_INTERVAL
  ) {

    lastAdviceTime =
      now;

    showAdvice();
  }
}

// ============================================================
// BUTTON
// ============================================================

void handleButton() {

  bool state =
    digitalRead(
      BOOT_BUTTON
    );

  unsigned long now =
    millis();

  if (
    lastButtonState == HIGH &&
    state == LOW
  ) {

    buttonDownTime =
      now;
  }

  if (
    lastButtonState == LOW &&
    state == HIGH
  ) {

    unsigned long duration =
      now - buttonDownTime;

    if (
      duration >=
      LONG_PRESS_TIME
    ) {

      temporaryMessage = false;
      currentMessage = "";
      todoAutoShowing = false;

      if (
        currentPage == 1 &&
        robotMode == MODE_PET
      ) {

        nextExpression();
      }

      else {

        currentPage = 1;
        robotMode = MODE_PET;
      }

      clickCount = 0;
    }

    else {

      clickCount++;

      lastButtonRelease =
        now;
    }
  }

  if (
    clickCount == 2 &&
    now - lastButtonRelease >
      DOUBLE_CLICK_TIME
  ) {

    temporaryMessage = false;
    currentMessage = "";
    todoAutoShowing = false;

    clickCount = 0;

    currentPage = 1;

    robotMode =
      MODE_PET;

    touchInteraction();
  }

  if (
    clickCount == 1 &&
    now - lastButtonRelease >
      DOUBLE_CLICK_TIME
  ) {

    temporaryMessage = false;
    currentMessage = "";
    todoAutoShowing = false;

    clickCount = 0;

    currentPage++;

    if (
      currentPage > 8
    ) {

      currentPage = 1;
    }

    switch (currentPage) {

      case 1:

        robotMode =
          MODE_PET;

        break;

      case 2:

        robotMode =
          MODE_CLOCK;

        break;

      case 3:

        robotMode =
          MODE_WEATHER;

        break;

      case 4:

        robotMode =
          MODE_POMODORO;

        break;

      case 5:

        robotMode =
          MODE_MESSAGE;

        break;

      case 6:

        robotMode =
          MODE_REMINDERS;

        break;

      case 7:

        robotMode =
          MODE_TODO;

        break;

      case 8:

        robotMode =
          MODE_STATUS;

        break;
    }

    touchInteraction();
  }

  lastButtonState =
    state;
}

// ============================================================
// MQTT CONNECT
// ============================================================

void MQTT_connect() {

  if (
    mqtt.connected()
  ) {
    return;
  }

  int8_t ret =
    mqtt.connect();

  if (
    ret != 0
  ) {

    Serial.print(
      "MQTT error: "
    );

    Serial.println(
      mqtt.connectErrorString(ret)
    );

    mqtt.disconnect();

    return;
  }

  Serial.println(
    "MQTT connected!"
  );

  lastStatusPublish = millis();
  publishStatus();
}

// ============================================================
// MQTT TOPICS
// ============================================================

void configureAioTopics() {

  snprintf(
    commandTopic,
    sizeof(commandTopic),
    "%s/feeds/robot-command/json",
    AIO_USERNAME
  );

  snprintf(
    statusTopic,
    sizeof(statusTopic),
    "%s/feeds/robot-status/json",
    AIO_USERNAME
  );

  robotCommand =
    new Adafruit_MQTT_Subscribe(
      &mqtt,
      commandTopic
    );

  robotStatus =
    new Adafruit_MQTT_Publish(
      &mqtt,
      statusTopic
    );

  mqtt.subscribe(
    robotCommand
  );
}

// ============================================================
// PUBLISH STATUS
// ============================================================

void publishStatus() {

  if (
    !mqtt.connected()
  ) {
    return;
  }

  JsonDocument doc;

  doc["mode"] =
    (int)robotMode;

  doc["page"] =
    currentPage;

  doc["expression"] =
    (int)currentExpression;

  doc["happiness"] =
    happiness;

  doc["hunger"] =
    hunger;

  doc["sleeping"] =
    sleeping;

  doc["pomodoroRunning"] =
    pomodoroRunning;

  doc["pomodoroPaused"] =
    pomodoroPaused;

  doc["pomodoroRemaining"] =
    pomodoroRemainingSeconds;

  doc["reminderCount"] =
    reminderCount;

  doc["todoCount"] =
    todoCount;

  doc["temperature"] =
    weatherTemperature;

  doc["humidity"] =
    weatherHumidity;

  doc["wind"] =
    weatherWind;

  String json;

  serializeJson(
    doc,
    json
  );

  // Adafruit IO's /json subscription delivers a feed envelope.
  JsonDocument envelope;
  envelope["value"] = json;
  String feedPayload;
  serializeJson(envelope, feedPayload);

  robotStatus->publish(
    feedPayload.c_str()
  );
}

// ============================================================
// MQTT COMMAND
// ============================================================

void handleMQTTCommand(
  String payload
) {

  JsonDocument doc;

  DeserializationError error =
    deserializeJson(
      doc,
      payload
    );

  if (error) {
    return;
  }

  // Unwrap Adafruit IO's /json feed envelope before reading the command.
  if (doc["action"].isNull()) {
    String nestedCommand = doc["data"]["value"] | "";
    if (nestedCommand.length() == 0)
      nestedCommand = doc["value"] | "";
    if (nestedCommand.length() == 0)
      nestedCommand = doc["last_value"] | "";
    if (nestedCommand.length() == 0)
      return;

    doc.clear();
    if (deserializeJson(doc, nestedCommand))
      return;
  }

  String action =
    doc["action"] | "";

  if (action == "todo") {
    handleTodoCommand(doc);
    return;
  }

  if (action == "reminder") {
    handleReminderCommand(doc);
    return;
  }

  if (action == "pet") {
    touchInteraction();
    happiness = min(100, happiness + 5);
    currentPage = 1;
    robotMode = MODE_PET;
    publishStatus();
    return;
  }

  if (
    action == "feed"
  ) {

    feedRobot();
    publishStatus();

    return;
  }

  if (
    action == "wake"
  ) {

    sleeping = false;

    robotMode =
      MODE_PET;

    currentPage = 1;

    touchInteraction();

    setExpression(
      EXP_NORMAL
    );
    publishStatus();

    return;
  }

  if (
    action == "sleep"
  ) {

    sleeping = true;

    robotMode =
      MODE_PET;

    currentPage = 1;

    setExpression(
      EXP_SLEEPY
    );
    publishStatus();

    return;
  }

  if (
    action == "mode"
  ) {

    String mode =
      doc["mode"] | "";

    if (mode == "pet")
      setMode(MODE_PET);

    else if (mode == "study")
      setMode(MODE_STUDY);

    else if (mode == "work")
      setMode(MODE_WORK);

    else if (mode == "pomodoro")
      setMode(MODE_POMODORO);

    else if (mode == "clock")
      setMode(MODE_CLOCK);

    else if (mode == "weather")
      setMode(MODE_WEATHER);

    else if (mode == "message")
      setMode(MODE_MESSAGE);

    else if (mode == "status")
      setMode(MODE_STATUS);

    else if (mode == "reminders")
      setMode(MODE_REMINDERS);

    else if (mode == "todo")
      setMode(MODE_TODO);

    return;
  }

  if (
    action == "emotion"
  ) {

    int expression =
      doc["expression"] | 0;

    if (
      expression >= 0 &&
      expression <= 6
    ) {

      setExpression(
        (Expression)expression
      );
    }

    return;
  }

  if (
    action == "message"
  ) {

    String message =
      doc["message"] | "";

    if (
      message.length() > 0
    ) {

      showTemporaryMessage(
        message,
        currentPage,
        robotMode
      );
    }

    return;
  }

  if (
    action == "timer"
  ) {

    String command =
      doc["command"] | "";

    if (
      command == "start" ||
      command == "resume"
    ) {

      long seconds =
        doc["remainingSeconds"] |
        pomodoroMinutes * 60;

      if (
        seconds <= 0
      ) {

        seconds =
          pomodoroMinutes * 60;
      }

      pomodoroRemainingSeconds =
        seconds;

      pomodoroEndMillis =
        millis() +
        (
          (unsigned long)
          seconds * 1000UL
        );

      pomodoroRunning = true;
      pomodoroPaused = false;

      robotMode =
        MODE_POMODORO;

      currentPage = 4;

      publishStatus();
    }

    else if (
      command == "pause"
    ) {

      if (
        pomodoroRunning
      ) {

        pomodoroRemainingSeconds =
          (
            pomodoroEndMillis -
            millis()
          ) / 1000;

        pomodoroRunning = false;
        pomodoroPaused = true;
      }

      publishStatus();
    }

    else if (
      command == "reset"
    ) {

      pomodoroRunning = false;
      pomodoroPaused = false;

      pomodoroRemainingSeconds =
        pomodoroMinutes * 60;

      robotMode =
        MODE_POMODORO;

      currentPage = 4;

      publishStatus();
    }

    return;
  }
}

// ============================================================
// MQTT CHECK
// ============================================================

void checkMQTT() {

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {
    return;
  }

  MQTT_connect();

  if (
    !mqtt.connected()
  ) {
    return;
  }

  Adafruit_MQTT_Subscribe*
  subscription;

  while (
    (
      subscription =
      mqtt.readSubscription(10)
    )
  ) {

    if (
      subscription ==
      robotCommand
    ) {

      String payload =
        (char*)
        robotCommand->lastread;

      handleMQTTCommand(
        payload
      );
    }
  }
}

// ============================================================
// API STATUS
// ============================================================

void handleApiStatus() {

  JsonDocument doc;

  doc["mode"] =
    (int)robotMode;

  doc["page"] =
    currentPage;

  doc["expression"] =
    (int)currentExpression;

  doc["happiness"] =
    happiness;

  doc["hunger"] =
    hunger;

  doc["sleeping"] =
    sleeping;

  doc["wifi"] =
    WiFi.status() ==
    WL_CONNECTED;

  doc["ip"] =
    WiFi.localIP().toString();

  doc["temperature"] =
    weatherTemperature;

  doc["feelsLike"] =
    weatherFeelsLike;

  doc["humidity"] =
    weatherHumidity;

  doc["wind"] =
    weatherWind;

  doc["weatherCode"] =
    weatherCode;

  doc["weatherDescription"] =
    weatherDescription;

  doc["pomodoroRunning"] =
    pomodoroRunning;

  doc["pomodoroPaused"] =
    pomodoroPaused;

  doc["pomodoroRemaining"] =
    pomodoroRemainingSeconds;

  doc["reminderCount"] =
    reminderCount;

  doc["todoCount"] =
    todoCount;

  String response;

  serializeJson(
    doc,
    response
  );

  server.send(
    200,
    "application/json",
    response
  );
}

// ============================================================
// API EMOTION
// ============================================================

void handleApiEmotion() {

  if (
    !server.hasArg("plain")
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Missing body\"}"
    );

    return;
  }

  JsonDocument doc;

  if (
    deserializeJson(
      doc,
      server.arg("plain")
    )
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Invalid JSON\"}"
    );

    return;
  }

  int expression =
    doc["expression"] | 0;

  if (
    expression < 0 ||
    expression > 6
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Invalid expression\"}"
    );

    return;
  }

  setExpression(
    (Expression)expression
  );

  publishStatus();

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}

// ============================================================
// API MESSAGE
// ============================================================

void handleApiMessage() {

  if (
    !server.hasArg("plain")
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Missing body\"}"
    );

    return;
  }

  JsonDocument doc;

  if (
    deserializeJson(
      doc,
      server.arg("plain")
    )
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Invalid JSON\"}"
    );

    return;
  }

  String message =
    doc["message"] | "";

  if (
    message.length() == 0
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Empty message\"}"
    );

    return;
  }

  showTemporaryMessage(
    message,
    currentPage,
    robotMode
  );

  publishStatus();

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}

// ============================================================
// API PET
// ============================================================

void handleApiPet() {

  if (
    !server.hasArg("plain")
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Missing body\"}"
    );

    return;
  }

  JsonDocument doc;

  if (
    deserializeJson(
      doc,
      server.arg("plain")
    )
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Invalid JSON\"}"
    );

    return;
  }

  String action =
    doc["action"] | "";

  if (
    action == "feed"
  ) {

    feedRobot();
  }

  else if (
    action == "wake"
  ) {

    sleeping = false;

    robotMode =
      MODE_PET;

    currentPage = 1;

    touchInteraction();

    setExpression(
      EXP_NORMAL
    );
  }

  else if (
    action == "sleep"
  ) {

    sleeping = true;

    robotMode =
      MODE_PET;

    currentPage = 1;

    setExpression(
      EXP_SLEEPY
    );
  }

  else if (
    action == "pet"
  ) {

    happiness += 10;

    if (
      happiness > 100
    ) {

      happiness = 100;
    }

    robotMode =
      MODE_PET;

    currentPage = 1;

    touchInteraction();

    setExpression(
      EXP_HAPPY
    );
  }

  else {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Unknown action\"}"
    );

    return;
  }

  publishStatus();

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}

void handleApiReminder() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  handleReminderCommand(doc);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiTodo() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  handleTodoCommand(doc);
  server.send(200, "application/json", "{\"ok\":true}");
}

// ============================================================
// API MODE
// ============================================================

void handleApiMode() {

  if (
    !server.hasArg("plain")
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Missing body\"}"
    );

    return;
  }

  JsonDocument doc;

  if (
    deserializeJson(
      doc,
      server.arg("plain")
    )
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Invalid JSON\"}"
    );

    return;
  }

  String mode =
    doc["mode"] | "";

  if (mode == "pet")
    setMode(MODE_PET);

  else if (mode == "study")
    setMode(MODE_STUDY);

  else if (mode == "work")
    setMode(MODE_WORK);

  else if (mode == "pomodoro")
    setMode(MODE_POMODORO);

  else if (mode == "clock")
    setMode(MODE_CLOCK);

  else if (mode == "weather")
    setMode(MODE_WEATHER);

  else if (mode == "message")
    setMode(MODE_MESSAGE);

  else if (mode == "status")
    setMode(MODE_STATUS);

  else if (mode == "reminders")
    setMode(MODE_REMINDERS);

  else if (mode == "todo")
    setMode(MODE_TODO);

  else {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Unknown mode\"}"
    );

    return;
  }

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}

// ============================================================
// API TIMER
// ============================================================

void handleApiTimer() {

  if (
    !server.hasArg("plain")
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Missing body\"}"
    );

    return;
  }

  JsonDocument doc;

  if (
    deserializeJson(
      doc,
      server.arg("plain")
    )
  ) {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Invalid JSON\"}"
    );

    return;
  }

  String command =
    doc["command"] | "";

  if (
    command == "start" ||
    command == "resume"
  ) {

    long seconds =
      doc["remainingSeconds"] |
      pomodoroMinutes * 60;

    if (
      seconds <= 0
    ) {

      seconds =
        pomodoroMinutes * 60;
    }

    pomodoroRemainingSeconds =
      seconds;

    pomodoroEndMillis =
      millis() +
      (
        (unsigned long)
        seconds * 1000UL
      );

    pomodoroRunning = true;
    pomodoroPaused = false;

    robotMode =
      MODE_POMODORO;

    currentPage = 4;
  }

  else if (
    command == "pause"
  ) {

    if (
      pomodoroRunning
    ) {

      pomodoroRemainingSeconds =
        (
          pomodoroEndMillis -
          millis()
        ) / 1000;

      pomodoroRunning = false;
      pomodoroPaused = true;
    }
  }

  else if (
    command == "reset"
  ) {

    pomodoroRunning = false;
    pomodoroPaused = false;

    pomodoroRemainingSeconds =
      pomodoroMinutes * 60;

    robotMode =
      MODE_POMODORO;

    currentPage = 4;
  }

  else {

    server.send(
      400,
      "application/json",
      "{\"error\":\"Unknown timer command\"}"
    );

    return;
  }

  publishStatus();

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}

// ============================================================
// ROOT
// ============================================================

void handleRoot() {

  String html =
    "<html>"
    "<head>"
    "<meta name='viewport' "
    "content='width=device-width'>"
    "</head>"
    "<body>"
    "<h1>Robot Online</h1>"
    "<p>ESP32-S3 Robot</p>"
    "<p>IP: "
    + WiFi.localIP().toString() +
    "</p>"
    "</body>"
    "</html>";

  server.send(
    200,
    "text/html",
    html
  );
}

// ============================================================
// SERVER
// ============================================================

void setupServer() {

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );

  server.on(
    "/api/status",
    HTTP_GET,
    handleApiStatus
  );

  server.on(
    "/api/emotion",
    HTTP_POST,
    handleApiEmotion
  );

  server.on(
    "/api/message",
    HTTP_POST,
    handleApiMessage
  );

  server.on(
    "/api/pet",
    HTTP_POST,
    handleApiPet
  );

  server.on(
    "/api/reminder",
    HTTP_POST,
    handleApiReminder
  );

  server.on(
    "/api/todo",
    HTTP_POST,
    handleApiTodo
  );

  server.on(
    "/api/mode",
    HTTP_POST,
    handleApiMode
  );

  server.on(
    "/api/timer",
    HTTP_POST,
    handleApiTimer
  );

  server.begin();

  Serial.println(
    "Web server started"
  );
}

// ============================================================
// WIFI
// ============================================================

void connectWiFi() {

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.print(
    "Connecting WiFi"
  );

  int attempts = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    attempts < 40
  ) {

    delay(500);

    Serial.print(".");

    attempts++;
  }

  Serial.println();

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    Serial.println(
      "WiFi connected!"
    );

    Serial.print(
      "IP: "
    );

    Serial.println(
      WiFi.localIP()
    );
  }

  else {

    Serial.println(
      "WiFi connection failed"
    );
  }
}

// ============================================================
// TIME
// ============================================================

void setupTime() {

  // Jordan = UTC+3
  configTime(
    3 * 3600,
    0,
    "pool.ntp.org",
    "time.nist.gov"
  );

  Serial.println(
    "NTP configured"
  );
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(
    115200
  );

  delay(500);

  Serial.println();
  Serial.println(
    "=============================="
  );

  Serial.println(
    "ESP32-S3 ROBOT"
  );

  Serial.println(
    "=============================="
  );

  // Button
  pinMode(
    BOOT_BUTTON,
    INPUT_PULLUP
  );

  loadReminders();
  loadTodoItems();
  lastTodoAutoShow = millis();

  // OLED
  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      0x3C
    )
  ) {

    Serial.println(
      "OLED INIT FAILED"
    );

    while (true) {
      delay(100);
    }
  }

  Serial.println(
    "OLED INIT OK"
  );

  // ==========================================================
  // ROTATION 0
  // ==========================================================

  display.setRotation(0);

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  centerText(
    "ROBOT",
    20,
    2
  );

  centerText(
    "Starting...",
    45,
    1
  );

  display.display();

  // Initial timers
  lastHungerUpdate =
    millis();

  lastSleepActivity =
    millis();

  lastPetInteraction =
    millis();

  lastAdviceTime =
    millis();

  autoInfoStarted =
    millis();

  nextBlinkTime =
    millis() + 4000;

  // WiFi
  connectWiFi();

  // Time
  setupTime();

  // MQTT
  secureClient.setInsecure();

  configureAioTopics();

  MQTT_connect();

  // Weather
  lastWeatherUpdate = 0;

  updateWeather();

  // Server
  setupServer();

  // Default
  setExpression(
    EXP_NORMAL
  );

  robotMode =
    MODE_PET;

  currentPage =
    1;

  Serial.println(
    "Robot ready!"
  );

  Serial.print(
    "IP: "
  );

  Serial.println(
    WiFi.localIP()
  );
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  // Web
  server.handleClient();

  // MQTT
  checkMQTT();

  // Pet
  updateHunger();

  checkHungerMessage();

  updatePetSleep();

  // Advice
  updateAdvice();

  // Temporary messages
  updateTemporaryMessage();

  // Auto clock/weather
  updateAutoInformation();

  // Night
  updateAutomaticNightMode();

  // Pomodoro
  updatePomodoro();

  updateReminderAlerts();

  updateAutoTodoPage();

  // Weather
  updateWeather();

  // Button
  handleButton();

  // MQTT status
  if (
    millis() -
    lastStatusPublish >=
    STATUS_PUBLISH_INTERVAL
  ) {

    lastStatusPublish =
      millis();

    publishStatus();
  }

  // OLED
  static unsigned long lastDisplay = 0;

  if (
    millis() -
    lastDisplay >=
    50
  ) {

    lastDisplay =
      millis();

    display.clearDisplay();

    if (
      currentPage == 1
    ) {

      if (
        robotMode == MODE_NIGHT ||
        sleeping
      ) {

        drawSleepyFace();
      }

      else {

        drawFace();
      }
    }

    else if (
      currentPage == 2
    ) {

      drawClockPage();
    }

    else if (
      currentPage == 3
    ) {

      drawWeatherPage();
    }

    else if (
      currentPage == 4
    ) {

      drawPomodoroPage();
    }

    else if (
      currentPage == 5
    ) {

      drawMessagePage();
    }

    else if (
      currentPage == 6
    ) {

      drawReminderPage();
    }

    else if (
      currentPage == 7
    ) {

      drawTodoPage();
    }

    else if (
      currentPage == 8
    ) {

      drawStatusPage();
    }

    display.display();
  }

  delay(2);
}
