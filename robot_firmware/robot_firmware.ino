#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <string.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 4
#define OLED_SCL 5
#define BOOT_BUTTON 0

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);

// Adafruit IO cloud bridge. Fill these from your Adafruit IO account.
const char* AIO_SERVER = "io.adafruit.com";
const uint16_t AIO_SERVERPORT = 8883;
const char* AIO_USERNAME = "YOUR_AIO_USERNAME";
const char* AIO_KEY = "YOUR_AIO_KEY";
WiFiClientSecure aioNetworkClient;
Adafruit_MQTT_Client aioMqtt(&aioNetworkClient, AIO_SERVER, AIO_SERVERPORT,
                             AIO_USERNAME, AIO_KEY);
char aioCommandTopic[96];
char aioStatusTopic[96];
Adafruit_MQTT_Subscribe aioCommand(&aioMqtt, aioCommandTopic);
Adafruit_MQTT_Publish aioStatus(&aioMqtt, aioStatusTopic);
unsigned long lastAioConnectAttempt = 0;
unsigned long lastAioStatusPublish = 0;

String lastRobotMessage;
uint16_t messagePageOffset = 0;
unsigned long lastMessagePageFlip = 0;

String pomodoroMode = "focus";
bool pomodoroRunning = false;
uint32_t pomodoroRemainingSeconds = 0;
uint32_t pomodoroInitialSeconds = 0;
unsigned long pomodoroStartedAt = 0;

// ================= WIFI =================

const char* WIFI_SSID = "Virus";
const char* WIFI_PASSWORD = "orange2020";

const long GMT_OFFSET_SEC = 3 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// ================= PAGES =================

int currentPage = 1;

// ================= EXPRESSIONS =================

#define NORMAL     0
#define HAPPY      1
#define ANGRY      2
#define SAD        3
#define SLEEPY     4
#define SURPRISED  5
#define LOVE       6

int expression = NORMAL;

// ================= EYES =================

const int LEFT_X  = 34;
const int RIGHT_X = 94;

float pupilX = 0;
float pupilY = 0;

float pupilTargetX = 0;
float pupilTargetY = 0;

// ================= TIMING =================

unsigned long lastFrame = 0;

unsigned long lastBlink = 0;
unsigned long blinkStart = 0;

unsigned long lastLook = 0;

bool blinking = false;

const int blinkDuration = 150;

unsigned long nextBlinkTime = 3000;
unsigned long nextLookTime = 2500;

// ================= BUTTON =================

bool lastButtonState = HIGH;

unsigned long pressStart = 0;
unsigned long lastRelease = 0;

bool waitingForDouble = false;

const unsigned long LONG_PRESS = 1000;
const unsigned long DOUBLE_TIME = 350;

// =====================================================
// WEATHER
// =====================================================

float weatherTemperature = 0;
float weatherHigh = 0;
float weatherLow = 0;

int weatherCode = -1;

bool weatherReady = false;

unsigned long lastWeatherUpdate = 0;

const unsigned long WEATHER_UPDATE_INTERVAL =
  30UL * 60UL * 1000UL;

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {

    Serial.println("OLED FAILED");

    while (true);
  }

  display.setRotation(0);

  display.clearDisplay();
  display.display();

  randomSeed(analogRead(1));

  lastBlink = millis();
  lastLook = millis();

  // ================= WIFI =================

  connectWiFi();
  configureAioTopics();
  if (!aioMqtt.subscribe(&aioCommand))
    Serial.println("Adafruit IO command subscription setup failed");
  aioNetworkClient.setInsecure();
  beginControlServer();

  // ================= TIME =================

  configTime(
    GMT_OFFSET_SEC,
    DAYLIGHT_OFFSET_SEC,
    "pool.ntp.org",
    "time.nist.gov"
  );

  // ================= WEATHER =================

  if (WiFi.status() == WL_CONNECTED) {

    updateWeather();
  }

  drawFace();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  unsigned long now = millis();

  server.handleClient();
  handleButton();
  maintainAioConnection();
  readAioCommands();
  // Network handlers may start or resume the timer after `now` was sampled.
  // Refresh it before the Pomodoro tick to avoid unsigned underflow.
  now = millis();

  // ================= FACE =================

  if (currentPage == 1) {

    updateEyes();

    if (now - lastFrame >= 30) {

      lastFrame = now;

      drawFace();
    }
  }

  // ================= CLOCK =================

  else if (currentPage == 2) {

    if (now - lastFrame >= 1000) {

      lastFrame = now;

      drawClockPage();
    }
  }

  // ================= WEATHER =================

  else if (currentPage == 3) {

    if (WiFi.status() == WL_CONNECTED &&
        now - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL) {

      updateWeather();
    }

    if (now - lastFrame >= 1000) {

      lastFrame = now;

      drawWeatherPage();
    }
  }

  // ================= POMODORO =================

  else if (currentPage == 4 && now - lastFrame >= 1000) {

    lastFrame = now;

    if (pomodoroRunning) {

      uint32_t elapsed =
        (now - pomodoroStartedAt) / 1000UL;

      pomodoroRemainingSeconds =
        elapsed >= pomodoroInitialSeconds
        ? 0
        : pomodoroInitialSeconds - elapsed;

      if (pomodoroRemainingSeconds == 0)
        pomodoroRunning = false;
    }

    drawPomodoroPage();
  }

  // ================= MESSAGE / STATUS =================

  else if ((currentPage == 5 || currentPage == 6) &&
           now - lastFrame >= 1000) {

    lastFrame = now;

    drawPage();
  }
}

// =====================================================
// WIFI
// =====================================================

void connectWiFi() {

  Serial.println();
  Serial.println("Connecting to WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 15000) {

    delay(500);

    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("WiFi connected!");

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

  } else {

    Serial.println("WiFi connection failed!");
  }
}

// =====================================================
// WEB CONTROL API
// =====================================================

void addCorsHeaders() {

  server.sendHeader(
    "Access-Control-Allow-Origin",
    "*"
  );

  server.sendHeader(
    "Access-Control-Allow-Methods",
    "GET, POST, OPTIONS"
  );

  server.sendHeader(
    "Access-Control-Allow-Headers",
    "Content-Type"
  );

  server.sendHeader(
    "Access-Control-Max-Age",
    "600"
  );

  if (server.hasHeader(
        "Access-Control-Request-Private-Network")) {

    server.sendHeader(
      "Access-Control-Allow-Private-Network",
      "true"
    );
  }
}

// =====================================================
// API ERROR
// =====================================================

