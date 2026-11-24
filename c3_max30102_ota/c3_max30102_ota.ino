#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <MAX30105.h>  // SparkFun MAX3010x 函式庫也支援 MAX30102

#include "secrets.h"
#include "monitor_page.h"

// 依實際開發板與接線修改；GPIO2 固定作為 WS2812B 的 SIG / DIN。
const uint8_t LED_PIN = 2;
const uint16_t LED_COUNT = 30;
const uint8_t LED_BRIGHTNESS = 20;  // 0~255；先以低亮度使用
const uint8_t I2C_SDA_PIN = 6;
const uint8_t I2C_SCL_PIN = 7;
const uint8_t EMG_PIN = 0;        // 可選：EMG 類比輸出接 ADC1 GPIO0
const bool ENABLE_EMG = false;    // 確認接線與電壓安全後才改成 true

const char VERSION[] = "1.0.0";
const char HOSTNAME[] = "c3-max30102";
const unsigned long WIFI_RETRY_MS = 15000;
const unsigned long SENSOR_RETRY_MS = 5000;
const unsigned long SENSOR_STALE_MS = 3000;
const unsigned long EMG_INTERVAL_MS = 5;  // 目標 200 Hz；HTTP / OTA 忙碌時可能較慢
const size_t EMG_BUFFER_SIZE = 128;

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
MAX30105 sensor;
WebServer server(80);

bool wasConnected = false;
bool otaStarted = false;
bool otaInProgress = false;
bool wifiConfigured = false;
bool i2cStarted = false;
bool sensorReady = false;
bool haveOpticalSample = false;
unsigned long lastWifiRetry = 0;
unsigned long connectedAt = 0;
unsigned long lastBlink = 0;
unsigned long lastFrame = 0;
unsigned long lastSensorAttempt = 0;
unsigned long lastOpticalSample = 0;
unsigned long lastEmgSample = 0;
bool redOn = false;
uint16_t rainbowOffset = 0;
uint32_t opticalRed = 0;
uint32_t opticalIr = 0;
uint16_t emgSamples[EMG_BUFFER_SIZE];
size_t emgHead = 0;
size_t emgCount = 0;
uint32_t emgSequence = 0;
String serialInput;

void showColor(uint8_t red, uint8_t green, uint8_t blue) {
  strip.fill(strip.Color(red, green, blue));
  strip.show();
}

void showRainbow() {
  if (millis() - lastFrame < 40) return;
  lastFrame = millis();
  for (uint16_t i = 0; i < LED_COUNT; ++i) {
    uint16_t hue = rainbowOffset + (static_cast<uint32_t>(i) * 65536UL / LED_COUNT);
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue)));
  }
  strip.show();
  rainbowOffset += 256;
}

// 所有 API 都只接受以裝置 IP 存取，避免網頁透過其他主機名稱載入。
bool validHost() {
  const String ip = WiFi.localIP().toString();
  const String host = server.header("Host");
  return host == ip || host == ip + ":80";
}

bool checkHost() {
  if (validHost()) return true;
  server.send(403, "text/plain", "Invalid host; use the device IP");
  return false;
}

void sendJson(int status, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json", body);
}

// 紅光與紅外線均為原始 ADC 計數，不代表 SpO2。
void handleOptical() {
  if (!checkHost()) return;
  if (!sensorReady || !haveOpticalSample || millis() - lastOpticalSample > SENSOR_STALE_MS) {
    sendJson(503, "{\"error\":\"MAX30102 unavailable\"}");
    return;
  }
  char json[128];
  snprintf(json, sizeof(json), "{\"red\":%lu,\"ir\":%lu,\"age_ms\":%lu}",
           static_cast<unsigned long>(opticalRed), static_cast<unsigned long>(opticalIr),
           millis() - lastOpticalSample);
  sendJson(200, json);
}

