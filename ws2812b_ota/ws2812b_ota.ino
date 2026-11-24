#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>

// Wi-Fi 與 OTA 設定保存在不提交至 Git 的 secrets.h。
#include "secrets.h"
const char *HOSTNAME = "esp32c3-led";

const uint8_t LED_PIN = 2;            // WS2812B 的 DIN 接 GPIO 2
const uint16_t LED_COUNT = 30;        // 燈條共有 30 顆 LED
const uint8_t BRIGHTNESS = 40;        // 整體亮度：0～255，先使用低亮度
const unsigned long BLINK_MS = 500;   // 未連線時紅燈切換間隔
const unsigned long BLUE_MS = 5000;   // 連線後藍燈顯示 5 秒
const unsigned long FRAME_MS = 40;    // 漸變燈效更新間隔
const unsigned long FADE_MS = 2000;   // 每次漸亮或漸暗的時間
const unsigned long RETRY_MS = 15000; // 斷線後定期嘗試重新連線

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

bool wasConnected = false;
bool otaStarted = false;
bool otaInProgress = false;
bool redOn = true;
unsigned long connectedAt = 0;
unsigned long lastBlink = 0;
unsigned long lastFrame = 0;
unsigned long lastRetry = 0;

// 讓整條燈顯示同一種顏色。
void showColor(uint8_t red, uint8_t green, uint8_t blue)
{
  strip.fill(strip.Color(red, green, blue));
  strip.show();
}

// 每次只更新一個畫面，不使用長時間 delay，讓 OTA 可以持續運作。
void showRedBlueFade()
{
  unsigned long now = millis();
  if (now - lastFrame < FRAME_MS) return;
  lastFrame = now;

  // 四個階段：藍色漸亮、藍色漸暗、紅色漸亮、紅色漸暗。
  unsigned long elapsed = now - connectedAt - BLUE_MS;
  unsigned long phase = (elapsed / FADE_MS) % 4;
  uint8_t level = (elapsed % FADE_MS) * 255UL / FADE_MS;
  if (phase % 2 == 1) level = 255 - level;
  level = strip.gamma8(level); // 讓人眼看到的亮度變化更平順

  if (phase < 2) showColor(0, 0, level);
  else showColor(level, 0, 0);
}

void setupOTA()
{
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    showColor(255, 100, 0); // 更新時顯示橙色
    Serial.println("OTA 更新開始，請勿關閉電源。");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA 更新完成，裝置即將重新啟動。");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    otaInProgress = false;
    Serial.printf("OTA 更新失敗，錯誤代碼：%u\n", (unsigned int)error);
    if (WiFi.status() == WL_CONNECTED && millis() - connectedAt < BLUE_MS)
    {
      showColor(0, 0, 255);
    }
  });
  ArduinoOTA.begin();
  otaStarted = true;
}

void setup()
{
  Serial.begin(115200);
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  showColor(255, 0, 0);
  lastBlink = millis();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastRetry = millis();
  Serial.println("正在連線 Wi-Fi...");
}

void loop()
{
  unsigned long now = millis();
  bool connected = WiFi.status() == WL_CONNECTED;

  if (connected && !wasConnected)
  {
    connectedAt = now;
    showColor(0, 0, 255);
    Serial.print("Wi-Fi 已連線，IP 位址：");
    Serial.println(WiFi.localIP());
    setupOTA();
  }
  else if (!connected && wasConnected)
  {
    Serial.println("Wi-Fi 已斷線，正在重新連線...");
    if (otaStarted)
    {
      ArduinoOTA.end();
      otaStarted = false;
    }
    otaInProgress = false;
    redOn = true;
    showColor(255, 0, 0);
    lastBlink = now;
    lastRetry = now;
  }
  wasConnected = connected;

  if (!connected)
  {
    if (now - lastBlink >= BLINK_MS)
    {
      lastBlink = now;
      redOn = !redOn;
      showColor(redOn ? 255 : 0, 0, 0);
    }
    if (now - lastRetry >= RETRY_MS)
    {
      lastRetry = now;
      WiFi.reconnect();
    }
    return;
  }

  // 即使藍燈等待中，也要處理 OTA；更新進行時暫停燈效。
  ArduinoOTA.handle();
  if (!otaInProgress && now - connectedAt >= BLUE_MS)
  {
    showRedBlueFade();
  }
}