void sendApiError(
  int statusCode,
  const char* message
) {

  JsonDocument response;

  response["ok"] = false;
  response["error"] = message;

  String payload;

  serializeJson(
    response,
    payload
  );

  addCorsHeaders();

  server.send(
    statusCode,
    "application/json",
    payload
  );
}

// =====================================================
// API STATUS
// =====================================================

void handleApiStatus() {

  JsonDocument response;

  response["ok"] = true;
  response["device"] = "ESP32-S3";

  response["wifi"] =
    WiFi.status() == WL_CONNECTED
    ? "connected"
    : "disconnected";

  response["ip"] =
    WiFi.localIP().toString();

  response["signalDbm"] =
    WiFi.status() == WL_CONNECTED
    ? WiFi.RSSI()
    : 0;

  response["uptimeSeconds"] =
    millis() / 1000UL;

  response["expression"] =
    expression;

  response["page"] =
    currentPage;

  response["pomodoroMode"] =
    pomodoroMode;

  response["pomodoroRunning"] =
    pomodoroRunning;

  response["pomodoroRemainingSeconds"] =
    pomodoroRemainingSeconds;

  String payload;

  serializeJson(
    response,
    payload
  );

  addCorsHeaders();

  server.send(
    200,
    "application/json",
    payload
  );
}

// =====================================================
// API EMOTION
// =====================================================

void handleApiEmotion() {

  JsonDocument request;

  DeserializationError error =
    deserializeJson(
      request,
      server.arg("plain")
    );

  if (error)
    return sendApiError(
      400,
      "Invalid JSON body"
    );

  String name =
    request["emotion"] | "";

  name.toLowerCase();

  if (name == "happy")
    expression = HAPPY;

  else if (name == "angry")
    expression = ANGRY;

  else if (name == "sad")
    expression = SAD;

  else if (name == "sleepy")
    expression = SLEEPY;

  else if (name == "surprised")
    expression = SURPRISED;

  else if (name == "love")
    expression = LOVE;

  else if (
    name == "neutral" ||
    name == "normal"
  )
    expression = NORMAL;

  else
    return sendApiError(
      400,
      "Unsupported emotion"
    );

  currentPage = 1;

  pupilTargetX = 0;
  pupilTargetY = 0;

  drawFace();

  addCorsHeaders();

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}

// =====================================================
// API MESSAGE
// =====================================================

void handleApiMessage() {

  JsonDocument request;

  DeserializationError error =
    deserializeJson(
      request,
      server.arg("plain")
    );

  if (error)
    return sendApiError(
      400,
      "Invalid JSON body"
    );

  String message =
    request["message"] | "";

  message.trim();

  if (message.length() == 0)
    return sendApiError(
      400,
      "Message cannot be empty"
    );

  lastRobotMessage =
    message.substring(0, 84);

  messagePageOffset = 0;

  lastMessagePageFlip =
    millis();

  currentPage = 5;

  drawPage();

  addCorsHeaders();

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}

// =====================================================
// API POMODORO
// =====================================================

void handleApiPomodoro() {

  JsonDocument request;

  DeserializationError error =
    deserializeJson(
      request,
      server.arg("plain")
    );

  if (error)
    return sendApiError(
      400,
      "Invalid JSON body"
    );

  String eventName =
    request["event"] | "";

  String mode =
    request["mode"] | "focus";

  mode.toLowerCase();

  if (
    mode != "focus" &&
    mode != "short" &&
    mode != "long"
  )
    return sendApiError(
      400,
      "Unsupported timer mode"
    );

  // Check whether the website actually sent
  // remainingSeconds.
  bool hasRequestedSeconds =
    request["remainingSeconds"].is<uint32_t>() ||
    request["remainingSeconds"].is<int>() ||
    request["remainingSeconds"].is<long>();

  uint32_t requestedSeconds = 0;

  if (hasRequestedSeconds) {

    requestedSeconds =
      request["remainingSeconds"].as<uint32_t>();

  } else {

    requestedSeconds =
      pomodoroRemainingSeconds;
  }

  // ===================================================
  // START / RESUME
  // ===================================================

  if (
    eventName == "start" ||
    eventName == "resume"
  ) {

    /*
      If the website sends a timer value,
      use that exact value.

      If it doesn't send one, keep the current
      value if available.

      If there is no current value at all,
      use the normal Pomodoro defaults.
    */

    if (requestedSeconds == 0) {

      if (pomodoroRemainingSeconds > 0) {

        requestedSeconds =
          pomodoroRemainingSeconds;

      } else {

        if (mode == "short")
          requestedSeconds = 5 * 60;

        else if (mode == "long")
          requestedSeconds = 15 * 60;

        else
          requestedSeconds = 25 * 60;
      }
    }

    pomodoroMode =
      mode;

    pomodoroRemainingSeconds =
      requestedSeconds;

    pomodoroInitialSeconds =
      requestedSeconds;

    pomodoroStartedAt =
      millis();

    pomodoroRunning =
      pomodoroRemainingSeconds > 0;

  }

  // ===================================================
  // PAUSE
  // ===================================================

  else if (eventName == "pause") {

    pomodoroMode =
      mode;

    /*
      First calculate the actual live remaining time
      before stopping the timer.
    */

    if (pomodoroRunning) {

      unsigned long now =
        millis();

      uint32_t elapsed =
        (now - pomodoroStartedAt) / 1000UL;

      pomodoroRemainingSeconds =
        elapsed >= pomodoroInitialSeconds
        ? 0
        : pomodoroInitialSeconds - elapsed;
    }

    /*
      If the website supplied a remaining value,
      use it. Otherwise keep the calculated live value.
    */

    if (hasRequestedSeconds) {

      pomodoroRemainingSeconds =
        requestedSeconds;
    }

    pomodoroRunning =
      false;
  }

  // ===================================================
  // RESET
  // ===================================================

  else if (eventName == "reset") {

    pomodoroMode =
      mode;

    if (requestedSeconds == 0) {

      if (mode == "short")
        requestedSeconds = 5 * 60;

      else if (mode == "long")
        requestedSeconds = 15 * 60;

      else
        requestedSeconds = 25 * 60;
    }

    pomodoroRemainingSeconds =
      requestedSeconds;

    pomodoroInitialSeconds =
      requestedSeconds;

    pomodoroRunning =
      false;
  }

  // ===================================================
  // SESSION COMPLETE
  // ===================================================

  else if (
    eventName == "session-complete"
  ) {

    String nextMode =
      request["nextMode"] | mode;

    nextMode.toLowerCase();

    if (
      nextMode != "focus" &&
      nextMode != "short" &&
      nextMode != "long"
    )
      return sendApiError(
        400,
        "Unsupported next timer mode"
      );

    pomodoroMode =
      nextMode;

    pomodoroRemainingSeconds =
      request["remainingSeconds"] | 0;

    pomodoroInitialSeconds =
      pomodoroRemainingSeconds;

    pomodoroRunning =
      false;
  }

  else {

    return sendApiError(
      400,
      "Unsupported timer event"
    );
  }

  // ===================================================
  // SHOW POMODORO IMMEDIATELY
  // ===================================================

  currentPage = 4;

  /*
    Force the display to update immediately instead
    of waiting for the next 1-second loop.
  */

  lastFrame = millis();

  drawPomodoroPage();

  addCorsHeaders();

  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}

