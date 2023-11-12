#include <Keypad.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
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


enum GameState {
  GAME_WIFI_CONNECTING,
  GAME_IDLE,
  GAME_STARTING,
  GAME_PLAYING,
  GAME_END
};

// LED configuration
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, NEOPIXEL_PIN, NEO_GRBW + NEO_KHZ800);

// Key configuration
const byte numRows = 2;  //number of rows on the keypad
const byte numCols = 3;  //number of columns on the keypad
char keymap[numRows][numCols] = {
  { '6', '5', '4' },
  { '3', '2', '1' }
};
byte rowPins[numRows] = { 14, 27 };
byte colPins[numCols] = { 26, 25, 33 };
Keypad myKeypad = Keypad(makeKeymap(keymap), rowPins, colPins, numRows, numCols);

// States
unsigned long lastGameStateChange = 0;

unsigned long lastLedOn = 0;
unsigned long lastLedOff = 0;
unsigned long lastButtonPressed = 0;

unsigned long lastHit = 0;

int previousLedIdx = -1;
int currentLedIdx = -1;
int lastHitLedIdx = -1;
int score = 0;
bool lastHitWasSucces = false;

GameState state = GAME_WIFI_CONNECTING;

int timeBetweenLED = INITIAL_TIME_BETWEEN_LED;
int timeLEDOn = INITIAL_TIME_LED_ON;

int wifiLedIdx = 0;
int wifiLedReverse = false;
int WIFI_LED_TRAIL = NUMPIXELS / 3;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Whack-an-angineer</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
  html {
    font-family: Arial, Helvetica, sans-serif;
    text-align: center;
  }
  h1 {
    font-size: 1.8rem;
    color: white;
  }
  h2{
    font-size: 1.5rem;
    font-weight: bold;
    color: #143642;
  }
  .topnav {
    overflow: hidden;
    background-color: #143642;
  }
  body {
    margin: 0;
  }
  .content {
    padding: 30px;
    max-width: 600px;
    margin: 0 auto;
  }
  </style>
</head>
<body>
  <div class="topnav">
    <h1>Whack-an-engineer</h1>
  </div>
  <div class="content">
    <h2>Game state: <span id="gameState">-</span></h2>
    <h2>Score: <span id="score">-</span></h2>
  </div>
<script>
  const gateway = `ws://${window.location.hostname}/ws`;
  window.addEventListener(
    'load', 
    () => {
      console.log('Trying to open a WebSocket connection...');
      const websocket = new WebSocket(gateway);
      websocket.onopen = () => console.log("Conneciton opened");
      websocket.onclose = () => {
        console.log('Connection closed');
        setTimeout(initWebSocket, 2000);
      };
      websocket.onmessage = (evt) => {
        console.log("Received message", evt.data);
        const split = evt.data.split(" ");
        const id = split[0];
        const value = split[1];

        document.getElementById(id).textContent = value;
      };
    }
  );

</script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(9600);
  pixels.begin();

  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(WIFI_HOST);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting to Wi-Fi...");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html);
  });
  ws.onEvent(onWSEvent);
  server.addHandler(&ws);
  server.begin();
}

void loop() {

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

  switch (state) {

    case GAME_WIFI_CONNECTING:
      {

        if (WiFi.status() == WL_CONNECTED) {
          Serial.println("\nConnected to WiFi");
          Serial.print("Local ESP32 IP: ");
          Serial.println(WiFi.localIP());
          Serial.println(WiFi.getHostname());
          setGameState(GAME_IDLE, currentMillis);
          break;
        }

        for (int i = 0; i < NUMPIXELS; i++) {
          int brightness = 255 - 255.0 * min(1.0, 1.0 * abs(i - wifiLedIdx) / WIFI_LED_TRAIL);
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
        if (keyWasPressed) setGameState(GAME_IDLE, currentMillis);
        break;
      }

    case GAME_IDLE:
      {
        if (keyWasPressed) {
          setGameState(GAME_STARTING, currentMillis);
          break;
        }
        pixels.rainbow(
          getStartHue(currentMillis),
          1,
          255,
          min(
            (currentMillis - lastGameStateChange) / 1250.0 * 255,
            255.0),
          true);
        break;
      }

    case GAME_STARTING:
      {
        // Blocking fade out and game start effect

        // Fade out
        {
          int hue = getStartHue(currentMillis);
          int steps = 200;
          int stepMs = 1;
          for (int i = 0; i < steps; i++) {
            pixels.rainbow(
              getStartHue(currentMillis),
              1,
              255,
              255 - ((i * 1.0) / steps * 255),
              true);
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
              pixels.fill(
                pixels.Color(255.0 / steps * j, 0, 0, 0),
                0,
                NUMPIXELS);
              pixels.show();
              delay(stepMs);
            }
            for (int j = 0; j < steps; j++) {
              pixels.fill(
                pixels.Color(255 - 255.0 / steps * j, 0, 0, 0),
                0,
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

    case GAME_PLAYING:
      {

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
            timeBetweenLED = max(MIN_TIME_BETWEEN_LED, timeBetweenLED - STEP_TIME_BETWEEN_LED);
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

        bool doHitEffect = lastHitLedIdx != -1 && currentMillis < lastHit + HIT_FADE_MS;
        // Hit effect
        if (doHitEffect) {
          float brightness = 255 - 255.0 * (currentMillis - lastHit) / HIT_FADE_MS;
          pixels.setPixelColor(
            lastHitLedIdx,
            lastHitWasSucces
              ? pixels.Color(0, brightness, 0)
              : pixels.Color(brightness, 0, 0));
        }

        // Game end
        if (
          // Game time finished
          gameFinished &&
          // No LED's on (TODO: and all animations finished)
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

  Serial.print("Set game state to ");
  Serial.println(state);

  wsReportGameState();
}

void turnOnRandomLED(unsigned long millis) {
  currentLedIdx = random(0, NUMPIXELS - 1);
  if (currentLedIdx >= previousLedIdx) currentLedIdx++;
  lastLedOn = millis;
  // Serial.print("Turned on LED with index ");
  // Serial.println(currentLedIdx);
}

void turnOffCurrentLED(unsigned long millis) {
  previousLedIdx = currentLedIdx;
  currentLedIdx = -1;
  lastLedOff = millis;
  // Serial.println("Turned off LED");
}

int getStartHue(unsigned long currentMillis) {
  return (currentMillis % RAINBOW_CYCLE_MS) / (RAINBOW_CYCLE_MS * 1.0) * 65535;
}

void onWSEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
 void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
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
  ws.textAll(gameStateString);
  ws.
}

void wsReportScore() {
  String scoreString = "score ";
  scoreString.concat(score);
  ws.textAll(scoreString);
}