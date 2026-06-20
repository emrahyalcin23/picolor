# PiColor

**PiColor** is a smart ambient light and color analyzer built on the Raspberry Pi Pico 2W (RP2350). It reads RGB and clear-channel data from a TCS34725 sensor, normalizes the results to a 0–100 scale, and streams them over USB Serial, WiFi TCP, and Bluetooth Low Energy simultaneously.

Current firmware: **v0.09.04**

---

## Hardware

| Component | Interface | Pins |
|---|---|---|
| TCS34725 color sensor | I2C | SDA=4, SCL=5 |
| TCS white LED | GPIO (active-LOW) | 15 |
| Rotary encoder | GPIO + ISR | CLK=16, DT=17, SW=18 |
| NeoPixel strip (8 LEDs) | Data | 28 |
| SD card | SPI | CS=1, SCK=2, MOSI=3, MISO=0 |

Sensor settings compiled in: gain **4×**, integration time **50 ms**.

---

## Connections

### USB Serial
Connect via any serial terminal at **115200 baud**. The device identifies itself by returning `IDENTITY=PICOLOR_v0.09.04` to the `KIMSIN` command — use this for automatic port detection.

> **Note:** USB Serial (COM port) is **disabled** in the current build. The firmware uses **No USB** mode — the device never appears as a COM port. To re-enable: set **USB Stack → TinyUSB** in Arduino IDE settings and rebuild. The Apple 20W USB-C charger incompatibility is unrelated to USB stack choice and affects both modes equally (see `SINIRLILIKLAR.md`).

### WiFi (AP + STA dual mode)
The device simultaneously runs its own access point **and** can connect to a home/office network.

| Mode | Default | Notes |
|---|---|---|
| AP SSID | `PiColor` | Always active |
| AP Password | `picolor123` | WPA2 |
| AP IP | `192.168.4.1` | Fixed (default) |
| TCP port | `8266` | Configurable at runtime |
| mDNS | `picolor.local` | On connected network |
| Max TCP clients | 4 | Simultaneous |

STA credentials are read from `config.json` (SD card), then EEPROM, then compile-time defaults. Use `WIFI_STA_KAYDET` to persist credentials to EEPROM without SD.

### Bluetooth LE
Implements **Nordic UART Service (NUS)** via BTstack.

| Characteristic | UUID | Direction |
|---|---|---|
| RX (host → device) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write |
| TX (device → host) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Notify |

All three channels receive identical output with `;wifi=<status>` appended. TCP and BLE responses additionally append `;user=<username>`. The `wifi=` field contains the home network SSID when connected, `CONN` while connecting, or `AP` when only the access point is active.

### Connection Channel Priority

Recommended order for first-time connection or troubleshooting:

| Priority | Channel | Address | Condition |
|---|---|---|---|
| 1 | **WiFi AP** | `192.168.4.1:8266` | Always available — no configuration needed |
| 2 | **WiFi STA** | `picolor.local:8266` | Home network credentials configured |
| 3 | **Bluetooth LE** | NUS profile | No WiFi required; BLE pairing needed |
| 4 | **USB Serial** | COM port | Disabled in current build (No USB mode) |

WiFi AP is the most reliable starting point because it is always active and requires no prior configuration. STA is preferred once credentials are set — it allows the host machine to reach the device without switching networks. BLE is a useful fallback when no network is available. COM is last because it is currently disabled; re-enabling it requires a firmware rebuild.

**Expected startup time:** approximately 50–70 seconds from power-on to ready state (Bluetooth stack init ~5 s, AP bringup ~5 s, STA connection ~10–30 s depending on router proximity). The NeoPixel strip signals progress: R→G→B blink sequence at boot completion, solid red while connecting to the home network, 1-second solid green when connected.

---

## Configuration

At boot the device applies settings in this priority order:

1. `config.json` on SD card (highest priority)
2. EEPROM (WiFi STA credentials only)
3. Compile-time defaults (lowest priority)

`/config.json` reference — all keys are optional:

