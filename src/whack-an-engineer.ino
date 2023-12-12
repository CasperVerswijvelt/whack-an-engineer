#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
#include <ESPAsyncWebServer.h>
#include <Keypad.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

#include "CircularBuffer.h"
#include "secrets.h"

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1  // Reset pin # (or -1 if sharing Arduino reset pin)

#define NEOPIXEL_PIN 13
#define NUMPIXELS 17

#define WIFI_ANIMATION_CYCLE_MS 1000

#define RAINBOW_CYCLE_MS 5000
#define GAME_LENGTH 30000

#define INITIAL_TIME_BETWEEN_LED 650
#define INITIAL_TIME_LED_ON 1000
#define MIN_TIME_BETWEEN_LED 350
#define MIN_TIME_LED_ON 500
#define STEP_TIME_BETWEEN_LED 20
#define STEP_TIME_LED_ON 20

#define LED_FADE_MS 100
#define HIT_FADE_MS 450

// LED configuration
Adafruit_NeoPixel pixels =
    Adafruit_NeoPixel(NUMPIXELS, NEOPIXEL_PIN, NEO_GRBW + NEO_KHZ800);

// Key configuration
const byte numRows = 4;  // number of rows on the keypad
const byte numCols = 4;  // number of columns on the keypad
char keymap[numRows][numCols] = {
  {'D', 'E', 'C', 'F'},
  {'6', '5', '7', '4'},
  {'2', '1', '3', '0'},
  {'9', 'A', '8', 'B'}
  };
byte rowPins[numRows] = {5, 18, 19, 4};
byte colPins[numCols] = {12, 14, 27, 26};
Keypad myKeypad =
    Keypad(makeKeymap(keymap), rowPins, colPins, numRows, numCols);

// OLED Screen
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// States

enum GameState { GAME_WIFI_CONNECTING, GAME_IDLE, GAME_STARTING, GAME_PLAYING };

// Millis
unsigned long lastGameStateChange = 0;
unsigned long lastLedOn = 0;
unsigned long lastLedOff = 0;
unsigned long lastButtonPressed = 0;
unsigned long lastHit = 0;
unsigned long lastTimeLeftSent = 0;
unsigned long lastWsMsgSent = 0;

// Game states
int currentLedIdx = -1;
int lastHitLedIdx = -1;
int score = 0;
bool lastHitWasSucces = false;
int lastSecondsLeft = 0;
CircularBuffer<int, 5> previousLedIndices;

GameState state = GAME_WIFI_CONNECTING;
int previousWifiState = -1;

int timeBetweenLED = INITIAL_TIME_BETWEEN_LED;
int timeLEDOn = INITIAL_TIME_LED_ON;

// Webserver
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

void setup() {
  Serial.begin(115200);
  pixels.begin();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE);  // Draw white text
  display.cp437(true);
  display.setTextWrap(false);
  drawTitle();

  // Mount filesystem (for web files)
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
    // No point in connecting to Wi-Fi and starting webserver
    return;
  } else {
    Serial.println("LittleFS Mount Success");
  }

  // Connect to Wi-Fi
  Serial.println("Connecting to Wi-Fi...");
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(WIFI_HOST);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Web server
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  ws.onEvent(onWSEvent);
  server.addHandler(&ws);
  server.begin();
  lastGameStateChange = millis();
}