// =====================================================
// SERVER
// =====================================================

void beginControlServer() {

  const char* corsRequestHeaders[] = {
    "Access-Control-Request-Private-Network"
  };

  server.collectHeaders(
    corsRequestHeaders,
    1
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
    "/api/timer",
    HTTP_POST,
    handleApiPomodoro
  );

  server.onNotFound([]() {

    if (
      server.method() ==
      HTTP_OPTIONS
    ) {

      addCorsHeaders();

      server.send(204);

    } else {

      sendApiError(
        404,
        "API route not found"
      );
    }
  });

  server.begin();

  Serial.print(
    "Robot control API: http://"
  );

  Serial.println(
    WiFi.localIP()
  );
}

void configureAioTopics() {
  snprintf(aioCommandTopic, sizeof(aioCommandTopic), "%s/feeds/robot-command/json", AIO_USERNAME);
  snprintf(aioStatusTopic, sizeof(aioStatusTopic), "%s/feeds/robot-status/json", AIO_USERNAME);
}

void maintainAioConnection() {
  if (WiFi.status() != WL_CONNECTED ||
      strcmp(AIO_USERNAME, "YOUR_AIO_USERNAME") == 0 ||
      strcmp(AIO_KEY, "YOUR_AIO_KEY") == 0)
    return;

  if (aioMqtt.connected()) {
    if (millis() - lastAioStatusPublish >= 15000UL)
      publishAioStatus();
    return;
  }

  if (millis() - lastAioConnectAttempt < 5000UL)
    return;
  lastAioConnectAttempt = millis();

  int8_t result = aioMqtt.connect();
  if (result == 0) {
    Serial.println("Adafruit IO connected");
    Serial.println("Listening for robot-command feed");
    publishAioStatus();
  } else {
    Serial.print("Adafruit IO connection failed: ");
    Serial.println(aioMqtt.connectErrorString(result));
    aioMqtt.disconnect();
  }
}

void readAioCommands() {
  if (!aioMqtt.connected())
    return;

  Adafruit_MQTT_Subscribe* subscription;
  while ((subscription = aioMqtt.readSubscription(5))) {
    if (subscription == &aioCommand) {
      Serial.print("MQTT command received: ");
      Serial.println((const char*)aioCommand.lastread);
      handleAioCommand((const char*)aioCommand.lastread);
    }
  }
}

void handleAioCommand(const char* rawPayload) {
  JsonDocument envelope;
  DeserializationError envelopeError = deserializeJson(envelope, rawPayload);
  if (envelopeError) {
    Serial.print("MQTT envelope JSON error: ");
    Serial.println(envelopeError.c_str());
    return;
  }

  String commandText;
  if (!envelope["value"].isNull())
    commandText = envelope["value"].as<String>();
  else if (!envelope["data"]["value"].isNull())
    commandText = envelope["data"]["value"].as<String>();
  else
    commandText = envelope["last_value"] | "";
  JsonDocument command;
  DeserializationError commandError = deserializeJson(command, commandText);
  if (commandError) {
    Serial.print("MQTT command JSON error: ");
    Serial.println(commandError.c_str());
    return;
  }

  String path = command["path"] | "";
  Serial.print("MQTT command path: ");
  Serial.println(path);
  JsonVariantConst payload = command["payload"];

  if (path == "/api/emotion") {
    String name = payload["emotion"] | "";
    name.toLowerCase();
    if (name == "normal" || name == "neutral") expression = NORMAL;
    else if (name == "happy") expression = HAPPY;
    else if (name == "angry") expression = ANGRY;
    else if (name == "sad") expression = SAD;
    else if (name == "sleepy") expression = SLEEPY;
    else if (name == "surprised") expression = SURPRISED;
    else if (name == "love") expression = LOVE;
    else return;

    currentPage = 1;
    pupilTargetX = pupilTargetY = 0;
    drawFace();
  } else if (path == "/api/message") {
    String message = payload["message"] | "";
    message.trim();
    if (message.length() == 0)
      return;
    lastRobotMessage = message.substring(0, 84);
    messagePageOffset = 0;
    lastMessagePageFlip = millis();
    currentPage = 5;
    drawPage();
  } else if (path == "/api/timer") {
    String eventName = payload["event"] | "";
    String mode = payload["mode"] | "focus";
    mode.toLowerCase();
    if (mode != "focus" && mode != "short" && mode != "long")
      return;

    uint32_t requestedSeconds = payload["remainingSeconds"] | pomodoroRemainingSeconds;
    if (eventName == "start" || eventName == "resume") {
      pomodoroMode = mode;
      pomodoroRemainingSeconds = requestedSeconds;
      pomodoroInitialSeconds = requestedSeconds;
      pomodoroStartedAt = millis();
      pomodoroRunning = pomodoroRemainingSeconds > 0;
    } else if (eventName == "pause" || eventName == "reset") {
      pomodoroMode = mode;
      pomodoroRemainingSeconds = requestedSeconds;
      pomodoroRunning = false;
    } else if (eventName == "session-complete") {
      String nextMode = payload["nextMode"] | mode;
      if (nextMode != "focus" && nextMode != "short" && nextMode != "long")
        return;
      pomodoroMode = nextMode;
      pomodoroRemainingSeconds = payload["remainingSeconds"] | 0;
      pomodoroRunning = false;
    } else {
      return;
    }
    Serial.print("Pomodoro ");
    Serial.print(eventName);
    Serial.print(" | mode: ");
    Serial.print(pomodoroMode);
    Serial.print(" | remaining: ");
    Serial.print(pomodoroRemainingSeconds);
    Serial.println(" seconds");
    currentPage = 4;
    lastFrame = millis();
    drawPomodoroPage();
  } else {
    return;
  }

  publishAioStatus();
}