```json
{
  "wifi_ap_ssid":      "PiColor",
  "wifi_ap_pass":      "picolor123",
  "wifi_sta_otomatik": true,
  "wifi_sta_ssid":     "",
  "wifi_sta_pass":     "",
  "tcp_port":          8266,
  "pil_modu":          false,
  "basamak":           1,
  "logaritmik":        false,
  "log_dekad":         3.0,
  "mod":               "STABIL",
  "dual_cikti":        true,
  "logging_enabled":   false,
  "log_file":          "/user_log.csv"
}
```

`pil_modu`: when `true`, STA reconnection stops after 3 failed attempts (battery-powered use). When `false` (default), reconnection continues indefinitely (mains-powered use).

---

## Output Protocol

Every output line follows this format:

**Serial:**
```
<timestamp_ms>;<type>;<mode>;<v1>;<v2>;<v3>[;<meta>];wifi=<status>
```

**TCP / BLE:**
```
<timestamp_ms>;<type>;<mode>;<v1>;<v2>;<v3>[;<meta>];wifi=<status>;user=<username>
```

The `wifi=` field is the home network SSID when connected, `CONN` while connecting, or `AP` when only the access point is active.

**Type field:**

| Value | Name | Description |
|---|---|---|
| 0 | LIVE | Live stream line (300 ms cadence) |
| 1 | SINGLE | Single instantaneous reading |
| 2 | AVERAGE | Time-averaged reading |
| 3 | RAW | Raw 16-bit sensor values |
| 4 | STATUS | Status message (v1/v2/v3 = 0) |
| 5 | ERROR | Error message (v1/v2/v3 = 0) |
| 6 | DUAL | Raw + processed combined |
| 7 | FULL | Complete data packet |

**Mode field:** `0` = STABIL, `1` = DINAMIK

**DUAL format (type 6):**
```
<ts>;6;<mode>;<rawR>;<rawG>;<rawB>;<rawC>;<procR>;<procG>;<procB>[;<meta>]
```

### Value range
Processed RGB values are normalized to **0–100**. The `RAW` command returns raw 16-bit sensor values (0–65535). Lux and color temperature appear in the `meta` field where applicable.

---

## Calibration

### STABIL mode (default)
Normalizes against a fixed reference (`NORM_INPUT_MAX = 1000`). Calibration coefficients wR, wG, wB, wL shift individual channels:

```
output = (raw × (1 + w_channel) × (1 + wL)) / NORM_INPUT_MAX × 100
```

Coefficients range from −1.0 to +1.0 (minimum effective value: 0.05). Adjust them in real time with the rotary encoder — short-press cycles through R → G → B → L channels, and turning adjusts the selected coefficient.

### DINAMIK mode
Scales to the highest value observed so far (`maxObserved`). When a new maximum is detected, `maxObserved` grows immediately with a 10 % margin; every minute it decays by 5 %. This keeps the full 0–100 range occupied even as lighting changes. Use `SIFIRLA` to reset `maxObserved` to 1000.

Both modes can be overridden per command (e.g. `STABIL_OKU_0`) without changing the global mode.

### Output scaling
| Command | Effect |
|---|---|
| `BASAMAK_<n>` | Decimal places in output (0–6, default 1) |
| `LOGARITMIK` | Apply Weber-Fechner log transform: `y = log₁₀(1 + x·(10^D−1)/100) / D · 100` |
| `LOGARITMIK_<n>` | Same, with custom decade range D (1.0–5.0; 3 = 1000:1 recommended) |
| `LINEER` | Remove log transform (default) |
| `VARSAYILAN` | Reset to defaults: 1 decimal, linear |

Log scaling preserves f(0)=0 and f(100)=100 and does not affect RAW values, lux, or coefficients.

---

## Command Reference

Commands are case-insensitive. Multiple commands can be sent on one line, space or comma separated. Use `USER:<name> <command>` prefix on TCP/BLE to tag output lines with a username.

### Device info
```
KIMSIN                    → IDENTITY=PICOLOR_v0.09.04
VERSIYON / VERSION        → firmware version string
DURUM / STATUS            → full system status (WiFi, BLE, settings)
MOD                       → current calibration mode
YARDIM / HELP             → command list
TEST                      → system self-test
```