void loop() {
  // Clean up non connected web socket clients
  //  to avoid memory leaks or something
  ws.cleanupClients();

  unsigned long currentMillis = millis();

  // // Respond to serial messages
  if (Serial.available() > 0) {
    // Don't really care what is sent, report game state
    Serial.read();
    reportGameState();
    reportScore();
    reportTimeLeft(currentMillis);
  }

  // Key scanning
  char pressedKey = myKeypad.getKey();
  bool keyWasPressed = pressedKey != NO_KEY;
  if (keyWasPressed) {
    // Serial.print("Key pressed: ");
    // Serial.print(pressedKey);
    // Serial.print(" in state ");
    // Serial.println(state);
    lastButtonPressed = currentMillis;
  }

  // Print changed Wi-Fi state
  wl_status_t currentWifiStatus = WiFi.status();
  if (currentWifiStatus != previousWifiState) {
    // Serial.print("Wi-Fi state");
    // Serial.println(currentWifiStatus);
    previousWifiState = currentWifiStatus;
  }

  // Send something on websocket to show that it's alive
  if (currentMillis > lastWsMsgSent + 2000) {
    report("pong");
  }

  // Handling for specific game states
  switch (state) {
    case GAME_WIFI_CONNECTING: {
      if (WiFi.status() == WL_CONNECTED) {
        // Serial.println("\nConnected to WiFi");
        // Serial.print("IP address: ");
        // Serial.println(WiFi.localIP());
        // Serial.print("Hostname: ");
        // Serial.println(WiFi.getHostname());
        setGameState(GAME_IDLE, currentMillis);
        break;
      }

      int pointInCycle =
          (currentMillis - lastGameStateChange) % WIFI_ANIMATION_CYCLE_MS;
      int maxIndex = NUMPIXELS - 1;

      float position =
          pointInCycle / (float)WIFI_ANIMATION_CYCLE_MS * maxIndex * 2;

      if (position > maxIndex) {
        position = maxIndex - fmod(position, maxIndex);
      }

      float fadeInPercentage =
          min(1.f, (currentMillis - lastGameStateChange) / 500.f);

      for (int i = 0; i < NUMPIXELS; i++) {
        float distance = abs(i - position);
        float distancePercentage = 1 - distance / maxIndex;
        int brightness =
            255.f * pow(distancePercentage, 5) * pow(fadeInPercentage, 3);
        pixels.setPixelColor(i, pixels.Color(brightness, 0, 0, 0));
      }

      // Skip WI-Fi connection state
      if (keyWasPressed) {
        setGameState(GAME_IDLE, currentMillis);
      }
      break;
    }

    case GAME_IDLE: {
      if (keyWasPressed) {
        setGameState(GAME_STARTING, currentMillis);
        drawGetReady();
        break;
      }
      pixels.rainbow(
          getStartHue(currentMillis), 1, 255,
          min((currentMillis - lastGameStateChange) / 1250.0 * 255, 255.0), true
      );
      break;
    }

    case GAME_STARTING: {
      // Blocking fade out and game start effect

      int fadeOutDuration = 200;
      int flashCount = 3;
      int flashDuration = (flashCount + 1) * 1000;

      if (currentMillis < lastGameStateChange + fadeOutDuration) {
        pixels.rainbow(
            getStartHue(currentMillis), 1, 255,
            255 - (float)(currentMillis - lastGameStateChange) /
                      fadeOutDuration * 200,
            true
        );
      } else if (currentMillis < lastGameStateChange + fadeOutDuration + flashDuration) {
        int minDiff = 1000;
        for (int i = 1; i <= flashCount; i++) {
          minDiff =
              min(minDiff,
                  abs((int)(currentMillis - (lastGameStateChange +
                                             fadeOutDuration + 1000 * i))));
        }
        pixels.fill(
            pixels.Color(max(0.0, 255 - minDiff / 200.0 * 255), 0, 0, 0), 0,
            NUMPIXELS
        );
      } else if (currentMillis < lastGameStateChange + fadeOutDuration + flashDuration + 1000) {
        pixels.clear();
      } else {
        // Reset game states
        timeBetweenLED = INITIAL_TIME_BETWEEN_LED;
        timeLEDOn = INITIAL_TIME_LED_ON;
        score = 0;
        reportScore();
        lastHitLedIdx = -1;
        currentLedIdx = -1;
        previousLedIndices.clear();
        setGameState(GAME_PLAYING, currentMillis);
      }
      break;
    }

    case GAME_PLAYING: {
      reportTimeLeft(currentMillis);

      unsigned long gameEnd = lastGameStateChange + GAME_LENGTH;
      bool gameFinished = currentMillis > gameEnd;

      // Key handling
      if (keyWasPressed) {
        int pressedKeyIdx = hex2int(pressedKey);
        if (pressedKeyIdx >= 8) pressedKeyIdx++;
        lastHitWasSucces = pressedKeyIdx == currentLedIdx;
        if (lastHitWasSucces) {
          // It's a hit!
          // Turn of LED and increment score
          turnOffCurrentLED(currentMillis);
          score++;
          reportScore();
          drawTimerAndScore(lastSecondsLeft, score);
          // Serial.print("Score: ");
          // Serial.println(score);
          // More faster!
          timeBetweenLED =
              max(MIN_TIME_BETWEEN_LED, timeBetweenLED - STEP_TIME_BETWEEN_LED);
          timeLEDOn = max(MIN_TIME_LED_ON, timeLEDOn - STEP_TIME_LED_ON);
        } else {
          // Serial.println("Incorrect!");
        }

        // Save last hit info
        lastHit = currentMillis;
        lastHitLedIdx = pressedKeyIdx;
      }

      // Update game state
      if (currentLedIdx == -1) {
        // No LED on right now, check if we should turn one
        if (currentMillis > (lastLedOff + timeBetweenLED)) {
          // Make sure that new led idx is never same as previous
          if (!gameFinished) turnOnRandomLED(currentMillis);
        }
      } else {
        // A LED is on right now, check if it should be turned off
        if (currentMillis > (lastLedOn + timeLEDOn)) {
          turnOffCurrentLED(currentMillis);
        }
      }

      // Update LEDs according to game state
      pixels.clear();
      if (currentLedIdx != -1) {
        float brightness = 255.0;
        int msSinceStart = currentMillis - lastLedOn;
        int msUntilEnd = timeLEDOn - msSinceStart;

        if (msSinceStart < LED_FADE_MS) {
          brightness *= msSinceStart * 1.0 / LED_FADE_MS;
        } else if (msUntilEnd < LED_FADE_MS) {
          brightness *= msUntilEnd * 1.0 / LED_FADE_MS;
        }
        // White LED only
        pixels.setPixelColor(currentLedIdx, pixels.Color(0, 0, 0, brightness));
      }

      unsigned long hitEffectEnd = lastHit + HIT_FADE_MS;
      bool doHitEffect = lastHitLedIdx != -1 && currentMillis < hitEffectEnd;
      // Hit effect
      if (doHitEffect) {
        float brightness =
            255 - 255.0 * (currentMillis - lastHit) / HIT_FADE_MS;
        pixels.setPixelColor(
            lastHitLedIdx, lastHitWasSucces ? pixels.Color(0, brightness, 0)
                                            : pixels.Color(brightness, 0, 0)
        );
      }

      int silentMs = 1000;
      // Game end
      if (
          // Game time finished
          gameFinished &&
          // No LED's on and all animations finished
          currentLedIdx == -1 &&
          !doHitEffect &&
          currentMillis > hitEffectEnd + silentMs &&
          currentMillis > gameEnd + silentMs
      ) {
        drawFinalScore(score);
        setGameState(GAME_IDLE, currentMillis);
      }
      break;
    }
  }
  pixels.show();
}

