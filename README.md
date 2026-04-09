# PiColor 🎨

PiColor is a smart ambient light, color, and brightness sensor based on the Raspberry Pi Pico, utilizing a TCS34725 sensor and NeoPixel LEDs. 

it automatically identifies its port when connected to a computer and accepts commands directly via a standalone terminal or PowerShell script. 
While its primary focus is real-time ambient color and light analysis, it also features a 15-minute rolling memory buffer to calculate and output color averages for requested short-term intervals.

## ✨ Features

* **Ambient Light & Color Sensing:** Reads environmental RGB values and ambient light intensity using the TCS34725 sensor.
* **Hardware Calibration:** R, G, B, and L (Lightness) coefficients can be adjusted on the fly to match specific environmental conditions using the attached Rotary Encoder.
* **Automatic Device Recognition (Handshake):** Prevents serial port conflicts by returning a unique response to the `KIMSIN` command, allowing the computer software to automatically find the correct device.
* **Short-Term Memory (15-Minute Rolling Buffer):** Stores calibrated color data in RAM every second, allowing you to fetch historical average data.
* **Visual Feedback:** Dynamically visualizes the calibration state and standby/active modes via an 8-LED NeoPixel strip/ring.

## 📊 Output Value Range
 
All calibrated RGB outputs (`OKU`, `OKU_0`, `OKU_X`) are normalized to **0–100**.
 
| Constant | Value | Description |
|---|---|---|
| `NORM_INPUT_MAX` | 65535 × 2.0 × 2.0 = **262140** | Theoretical max calibrated value (raw max × max color factor × max light factor) |
| `NORM_OUTPUT_MIN` | **0** | Minimum output value |
| `NORM_OUTPUT_MAX` | **100** | Maximum output value |
 
- Designed for **indoor ambient light** conditions.
- Values exceeding 262140 are clamped to 100.
- The `RAW` command returns uncalibrated 16-bit sensor values (0–65535).
 

# PiColor [TÜRKÇE] 🎨

PiColor, Raspberry Pi Pico tabanlı, TCS34725 sensörü ve NeoPixel LED'ler kullanan akıllı bir ortam ışığı, renk ve parlaklık sensörüdür. 

Bir bilgisayara bağlandığında portunu otomatik olarak tanımlar ve bağımsız bir terminal veya PowerShell betiği üzerinden doğrudan komut kabul eder. 
Ana odak noktası gerçek zamanlı ortam rengi ve ışık analizi olmakla birlikte, talep edilen kısa süreli aralıklar için renk ortalamalarını hesaplayıp çıktı vermek üzere 15 dakikalık döngüsel bir bellek havuzuna (rolling memory buffer) sahiptir.

## ✨ Özellikler

* **Ortam Işığı ve Renk Algılama:** TCS34725 sensörünü kullanarak çevresel RGB değerlerini ve ortam ışığı şiddetini okur.
* **Donanımsal Kalibrasyon:** Sisteme bağlı Rotary Encoder kullanılarak R, G, B ve L (Aydınlık) katsayıları, spesifik ortam koşullarına uyum sağlamak üzere anlık olarak ayarlanabilir.
* **Otomatik Cihaz Tanıma (Handshake):** `KIMSIN` komutuna benzersiz bir yanıt dönerek seri port çakışmalarını önler ve bilgisayar yazılımının doğru cihazı otomatik olarak bulmasını sağlar.
* **Kısa Süreli Hafıza (15 Dakikalık Veri Havuzu):** Kalibre edilmiş renk verilerini her saniye RAM üzerinde depolayarak geçmişe dönük ortalama verileri çekmenize olanak tanır.
* **Görsel Geri Bildirim:** 8'li NeoPixel LED şerit/halka üzerinden kalibrasyon durumunu ve bekleme/aktif modları dinamik olarak görselleştirir.


## 📊 Çıktı Değer Aralığı
 
Tüm kalibre edilmiş RGB çıktıları (`OKU`, `OKU_0`, `OKU_X`) **0–100** aralığına normalize edilir.
 
| Sabit | Değer | Açıklama |
|---|---|---|
| `NORM_INPUT_MAX` | 65535 × 2.0 × 2.0 = **262140** | Teorik maksimum kalibre değer (ham maks × maks renk katsayısı × maks ışık katsayısı) |
| `NORM_OUTPUT_MIN` | **0** | Minimum çıktı değeri |
| `NORM_OUTPUT_MAX` | **100** | Maksimum çıktı değeri |
 
- **İç mekan** ortam ışığı koşulları için tasarlanmıştır.
- 262140 değerini aşan değerler 100 ile kesilir.
- `RAW` komutu kalibre edilmemiş 16-bit ham sensör değerlerini döndürür (0–65535).
