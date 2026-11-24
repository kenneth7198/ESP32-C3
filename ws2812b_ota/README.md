# ESP32-C3 WS2812B OTA

1. 在 Arduino IDE 安裝 ESP32 開發板套件與 Adafruit NeoPixel 函式庫。
2. 將 `secrets.example.h` 複製為 `secrets.h`，填入 2.4 GHz Wi-Fi 名稱、Wi-Fi 密碼及 OTA 密碼。`secrets.h` 不會提交至 Git。
3. 選擇 ESP32C3 Dev Module 與支援 OTA 的分割區；使用 USB 連接時可啟用 USB CDC On Boot。首次以 USB 上傳，之後可透過可信任的區域網路進行 OTA 更新。

30 顆 WS2812B 的 DIN 接 GPIO 2，建議使用 330–470 Ω 電阻及 3.3V 轉 5V 電位轉換器。燈條需使用適當的 5V 電源，並與 ESP32-C3 共地；全白全亮約需 1.8A。GPIO 2 為啟動配置腳，重置時不可被外部電路拉低。

未連線時紅燈閃爍；連線後顯示藍燈 5 秒，再交替播放藍色與紅色漸亮、漸暗。OTA 密碼不加密傳輸，請勿將 OTA 暴露至網際網路。