void setGameState(GameState newGameState, unsigned long millis) {
  state = newGameState;
  lastGameStateChange = millis;

  reportGameState();
}

void turnOnRandomLED(unsigned long millis) {
  currentLedIdx = random(0, NUMPIXELS - 1);
  // Yes this could be done without a while loop, but I can't be
  //  arsed to do so, it's fast enough this way too
  while (true) {
    // Pick random LED
    currentLedIdx = random(0, NUMPIXELS);
    // Middle led does not have a button, try again
    if (currentLedIdx == 8) continue;
    // New led is one of last x leds, try again
    uint8_t length = previousLedIndices.size();
    bool doContinue = false;
    for (uint8_t i = 0; i < length; i++) {
      if (currentLedIdx == previousLedIndices[(int)i]) {
        doContinue = true;
        break;
      }
    }
    if (doContinue) continue;
    // Led is not far enough away from previous led, try again
    if (!previousLedIndices.isEmpty() &&
        abs(previousLedIndices[0] - currentLedIdx) < 4) {
      continue;
    }
    break;
  }
  lastLedOn = millis;
}

void turnOffCurrentLED(unsigned long millis) {
  previousLedIndices.unshift(currentLedIdx);
  currentLedIdx = -1;
  lastLedOff = millis;
}

int getStartHue(unsigned long currentMillis) {
  return (currentMillis % RAINBOW_CYCLE_MS) / (RAINBOW_CYCLE_MS * 1.0) * 65535;
}