### Calibration mode
```
MOD_STABIL                Switch to STABIL (fixed reference)
MOD_DINAMIK               Switch to DINAMIK (adaptive)
KATSAYILAR / COEFF        Show wR, wG, wB, wL values
SIFIRLA / RESET           Zero all coefficients; reset maxObserved to 1000
```

### Reading commands
```
OKU                       Start live stream (type 0, 300 ms interval)
OKU_STOP                  Stop live stream
OKU_0                     Single reading, global mode (type 1)
OKU_S<n>                  Average of last n seconds, e.g. OKU_S60 (type 2)
OKU_<m>                   Average of last m minutes, e.g. OKU_15  (type 2)
RAW                       Single raw 16-bit reading (type 3)
TUM / ALL                 Full data packet: instant + 60/300/900 s averages (type 7)
```

Mode override prefixes work with all reading commands:
```
STABIL_OKU_0 / STABIL_OKU_S<n> / STABIL_OKU_<m> / STABIL_OKU
DINAMIK_OKU_0 / DINAMIK_OKU_S<n> / DINAMIK_OKU_<m> / DINAMIK_OKU
```

### Dual output (raw + processed)
```
DUAL_MODE_ON / DUAL_MODE_OFF     Enable/disable dual output globally
DUAL_OKU                         Single dual reading (type 6)
DUAL_OKU_S<n>                    Dual average, last n seconds
DUAL_AKIS                        Live dual stream
STABIL_DUAL_OKU[_0/_S<n>]        Dual with STABIL override
DINAMIK_DUAL_OKU[_0/_S<n>]       Dual with DINAMIK override
STABIL_DUAL_AKIS / DINAMIK_DUAL_AKIS
```

### Output format
```
BASAMAK_<n>               Decimal places 0–6 (default 1)
LOGARITMIK                Log scale on (current decade setting)
LOGARITMIK_<n>            Log scale on + set decade (1.0–5.0)
LINEER                    Linear scale (default)
VARSAYILAN / DEFAULT      Factory defaults: 1 decimal, linear
OLCEK / SCALE             Show current format settings
```

### Buffer & SD
```
TAMPON / BUFFER           Buffer fill: count / 900 samples
TAMPON_SIL / BUFFER_CLEAR Clear the 15-minute rolling buffer
SD_DURUM / SD_STATUS      SD card status and auto-log state
SD_AKTAR / SD_EXPORT      Export buffer to /export_<ts>.csv on SD
SDCARD_YAZ_AKTIF          Enable per-sample auto-logging to CSV
SDCARD_YAZ_PASIF          Disable per-sample auto-logging
```

### WiFi
```
WIFI_AP_SSID=<name>       Set AP SSID in RAM (this session only)
WIFI_AP_PASS=<pass>       Set AP password in RAM (this session only)
WIFI_AP_YENILE            Restart AP with current RAM settings

WIFI_STA_SSID=<network>   Set home network SSID in RAM
WIFI_STA_PASS=<pass>      Set home network password in RAM
WIFI_STA_BAGLAN           Connect to configured home network
WIFI_STA_KES              Disconnect from home network (AP unaffected)
WIFI_STA_KAYDET           Save STA credentials to EEPROM (no SD needed)
WIFI_STA_SIFIRLA          Erase EEPROM credentials, revert to defaults
WIFI_BILGI                Show AP IP, STA IP, and connection status

TCP_PORT=<port>           Change TCP server port at runtime
WIRELESS_ENABLED=<0|1>    Enable/disable WiFi for this session
```

---

## Data Storage

### 15-minute RAM buffer
The device samples sensor data every second and stores up to **900 samples** in a circular buffer. Both processed (histR/G/B, float 0–100) and raw (histRawR/G/B/C, uint16_t) values are kept. Averages over any interval up to 15 minutes can be read with `OKU_S<n>` or `OKU_<m>`.

### SD card
When an SD card is present, the device:
- Reads `config.json` on boot (overrides compile-time defaults).
- Creates `/picolor_data.csv` and appends a row each time `SD_AKTAR` or auto-logging triggers.

