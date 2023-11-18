#include <Adafruit_NeoPixel.h>
#include <ESPAsyncWebServer.h>
#include <Keypad.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "secrets.h"

#define NEOPIXEL_PIN 12
#define NUMPIXELS 6

#define WIFI_ANIMATION_CYCLE_MS 1000

#define RAINBOW_CYCLE_MS 5000
#define GAME_LENGTH 30000

#define INITIAL_TIME_BETWEEN_LED 850
#define INITIAL_TIME_LED_ON 1000
#define MIN_TIME_BETWEEN_LED 450
#define MIN_TIME_LED_ON 500
#define STEP_TIME_BETWEEN_LED 20
#define STEP_TIME_LED_ON 20

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
  lastGameStateChange = millis();
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
        wsReportScore();
        lastHitLedIdx = -1;
        currentLedIdx = -1;
        previousLedIdx = -1;
        setGameState(GAME_PLAYING, currentMillis);
      }
      break;
    }

    case GAME_PLAYING: {
      wsReportTimeLeft(currentMillis);

      unsigned long gameEnd = lastGameStateChange + GAME_LENGTH;
      bool gameFinished = currentMillis > gameEnd;

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
        // TODO: Go to GAME_END state instead
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
  int msSinceGameStateChange = millis - lastGameStateChange;
  int msUntilGameEnd = GAME_LENGTH - msSinceGameStateChange;
  if (millis > lastTimeLeftSent + 1000) {
    Serial.println(msUntilGameEnd);
    int secondsLeft = max(0, (int)ceil(msUntilGameEnd / 1000.0));
    String scoreString = "time ";
    scoreString.concat(secondsLeft);
    wsReport(scoreString);
    lastTimeLeftSent = lastGameStateChange + msSinceGameStateChange -
                       msSinceGameStateChange % 1000;
  }
}

void wsReport(String message) {
  ws.textAll(message);
  lastWsMsgSent = millis();
}