void publishAioStatus() {
  if (!aioMqtt.connected())
    return;

  JsonDocument state;
  state["ok"] = true;
  state["device"] = "ESP32-S3";
  state["wifi"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
  state["ip"] = WiFi.localIP().toString();
  state["signalDbm"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  state["uptimeSeconds"] = millis() / 1000UL;

  String statusValue;
  serializeJson(state, statusValue);
  JsonDocument outputEnvelope;
  outputEnvelope["value"] = statusValue;
  String output;
  serializeJson(outputEnvelope, output);
  aioStatus.publish(output.c_str());
  lastAioStatusPublish = millis();
}

// =====================================================
// POMODORO DISPLAY
// =====================================================

void drawPomodoroPage() {

  /*
    Keep the displayed value synchronized with the
    real running timer every time the page is drawn.
  */

  if (pomodoroRunning) {

    unsigned long now =
      millis();

    uint32_t elapsed =
      (now - pomodoroStartedAt) / 1000UL;

    pomodoroRemainingSeconds =
      elapsed >= pomodoroInitialSeconds
      ? 0
      : pomodoroInitialSeconds - elapsed;

    if (pomodoroRemainingSeconds == 0)
      pomodoroRunning = false;
  }

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(1);

  display.setCursor(
    0,
    0
  );

  display.print(
    "FOCUS TIMER"
  );

  display.drawLine(
    0, 11,
    127, 11,
    SSD1306_WHITE
  );

  display.setCursor(
    0,
    18
  );

  if (pomodoroMode == "short")
    display.print("SHORT BREAK");

  else if (pomodoroMode == "long")
    display.print("LONG BREAK");

  else
    display.print("FOCUS SESSION");

  uint32_t seconds =
    pomodoroRemainingSeconds;

  char timerText[8];

  snprintf(
    timerText,
    sizeof(timerText),
    "%02lu:%02lu",
    (unsigned long)(
      seconds / 60
    ),
    (unsigned long)(
      seconds % 60
    )
  );

  display.setTextSize(2);

  display.setCursor(
    30,
    38
  );

  display.print(
    timerText
  );

  display.display();
}

// =====================================================
// BUTTON
// =====================================================

void handleButton() {

  bool buttonState =
    digitalRead(
      BOOT_BUTTON
    );

  unsigned long now =
    millis();

  if (
    lastButtonState == HIGH &&
    buttonState == LOW
  ) {

    pressStart = now;
  }

  if (
    lastButtonState == LOW &&
    buttonState == HIGH
  ) {

    unsigned long pressTime =
      now - pressStart;

    if (
      pressTime >= LONG_PRESS
    ) {

      waitingForDouble = false;

      longPressAction();

    } else {

      if (
        waitingForDouble &&
        now - lastRelease <= DOUBLE_TIME
      ) {

        waitingForDouble = false;

        doublePressAction();

      } else {

        waitingForDouble = true;

        lastRelease = now;
      }
    }
  }

  if (
    waitingForDouble &&
    now - lastRelease > DOUBLE_TIME
  ) {

    waitingForDouble = false;

    singlePressAction();
  }

  lastButtonState =
    buttonState;
}

// =====================================================
// SINGLE PRESS
// =====================================================

void singlePressAction() {

  currentPage++;

  if (currentPage > 6)
    currentPage = 1;

  if (currentPage == 1) {

    drawFace();

  }

  else if (currentPage == 3) {

    if (!weatherReady) {

      updateWeather();
    }

    drawWeatherPage();

  }

  else {

    drawPage();
  }
}

// =====================================================
// DOUBLE PRESS
// =====================================================

void doublePressAction() {

  currentPage = 1;

  drawFace();
}

// =====================================================
// LONG PRESS
// =====================================================

void longPressAction() {

  if (currentPage == 1)

    nextExpression();

  else

    showLongPress();
}

// =====================================================
// NEXT EXPRESSION
// =====================================================

void nextExpression() {

  expression++;

  if (expression > LOVE)
    expression = NORMAL;

  pupilTargetX = 0;
  pupilTargetY = 0;

  drawFace();
}

// =====================================================
// UPDATE EYES
// =====================================================

void updateEyes() {

  unsigned long now =
    millis();

  if (
    now - lastLook >=
    nextLookTime
  ) {

    lastLook = now;

    int direction =
      random(0, 9);

    if (direction == 0) {

      pupilTargetX = -10;
      pupilTargetY = 0;

    }

    else if (direction == 1) {

      pupilTargetX = 10;
      pupilTargetY = 0;

    }

    else if (direction == 2) {

      pupilTargetX = 0;
      pupilTargetY = -7;

    }

    else if (direction == 3) {

      pupilTargetX = 0;
      pupilTargetY = 7;

    }

    else {

      pupilTargetX =
        random(-6, 7);

      pupilTargetY =
        random(-4, 5);
    }

    nextLookTime =
      random(1800, 4500);
  }

  pupilX +=
    (pupilTargetX - pupilX)
    * 0.12;

  pupilY +=
    (pupilTargetY - pupilY)
    * 0.12;

  if (
    !blinking &&
    now - lastBlink >=
    nextBlinkTime
  ) {

    blinking = true;

    blinkStart = now;
  }

  if (blinking) {

    if (
      now - blinkStart >=
      blinkDuration
    ) {

      blinking = false;

      lastBlink = now;

      nextBlinkTime =
        random(2500, 5500);
    }
  }
}

// =====================================================
// DRAW FACE
// =====================================================

void drawFace() {

  display.clearDisplay();

  if (blinking) {

    drawBlinkFace();

    display.display();

    return;
  }

  switch (expression) {

    case NORMAL:
      drawNormal();
      break;

    case HAPPY:
      drawHappy();
      break;

    // ===============================================
    // ANGRY NOW USES THE OLD SAD FACE
    // ===============================================

    case ANGRY:
      drawSad();
      break;

    // ===============================================
    // SAD NOW USES THE OLD ANGRY FACE + TEAR
    // ===============================================

    case SAD:
      drawAngry();
      break;

    case SLEEPY:
      drawSleepy();
      break;

    case SURPRISED:
      drawSurprised();
      break;

    case LOVE:
      drawLove();
      break;
  }

  display.display();
}

// =====================================================
// NORMAL
// =====================================================

void drawNormal() {

  drawEye(
    LEFT_X,
    30,
    19,
    23
  );

  drawEye(
    RIGHT_X,
    30,
    19,
    23
  );

  drawPupil(
    LEFT_X,
    30
  );

  drawPupil(
    RIGHT_X,
    30
  );

  display.drawLine(
    59, 52,
    69, 52,
    SSD1306_WHITE
  );
}

// =====================================================
// HAPPY
// =====================================================

void drawHappy() {

  drawEye(
    LEFT_X,
    30,
    19,
    24
  );

  drawEye(
    RIGHT_X,
    30,
    19,
    24
  );

  int happyPupilY = -3;

  display.fillCircle(
    LEFT_X,
    30 + happyPupilY,
    8,
    SSD1306_BLACK
  );

  display.fillCircle(
    RIGHT_X,
    30 + happyPupilY,
    8,
    SSD1306_BLACK
  );

  display.fillCircle(
    LEFT_X - 3,
    27 + happyPupilY,
    2,
    SSD1306_WHITE
  );

  display.fillCircle(
    RIGHT_X - 3,
    27 + happyPupilY,
    2,
    SSD1306_WHITE
  );

  display.drawLine(
    19, 13,
    39, 9,
    SSD1306_WHITE
  );

  display.drawLine(
    89, 9,
    109, 13,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    49,
    46,
    30,
    17,
    8,
    SSD1306_WHITE
  );

  display.fillCircle(
    55,
    54,
    8,
    SSD1306_WHITE
  );

  display.fillCircle(
    73,
    54,
    8,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    53,
    49,
    22,
    11,
    5,
    SSD1306_BLACK
  );

  display.fillRoundRect(
    54,
    49,
    20,
    5,
    2,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    59,
    56,
    10,
    4,
    2,
    SSD1306_WHITE
  );
}

// =====================================================
// SAD OLD DESIGN
// =====================================================

void drawSad() {

  drawEye(
    LEFT_X,
    33,
    18,
    21
  );

  drawEye(
    RIGHT_X,
    33,
    18,
    21
  );

  drawPupil(
    LEFT_X,
    33
  );

  drawPupil(
    RIGHT_X,
    33
  );

  display.drawLine(
    24, 15,
    43, 20,
    SSD1306_WHITE
  );

  display.drawLine(
    85, 20,
    104, 15,
    SSD1306_WHITE
  );

  display.drawLine(
    58, 55,
    62, 52,
    SSD1306_WHITE
  );

  display.drawLine(
    62, 52,
    66, 52,
    SSD1306_WHITE
  );

  display.drawLine(
    66, 52,
    70, 55,
    SSD1306_WHITE
  );
}

// =====================================================
// ANGRY OLD DESIGN
// =====================================================
// This is now used for SAD.
// The tear is also added here.

void drawAngry() {

  display.drawLine(
    20, 17,
    39, 13,
    SSD1306_WHITE
  );

  display.drawLine(
    89, 13,
    108, 18,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    LEFT_X - 17,
    25,
    34,
    18,
    8,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    RIGHT_X - 17,
    25,
    34,
    18,
    8,
    SSD1306_WHITE
  );

  display.fillTriangle(
    LEFT_X - 18,
    25,
    LEFT_X + 3,
    25,
    LEFT_X - 18,
    31,
    SSD1306_BLACK
  );

  display.fillTriangle(
    RIGHT_X - 3,
    25,
    RIGHT_X + 18,
    25,
    RIGHT_X + 18,
    31,
    SSD1306_BLACK
  );

  drawPupil(
    LEFT_X,
    34
  );

  drawPupil(
    RIGHT_X,
    34
  );

  // SAD MOUTH

  display.drawLine(
    53, 53,
    58, 49,
    SSD1306_WHITE
  );

  display.drawLine(
    58, 49,
    64, 47,
    SSD1306_WHITE
  );

  display.drawLine(
    64, 47,
    70, 49,
    SSD1306_WHITE
  );

  display.drawLine(
    70, 49,
    75, 53,
    SSD1306_WHITE
  );

  // =================================================
  // TEAR
  // =================================================

  display.fillTriangle(
    111,
    43,
    117,
    43,
    114,
    37,
    SSD1306_WHITE
  );

  display.fillCircle(
    114,
    43,
    3,
    SSD1306_WHITE
  );
}

// =====================================================
// SLEEPY
// =====================================================

void drawSleepy() {

  display.fillRoundRect(
    LEFT_X - 20,
    24,
    40,
    16,
    8,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    RIGHT_X - 20,
    24,
    40,
    16,
    8,
    SSD1306_WHITE
  );

  display.fillRect(
    LEFT_X - 21,
    24,
    42,
    9,
    SSD1306_BLACK
  );

  display.fillRect(
    RIGHT_X - 21,
    24,
    42,
    9,
    SSD1306_BLACK
  );

  display.drawLine(
    LEFT_X - 17,
    31,
    LEFT_X - 7,
    28,
    SSD1306_WHITE
  );

  display.drawLine(
    LEFT_X - 7,
    28,
    LEFT_X + 7,
    29,
    SSD1306_WHITE
  );

  display.drawLine(
    LEFT_X + 7,
    29,
    LEFT_X + 16,
    32,
    SSD1306_WHITE
  );

  display.drawLine(
    RIGHT_X - 16,
    32,
    RIGHT_X - 7,
    29,
    SSD1306_WHITE
  );

  display.drawLine(
    RIGHT_X - 7,
    29,
    RIGHT_X + 7,
    28,
    SSD1306_WHITE
  );

  display.drawLine(
    RIGHT_X + 7,
    28,
    RIGHT_X + 17,
    31,
    SSD1306_WHITE
  );

  display.drawLine(
    22, 19,
    42, 21,
    SSD1306_WHITE
  );

  display.drawLine(
    86, 21,
    106, 19,
    SSD1306_WHITE
  );

  display.drawLine(
    57, 53,
    61, 55,
    SSD1306_WHITE
  );

  display.drawLine(
    61, 55,
    67, 55,
    SSD1306_WHITE
  );

  display.drawLine(
    67, 55,
    71, 53,
    SSD1306_WHITE
  );
}

// =====================================================
// SURPRISED
// =====================================================

void drawSurprised() {

  display.drawLine(
    20, 11,
    43, 6,
    SSD1306_WHITE
  );

  display.drawLine(
    85, 6,
    108, 11,
    SSD1306_WHITE
  );

  display.fillCircle(
    LEFT_X,
    31,
    17,
    SSD1306_WHITE
  );

  display.fillCircle(
    RIGHT_X,
    31,
    17,
    SSD1306_WHITE
  );

  display.fillCircle(
    LEFT_X,
    31,
    8,
    SSD1306_BLACK
  );

  display.fillCircle(
    RIGHT_X,
    31,
    8,
    SSD1306_BLACK
  );

  display.fillCircle(
    LEFT_X - 3,
    28,
    2,
    SSD1306_WHITE
  );

  display.fillCircle(
    RIGHT_X - 3,
    28,
    2,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    57,
    49,
    14,
    14,
    6,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    61,
    52,
    6,
    9,
    3,
    SSD1306_BLACK
  );
}

// =====================================================
// LOVE
// =====================================================

void drawLove() {

  display.drawLine(
    24, 17,
    42, 14,
    SSD1306_WHITE
  );

  display.drawLine(
    86, 14,
    104, 17,
    SSD1306_WHITE
  );

  drawEye(
    LEFT_X,
    31,
    18,
    22
  );

  drawEye(
    RIGHT_X,
    31,
    18,
    22
  );

  display.fillCircle(
    LEFT_X,
    31,
    7,
    SSD1306_BLACK
  );

  display.fillCircle(
    RIGHT_X,
    31,
    7,
    SSD1306_BLACK
  );

  drawSmallHeart(
    LEFT_X,
    31,
    5
  );

  drawSmallHeart(
    RIGHT_X,
    31,
    5
  );

  display.drawCircle(
    42,
    50,
    3,
    SSD1306_WHITE
  );

  display.drawCircle(
    86,
    50,
    3,
    SSD1306_WHITE
  );

  display.drawLine(
    55, 49,
    58, 53,
    SSD1306_WHITE
  );

  display.drawLine(
    58, 53,
    62, 56,
    SSD1306_WHITE
  );

  display.drawLine(
    62, 56,
    66, 56,
    SSD1306_WHITE
  );

  display.drawLine(
    66, 56,
    70, 53,
    SSD1306_WHITE
  );

  display.drawLine(
    70, 53,
    73, 49,
    SSD1306_WHITE
  );
}

// =====================================================
// EYE
// =====================================================

void drawEye(
  int x,
  int y,
  int w,
  int h
) {

  display.fillRoundRect(
    x - w,
    y - h / 2,
    w * 2,
    h,
    9,
    SSD1306_WHITE
  );
}

// =====================================================
// PUPIL
// =====================================================

void drawPupil(
  int x,
  int y
) {

  int px =
    x + (int)pupilX;

  int py =
    y + (int)pupilY;

  display.fillCircle(
    px,
    py,
    8,
    SSD1306_BLACK
  );

  display.fillCircle(
    px - 3,
    py - 3,
    2,
    SSD1306_WHITE
  );
}

// =====================================================
// BLINK
// =====================================================

void drawBlinkFace() {

  display.drawLine(
    LEFT_X - 18,
    32,
    LEFT_X + 18,
    32,
    SSD1306_WHITE
  );

  display.drawLine(
    RIGHT_X - 18,
    32,
    RIGHT_X + 18,
    32,
    SSD1306_WHITE
  );

  display.drawLine(
    59, 52,
    64, 54,
    SSD1306_WHITE
  );

  display.drawLine(
    64, 54,
    69, 52,
    SSD1306_WHITE
  );
}

// =====================================================
// SMALL HEART
// =====================================================

void drawSmallHeart(
  int x,
  int y,
  int size
) {

  display.fillCircle(
    x - 2,
    y - 1,
    2,
    SSD1306_WHITE
  );

  display.fillCircle(
    x + 2,
    y - 1,
    2,
    SSD1306_WHITE
  );

  display.fillTriangle(
    x - 4,
    y,
    x + 4,
    y,
    x,
    y + 5,
    SSD1306_WHITE
  );
}

// =====================================================
// CLOCK PAGE
// =====================================================

void drawClockPage() {

  display.clearDisplay();

  struct tm timeinfo;

  if (
    !getLocalTime(
      &timeinfo,
      100
    )
  ) {

    display.setTextColor(
      SSD1306_WHITE
    );

    display.setTextSize(1);

    display.setCursor(
      28,
      28
    );

    display.print(
      "SYNCING..."
    );

    display.display();

    return;
  }

  // ================= TIME =================

  char timeString[9];

  strftime(
    timeString,
    sizeof(timeString),
    "%I:%M %p",
    &timeinfo
  );

  display.setTextSize(2);

  display.setCursor(
    4,
    3
  );

  display.print(
    timeString
  );

  // ================= WIFI =================

  drawWiFiIcon(
    116,
    10
  );

  // ================= DATE =================

  char dateString[20];

  strftime(
    dateString,
    sizeof(dateString),
    "%a %d %b",
    &timeinfo
  );

  display.setTextSize(1);

  display.setCursor(
    38,
    29
  );

  display.print(
    dateString
  );

  // ================= LINE =================

  display.drawLine(
    8,
    45,
    120,
    45,
    SSD1306_WHITE
  );

  // ================= WIFI STATUS =================

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    display.setCursor(
      44,
      52
    );

    display.print(
      "WiFi OK"
    );

  } else {

    display.setCursor(
      32,
      52
    );

    display.print(
      "WiFi OFF"
    );
  }

  display.display();
}

// =====================================================
// WIFI ICON
// =====================================================

void drawWiFiIcon(
  int x,
  int y
) {

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    display.drawLine(
      x - 7,
      y - 5,
      x + 7,
      y + 5,
      SSD1306_WHITE
    );

    display.drawLine(
      x + 7,
      y - 5,
      x - 7,
      y + 5,
      SSD1306_WHITE
    );

    return;
  }

  display.drawLine(
    x - 7,
    y - 3,
    x - 4,
    y - 5,
    SSD1306_WHITE
  );

  display.drawLine(
    x - 4,
    y - 5,
    x,
    y - 7,
    SSD1306_WHITE
  );

  display.drawLine(
    x,
    y - 7,
    x + 4,
    y - 5,
    SSD1306_WHITE
  );

  display.drawLine(
    x + 4,
    y - 5,
    x + 7,
    y - 3,
    SSD1306_WHITE
  );

  display.drawLine(
    x - 5,
    y,
    x - 2,
    y - 2,
    SSD1306_WHITE
  );

  display.drawLine(
    x - 2,
    y - 2,
    x,
    y - 3,
    SSD1306_WHITE
  );

  display.drawLine(
    x,
    y - 3,
    x + 2,
    y - 2,
    SSD1306_WHITE
  );

  display.drawLine(
    x + 2,
    y - 2,
    x + 5,
    y,
    SSD1306_WHITE
  );

  display.fillCircle(
    x,
    y + 3,
    2,
    SSD1306_WHITE
  );
}