CSV columns: `timestamp_ms, raw_r, raw_g, raw_b, raw_c, proc_r, proc_g, proc_b, lux, color_temp_k, wR, wG, wB, wL, state, mode`

User command history (optional): `/user_log.csv` — `timestamp, username, client_type, command`. WiFi passwords are masked as `***`.

---

---

# PiColor [TÜRKÇE]

**PiColor**, Raspberry Pi Pico 2W (RP2350) üzerine kurulu akıllı bir ortam ışığı ve renk analizörüdür. TCS34725 sensöründen RGB ve clear-kanal verisi okur, sonuçları 0–100 skalasına normalize eder ve USB Serial, WiFi TCP ve Bluetooth Low Energy üzerinden eş zamanlı olarak yayınlar.

Güncel firmware: **v0.09.04**

---

## Donanım

| Bileşen | Arabirim | Pinler |
|---|---|---|
| TCS34725 renk sensörü | I2C | SDA=4, SCL=5 |
| TCS beyaz LED | GPIO (aktif-LOW) | 15 |
| Rotary encoder | GPIO + ISR | CLK=16, DT=17, SW=18 |
| NeoPixel şerit (8 LED) | Data | 28 |
| SD kart | SPI | CS=1, SCK=2, MOSI=3, MISO=0 |

Derleme zamanı sensör ayarları: kazanım **4×**, entegrasyon süresi **50 ms**.

---

## Bağlantı Kanalları

### USB Serial
115200 baud ile herhangi bir seri terminale bağlanın. `KIMSIN` komutuna `IDENTITY=PICOLOR_v0.09.04` yanıtı döner — otomatik port tanıma için kullanın.

> **Not:** USB Serial (COM portu) bu derlemede **devre dışıdır.** Firmware **No USB** modunu kullanıyor — cihaz hiçbir zaman COM portu olarak görünmez. Yeniden etkinleştirmek için: Arduino IDE'de **USB Stack → TinyUSB** seçin ve yeniden derleyin. Apple 20W USB-C şarj başlığı uyumsuzluğu, USB stack seçimiyle ilgisizdir; her iki modda da aynı şekilde görünür (bkz. `SINIRLILIKLAR.md`).

### WiFi (AP + STA çift mod)
Cihaz kendi erişim noktasını açarken aynı anda ev/ofis ağına da bağlanabilir.

| Mod | Varsayılan | Not |
|---|---|---|
| AP SSID | `PiColor` | Her zaman aktif |
| AP Şifre | `picolor123` | WPA2 |
| AP IP | `192.168.4.1` | Sabit (varsayılan) |
| TCP port | `8266` | Çalışma zamanında değiştirilebilir |
| mDNS | `picolor.local` | Bağlı ağda çalışır |
| Maks TCP istemci | 4 | Eş zamanlı |

STA kimlik bilgileri önce `config.json` (SD), sonra EEPROM, sonra derleme zamanı sabitlerinden okunur. SD kart yoksa `WIFI_STA_KAYDET` ile EEPROM'a kalıcı olarak kaydedilir.

### Bluetooth LE
BTstack tabanlı **Nordic UART Service (NUS)** uygular.

| Characteristic | UUID | Yön |
|---|---|---|
| RX (host → cihaz) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write |
| TX (cihaz → host) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Notify |

Üç kanalın çıktısı aynıdır; tüm satırlara `;wifi=<durum>` eklenir. TCP ve BLE yanıtlarına ek olarak `;user=<kullanıcı>` da eklenir. `wifi=` alanı; ev ağına bağlıyken SSID adı, bağlanırken `CONN`, yalnızca AP aktifken `AP` değerini taşır.

### Bağlantı Kanalı Öncelik Sırası

İlk bağlantı veya sorun giderme için önerilen sıra:

| Öncelik | Kanal | Adres | Koşul |
|---|---|---|---|
| 1 | **WiFi AP** | `192.168.4.1:8266` | Her zaman aktif — yapılandırma gerekmez |
| 2 | **WiFi STA** | `picolor.local:8266` | Ev ağı kimlik bilgileri yapılandırılmış olmalı |
| 3 | **Bluetooth LE** | NUS profili | WiFi gerekmez; BLE eşleştirme şart |
| 4 | **USB Serial** | COM portu | Bu derlemede devre dışı (No USB modu) |

WiFi AP en güvenilir başlangıç noktasıdır; her zaman aktiftir ve önceden yapılandırma gerektirmez. STA kimlik bilgileri ayarlandıktan sonra STA tercih edilir — ana bilgisayar, ağ değiştirmeden cihaza erişebilir. BLE, ağ yokken kullanışlı bir alternatiftir. COM sonuncu sıradadır çünkü bu derlemede devre dışıdır; etkinleştirmek için yeniden derleme gerekir.

**Beklenen açılış süresi:** Güç açılışından hazır duruma ~50–70 saniye (Bluetooth yığını başlatma ~5 s, AP kurulumu ~5 s, STA bağlantısı modem mesafesine göre ~10–30 s). NeoPixel şerit ilerlemeyi gösterir: boot tamamlandığında R→G→B blink dizisi, ev ağına bağlanılırken sürekli kırmızı, bağlantı tamamlandığında 1 saniye sürekli yeşil.

---

## Yapılandırma

Önyüklemede ayarlar şu öncelik sırasıyla uygulanır:

1. SD karttaki `config.json` (en yüksek öncelik)
2. EEPROM (yalnızca WiFi STA kimlik bilgileri)
3. Derleme zamanı sabitleri (en düşük öncelik)

`/config.json` referansı — tüm anahtarlar isteğe bağlıdır:

```json
{
  "wifi_ap_ssid":      "PiColor",
  "wifi_ap_pass":      "picolor123",
  "wifi_sta_otomatik": true,
  "wifi_sta_ssid":     "",
  "wifi_sta_pass":     "",
  "tcp_port":          8266,
  "pil_modu":          false,
  "basamak":           1,
  "logaritmik":        false,
  "log_dekad":         3.0,
  "mod":               "STABIL",
  "dual_cikti":        true,
  "logging_enabled":   false,
  "log_file":          "/user_log.csv"
}
```

`pil_modu`: `true` ise STA yeniden bağlantı 3 başarısız denemeden sonra durur (pil beslemeli kullanım). `false` (varsayılan) ise bağlantı denemesi süreklidir (şebeke beslemeli kullanım).

---

## Çıktı Protokolü

Her çıktı satırı şu formatı izler:

**Seri:**
```
<timestamp_ms>;<tip>;<mod>;<v1>;<v2>;<v3>[;<meta>];wifi=<durum>
```

**TCP / BLE:**
```
<timestamp_ms>;<tip>;<mod>;<v1>;<v2>;<v3>[;<meta>];wifi=<durum>;user=<kullanıcı>
```

`wifi=` alanı; ev ağına bağlıyken SSID adı, bağlanırken `CONN`, yalnızca AP aktifken `AP` değerini taşır.

**Tip alanı:**

| Değer | Ad | Açıklama |
|---|---|---|
| 0 | LIVE | Canlı akış satırı (300 ms aralık) |
| 1 | SINGLE | Tek anlık okuma |
| 2 | AVERAGE | Zaman ortalamalı okuma |
| 3 | RAW | Ham 16-bit sensör değerleri |
| 4 | STATUS | Durum mesajı (v1/v2/v3 = 0) |
| 5 | ERROR | Hata mesajı (v1/v2/v3 = 0) |
| 6 | DUAL | Ham + işlenmiş birleşik |
| 7 | FULL | Tam veri paketi |

**Mod alanı:** `0` = STABIL, `1` = DINAMIK

**DUAL format (tip 6):**
```
<ts>;6;<mod>;<hamR>;<hamG>;<hamB>;<hamC>;<prR>;<prG>;<prB>[;<meta>]
```

### Değer aralığı
İşlenmiş RGB değerleri **0–100** aralığına normalize edilir. `RAW` komutu ham 16-bit sensör değerlerini (0–65535) döndürür. Lüks ve renk sıcaklığı uygun komutların `meta` alanında yer alır.

