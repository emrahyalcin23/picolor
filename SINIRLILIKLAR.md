# PiColor — Bilinen Sınırlılıklar

Bu belge, PiColor firmware geliştirme sürecinde karşılaşılan donanım ve yazılım sınırlılıklarını, denenen çözümleri ve vazgeçilme nedenlerini belgeler.

---

## 1. USB-C Hızlı Şarj Başlığı Uyumsuzluğu

**Etkilenen donanım:** Apple 20W USB-C ve USB-C PD (Power Delivery) destekli diğer hızlı şarj başlıkları  
**Durum:** Çözümsüz — yazılımla çözülemiyor

### Belirti

Pico 2W, USB-A yavaş şarj başlığıyla (≥1A) sorunsuz çalışır. Ancak Apple 20W USB-C şarj başlığına bağlandığında cihaz hiç başlamaz: yalnızca TCS34725 sensörünün donanım güç-açılış LED'i (1 kez) yanar, kullanıcı firmware'i hiç çalışmaz.

### Neden Çalışmıyor

Apple 20W USB-C şarj başlığı, micro-USB kablo üzerinden bağlandığında (USB-C → micro-USB) legacy USB moduna girer. Bu modda, bağlı cihaz Apple şarj protokolünü (D+/D− voltaj sinyali) desteklemediğinde şarj başlığı akımı **~500mA** ile sınırlar.

RP2350 + CYW43439 kombinasyonu, `runtime_init()` aşamasında (herhangi bir kullanıcı kodu çalışmadan önce) bu sınırı aşan anlık akım çekimi yapabilir. Bu durum VSYS geriliminde düşüşe neden olur; RP2350 brownout sıfırlaması yapar ve döngü başa döner.

Sorun, kullanıcı firmware katmanının altında gerçekleştiğinden **yazılım değişikliğiyle çözülmesi mümkün değildir.**

### Çalışması İçin Gereken Donanım Koşulu

Pico 2W ile USB-C hızlı şarj başlığı arasına **BMS (Battery Management System)** devresi bağlanmalıdır. BMS, şarj başlığından gelen enerjiyi (pil veya kondansatör ile) tamponlar ve Pico 2W'ye her koşulda kararlı 5V/≥1A sağlar.

Alternatif olarak USB-A çıkışlı herhangi bir şarj başlığı (≥1A) doğrudan kullanılabilir.

### Denenen Yazılım Yaklaşımları ve Sonuçları

| Yaklaşım | Sonuç | Neden Yetmedi |
|---|---|---|
| `PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0` | Kısmen — yavaş şarjda yeterli | USB zaman aşımını kaldırır, güç sorununu çözmez |
| TinyUSB → Adafruit TinyUSB geçişi | Etkisiz | USB stack seçimi güç sorunuyla ilgisiz |
| "No USB" modu | Yavaş şarjda çalışır, Apple 20W'da hayır | USB stack kaldırılsa da boot güç talebi aynı kalır |
| `SKIP_DOUBLE_TAP` (çift-sıfırlama BOOTSEL engeli) | Etkisiz | Sorun BOOTSEL değil, brownout döngüsüydü |

### Denenen Donanım Yaklaşımları ve Sonuçları

| Yaklaşım | Sonuç | Neden Vazgeçildi |
|---|---|---|
| 2×220µF kondansatör — VSYS ile GND arasına (pin 39 – pin 38) | Cihaz geçici olarak yanık gibi davrandı; kondansatörler söküldüğünde düzeldi | Riski nedeniyle bu yol kapatıldı |
| 150Ω direnç — D+ ile D− arasına (BC1.2 DCP protokolü) | Test edilmedi | Pico 2W PCB'sinde D+/D− SMD padlerine erişim gerektirir veya kablo modifikasyonu şarttır; şarj kablosuna veya PCB'ye müdahale mümkün olmadığından vazgeçildi |

---

## 2. `WiFi.softAPConfig()` arduino-pico 5.6.0'da Çalışmıyor

**Durum:** Workaround uygulandı (varsayılan IP kullanılıyor)

`WiFi.softAPConfig(IPAddress(192,168,42,1), ...)` çağrısı CYW43439 sürücüsünde etkisiz kalmaktadır; AP her zaman varsayılan IP olan `192.168.4.1` adresini alır. Firmware'de sabit olarak `192.168.4.1` beklenmesi gerekir.

---

## 3. `initVariant()` Override Edilemiyor

**Durum:** Denendi, derleme hatası nedeniyle vazgeçildi

`rpipico2w/init.cpp:24` içindeki `initVariant()` fonksiyonu `weak` olarak tanımlı değildir. Sketch içinde aynı isimde bir fonksiyon tanımlanınca linker "multiple definition" hatası verir. CYW43439 başlatma zamanlaması bu yolla değiştirilemiyor.

---

## 4. "No USB" Modunda Arduino IDE Upload Portu Bulunamıyor

**Durum:** Kalıcı sınırlılık — beklenen davranış

"No USB" modunda Pico 2W, USB üzerinden COM port veya seri port sunmaz. Arduino IDE standart upload yöntemiyle firmware yükleyemez.

**Yükleme yöntemi:** BOOTSEL butonu basılı tutularak USB bağlanır → `RPI-RP2` sürücüsü görünür → Arduino IDE'de `Sketch > Export Compiled Binary` ile `.uf2` oluşturulur → sürücüye kopyalanır.

---

## 5. BLE Cihazı Bluetooth Menüsünde Görünmüyor

**Durum:** İncelenmedi — düşük öncelik

Geliştirme sürecinde BLE cihazı, ana bilgisayarın Bluetooth menüsünde listelenmedi. Sorunun nedeni (cihaz adı ayarı, CYW43439 BLE başlatma sırası veya arduino-pico BLE kütüphanesi) henüz araştırılmadı.
