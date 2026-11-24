# ESP32-C3、MAX30102 與 OTA

這是獨立的 Arduino IDE 專案，不會修改既有燈條韌體或 `C3Monitor`。
功能包含 Wi-Fi、OTA、WS2812B 狀態燈、手機監看 MAX30102 原始訊號，
以及可選的 EMG 讀取。**不計算血氧濃度、心率或診斷結果，不可作醫療用途。**

## 接線前先確認

此範例**假設 GPIO2 是 WS2812B 燈條的 SIG / DIN 輸入**。MAX30102 以
I²C 溝通，沒有接在此 GPIO2 上；EMG 類比輸出也不能同時接到 GPIO2。
如果你的「SIG Input」指的是其他裝置，請先確認接線，不要照此範例通電。

| 模組 | 腳位 | ESP32-C3 |
|---|---|---|
| WS2812B | SIG / DIN | GPIO2 |
| MAX30102 | SDA | GPIO6 |
| MAX30102 | SCL | GPIO7 |
| MAX30102 | GND | GND |
| EMG 模組（可選） | 類比輸出 | GPIO0 / ADC1 |

MAX30102 的 VCC、I²C 電壓依**手上模組的規格**接線：確認模組包含
合適的穩壓器與電位轉換，I²C 訊號不可超過 ESP32-C3 的 3.3 V。
未經確認不要將 5 V 直接接至 ESP32-C3 的任何 GPIO。I²C 需要適當
的上拉電阻；先確認模組是否已內建。所有模組須共地。GPIO6、GPIO7
與 GPIO0 可在程式開頭修改，請先確認所用開發板實際可用的腳位。

燈條數量預設 30、亮度為 20/255。燈條使用適當的外部 5 V 電源，
電源地要與 ESP32-C3 共地；全白全亮的 30 顆燈條可能需要約 1.8 A。
DIN 建議串接 330–470 Ω 電阻並使用合適的 3.3 V 轉 5 V 電位轉換器。
GPIO2 是啟動配置腳，外部電路不可在重置時拉低。勿帶電插拔燈條。

EMG 預設**停用**，避免未接線時產生虛假讀值；接上合適的感測器並確認
其輸出全程在 0–3.3 V、電源與 ADC 接法符合規格後，才把
`ENABLE_EMG` 改為 `true`。不要將電極直接接到 ESP32-C3。連接人體
前須使用符合人體使用規範的隔離設備；不要將人體連接到此實驗電路、
USB 電腦或市電供電設備。

## Arduino IDE 安裝與上傳

1. 在 Arduino IDE 的開發板管理員安裝 Espressif `esp32` 開發板套件
   （本範例以 2.0.17 編譯）；
   開啟 `c3_max30102_ota.ino`，選擇 ESP32C3 Dev Module 與正確序列埠。
   USB 序列埠若無輸出，檢查開發板的 USB CDC On Boot 設定。
2. 在函式庫管理員安裝 **Adafruit NeoPixel** 與
   **SparkFun MAX3010x Pulse and Proximity Sensor Library**（1.1.2）。
3. 把 `secrets.example.h` 複製為同目錄的 `secrets.h`，填入 2.4 GHz
   Wi-Fi 名稱、Wi-Fi 密碼，以及**至少 12 字元且與 Wi-Fi 不同**的 OTA
   密碼。`secrets.h` 已加入忽略清單，不要分享或提交。密碼會編入韌體；
   有實體存取權的人可能讀出，除非另行啟用適當的裝置保護。
4. 確認接線後，首次使用 USB 上傳。上傳會取代目前裝置韌體；選擇支援
   OTA 的分割區。以 115200 baud 開啟序列埠監控視窗，記下
   `WIFI_CONNECTED ip=...` 的 IP。
5. 手機連上同一可信任網路，用瀏覽器開啟 `http://裝置IP/`。請使用 IP，
   不要用主機名稱。若出現 `MAX30102_ERROR` 或網頁顯示 HTTP 503，
   請檢查供電、I²C 接線與模組電壓。

未連線時紅燈閃爍；連線後藍燈 5 秒，再顯示彩虹燈效。
OTA 更新時顯示橙色。重新連線後會再次顯示藍燈 5 秒。

## OTA

初次 USB 上傳成功後，Arduino IDE 的網路序列埠若能發現 `c3-max30102`，
可選擇該裝置上傳並輸入 OTA 密碼。電腦與裝置必須能在網路中互相連線；
不同網段、mDNS 或用戶端隔離可能妨礙發現與上傳。OTA 期間保持供電。
後續韌體仍須保留相容的 OTA、Wi-Fi 設定及分割區；失敗時可能需要 USB
重新燒錄。

**HTTP 網頁與 API 沒有登入或加密**，可連上裝置的人都能讀取感測資料。
ArduinoOTA 密碼不加密傳輸，且本範例不提供韌體簽章或安全開機。
僅使用可信任的隔離網路，勿將 HTTP 或 OTA 暴露至網際網路。

## API 與 Processing

請使用 `http://裝置IP`，回應都不快取：

| 路徑 | 回應 |
|---|---|
| `GET /api/max30102` | `{"red":1234,"ir":5678,"age_ms":10}`；未連接、沒有新樣本或資料過期時 HTTP 503 |
| `GET /api/emg` | `{"value":2048}`，與現有 `C3Monitor` 相容；停用或未取樣時 HTTP 503 |
| `GET /api/emg?after=序號` | `{"samples":[...],"next_seq":123,"dropped":false}`；見下方說明 |
| `GET /api/telemetry` | `{"rssi":-50,"uptime":123,"heap":123456}`，uptime 單位為秒 |
| `GET /api/state` | 唯讀燈效狀態，供現有 `C3Monitor` 使用；沒有 POST 控制功能 |

要使用緩衝樣本，第一次傳 `after=0`，之後將回應的 `next_seq` 帶入下一次
請求。`dropped=true` 表示呼叫間隔太長、超過 128 筆緩衝區，部分資料遺失。
序號是 32 位元無號整數，會循環；裝置重啟後須重新從 0 開始。韌體以約
200 Hz 嘗試取樣，但 Wi-Fi、HTTP、OTA 和 I²C 會影響實際速率，無法保證
固定取樣間隔。此 API 不附每筆時間戳。

開啟 EMG 後，USB 序列埠也會輸出 `EMG,2048` 格式供現有 Processing
程式讀取；同時開啟序列埠監控視窗可能佔用埠。既有 `C3Monitor` 的 HTTP
模式讀取的是最新單筆值，不會自動使用 `after` 緩衝 API，可能遺失波形細節；
需要完整緩衝資料時，Processing 端須另外實作序號輪詢。
MAX30102 的網頁圖表每 0.5 秒顯示一次最新值，不是完整原始波形。
