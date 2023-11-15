#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <Keypad.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "secrets.h"

#define NEOPIXEL_PIN 12
#define NUMPIXELS 6

#define RAINBOW_CYCLE_MS 5000
#define GAME_LENGTH 30000

#define INITIAL_TIME_BETWEEN_LED 850
#define INITIAL_TIME_LED_ON 750
#define MIN_TIME_BETWEEN_LED 450
#define MIN_TIME_LED_ON 500
#define STEP_TIME_BETWEEN_LED 20
#define STEP_TIME_LED_ON 10

#define LED_FADE_MS 100
#define HIT_FADE_MS 450

// LED configuration
Adafruit_NeoPixel pixels =
    Adafruit_NeoPixel(NUMPIXELS, NEOPIXEL_PIN, NEO_GRBW + NEO_KHZ800);

// Key configuration
const byte numRows = 2;  // number of rows on the keypad
const byte numCols = 3;  // number of columns on the keypad
char keymap[numRows][numCols] = {{'6', '5', '4'}, {'3', '2', '1'}};
byte rowPins[numRows] = {14, 27};
byte colPins[numCols] = {26, 25, 33};
Keypad myKeypad =
    Keypad(makeKeymap(keymap), rowPins, colPins, numRows, numCols);

// States

enum GameState {
  GAME_WIFI_CONNECTING,
  GAME_IDLE,
  GAME_STARTING,
  GAME_PLAYING,
  GAME_END
};

// Millis
unsigned long lastGameStateChange = 0;
unsigned long lastLedOn = 0;
unsigned long lastLedOff = 0;
unsigned long lastButtonPressed = 0;
unsigned long lastHit = 0;
unsigned long lastTimeLeftSent = 0;
unsigned long lastWsMsgSent = 0;

// Game states
int previousLedIdx = -1;
int currentLedIdx = -1;
int lastHitLedIdx = -1;
int score = 0;
bool lastHitWasSucces = false;

GameState state = GAME_WIFI_CONNECTING;
int previousWifiState = -1;

int timeBetweenLED = INITIAL_TIME_BETWEEN_LED;
int timeLEDOn = INITIAL_TIME_LED_ON;

int wifiLedIdx = 0;
int wifiLedReverse = false;
int wifiLedTrailLength = NUMPIXELS / 3;

// Webserver
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

void setup() {
  Serial.begin(115200);
  pixels.begin();

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
}