// =====================================================
// WEATHER UPDATE
// =====================================================

void updateWeather() {

  if (
    WiFi.status() !=
    WL_CONNECTED
  ) {

    Serial.println(
      "Weather: WiFi not connected"
    );

    weatherReady = false;

    return;
  }

  Serial.println();
  Serial.println(
    "Updating weather..."
  );

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient http;

  String url =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=31.9539"
    "&longitude=35.9106"
    "&current=temperature_2m,weather_code"
    "&daily=temperature_2m_max,temperature_2m_min"
    "&timezone=Asia%2FAmman"
    "&forecast_days=1";

  if (
    !http.begin(
      client,
      url
    )
  ) {

    Serial.println(
      "Weather HTTP begin failed"
    );

    weatherReady = false;

    return;
  }

  http.setTimeout(
    10000
  );

  int httpCode =
    http.GET();

  Serial.print(
    "Weather HTTP code: "
  );

  Serial.println(
    httpCode
  );

  if (
    httpCode ==
    HTTP_CODE_OK
  ) {

    String payload =
      http.getString();

    Serial.println(
      "Weather data received"
    );

    JsonDocument doc;

    DeserializationError error =
      deserializeJson(
        doc,
        payload
      );

    if (error) {

      Serial.print(
        "JSON parsing failed: "
      );

      Serial.println(
        error.c_str()
      );

      weatherReady = false;

      http.end();

      return;
    }

    if (
      !doc["current"]
          ["temperature_2m"]
          .isNull()
    ) {

      weatherTemperature =
        doc["current"]
            ["temperature_2m"]
            .as<float>();
    }

    if (
      !doc["current"]
          ["weather_code"]
          .isNull()
    ) {

      weatherCode =
        doc["current"]
            ["weather_code"]
            .as<int>();
    }

    if (
      !doc["daily"]
          ["temperature_2m_max"][0]
          .isNull()
    ) {

      weatherHigh =
        doc["daily"]
            ["temperature_2m_max"][0]
            .as<float>();
    }

    if (
      !doc["daily"]
          ["temperature_2m_min"][0]
          .isNull()
    ) {

      weatherLow =
        doc["daily"]
            ["temperature_2m_min"][0]
            .as<float>();
    }

    weatherReady = true;

    lastWeatherUpdate =
      millis();

    Serial.println(
      "Weather updated successfully!"
    );

    Serial.print(
      "Temperature: "
    );

    Serial.println(
      weatherTemperature
    );

    Serial.print(
      "Weather code: "
    );

    Serial.println(
      weatherCode
    );

    Serial.print(
      "High: "
    );

    Serial.println(
      weatherHigh
    );

    Serial.print(
      "Low: "
    );

    Serial.println(
      weatherLow
    );

  } else {

    Serial.print(
      "Weather HTTP error: "
    );

    Serial.println(
      httpCode
    );

    weatherReady = false;
  }

  http.end();
}

