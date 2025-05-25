#include <WiFi.h>
#include <WebServer.h>

// === PIN ASSIGNMENTS ===
#define ENCODER_PIN_A    20
#define ENCODER_PIN_B    21
#define MOTOR_IN1        7
#define MOTOR_IN2        5

// === CONFIGURATION ===
const char* ssid     = "";
const char* password = "";
const long PULSES_PER_REV = 20;

WebServer server(80);

// === STATE ===
volatile long encoderCount = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

long encoderOffset     = 0;
long topCount          = 0;
bool topSet            = false;
bool bottomSet         = false;
bool bottomCalibrating = false;

bool moving            = false;
int  moveDir           = 0;
bool gotoTop           = false;
bool gotoBottom        = false;

unsigned long lastEncTime = 0;

// === ENCODER ISR ===
void IRAM_ATTR handleEncoder() {
  int a = digitalRead(ENCODER_PIN_A);
  int b = digitalRead(ENCODER_PIN_B);
  int delta = (a == b) ? +1 : -1;
  portENTER_CRITICAL_ISR(&mux);
    encoderCount += delta;
    lastEncTime = millis();
  portEXIT_CRITICAL_ISR(&mux);
}

// === MOTOR CONTROL ===
void startMotor(int dir) {
  moveDir = dir;
  moving = true;
  lastEncTime = millis();
  digitalWrite(MOTOR_IN1, dir > 0 ? HIGH : LOW);
  digitalWrite(MOTOR_IN2, dir < 0 ? HIGH : LOW);
}

void stopMotor() {
  moving = false;
  gotoTop = false;
  gotoBottom = false;
  bottomCalibrating = false;
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
}

void runMotor() {
  long rawCount = encoderCount - encoderOffset;
  if (moving && millis() - lastEncTime > 1000) {
    if (moveDir < 0) {
      encoderOffset = encoderCount;
      bottomSet = true;
      Serial.println("Stall detected. Set bottom zero point.");
    }
    stopMotor();
    return;
  }

  if (gotoTop && topSet && rawCount >= topCount) stopMotor();
  if (gotoBottom && bottomSet && rawCount <= 0) {
    encoderOffset = encoderCount;
    stopMotor();
    return;
  }
  if (bottomSet && moveDir < 0 && rawCount <= 0) {
    encoderOffset = encoderCount;
    stopMotor();
    return;
  }
  if (topSet && moveDir > 0 && rawCount >= topCount) stopMotor();
}

// === WEB ===
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>ESP32 Motor Control</title></head><body>"
                "<h2>Motor Control</h2>"
                "<p><strong>Rotations:</strong> <span id='rotations'>0.00</span></p>"
                "<p id='bottomStatus'></p>"
                "<p id='topStatus'></p>"
                "<form action='/up'><button style='font-size:1.5em;'>&#9650; Up</button></form>"
                "<form action='/down'><button style='font-size:1.5em;'>&#9660; Down</button></form>"
                "<form action='/stop'><button>Stop</button></form>"
                "<hr>"
                "<form action='/setBottom'><button>Save Bottom</button></form>"
                "<form action='/setTop'><button>Save Top</button></form>"
                "<hr>"
                "<form action='/gotoBottom'><button>Go to Bottom</button></form>"
                "<form action='/gotoTop'><button>Go to Top</button></form>"
                "<script>"
                "async function updateStatus() {"
                "  try {"
                "    const res = await fetch('/status');"
                "    const json = await res.json();"
                "    document.getElementById('rotations').textContent = json.rotations.toFixed(2);"
                "    document.getElementById('topStatus').textContent = json.topSet ? `Top at ${json.topRotations.toFixed(2)} rotations.` : '';"
                "    document.getElementById('bottomStatus').textContent = json.bottomSet ? 'Bottom set.' : '';"
                "  } catch (e) { console.error('Status update failed', e); }"
                "}"
                "setInterval(updateStatus, 1000);"
                "</script>"
                "</body></html>";
  server.send(200, "text/html", html);
}

void handleStatus() {
  long rawCount = encoderCount - encoderOffset;
  float rotations = (float)rawCount / PULSES_PER_REV;
  String json = "{";
  json += "\"rotations\":" + String(rotations, 2);
  json += ",\"topSet\":" + String(topSet ? "true" : "false");
  json += ",\"bottomSet\":" + String(bottomSet ? "true" : "false");
  json += ",\"topRotations\":" + String((float)topCount / PULSES_PER_REV, 2);
  json += "}";
  server.send(200, "application/json", json);
}

void handleUp()        { startMotor(+1); server.sendHeader("Location","/"); server.send(303); }
void handleDown()      { startMotor(-1); server.sendHeader("Location","/"); server.send(303); }
void handleStop()      { stopMotor(); server.sendHeader("Location","/"); server.send(303); }
void handleSetBottom() { bottomCalibrating=true; startMotor(-1); server.sendHeader("Location","/"); server.send(303); }
void handleSetTop()    { stopMotor(); topCount = encoderCount - encoderOffset; topSet=true; server.sendHeader("Location","/"); server.send(303); }
void handleGotoBottom(){ if(bottomSet){ gotoBottom=true; startMotor(-1);} server.sendHeader("Location","/"); server.send(303); }
void handleGotoTop()   { if(topSet){ gotoTop=true; startMotor(+1);} server.sendHeader("Location","/"); server.send(303); }

void setup() {
  Serial.begin(115200);
  pinMode(ENCODER_PIN_A, INPUT_PULLUP);
  pinMode(ENCODER_PIN_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), handleEncoder, CHANGE);

  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  stopMotor();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print('.');
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/up", handleUp);
  server.on("/down", handleDown);
  server.on("/stop", handleStop);
  server.on("/setBottom", handleSetBottom);
  server.on("/setTop", handleSetTop);
  server.on("/gotoBottom", handleGotoBottom);
  server.on("/gotoTop", handleGotoTop);
  server.begin();
}

void loop() {
  server.handleClient();
  if (moving) runMotor();
}