void onWSEvent(
    AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
    void *arg, uint8_t *data, size_t len
) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf(
          "WebSocket client #%u connected from %s\n", client->id(),
          client->remoteIP().toString().c_str()
      );
      reportGameState();
      reportScore();
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void reportGameState() {
  String gameStateString = "gameState ";
  gameStateString.concat(state);
  report(gameStateString);
}

void reportScore() {
  String scoreString = "score ";
  scoreString.concat(score);
  report(scoreString);
}

void reportTimeLeft(unsigned long millis) {
  int msSinceGameStateChange = millis - lastGameStateChange;
  int msUntilGameEnd = GAME_LENGTH - msSinceGameStateChange;
  if (millis > lastTimeLeftSent + 1000) {
    lastSecondsLeft = max(0, (int)ceil(msUntilGameEnd / 1000.0));
    String scoreString = "time ";
    scoreString.concat(lastSecondsLeft);
    report(scoreString);
    lastTimeLeftSent = lastGameStateChange + msSinceGameStateChange -
                       msSinceGameStateChange % 1000;

    drawTimerAndScore(lastSecondsLeft, score);
  }
}

void report(String message) {
  ws.textAll(message);
  Serial.println(message);
  lastWsMsgSent = millis();
}

int hex2int(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}

void drawTitle() {
  display.clearDisplay();
  display.setTextSize(2);
  drawCenteredString("WHACK AN", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3);
  drawCenteredString("ENGINEER", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 3 * 2);
  display.display();
}

void drawGetReady() {
  display.clearDisplay();
  display.setTextSize(3);
  drawCenteredString("GET", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4);
  drawCenteredString("READY", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4 * 3);
  display.display();
}

void drawTimerAndScore(int seconds, int score) {
  int minutes = seconds / 60;
  int remainingSeconds = seconds % 60;

  String clock = "";
  if (minutes < 10) clock.concat("0");
  clock.concat(minutes);
  clock.concat(":");
  if (remainingSeconds < 10) clock.concat("0");
  clock.concat(remainingSeconds);

  display.clearDisplay();
  display.setTextSize(3);

  char chars[6];
  clock.toCharArray(chars, 6);
  drawCenteredString(chars, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4);

  char scoreString[3];
  itoa(score, scoreString, 10);
  drawCenteredString(scoreString, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4 * 3);
  display.display();
}

void drawFinalScore(int score) {
  display.clearDisplay();

  display.setTextSize(2);
  drawCenteredString("SCORE", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4);

  display.setTextSize(3);
  char scoreString[3];
  itoa(score, scoreString, 10);
  drawCenteredString(scoreString, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 4 * 3);
  display.display();
}

void drawCenteredString(const char *buf, int x, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(
      buf, 0, 0, &x1, &y1, &w, &h
  );  // calc width of new string
  display.setCursor(x - w / 2, y - h / 2);
  display.print(buf);
}