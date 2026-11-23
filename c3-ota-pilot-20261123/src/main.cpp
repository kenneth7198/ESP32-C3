#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFi.h>

namespace {
constexpr char kVersion[] = "0.1.2";
constexpr char kHostname[] = "c3-led-ota";
constexpr uint8_t kLedPin = 2;
constexpr uint16_t kLedCount = 30;
constexpr unsigned long kRetryMs = 15000;
constexpr unsigned long kFrameMs = 40;
Adafruit_NeoPixel strip(kLedCount, kLedPin, NEO_GRB + NEO_KHZ800);
Preferences settings;
String ssid;
String wifiPassword;
String otaPassword;
String input;
uint8_t configStep = 0;
unsigned long lastRetry = 0;
bool otaStarted = false;
bool wasConnected = false;
bool otaInProgress = false;
unsigned long lastFrame = 0;
uint16_t rainbowOffset = 0;

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  strip.fill(strip.Color(r, g, b));
  strip.show();
}

void animateRainbow() {
  const unsigned long now = millis();
  if (now - lastFrame < kFrameMs) return;
  lastFrame = now;
  for (uint16_t i = 0; i < kLedCount; ++i) {
    const uint16_t hue = rainbowOffset + (static_cast<uint32_t>(i) * 65536UL / kLedCount);
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue)));
  }
  strip.show();
  rainbowOffset += 256;
}

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kHostname);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), wifiPassword.c_str());
  lastRetry = millis();
  Serial.println("WIFI_CONNECTING");
}

void handleConfiguration(const String& line) {
  if (line == "\x1b" "STATUS") {
    configStep = 0;
    Serial.printf("STATUS version=%s wifi=%s ip=%s\n", kVersion,
                  WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                  WiFi.localIP().toString().c_str());
    return;
  }
  if (line == "\x1b" "CONFIG") {
    configStep = 1;
    Serial.println("SSID?");
    return;
  }
  if (configStep == 0) {
    if (line == "CONFIG") {
      configStep = 1;
      Serial.println("SSID?");
    }
    return;
  }
  if (configStep == 1) {
    if (line.length() == 0 || line.length() > 32) {
      configStep = 0;
      Serial.println("CONFIG_ERROR: invalid SSID");
      return;
    }
    ssid = line;
    configStep = 2;
    Serial.println("WIFI_PASSWORD?");
  } else if (configStep == 2) {
    if (line.length() < 8 || line.length() > 63) {
      configStep = 0;
      Serial.println("CONFIG_ERROR: invalid Wi-Fi password");
      return;
    }
    wifiPassword = line;
    configStep = 3;
    Serial.println("OTA_PASSWORD?");
  } else {
    if (line.length() < 6 || line.length() > 63) {
      configStep = 0;
      Serial.println("CONFIG_ERROR: OTA password must be 6-63 bytes");
      return;
    }
    otaPassword = line;
    if (!settings.begin("c3-ota", false)) {
      configStep = 0;
      Serial.println("CONFIG_ERROR: settings unavailable");
      return;
    }
    const bool saved = settings.putString("ssid", ssid) == ssid.length() &&
                       settings.putString("wifi", wifiPassword) == wifiPassword.length() &&
                       settings.putString("ota", otaPassword) == otaPassword.length();
    settings.end();
    configStep = 0;
    if (!saved) {
      Serial.println("CONFIG_ERROR: settings write failed");
      return;
    }
    Serial.println("CONFIG_SAVED: restarting");
    delay(200);
    ESP.restart();
  }
}

void setupOta() {
  ArduinoOTA.setHostname(kHostname);
  ArduinoOTA.setPassword(otaPassword.c_str());
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    setColor(48, 24, 0);
    Serial.println("OTA_START");
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA_FINISHED"); });
  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    Serial.printf("OTA_ERROR: %u\n", static_cast<unsigned>(error));
    setColor(32, 0, 0);
  });
  ArduinoOTA.begin();
  otaStarted = true;
}
}

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.setBrightness(20);
  setColor(32, 0, 0);
  Serial.printf("\nC3_LED_OTA version=%s\n", kVersion);
  if (!settings.begin("c3-ota", true)) {
    Serial.println("SETTINGS_ERROR: connect USB and send CONFIG");
    return;
  }
  ssid = settings.getString("ssid", "");
  wifiPassword = settings.getString("wifi", "");
  otaPassword = settings.getString("ota", "");
  settings.end();
  if (ssid.isEmpty() || wifiPassword.length() < 8 || otaPassword.length() < 6) {
    Serial.println("CONFIG_REQUIRED: connect USB and send CONFIG");
    return;
  }
  startWifi();
}

void loop() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      if (input.endsWith("\r")) input.remove(input.length() - 1);
      handleConfiguration(input);
      input = "";
    } else if (input.length() < 63) {
      input += c;
    } else {
      input = "";
      configStep = 0;
      Serial.println("CONFIG_ERROR: input too long");
    }
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.printf("WIFI_CONNECTED ip=%s version=%s\n", WiFi.localIP().toString().c_str(), kVersion);
    setColor(0, 0, 40);
    if (!otaStarted) setupOta();
  } else if (!connected && wasConnected) {
    Serial.println("WIFI_DISCONNECTED");
    setColor(32, 0, 0);
    if (otaStarted) {
      ArduinoOTA.end();
      otaStarted = false;
    }
  }
  wasConnected = connected;
  if (connected && otaStarted) {
    ArduinoOTA.handle();
    if (!otaInProgress) animateRainbow();
  }
  if (!connected && configStep == 0 && !ssid.isEmpty() && otaPassword.length() >= 6 &&
      millis() - lastRetry >= kRetryMs) {
    WiFi.disconnect();
    startWifi();
  }
  delay(5);
}