// =====================================================
// WEATHER DESCRIPTION
// =====================================================

const char* getWeatherDescription(
  int code
) {

  if (code == 0)
    return "Clear sky";

  if (
    code == 1 ||
    code == 2
  )
    return "Partly cloudy";

  if (code == 3)
    return "Cloudy";

  if (
    code == 45 ||
    code == 48
  )
    return "Foggy";

  if (
    code == 51 ||
    code == 53 ||
    code == 55
  )
    return "Drizzle";

  if (
    code == 56 ||
    code == 57
  )
    return "Freezing drizzle";

  if (
    code == 61 ||
    code == 63 ||
    code == 65
  )
    return "Rain";

  if (
    code == 66 ||
    code == 67
  )
    return "Freezing rain";

  if (
    code == 71 ||
    code == 73 ||
    code == 75 ||
    code == 77
  )
    return "Snow";

  if (
    code == 80 ||
    code == 81 ||
    code == 82
  )
    return "Rain showers";

  if (
    code == 85 ||
    code == 86
  )
    return "Snow showers";

  if (code == 95)
    return "Thunderstorm";

  if (
    code == 96 ||
    code == 99
  )
    return "Storm + hail";

  return "Unknown";
}

// =====================================================
// WEATHER ICON
// =====================================================

void drawWeatherIcon(
  int x,
  int y,
  int code
) {

  if (code == 0) {

    display.drawCircle(
      x,
      y,
      6,
      SSD1306_WHITE
    );

    display.drawLine(
      x,
      y - 10,
      x,
      y - 7,
      SSD1306_WHITE
    );

    display.drawLine(
      x,
      y + 7,
      x,
      y + 10,
      SSD1306_WHITE
    );

    display.drawLine(
      x - 10,
      y,
      x - 7,
      y,
      SSD1306_WHITE
    );

    display.drawLine(
      x + 7,
      y,
      x + 10,
      y,
      SSD1306_WHITE
    );

    display.drawLine(
      x - 7,
      y - 7,
      x - 5,
      y - 5,
      SSD1306_WHITE
    );

    display.drawLine(
      x + 5,
      y + 5,
      x + 7,
      y + 7,
      SSD1306_WHITE
    );

    display.drawLine(
      x + 7,
      y - 7,
      x + 5,
      y - 5,
      SSD1306_WHITE
    );

    display.drawLine(
      x - 5,
      y + 5,
      x - 7,
      y + 7,
      SSD1306_WHITE
    );

    return;
  }

  if (code <= 48) {

    display.fillCircle(
      x - 5,
      y + 2,
      6,
      SSD1306_WHITE
    );

    display.fillCircle(
      x + 3,
      y - 1,
      8,
      SSD1306_WHITE
    );

    display.fillRoundRect(
      x - 11,
      y + 2,
      25,
      9,
      4,
      SSD1306_WHITE
    );

    return;
  }

  if (
    code >= 51 &&
    code <= 82
  ) {

    display.fillCircle(
      x - 5,
      y - 1,
      5,
      SSD1306_WHITE
    );

    display.fillCircle(
      x + 3,
      y - 3,
      7,
      SSD1306_WHITE
    );

    display.fillRoundRect(
      x - 11,
      y - 1,
      24,
      8,
      4,
      SSD1306_WHITE
    );

    display.drawLine(
      x - 6,
      y + 9,
      x - 8,
      y + 13,
      SSD1306_WHITE
    );

    display.drawLine(
      x + 1,
      y + 9,
      x - 1,
      y + 13,
      SSD1306_WHITE
    );

    display.drawLine(
      x + 8,
      y + 9,
      x + 6,
      y + 13,
      SSD1306_WHITE
    );

    return;
  }

  if (code >= 95) {

    display.fillCircle(
      x - 5,
      y - 1,
      5,
      SSD1306_WHITE
    );

    display.fillCircle(
      x + 3,
      y - 3,
      7,
      SSD1306_WHITE
    );

    display.fillRoundRect(
      x - 11,
      y - 1,
      24,
      8,
      4,
      SSD1306_WHITE
    );

    display.fillTriangle(
      x + 1,
      y + 5,
      x - 4,
      y + 13,
      x + 1,
      y + 11,
      SSD1306_WHITE
    );

    display.fillTriangle(
      x + 1,
      y + 8,
      x + 6,
      y + 5,
      x + 1,
      y + 14,
      SSD1306_WHITE
    );

    return;
  }

  display.drawLine(
    x - 5,
    y - 6,
    x - 5,
    y + 8,
    SSD1306_WHITE
  );

  display.drawLine(
    x - 11,
    y + 1,
    x + 1,
    y + 1,
    SSD1306_WHITE
  );

  display.drawLine(
    x - 9,
    y - 4,
    x - 1,
    y + 6,
    SSD1306_WHITE
  );

  display.drawLine(
    x - 9,
    y + 6,
    x - 1,
    y - 4,
    SSD1306_WHITE
  );
}