// 不帶 after 時回傳最新值，與現有 Processing 監看程式相容。
// 帶 after 時回傳該序號以後的緩衝樣本，供需要連續波形的程式使用。
void handleEmg() {
  if (!checkHost()) return;
  if (!ENABLE_EMG || emgCount == 0) {
    sendJson(503, "{\"error\":\"EMG disabled or no samples\"}");
    return;
  }
  if (!server.hasArg("after")) {
    const size_t latest = (emgHead + EMG_BUFFER_SIZE - 1) % EMG_BUFFER_SIZE;
    sendJson(200, "{\"value\":" + String(emgSamples[latest]) + "}");
    return;
  }

  const String afterText = server.arg("after");
  if (afterText.length() == 0 || afterText.length() > 10) {
    sendJson(400, "{\"error\":\"invalid after sequence\"}");
    return;
  }
  uint64_t parsed = 0;
  for (unsigned int i = 0; i < afterText.length(); ++i) {
    if (afterText[i] < '0' || afterText[i] > '9') {
      sendJson(400, "{\"error\":\"invalid after sequence\"}");
      return;
    }
    parsed = parsed * 10 + (afterText[i] - '0');
  }
  if (parsed > 0xffffffffULL) {
    sendJson(400, "{\"error\":\"invalid after sequence\"}");
    return;
  }
  const uint32_t distance = emgSequence - static_cast<uint32_t>(parsed);
  if (distance > 0x7fffffffUL) {
    sendJson(400, "{\"error\":\"after sequence is ahead\"}");
    return;
  }
  const bool dropped = distance > emgCount;
  const size_t count = dropped ? emgCount : distance;
  const size_t start = (emgHead + EMG_BUFFER_SIZE - count) % EMG_BUFFER_SIZE;
  String json;
  json.reserve(900);
  json = "{\"samples\":[";
  for (size_t i = 0; i < count; ++i) {
    if (i) json += ',';
    json += String(emgSamples[(start + i) % EMG_BUFFER_SIZE]);
  }
  json += "],\"next_seq\":" + String(emgSequence);
  json += dropped ? ",\"dropped\":true}" : ",\"dropped\":false}";
  sendJson(200, json);
}

void setupWeb() {
  const char* headers[] = {"Host"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, []() {
    if (!checkHost()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", MONITOR_PAGE);
  });
  server.on("/api/max30102", HTTP_GET, handleOptical);
  server.on("/api/emg", HTTP_GET, handleEmg);
  server.on("/api/telemetry", HTTP_GET, []() {
    if (!checkHost()) return;
    char json[128];
    snprintf(json, sizeof(json), "{\"rssi\":%d,\"uptime\":%lu,\"heap\":%lu}",
             WiFi.RSSI(), millis() / 1000UL, static_cast<unsigned long>(ESP.getFreeHeap()));
    sendJson(200, json);
  });
  // 供既有 Processing 面板顯示燈效；本專案沒有遠端控制燈效。
  server.on("/api/state", HTTP_GET, []() {
    if (!checkHost()) return;
    const bool blue = millis() - connectedAt < 5000;
    String json = "{\"mode\":\"";
    json += blue ? "solid\",\"color\":\"0000ff" : "rainbow\",\"color\":\"ffffff";
    json += "\",\"brightness\":" + String(LED_BRIGHTNESS) + "}";
    sendJson(200, json);
  });
  server.onNotFound([]() {
    if (!checkHost()) return;
    sendJson(404, "{\"error\":\"not found\"}");
  });
}

void setupOta() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    showColor(48, 24, 0);
    Serial.println("OTA_START");
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA_FINISHED"); });
  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    Serial.printf("OTA_ERROR: %u\n", static_cast<unsigned>(error));
  });
  ArduinoOTA.begin();
  otaStarted = true;
}

void connectWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiRetry = millis();
  Serial.println("WIFI_CONNECTING");
}