---

## Kalibrasyon

### STABIL mod (varsayılan)
Sabit bir referans değere (`NORM_INPUT_MAX = 1000`) göre normalize eder. wR, wG, wB, wL katsayıları her kanalı ayrı ayrı kaydırır:

```
çıktı = (ham × (1 + w_kanal) × (1 + wL)) / NORM_INPUT_MAX × 100
```

Katsayılar −1.0 ile +1.0 arasındadır (minimum etkin değer: 0.05). Rotary encoder ile gerçek zamanlı ayarlanır — kısa basış R → G → B → L kanallarını döngüsel seçer, çevirme seçili katsayıyı değiştirir.

### DINAMIK mod
Şimdiye kadar gözlemlenen en yüksek değere (`maxObserved`) göre ölçekler. Yeni maksimum algılandığında `maxObserved` %10 marjla anında büyür; her dakika %5 azalır. Işık değiştikçe 0–100 aralığı dolu kalmaya devam eder. `SIFIRLA` ile `maxObserved` 1000'e döner.

Her iki mod da global modu değiştirmeden komut bazlı geçersiz kılınabilir (örn. `STABIL_OKU_0`).

### Çıktı ölçeği
| Komut | Etki |
|---|---|
| `BASAMAK_<n>` | Ondalık basamak sayısı (0–6, varsayılan 1) |
| `LOGARITMIK` | Weber-Fechner log dönüşümü: `y = log₁₀(1 + x·(10^D−1)/100) / D · 100` |
| `LOGARITMIK_<n>` | Aynı, özel dekad aralığı D ile (1.0–5.0; 3 = 1000:1 önerilen) |
| `LINEER` | Log dönüşümü kaldır (varsayılan) |
| `VARSAYILAN` | Fabrika ayarına dön: 1 basamak, lineer |

Log ölçeği f(0)=0 ve f(100)=100'ü korur; ham değerleri, lüksi ve katsayıları etkilemez.

---

## Komut Referansı

Komutlar büyük/küçük harf duyarsızdır. Aynı satıra birden fazla komut yazılabilir (boşluk veya virgülle ayrılır). TCP/BLE'de `USER:<ad> <komut>` öneki kullanılarak çıktı satırlarına kullanıcı adı etiketi eklenir.

### Cihaz bilgisi
```
KIMSIN                    → IDENTITY=PICOLOR_v0.09.04
VERSIYON / VERSION        → firmware sürüm dizisi
DURUM / STATUS            → tam sistem durumu (WiFi, BLE, ayarlar)
MOD                       → mevcut kalibrasyon modu
YARDIM / HELP             → komut listesi
TEST                      → sistem öz-testi
```

### Kalibrasyon modu
```
MOD_STABIL                STABIL moda geç (sabit referans)
MOD_DINAMIK               DINAMIK moda geç (uyarlamalı)
KATSAYILAR / COEFF        wR, wG, wB, wL değerlerini göster
SIFIRLA / RESET           Tüm katsayıları sıfırla; maxObserved = 1000
```

### Okuma komutları
```
OKU                       Canlı akışı başlat (tip 0, 300 ms aralık)
OKU_STOP                  Canlı akışı durdur
OKU_0                     Tek anlık okuma, global mod (tip 1)
OKU_S<n>                  Son n SANİYE ortalaması, ör. OKU_S60 (tip 2)
OKU_<m>                   Son m DAKİKA ortalaması, ör. OKU_15  (tip 2)
RAW                       Tek ham 16-bit okuma (tip 3)
TUM / ALL                 Tam paket: anlık + 60/300/900 s ortalamaları (tip 7)
```

Mod geçersiz kılma önekleri tüm okuma komutlarıyla çalışır:
```
STABIL_OKU_0 / STABIL_OKU_S<n> / STABIL_OKU_<m> / STABIL_OKU
DINAMIK_OKU_0 / DINAMIK_OKU_S<n> / DINAMIK_OKU_<m> / DINAMIK_OKU
```