// =====================================================
// WEATHER PAGE
// =====================================================

void drawWeatherPage() {

  display.clearDisplay();

  if (!weatherReady) {

    display.setTextSize(1);

    display.setCursor(
      31,
      20
    );

    display.print(
      "LOADING"
    );

    display.setCursor(
      38,
      36
    );

    display.print(
      "WEATHER"
    );

    display.display();

    return;
  }

  drawWeatherIcon(
    17,
    17,
    weatherCode
  );

  display.setTextSize(2);

  display.setCursor(
    34,
    6
  );

  display.print(
    (int)round(
      weatherTemperature
    )
  );

  display.print(
    (char)247
  );

  display.print(
    "C"
  );

  display.setTextSize(1);

  String description =
    getWeatherDescription(
      weatherCode
    );

  int textWidth =
    description.length() * 6;

  int descriptionX =
    (128 - textWidth) / 2;

  if (descriptionX < 0)
    descriptionX = 0;

  display.setCursor(
    descriptionX,
    31
  );

  display.print(
    description
  );

  display.drawLine(
    8,
    43,
    120,
    43,
    SSD1306_WHITE
  );

  display.setCursor(
    12,
    51
  );

  display.print(
    "H "
  );

  display.print(
    (int)round(
      weatherHigh
    )
  );

  display.print(
    (char)247
  );

  display.print(
    "C"
  );

  display.setCursor(
    72,
    51
  );

  display.print(
    "L "
  );

  display.print(
    (int)round(
      weatherLow
    )
  );

  display.print(
    (char)247
  );

  display.print(
    "C"
  );

  display.display();
}

