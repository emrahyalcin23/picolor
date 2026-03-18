# picolor

PiColor is a smart ambient light, color, and brightness sensor based on the Raspberry Pi Pico, utilizing a TCS34725 sensor and NeoPixel LEDs. 

it automatically identifies its port when connected to a computer and accepts commands directly via a standalone terminal or PowerShell script. 
While its primary focus is real-time ambient color and light analysis, it also features a 15-minute rolling memory buffer to calculate and output color averages for requested short-term intervals.

## ✨ Features

* **Ambient Light & Color Sensing:** Reads environmental RGB values and ambient light intensity using the TCS34725 sensor.
* **Hardware Calibration:** R, G, B, and L (Lightness) coefficients can be adjusted on the fly to match specific environmental conditions using the attached Rotary Encoder.
* **Automatic Device Recognition (Handshake):** Prevents serial port conflicts by returning a unique response to the `KIMSIN` command, allowing the computer software to automatically find the correct device.
* **Short-Term Memory (15-Minute Rolling Buffer):** Stores calibrated color data in RAM every second, allowing you to fetch historical average data.
* **Visual Feedback:** Dynamically visualizes the calibration state and standby/active modes via an 8-LED NeoPixel strip/ring.


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