void updateWifiAndLeds() {
  const unsigned long now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    connectedAt = now;
    showColor(0, 0, 255);
    Serial.printf("WIFI_CONNECTED ip=%s version=%s\n", WiFi.localIP().toString().c_str(), VERSION);
    setupOta();
    server.begin();
  } else if (!connected && wasConnected) {
    Serial.println("WIFI_DISCONNECTED");
    server.stop();
    if (otaStarted) ArduinoOTA.end();
    otaStarted = false;
    otaInProgress = false;
    lastBlink = now - 500;
  }
  wasConnected = connected;

  if (!connected) {
    if (now - lastBlink >= 500) {
      lastBlink = now;
      redOn = !redOn;
      showColor(redOn ? 255 : 0, 0, 0);
    }
    if (wifiConfigured && now - lastWifiRetry >= WIFI_RETRY_MS) {
      WiFi.disconnect();
      connectWifi();
    }
  } else {
    ArduinoOTA.handle();
    if (!otaInProgress) {
      server.handleClient();
      if (now - connectedAt >= 5000) showRainbow();
      else showColor(0, 0, 255);
    }
  }
}

void updateOpticalSensor() {
  if (!i2cStarted) return;
  const unsigned long now = millis();
  if (!sensorReady) {
    if (now - lastSensorAttempt < SENSOR_RETRY_MS) return;
    lastSensorAttempt = now;
    if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
      Serial.println("MAX30102_ERROR: check I2C wiring");
      return;
    }
    // LED 電流、平均數、雙通道、取樣率、脈衝寬度、ADC 範圍。
    sensor.setup(0x1f, 1, 2, 100, 411, 4096);
    sensorReady = true;
    haveOpticalSample = false;
    lastOpticalSample = now;
    Serial.println("MAX30102_READY");
  }
  sensor.check();
  while (sensor.available()) {
    // 此函式庫先推進 head 寫入新值；先移動 tail 才會讀到該筆新取樣。
    sensor.nextSample();
    opticalRed = sensor.getFIFORed();
    opticalIr = sensor.getFIFOIR();
    lastOpticalSample = millis();
    haveOpticalSample = true;
  }
  if (millis() - lastOpticalSample > SENSOR_STALE_MS) {
    sensorReady = false;
    haveOpticalSample = false;
    Serial.println("MAX30102_ERROR: no new samples");
  }
}

void updateEmg() {
  if (!ENABLE_EMG || millis() - lastEmgSample < EMG_INTERVAL_MS) return;
  lastEmgSample = millis();
  const uint16_t value = analogRead(EMG_PIN);
  emgSamples[emgHead] = value;
  emgHead = (emgHead + 1) % EMG_BUFFER_SIZE;
  if (emgCount < EMG_BUFFER_SIZE) ++emgCount;
  ++emgSequence;
  // Processing 的序列埠讀取格式；不包含健康或醫療判斷。
  Serial.printf("EMG,%u\n", static_cast<unsigned>(value));
}

void updateSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      serialInput.trim();
      if (serialInput == "\x1b" "STATUS" || serialInput == "STATUS") {
        Serial.printf("STATUS version=%s wifi=%s ip=%s\n", VERSION,
                      WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                      WiFi.localIP().toString().c_str());
      }
      serialInput = "";
    } else if (serialInput.length() < 32) {
      serialInput += c;
    } else {
      serialInput = "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS);
  showColor(255, 0, 0);
  Serial.printf("\nC3_MAX30102 version=%s\n", VERSION);

  if (ENABLE_EMG) {
    analogReadResolution(12);
    pinMode(EMG_PIN, INPUT);
  }
  i2cStarted = Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!i2cStarted) Serial.println("MAX30102_ERROR: I2C initialization failed");
  Wire.setTimeOut(50);
  lastSensorAttempt = millis() - SENSOR_RETRY_MS;
  setupWeb();

  // 不允許範例密碼直接啟動 OTA，也避免無密碼 OTA。
  if (String(WIFI_SSID) == "YOUR_WIFI_SSID" || String(WIFI_PASSWORD) == "YOUR_WIFI_PASSWORD" ||
      String(OTA_PASSWORD) == "CHANGE_THIS_OTA_PASSWORD" ||
      strlen(WIFI_SSID) == 0 || strlen(OTA_PASSWORD) < 12) {
    Serial.println("CONFIG_ERROR: edit secrets.h; OTA password must be at least 12 characters");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  wifiConfigured = true;
  connectWifi();
}

void loop() {
  updateSerial();
  updateOpticalSensor();
  updateEmg();
  // 未完成密碼設定時仍閃紅燈，但不嘗試連線。
  updateWifiAndLeds();
  delay(1);
}