void loop() {
  // Clean up non connected web socket clients
  //  to avoid memory leaks or something
  ws.cleanupClients();

  unsigned long currentMillis = millis();

  // Key scanning
  char pressedKey = myKeypad.getKey();
  bool keyWasPressed = pressedKey != NO_KEY;
  if (keyWasPressed) {
    Serial.print("Key pressed: ");
    Serial.print(pressedKey);
    Serial.print(" in state ");
    Serial.println(state);
    lastButtonPressed = currentMillis;
  }

  // Print changed Wi-Fi state
  wl_status_t currentWifiStatus = WiFi.status();
  if (currentWifiStatus != previousWifiState) {
    Serial.print("Wi-Fi state");
    Serial.println(currentWifiStatus);
    previousWifiState = currentWifiStatus;
  }

  // Send something on websocket to show that it's alive
  if (currentMillis > lastWsMsgSent + 5000) {
    wsReport("pong");
  }

  // Handling for specific game states
  switch (state) {
    case GAME_WIFI_CONNECTING: {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected to WiFi");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.print("Hostname: ");
        Serial.println(WiFi.getHostname());
        setGameState(GAME_IDLE, currentMillis);
        break;
      }

      for (int i = 0; i < NUMPIXELS; i++) {
        int brightness = 255 - 255.0 * min(1.0, 1.0 * abs(i - wifiLedIdx) /
                                                    wifiLedTrailLength);
        pixels.setPixelColor(i, pixels.Color(brightness, 0, 0, 0));
      }
      pixels.show();
      delay(125);
      if (wifiLedIdx == 0) {
        wifiLedReverse = false;
      } else if (wifiLedIdx == NUMPIXELS - 1) {
        wifiLedReverse = true;
      }
      wifiLedIdx = wifiLedIdx + (wifiLedReverse ? -1 : 1);

      // Skip WI-Fi connection state
      if (keyWasPressed) {
        setGameState(GAME_IDLE, currentMillis);
      }
      break;
    }

    case GAME_IDLE: {
      if (keyWasPressed) {
        setGameState(GAME_STARTING, currentMillis);
        break;
      }
      pixels.rainbow(
          getStartHue(currentMillis), 1, 255,
          min((currentMillis - lastGameStateChange) / 1250.0 * 255, 255.0),
          true);
      break;
    }

    case GAME_STARTING: {
      // Blocking fade out and game start effect

      // Fade out
      {
        int hue = getStartHue(currentMillis);
        int steps = 200;
        int stepMs = 1;
        for (int i = 0; i < steps; i++) {
          pixels.rainbow(getStartHue(currentMillis), 1, 255,
                         255 - ((i * 1.0) / steps * 255), true);
          pixels.show();
          delay(stepMs);
        }
      }

      // 3x red flash
      {
        delay(1000);
        for (int i = 0; i < 3; i++) {
          int steps = 40;
          int stepMs = 5;
          for (int j = 0; j < steps; j++) {
            pixels.fill(pixels.Color(255.0 / steps * j, 0, 0, 0), 0, NUMPIXELS);
            pixels.show();
            delay(stepMs);
          }
          for (int j = 0; j < steps; j++) {
            pixels.fill(pixels.Color(255 - 255.0 / steps * j, 0, 0, 0), 0,
                        NUMPIXELS);
            pixels.show();
            delay(stepMs);
          }
          pixels.clear();
          pixels.show();
          delay(800);
        }
        delay(1000);
      }

      // Reset game states
      timeBetweenLED = INITIAL_TIME_BETWEEN_LED;
      timeLEDOn = INITIAL_TIME_LED_ON;
      score = 0;
      wsReportScore();
      lastHitLedIdx = -1;
      currentLedIdx = -1;
      previousLedIdx = -1;
      // Re-read millis since we did blocking stuff
      setGameState(GAME_PLAYING, millis());
      break;
    }

    case GAME_PLAYING: {
      wsReportTimeLeft(currentMillis);

      bool gameFinished = currentMillis > lastGameStateChange + GAME_LENGTH;

      // Key handling
      if (keyWasPressed) {
        int pressedKeyIdx = String(pressedKey).toInt() - 1;
        lastHitWasSucces = pressedKeyIdx == currentLedIdx;
        if (lastHitWasSucces) {
          // It's a hit!
          // Turn of LED and increment score
          turnOffCurrentLED(currentMillis);
          score++;
          wsReportScore();
          Serial.print("Score: ");
          Serial.println(score);
          // More faster!
          timeBetweenLED =
              max(MIN_TIME_BETWEEN_LED, timeBetweenLED - STEP_TIME_BETWEEN_LED);
          timeLEDOn = max(MIN_TIME_LED_ON, timeLEDOn - STEP_TIME_LED_ON);
        } else {
          Serial.println("Incorrect!");
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

      bool doHitEffect =
          lastHitLedIdx != -1 && currentMillis < lastHit + HIT_FADE_MS;
      // Hit effect
      if (doHitEffect) {
        float brightness =
            255 - 255.0 * (currentMillis - lastHit) / HIT_FADE_MS;
        pixels.setPixelColor(lastHitLedIdx,
                             lastHitWasSucces ? pixels.Color(0, brightness, 0)
                                              : pixels.Color(brightness, 0, 0));
      }

      // Game end
      if (
          // Game time finished
          gameFinished &&
          // No LED's on and all animations finished
          currentLedIdx == -1 && !doHitEffect) {
        delay(1000);
        // TODO: Go to GAME_END state instead
        // Re read millis since we did blocking delay
        setGameState(GAME_IDLE, millis());
      }
      break;
    }
  }
  pixels.show();
}

void setGameState(GameState newGameState, unsigned long millis) {
  state = newGameState;
  lastGameStateChange = millis;

  Serial.print("Game state: ");
  Serial.println(state);

  wsReportGameState();
}

void turnOnRandomLED(unsigned long millis) {
  currentLedIdx = random(0, NUMPIXELS - 1);
  if (currentLedIdx >= previousLedIdx) currentLedIdx++;
  lastLedOn = millis;
}

void turnOffCurrentLED(unsigned long millis) {
  previousLedIdx = currentLedIdx;
  currentLedIdx = -1;
  lastLedOff = millis;
}

int getStartHue(unsigned long currentMillis) {
  return (currentMillis % RAINBOW_CYCLE_MS) / (RAINBOW_CYCLE_MS * 1.0) * 65535;
}

void onWSEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(),
                    client->remoteIP().toString().c_str());
      wsReportGameState();
      wsReportScore();
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

void wsReportGameState() {
  String gameStateString = "gameState ";
  gameStateString.concat(state);
  wsReport(gameStateString);
}

void wsReportScore() {
  String scoreString = "score ";
  scoreString.concat(score);
  wsReport(scoreString);
}

void wsReportTimeLeft(unsigned long millis) {
  if (millis > lastTimeLeftSent + 1000) {
    String scoreString = "time ";
    scoreString.concat(max(
        0,
        (int)round((GAME_LENGTH - (millis - lastGameStateChange)) / 1000.0)));
    wsReport(scoreString);
    lastTimeLeftSent = millis;
  }
}

void wsReport(String message) {
  ws.textAll(message);
  lastWsMsgSent = millis();
}