// =====================================================
// OTHER PAGES
// =====================================================

void drawPage() {

  if (currentPage == 4) {

    drawPomodoroPage();

    return;
  }

  if (currentPage == 5) {

    drawMessagePage();

    return;
  }

  if (currentPage == 6) {

    drawStatusPage();

    return;
  }

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(2);

  display.setCursor(
    10,
    25
  );

  if (currentPage == 2)
    display.print(
      "CLOCK"
    );

  else if (currentPage == 3)
    display.print(
      "WEATHER"
    );

  else if (currentPage == 5)
    display.print(
      "MESSAGE"
    );

  else if (currentPage == 6)
    display.print(
      "STATUS"
    );

  display.display();
}

// =====================================================
// MESSAGE PAGE
// =====================================================

void drawMessagePage() {

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(1);

  display.setCursor(
    0,
    0
  );

  display.print(
    "MESSAGE"
  );

  display.drawLine(
    0,
    10,
    127,
    10,
    SSD1306_WHITE
  );

  if (
    lastRobotMessage.length() == 0
  ) {

    display.setCursor(
      0,
      26
    );

    display.print(
      "No message yet"
    );

    display.display();

    return;
  }

  const uint16_t charsPerPage =
    30;

  if (
    lastRobotMessage.length() >
      charsPerPage &&
    millis() -
      lastMessagePageFlip >=
      2500
  ) {

    messagePageOffset +=
      charsPerPage;

    if (
      messagePageOffset >=
      lastRobotMessage.length()
    )
      messagePageOffset = 0;

    lastMessagePageFlip =
      millis();
  }

  uint16_t messagePageEnd =
    messagePageOffset +
    charsPerPage;

  if (
    messagePageEnd >
    lastRobotMessage.length()
  )
    messagePageEnd =
      lastRobotMessage.length();

  display.setTextSize(2);

  display.setCursor(
    0,
    14
  );

  display.setTextWrap(
    true
  );

  display.print(
    lastRobotMessage.substring(
      messagePageOffset,
      messagePageEnd
    )
  );

  display.setTextWrap(
    false
  );

  display.display();
}

// =====================================================
// STATUS PAGE
// =====================================================

void drawStatusPage() {

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(1);

  display.setCursor(
    0,
    0
  );

  display.print(
    "ROBOT STATUS"
  );

  display.drawLine(
    0,
    10,
    127,
    10,
    SSD1306_WHITE
  );

  const bool connected =
    WiFi.status() ==
    WL_CONNECTED;

  display.setCursor(
    0,
    13
  );

  display.print(
    "WiFi: "
  );

  display.print(
    connected
    ? "Connected"
    : "Offline"
  );

  display.setCursor(
    0,
    24
  );

  display.print(
    "IP: "
  );

  display.print(
    connected
    ? WiFi.localIP().toString()
    : String("--")
  );

  display.setCursor(
    0,
    35
  );

  display.print(
    "Signal: "
  );

  if (connected) {

    display.print(
      WiFi.RSSI()
    );

    display.print(
      " dBm"
    );

  } else {

    display.print(
      "N/A"
    );
  }

  unsigned long uptime =
    millis() / 1000UL;

  char uptimeText[16];

  snprintf(
    uptimeText,
    sizeof(uptimeText),
    "%02lu:%02lu:%02lu",
    uptime / 3600UL,
    (uptime / 60UL) % 60UL,
    uptime % 60UL
  );

  display.setCursor(
    0,
    46
  );

  display.print(
    "Uptime: "
  );

  display.print(
    uptimeText
  );

  display.display();
}

// =====================================================
// LONG PRESS ACTION
// =====================================================

void showLongPress() {

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(2);

  display.setCursor(
    25,
    25
  );

  display.print(
    "ACTION"
  );

  display.display();

  delay(500);

  if (currentPage == 3) {

    drawWeatherPage();

  } else if (currentPage != 1) {

    drawPage();
  }
}