### Dual çıktı (ham + işlenmiş)
```
DUAL_MODE_ON / DUAL_MODE_OFF     Global dual çıktıyı aç/kapat
DUAL_OKU                         Tek dual okuma (tip 6)
DUAL_OKU_S<n>                    Dual ortalama, son n saniye
DUAL_AKIS                        Canlı dual akış
STABIL_DUAL_OKU[_0/_S<n>]        STABIL geçersiz kılma ile dual
DINAMIK_DUAL_OKU[_0/_S<n>]       DINAMIK geçersiz kılma ile dual
STABIL_DUAL_AKIS / DINAMIK_DUAL_AKIS
```

### Çıktı formatı
```
BASAMAK_<n>               Ondalık basamak 0–6 (varsayılan 1)
LOGARITMIK                Log ölçeği aç (mevcut dekad ayarıyla)
LOGARITMIK_<n>            Log ölçeği aç + dekad ayarla (1.0–5.0)
LINEER                    Lineer ölçek (varsayılan)
VARSAYILAN / DEFAULT      Fabrika ayarı: 1 basamak, lineer
OLCEK / SCALE             Mevcut format ayarlarını göster
```

### Tampon ve SD
```
TAMPON / BUFFER           Tampon doluluk: örnek / 900
TAMPON_SIL / BUFFER_CLEAR Tamponu temizle
SD_DURUM / SD_STATUS      SD kart durumu ve otomatik kayıt durumu
SD_AKTAR / SD_EXPORT      Tamponu SD'ye /export_<ts>.csv olarak yaz
SDCARD_YAZ_AKTIF          Her örnekte SD'ye otomatik kayıt AÇ
SDCARD_YAZ_PASIF          Her örnekte SD'ye otomatik kayıt KAPAT
```

### WiFi
```
WIFI_AP_SSID=<adi>        AP SSID'sini RAM'e yaz (bu oturum)
WIFI_AP_PASS=<sifre>      AP şifresini RAM'e yaz (bu oturum)
WIFI_AP_YENILE            AP'yi mevcut RAM bilgileriyle yeniden başlat

WIFI_STA_SSID=<ag>        Modem ağ adını RAM'e yaz
WIFI_STA_PASS=<sifre>     Modem şifresini RAM'e yaz
WIFI_STA_BAGLAN           Yapılandırılmış ev ağına bağlan
WIFI_STA_KES              Ev ağından kes (AP etkilenmez)
WIFI_STA_KAYDET           STA kimlik bilgilerini EEPROM'a kaydet
WIFI_STA_SIFIRLA          EEPROM kimlik bilgilerini sil, varsayılana dön
WIFI_BILGI                AP IP, STA IP ve bağlantı durumunu göster

TCP_PORT=<port>           TCP sunucu portunu çalışma zamanında değiştir
WIRELESS_ENABLED=<0|1>    WiFi'yi bu oturum için etkinleştir/devre dışı bırak
```

---

## Veri Depolama

### 15 dakikalık RAM tamponu
Cihaz her saniye sensör verisi örnekler ve dairesel bir tamponda en fazla **900 örnek** saklar. Hem işlenmiş (histR/G/B, float 0–100) hem de ham (histRawR/G/B/C, uint16_t) değerler tutulur. `OKU_S<n>` veya `OKU_<m>` ile 15 dakikaya kadar herhangi bir aralığın ortalaması alınabilir.

### SD kart
SD kart takılıysa cihaz:
- Önyüklemede `config.json` okur (derleme zamanı sabitlerini geçersiz kılar).
- `/picolor_data.csv` oluşturur; `SD_AKTAR` veya otomatik kayıt tetiklendiğinde satır ekler.

CSV sütunları: `timestamp_ms, raw_r, raw_g, raw_b, raw_c, proc_r, proc_g, proc_b, lux, color_temp_k, wR, wG, wB, wL, state, mode`

İsteğe bağlı kullanıcı komut geçmişi: `/user_log.csv` — `timestamp, username, client_type, command`. WiFi şifreleri `***` olarak maskelenir.
