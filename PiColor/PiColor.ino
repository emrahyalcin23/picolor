/*
 * ============================================================
 * PiColor - Smart Ambient Light & Color Analyzer System
 * Copyright (c) 2026 Emrah YALÇIN
 * MIT License — https://opensource.org/licenses/MIT
 * ------------------------------------------------------------
 * VERSİYON : v0.09.04
 * TANIM    : AP+STA çift mod WiFi — cihaz hem ev ağına (STA) bağlanır
 *            hem kendi AP ağını (192.168.4.1) açar. STA kimlik bilgileri
 *            EEPROM'da saklanır; TCP sunucusu her iki arabirimde (AP+STA)
 *            dinler. SD karttaki config.json runtime yapılandırmasını
 *            destekler; SD yoksa varsayılan değerler geçerlidir.
 * ============================================================
 *
 * ARDUINO IDE AYARLARI
 * --------------------
 *  Board            : Raspberry Pi Pico 2W
 *  CPU Speed        : 150 MHz (varsayılan)
 *  Flash Size       : 4MB (no FS) — SD kart kullanıldığı için FS gerekmez
 *  USB Stack        : No USB   ← ÖNEMLİ: Pico SDK veya TinyUSB seçilirse
 *                               USB-A dışındaki şarj başlıklarında cihaz
 *                               başlamayabilir (bkz. SINIRLILIKLAR.md §1)
 *  IP Stack         : IPv4 + IPv6 + Bluetooth
 *  Upload Method    : Default (UF2) — "No USB" modunda COM port görünmez;
 *                     yüklemek için BOOTSEL basılı tutarak USB tak,
 *                     RPI-RP2 sürücüsü görününce Sketch > Export Compiled
 *                     Binary ile .uf2 oluştur ve sürücüye kopyala
 *  Debug Level      : None (release için)
 *  Compiler Warnings: None veya Default
 *
 * DONANIM
 * -------
 *  TCS34725  → I2C  SDA=4  SCL=5
 *  TCS LED   → GPIO 15  (aktif-LOW)
 *  Encoder   → CLK=16  DT=17  SW=18  (INPUT_PULLUP)
 *  NeoPixel  → GPIO 28  (8 LED, GRB 800 kHz)
 *  SD kart   → SPI  CS=1  SCK=2  MOSI=3  MISO=0
 *
 * ÇIKTI PROTOKOLÜ (tüm satırlar bu formatı kullanır)
 * ---------------------------------------------------
 *  Seri   : timestamp;type;mode;...;[meta];wifi=<durum>
 *  TCP/BLE: timestamp;type;mode;...;[meta];wifi=<durum>;user=<kullanıcı>
 *
 *  wifi= → ev modemine bağlıysa SSID | bağlanıyor ise CONN | yalnızca AP ise AP
 *
 *  type  → 0=LIVE  1=SINGLE  2=AVERAGE  3=RAW
 *          4=STATUS  5=ERROR  6=DUAL  7=FULL
 *  mode  → 0=STABIL  1=DINAMIK
 *
 *  İşlenmiş RGB değerleri (0–100): BASAMAK_<n> ile ondalık basamak,
 *  LOGARITMIK ile Weber-Fechner log dönüşümü uygulanır.
 *  Ham (RAW) değerler, lüks ve katsayılar ölçekten etkilenmez.
 *
 * YAPILANDIRMA (config.json seçili ayarlar)
 * -----------------------------------------
 *  pil_modu: true → STA 3 denemeden sonra yeniden bağlantı durur
 *            false (varsayılan) → sürekli enerji; sınırsız deneme
 *  (tüm seçenekler için README.md veya loadJsonConfig() bkz.)
 *
 * KOMUT LİSTESİ (YARDIM / HELP)
 * ------------------------------
 *  --- WiFi AP (modülün kendi ağı) ---
 *  WIFI_AP_SSID=<adi>          AP ağ adını RAM'e yaz (yalnızca bu oturum)
 *  WIFI_AP_PASS=<sifre>        AP şifresini RAM'e yaz (yalnızca bu oturum)
 *  WIFI_AP_YENILE              AP'yi mevcut RAM bilgileriyle yeniden başlat
 *
 *  --- WiFi STA (ev/ofis modemi) ---
 *  WIFI_STA_SSID=<ag_adi>      Modem ağ adını RAM'e yaz (yalnızca bu oturum)
 *  WIFI_STA_PASS=<sifre>       Modem şifresini RAM'e yaz (yalnızca bu oturum)
 *  WIFI_STA_BAGLAN             Mevcut RAM bilgileriyle modeme bağlan
 *  WIFI_STA_KES                Modem bağlantısını kes (AP etkilenmez)
 *  WIFI_STA_KAYDET             Mevcut STA bilgilerini EEPROM'a kaydet (SD kartsız cihazlar için)
 *  WIFI_STA_SIFIRLA            EEPROM'daki STA bilgilerini sil
 *
 *  WIFI_BILGI                  AP ve STA durumunu göster
 *
 *  KIMSIN                      kimlik
 *  VERSIYON / VERSION          firmware sürümünü göster
 *  DURUM / STATUS              sistem durumunu göster (WiFi, BLE, ayarlar)
 *  MOD                         mevcut modu göster
 *  MOD_STABIL / MOD_DINAMIK    global modu değiştir
 *  KATSAYILAR / COEFF          wR wG wB wL değerlerini göster
 *  SIFIRLA / RESET             katsayıları ve maxObserved'ü sıfırla
 *  TAMPON / BUFFER             tampon doluluk bilgisi
 *  TAMPON_SIL / BUFFER_CLEAR   tamponu temizle
 *  TUM / ALL                   tek seferde tüm veri (FULL formatı)
 *  SD_DURUM / SD_STATUS        SD kart durumu
 *  SD_AKTAR / SD_EXPORT        tamponu SD'ye CSV olarak yaz
 *  SDCARD_YAZ_AKTIF            her örnekte SD'ye otomatik kayıt AÇ
 *  SDCARD_YAZ_PASIF            her örnekte SD'ye otomatik kayıt KAPAT
 *
 *  OKU                         canlı akışı başlat (global mod)
 *  OKU_STOP                    canlı akışı durdur
 *  OKU_0                       tek anlık okuma (global mod)
 *  OKU_S<n>                    son n SANİYE ortalaması  (örn. OKU_S60)
 *  OKU_<m>                     son m DAKİKA ortalaması  (örn. OKU_15)
 *  RAW                         ham sensör değerleri
 *
 *  STABIL_OKU                  canlı akış — stabil mod (geçici override)
 *  STABIL_OKU_0                tek okuma  — stabil mod
 *  STABIL_OKU_S<n>             n saniyelik ortalama — stabil mod
 *  STABIL_OKU_<m>              m dakikalık ortalama — stabil mod
 *  DINAMIK_OKU                 canlı akış — dinamik mod (geçici override)
 *  DINAMIK_OKU_0               tek okuma  — dinamik mod
 *  DINAMIK_OKU_S<n>            n saniyelik ortalama — dinamik mod
 *  DINAMIK_OKU_<m>             m dakikalık ortalama — dinamik mod
 *
 *  DUAL_MODE_ON / OFF          dual çıktı modunu aç/kapat
 *  DUAL_OKU                    tek dual okuma (ham + işlenmiş)
 *  DUAL_OKU_S<n>               n saniyelik dual ortalama
 *  STABIL_DUAL_OKU             tek dual okuma — stabil mod
 *  STABIL_DUAL_OKU_S<n>        n saniyelik dual ortalama — stabil mod
 *  DINAMIK_DUAL_OKU            tek dual okuma — dinamik mod
 *  DINAMIK_DUAL_OKU_S<n>       n saniyelik dual ortalama — dinamik mod
 *  DUAL_AKIS                   canlı dual akış
 *  STABIL_DUAL_AKIS            canlı dual akış — stabil mod
 *  DINAMIK_DUAL_AKIS           canlı dual akış — dinamik mod
 *
 *  BASAMAK_<n>                 ondalık basamak sayısı (0–6); varsayılan: 1
 *  LOGARITMIK                  Weber-Fechner log ölçeği aç (mevcut dekad)
 *  LOGARITMIK_<n>              log ölçeği aç + dekad ayarla 1.0–5.0
 *                                  2 → 100:1  3 → 1000:1 (önerilen)  4 → 10000:1
 *  LINEER                      lineer ölçeğe dön (dönüşüm yok)
 *  VARSAYILAN / DEFAULT        fabrika ayarı: decimals=1, lineer ölçek
 *  OLCEK / SCALE               mevcut format ayarlarını göster
 *
 *  TEST                        sistem testini çalıştır
 *  YARDIM / HELP               bu yardım ekranı
 * ============================================================
 */

#define FIRMWARE_VERSION  "v0.09.04"   // Firmware sürümü

#include <ArduinoJson.h>

// USB Stack notu: charger'da çalışmıyorsa Tools > USB Stack = "No USB" seçin.

// ============================================================
// KÜTÜPHANELER
// ============================================================

#include <Wire.h>
#include <Adafruit_TCS34725.h>
#include <Adafruit_NeoPixel.h>
#include <SD.h>
#include <SPI.h>

#include <WiFi.h>
#include <WiFiServer.h>
#include <LEAmDNS.h>
#include <lwip/netif.h>
#include <lwip/dhcp.h>
#include <lwip/opt.h>
#ifdef LWIP_NETIF_HOSTNAME
#  if LWIP_NETIF_HOSTNAME
#    pragma message("LWIP_NETIF_HOSTNAME = 1 — DHCP hostname destekleniyor")
#  else
#    pragma message("LWIP_NETIF_HOSTNAME = 0 — DHCP hostname KAPALI (sorun bu!)")
#  endif
#else
#  pragma message("LWIP_NETIF_HOSTNAME tanimlanmamis — DHCP hostname KAPALI")
#endif
#include <BTstackLib.h>
extern "C" {
#include "ble/att_server.h"
}
#include <EEPROM.h>

// ============================================================
// VARSAYILAN AYARLAR — config.json yoksa bu değerler kullanılır
// SD karta config.json koyarak herhangi birini override edebilirsiniz.
// ============================================================
#define DEFAULT_DEVICE_NAME     "PiColor_Modul_1" // Cihaz adı (BLE adı, çıktı wifi= alanı)
#define DEFAULT_WIFI_AP_SSID    "PiColor"        // AP ağ adı
#define DEFAULT_WIFI_AP_PASS    "picolor123"      // AP şifresi (min 8 karakter)
#define DEFAULT_WIFI_STA_SSID   ""               // ev modemi ağ adı (boş = bağlanma)
#define DEFAULT_WIFI_STA_PASS   ""               // ev modemi şifresi
#define DEFAULT_WIFI_STA_AUTO   true              // ev modemine otomatik bağlan
#define DEFAULT_TCP_PORT        8266              // TCP sunucu portu
#define DEFAULT_BASAMAK         1                 // ondalık basamak sayısı
#define DEFAULT_LOGARITMIK      false             // logaritmik çıktı
#define DEFAULT_LOG_DEKAD       3.0f              // logaritmik dinamik aralık (dekad)
#define DEFAULT_MOD_STABIL      true              // true = STABIL, false = DINAMIK
#define DEFAULT_DUAL_CIKTI      true              // ham + normalize ikili çıktı
#define DEFAULT_LOGGING         false             // CSV log kaydı
#define DEFAULT_LOG_FILE        "/user_log.csv"   // CSV log dosyası yolu

// ============================================================
// PIN TANIMLAMALARI
// ============================================================

#define TCS_LED_PIN     15   // TCS34725 beyaz LED — aktif-LOW
#define ENC_CLK_PIN     16   // Encoder A kanalı (CLK) — kesme pini
#define ENC_DT_PIN      17   // Encoder B kanalı (DT)
#define ENC_SW_PIN      18   // Encoder butonu
#define NEO_PIN         28   // NeoPixel veri hattı
#define NEO_COUNT        8   // Şeritteki LED sayısı
#define LONG_PRESS_TIME 1000 // Uzun basış eşiği (ms)

// SD kart SPI pinleri
#define SD_CS_PIN    1
#define SD_SCK_PIN   2
#define SD_MOSI_PIN  3
#define SD_MISO_PIN  0

// ============================================================
// SENSÖR KAZANIM VE ENTEGRASYON SÜRESİ
// ============================================================

// Kazanım: 1, 4, 16 veya 60 — TCS34725 donanımsal limitleri
#define KAZANC_ORANI       4
// Entegrasyon süresi: ışık toplama penceresi
#define ENTEGRASYON_SURESI TCS34725_INTEGRATIONTIME_50MS

// ============================================================
// NORMALİZASYON SABİTLERİ
// ============================================================

// Stabil modda 0–100 normalleştirmesi için referans maksimum.
// Oda ortamı için 1000 uygundur; gerekirse değiştirin.
const float NORM_INPUT_MAX  = 1000.0f;
#define NORM_OUTPUT_MIN 0.0f
#define NORM_OUTPUT_MAX 100.0f

// ============================================================
// SENSÖR VE LED NESNELERİ
// ============================================================

// TCS34725 sensörü — kazanım seçimi derleme zamanında yapılır
Adafruit_TCS34725 tcs = Adafruit_TCS34725(
    ENTEGRASYON_SURESI,
    (KAZANC_ORANI == 1)  ? TCS34725_GAIN_1X  :
    (KAZANC_ORANI == 16) ? TCS34725_GAIN_16X :
    (KAZANC_ORANI == 60) ? TCS34725_GAIN_60X :
                           TCS34725_GAIN_4X
);

// NeoPixel şeridi — GRB sırası, 800 kHz
Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

// ============================================================
// DURUM MAKİNESİ — Encoder hangi kanalı ayarlıyor
// ============================================================

enum State { STATE_R, STATE_G, STATE_B, STATE_L };
State currentState = STATE_R;

// ============================================================
// KALİBRASYON KATSAYILARI (encoder ile ayarlanır)
// Aralık: [-1.0, +1.0]
// Kullanım: kalibre_deger = ham * (1 + w) * (1 + wL)
// ============================================================

volatile float wR = 0.0f; // Kırmızı kanal ağırlığı
volatile float wG = 0.0f; // Yeşil kanal ağırlığı
volatile float wB = 0.0f; // Mavi kanal ağırlığı
volatile float wL = 0.0f; // Işık (genel parlaklık) ağırlığı

// ============================================================
// ENCODER ZAMANLAMA — ISR tarafından kullanılır
// ============================================================

volatile unsigned long lastPulseTime = 0; // Son darbe zamanı (ms)
volatile bool          encoderMoved  = false; // Loop'a bildirim bayrağı

// ============================================================
// UYKU / AKTİVİTE YÖNETİMİ
// ============================================================

float         beklemeSuresiCevirme = 1.0f; // Çevirme sonrası LED söner (s)
float         beklemeSuresiTiklama = 3.0f; // Basış sonrası LED söner (s)
unsigned long gecerliUykuSuresi    = 1000; // Hesaplanan uyku süresi (ms)
unsigned long lastActivityTime     = 0;   // Son aktivite zamanı
unsigned long lastButtonPress      = 0;   // Son buton basış zamanı
bool          ledsActive           = false; // LED'ler şu an açık mı?

// ============================================================
// VERİ TAMPONU — son 15 dakika (900 saniye) RAM'de tutulur
// ============================================================

#define MAX_HISTORY_SECONDS 900

// İşlenmiş (normalize edilmiş, 0–100) değerler
float    histR[MAX_HISTORY_SECONDS];
float    histG[MAX_HISTORY_SECONDS];
float    histB[MAX_HISTORY_SECONDS];

// Ham (uint16_t, sensörden gelen) değerler
uint16_t histRawR[MAX_HISTORY_SECONDS];
uint16_t histRawG[MAX_HISTORY_SECONDS];
uint16_t histRawB[MAX_HISTORY_SECONDS];
uint16_t histRawC[MAX_HISTORY_SECONDS]; // Clear (beyaz) kanal

int           histIndex    = 0; // Bir sonraki yazılacak konum (dairesel)
int           histCount    = 0; // Tamponda biriken örnek sayısı (maks 900)
unsigned long lastSampleTime = 0; // Son örnekleme zamanı

// ============================================================
// ÇALIŞMA MOD DEĞİŞKENLERİ
// ============================================================

bool  testModeActive  = false; // Canlı akış aktif mi?
bool  dualOutputActive = DEFAULT_DUAL_CIKTI;
int   calibMode       = DEFAULT_MOD_STABIL ? 0 : 1;  // 0=STABIL  1=DINAMIK
float maxObserved     = 1000.0f; // Dinamik mod için gözlemlenen maksimum

// ============================================================
// ÇIKTI FORMAT DEĞİŞKENLERİ
// ============================================================

int   outputDecimals    = DEFAULT_BASAMAK;
bool  logarithmicOutput = DEFAULT_LOGARITMIK;
float logDecades        = DEFAULT_LOG_DEKAD;

// ============================================================
// SD KART DEĞİŞKENLERİ
// ============================================================

bool        sdCardAvailable  = false; // SD kart fiziksel olarak mevcut mu?
bool        sdCardMounted    = false; // SD başarıyla mount edildi mi?
bool        sdAutoLog        = false; // Her örnekte otomatik CSV yazma aktif mi?
const char* CSV_FILENAME     = "/picolor_data.csv"; // Ana kayıt dosyası

// ============================================================
// KABLOSUZ İLETİŞİM DEĞİŞKENLERİ
// ============================================================

// Mevcut komutu gönderen kullanıcı (Serial/TCP/BLE handler tarafından set edilir)
static String currentUsername = "serial";

// Çalışma zamanı yapılandırması — başlangıç değerleri DEFAULT_* sabitlerinden gelir,
// SD karttaki config.json varsa üzerine yazılır.
static char     cfgWifiApSSID[33] = DEFAULT_WIFI_AP_SSID;
static char     cfgWifiApPass[65] = DEFAULT_WIFI_AP_PASS;
static bool     cfgWifiStaAuto    = DEFAULT_WIFI_STA_AUTO;
static bool     cfgPilModu        = false;   // true = pil modu: 3 denemeden sonra STA reconnect durur
static int      staReconnectAttempts = 0;    // pil modunda deneme sayacı
static bool          staLedConnecting  = false;
static unsigned long staLedGreenUntil  = 0;
static unsigned long staLedConnectingStart = 0;
static bool     loggingEnabled    = DEFAULT_LOGGING;
static char     cfgLogFile[64]    = DEFAULT_LOG_FILE;
static uint16_t cfgTcpPort        = DEFAULT_TCP_PORT;
static bool     wifiEnabled       = true;   // WIRELESS_ENABLED=0/1 komutuyla oturum bazlı değişir
static bool     g_hostnameApplied = false;  // DHCP hostname STA bağlantısı sonrası bir kez uygulanır
// netif_set_hostname() pointer sakladığından buffer global olmalı —
// WiFi.begin() sonrası DHCP asenkron çalışır, yerel değişken o ana kadar yok olur
static char     g_wifiHostname[64] = {0};

#define MAX_TCP_CLIENTS 4
// Global nesne: arduino-pico'da WiFiServer::begin() güvenilir çalışması için global olmalı
static WiFiServer  g_tcpServer(DEFAULT_TCP_PORT);
static WiFiServer* tcpServer      = nullptr;
static WiFiClient  tcpClients[MAX_TCP_CLIENTS];
static char        tcpRxBuf[MAX_TCP_CLIENTS][256];
static int         tcpRxLen[MAX_TCP_CLIENTS];

// BLE NUS (Nordic UART Service) — BTstack tabanlı
static int    nusTxHandle    = 0;    // GATT TX characteristic handle
static int    nusRxHandle    = 0;    // GATT RX characteristic handle
static bool   bleConnected   = false;
static char   bleRxBuf[256];
static int    bleRxLen       = 0;
static hci_con_handle_t bleCentralHandle = HCI_CON_HANDLE_INVALID;
static bool   bleHasPendingCmd  = false;
static bool   serialReady    = false; // Serial.begin() çağrılana kadar Serial'a dokunma
static String blePendingCmd;
static String blePendingUser;

// !! EEPROM / FLASH YAZMA LİMİTİ UYARISI !!
// RP2350 flash belleği yaklaşık 100.000 blok silme döngüsüne sahiptir.
// EEPROM yalnızca WIFI_STA_KAYDET komutuyla yazılır (kullanıcı talebi üzerine);
// önyükleme başına veya döngüsel olarak ASLA yazılmamalıdır.
// Çalışma zamanı ayarları için SD karttaki config.json kullanılır.

// EEPROM düzeni — WiFi STA kimlik bilgileri kalıcı olarak burada saklanır
#define EEPROM_SIZE        128
#define EEPROM_MAGIC       0xAB
#define EEPROM_SSID_OFFSET 1
#define EEPROM_SSID_LEN    33   // 32 karakter + null
#define EEPROM_PASS_OFFSET 34
#define EEPROM_PASS_LEN    65   // 64 karakter + null

static char wifiStaSSID[EEPROM_SSID_LEN] = DEFAULT_WIFI_STA_SSID;
static char wifiStaPass[EEPROM_PASS_LEN] = DEFAULT_WIFI_STA_PASS;

// ============================================================
// ÇIKTI TİPİ ENUM
// ============================================================

enum OutputType {
    OUT_LIVE    = 0, // Canlı akış satırı
    OUT_SINGLE  = 1, // Tek okuma
    OUT_AVERAGE = 2, // Ortalama okuma
    OUT_RAW     = 3, // Ham sensör verisi
    OUT_STATUS  = 4, // Durum mesajı
    OUT_ERROR   = 5, // Hata mesajı
    OUT_DUAL    = 6, // Hem ham hem işlenmiş
    OUT_FULL    = 7  // TUM komutu için tam veri paketi
};

// ============================================================
// ========== ÇIKTI MULTİPLEKSERİ ============================
// ============================================================

void bleSendLine(const String& line);
void appendUserLog(const String& username, const char* clientType, const String& command);

/*
 * getWifiStatusStr
 * ----------------
 * Mevcut WiFi bağlantı durumunu kısa bir string olarak döndürür.
 * WL_CONNECTED  → bağlı ağın SSID adı
 * staLedConnecting true → "CONN" (bağlanma devam ediyor)
 * diğer          → "AP"  (yalnızca kendi AP ağı aktif)
 */
String getWifiStatusStr() {
    if (WiFi.status() == WL_CONNECTED) return DEFAULT_DEVICE_NAME;
    if (staLedConnecting)              return "CONN";
    return "AP";
}

/*
 * broadcastLine
 * -------------
 * Bir çıktı satırını tüm aktif kanallara gönderir.
 * Serial  : satıra ";wifi=<durum>" eklenir.
 * TCP/BLE : satıra ";wifi=<durum>;user=<currentUsername>" eklenir.
 */
void broadcastLine(const String& line) {
    String out = line + ";wifi=" + getWifiStatusStr();
    if (serialReady && Serial.availableForWrite() > (int)out.length() + 2) {
        Serial.println(out);
    }
    String wirelessLine = out + ";user=" + currentUsername;
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (tcpClients[i] && tcpClients[i].connected())
            tcpClients[i].println(wirelessLine);
    }
    if (bleConnected) bleSendLine(wirelessLine);
}

// ============================================================
// ========== STANDART ÇIKTI FONKSİYONLARI ===================
// ============================================================

/*
 * applyOutputScale
 * ----------------
 * Weber-Fechner logaritmik dönüşümü (fotometrik standart).
 * Formül: y = log10(1 + x*(10^D-1)/100) / D * 100
 * Özellikler: f(0)=0, f(100)=100; 0–100 aralığı korunur.
 * logarithmicOutput=false veya value<=0 ise değiştirilmeden döner.
 */
float applyOutputScale(float value) {
    if (!logarithmicOutput || value <= 0.0f) return value;
    float k = powf(10.0f, logDecades) - 1.0f;
    return log10f(1.0f + value * k / 100.0f) / logDecades * 100.0f;
}

/*
 * printStandardOutput
 * -------------------
 * Tüm tek-değer (işlenmiş veya ham) çıktılar bu fonksiyondan geçer.
 * Format: timestamp;type;mode;v1;v2;v3[;meta]
 *
 * RAW    tipinde v1/v2/v3 integer olarak yazılır.
 * STATUS/ERROR tipinde v1/v2/v3 yerine "0;0;0" yazılır, meta zorunludur.
 * Diğer tiplerde 1 ondalık float yazılır.
 */
void printStandardOutput(OutputType type, int mode,
                         float v1, float v2, float v3,
                         const char* meta = "") {
    unsigned long ts = millis();
    String line = String(ts) + ";" + String((int)type) + ";" + String(mode) + ";";

    if (type == OUT_RAW) {
        line += String((int)v1) + ";" + String((int)v2) + ";" + String((int)v3);
    } else if (type == OUT_STATUS || type == OUT_ERROR) {
        line += "0;0;0";
    } else {
        line += String(applyOutputScale(v1), outputDecimals) + ";"
              + String(applyOutputScale(v2), outputDecimals) + ";"
              + String(applyOutputScale(v3), outputDecimals);
    }

    if (meta != nullptr && strlen(meta) > 0) {
        line += ";";
        line += meta;
    }
    broadcastLine(line);
}

// String overload — meta için String kabul eder
void printStandardOutput(OutputType type, int mode,
                         float v1, float v2, float v3,
                         const String& meta) {
    printStandardOutput(type, mode, v1, v2, v3, meta.c_str());
}

/*
 * printStatusMessage
 * ------------------
 * Durum bildirimleri için kısayol.
 * Format: timestamp;4;mode;0;0;0;message
 */
void printStatusMessage(int mode, const char* message) {
    printStandardOutput(OUT_STATUS, mode, 0, 0, 0, message);
}

/*
 * printErrorMessage
 * -----------------
 * Hata bildirimleri için kısayol.
 * Format: timestamp;5;mode;0;0;0;error
 */
void printErrorMessage(int mode, const char* error) {
    printStandardOutput(OUT_ERROR, mode, 0, 0, 0, error);
}

/*
 * printDualOutput
 * ---------------
 * Hem ham (uint16_t) hem işlenmiş (float, 0–100) veriyi tek satırda yazar.
 * Format: timestamp;type;mode;raw_r;raw_g;raw_b;raw_c;proc_r;proc_g;proc_b[;meta]
 */
void printDualOutput(OutputType type, int mode,
                     uint16_t raw_r, uint16_t raw_g, uint16_t raw_b, uint16_t raw_c,
                     float proc_r, float proc_g, float proc_b,
                     const char* meta = "") {
    unsigned long ts = millis();
    String line = String(ts) + ";" + String((int)type) + ";" + String(mode) + ";"
                + String(raw_r) + ";" + String(raw_g) + ";"
                + String(raw_b) + ";" + String(raw_c) + ";"
                + String(applyOutputScale(proc_r), outputDecimals) + ";"
                + String(applyOutputScale(proc_g), outputDecimals) + ";"
                + String(applyOutputScale(proc_b), outputDecimals);

    if (strlen(meta) > 0) {
        line += ";";
        line += meta;
    }
    broadcastLine(line);
}

// String overload
void printDualOutput(OutputType type, int mode,
                     uint16_t raw_r, uint16_t raw_g, uint16_t raw_b, uint16_t raw_c,
                     float proc_r, float proc_g, float proc_b,
                     const String& meta) {
    printDualOutput(type, mode, raw_r, raw_g, raw_b, raw_c,
                    proc_r, proc_g, proc_b, meta.c_str());
}

// ============================================================
// ========== LÜKS VE RENK SICAKLIĞI HESABI ==================
// ============================================================

/*
 * calculateLuxAndTemp
 * -------------------
 * TCS34725 için DN40 tabanlı lüks formülü ve basit renk sıcaklığı tahmini.
 * raw_c == 0 ise güvenli varsayılan değerler döndürür.
 *
 * lux        : hesaplanan parlaklık (lüks)
 * colorTemp  : tahmini renk sıcaklığı (Kelvin, 2000–10000 aralığında kırpılır)
 */
void calculateLuxAndTemp(uint16_t raw_r, uint16_t raw_g, uint16_t raw_b, uint16_t raw_c,
                         float &lux, int &colorTemp) {
    if (raw_c > 0) {
        lux = (-0.32466f * raw_r) + (1.57837f * raw_g) + (-0.73191f * raw_b);
        if (lux < 0) lux = 0;

        float r_norm = (float)raw_r / raw_c;
        float g_norm = (float)raw_g / raw_c;

        if (g_norm > 0) {
            colorTemp = (int)(4000 + (r_norm - g_norm) * 5000);
            if (colorTemp < 2000)  colorTemp = 2000;
            if (colorTemp > 10000) colorTemp = 10000;
        } else {
            colorTemp = 5000;
        }
    } else {
        lux       = 0;
        colorTemp = 5000;
    }
}

// ============================================================
// ========== KALİBRE EDİLMİŞ RENK FONKSİYONLARI ============
// ============================================================

/*
 * getCalibratedColor_stabil
 * -------------------------
 * Sabit NORM_INPUT_MAX referansıyla normalize eder.
 * Işık koşulları değişse de ölçek sabittir — karşılaştırmalı ölçümler
 * için tercih edilir.
 *
 * Faktör hesabı: kalibre = ham * (1 + w_kanal) * (1 + wL)
 * Normalize    : 0–100 = kalibre / NORM_INPUT_MAX * 100
 * Güvenlik     : tüm faktörler minimum 0.05'e kırpılır (sıfır bölme önlemi)
 */
void getCalibratedColor_stabil(float &cR, float &cG, float &cB) {
    uint16_t r, g, b, c;
    tcs.getRawData(&r, &g, &b, &c);

    float l_factor = 1.0f + wL; if (l_factor < 0.05f) l_factor = 0.05f;
    float r_factor = 1.0f + wR; if (r_factor < 0.05f) r_factor = 0.05f;
    float g_factor = 1.0f + wG; if (g_factor < 0.05f) g_factor = 0.05f;
    float b_factor = 1.0f + wB; if (b_factor < 0.05f) b_factor = 0.05f;

    float calR = (r * r_factor) * l_factor;
    float calG = (g * g_factor) * l_factor;
    float calB = (b * b_factor) * l_factor;

    cR = (calR / NORM_INPUT_MAX) * 100.0f;
    cG = (calG / NORM_INPUT_MAX) * 100.0f;
    cB = (calB / NORM_INPUT_MAX) * 100.0f;

    cR = constrain(cR, 0.0f, 100.0f);
    cG = constrain(cG, 0.0f, 100.0f);
    cB = constrain(cB, 0.0f, 100.0f);
}

/*
 * getCalibratedColor_dinamik
 * --------------------------
 * Gözlemlenen maksimuma göre otomatik ölçeklenir.
 * Yüksek ışıkta tam skala, düşük ışıkta hassas çözünürlük sağlar.
 *
 * maxObserved güncelleme: yeni maksimum bulunursa %10 pay eklenmiş
 * olarak güncellenir; her dakika %5 azaltılarak adaptasyon sağlanır.
 * Alt sınır: 1000.0  Üst sınır: 65535 * 4 (teorik maks)
 */
void getCalibratedColor_dinamik(float &cR, float &cG, float &cB) {
    uint16_t r, g, b, c;
    tcs.getRawData(&r, &g, &b, &c);

    float l_factor = 1.0f + wL; if (l_factor < 0.05f) l_factor = 0.05f;
    float r_factor = 1.0f + wR; if (r_factor < 0.05f) r_factor = 0.05f;
    float g_factor = 1.0f + wG; if (g_factor < 0.05f) g_factor = 0.05f;
    float b_factor = 1.0f + wB; if (b_factor < 0.05f) b_factor = 0.05f;

    float calR = (r * r_factor) * l_factor;
    float calG = (g * g_factor) * l_factor;
    float calB = (b * b_factor) * l_factor;

    float currentMax = max(calR, max(calG, calB));

    if (currentMax > maxObserved) {
        maxObserved = currentMax * 1.1f;
        if (maxObserved > 65535.0f * 4.0f) maxObserved = 65535.0f * 4.0f;
    }

    static unsigned long lastDecay = 0;
    if (millis() - lastDecay > 60000UL) {
        lastDecay = millis();
        maxObserved *= 0.95f;
        if (maxObserved < 1000.0f) maxObserved = 1000.0f;
    }

    cR = constrain((calR / maxObserved) * 100.0f, 0.0f, 100.0f);
    cG = constrain((calG / maxObserved) * 100.0f, 0.0f, 100.0f);
    cB = constrain((calB / maxObserved) * 100.0f, 0.0f, 100.0f);
}

/*
 * getCalibratedColor
 * ------------------
 * secim == 0 → stabil, secim == 1 → dinamik.
 * Global calibMode'u DEĞİŞTİRMEZ — anlık override için kullanılır.
 * Bilinmeyen secim değerinde stabil mod uygulanır.
 */
void getCalibratedColor(float &cR, float &cG, float &cB, int secim) {
    if (secim == 1) {
        getCalibratedColor_dinamik(cR, cG, cB);
    } else {
        getCalibratedColor_stabil(cR, cG, cB);
    }
}

// ============================================================
// ========== SD KART FONKSİYONLARI ==========================
// ============================================================

/*
 * initSDCard
 * ----------
 * SPI pinlerini ayarlar ve SD kütüphanesini başlatır.
 * Başarılıysa CSV başlık satırını oluşturur (dosya yoksa).
 * sdCardAvailable ve sdCardMounted bayraklarını günceller.
 */
void initSDCard() {
    SPI.setRX(SD_MISO_PIN);
    SPI.setTX(SD_MOSI_PIN);
    SPI.setSCK(SD_SCK_PIN);
    SPI.setCS(SD_CS_PIN);
    SPI.begin();

    if (!SD.begin(SD_CS_PIN)) {
        sdCardAvailable = false;
        sdCardMounted   = false;
        printStatusMessage(calibMode, "SD_CARD_NOT_FOUND");
        return;
    }

    sdCardAvailable = true;
    sdCardMounted   = true;
    printStatusMessage(calibMode, "SD_CARD_MOUNTED");

    // CSV dosyası yoksa başlık satırı oluştur
    if (!SD.exists(CSV_FILENAME)) {
        File f = SD.open(CSV_FILENAME, FILE_WRITE);
        if (f) {
            f.println("timestamp_ms,raw_r,raw_g,raw_b,raw_c,"
                      "proc_r,proc_g,proc_b,lux,color_temp_k,"
                      "wR,wG,wB,wL,state,mode");
            f.close();
            printStatusMessage(calibMode, "SD_CSV_HEADER_WRITTEN");
        }
    }
}

/*
 * loadJsonConfig
 * --------------
 * SD karttaki /config.json dosyasını okur ve çalışma zamanı
 * yapılandırma değişkenlerini günceller. ArduinoJson 7 kullanır.
 * Dosya yoksa veya SD kart takılı değilse varsayılan değerler korunur.
 */
void loadJsonConfig() {
    // SD kart yoksa veya config.json bulunamazsa .ino içindeki sabit değerler geçerli
    if (!sdCardAvailable || !sdCardMounted) {
        printStatusMessage(calibMode, "CFG_NO_SD_USING_DEFAULTS");
        return;
    }
    File f = SD.open("/config.json", FILE_READ);
    if (!f) {
        printStatusMessage(calibMode, "CFG_NOT_FOUND_USING_DEFAULTS");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        char meta[48];
        snprintf(meta, sizeof(meta), "CFG_JSON_ERROR=%s", err.c_str());
        printStatusMessage(calibMode, meta);
        return;
    }

    strlcpy(cfgWifiApSSID, doc["wifi_ap_ssid"] | cfgWifiApSSID, sizeof(cfgWifiApSSID));
    strlcpy(cfgWifiApPass, doc["wifi_ap_pass"] | cfgWifiApPass, sizeof(cfgWifiApPass));
    cfgWifiStaAuto = doc["wifi_sta_otomatik"] | cfgWifiStaAuto;

    const char* staSSID = doc["wifi_sta_ssid"] | "";
    if (strlen(staSSID) > 0) strlcpy(wifiStaSSID, staSSID, EEPROM_SSID_LEN);
    const char* staPass = doc["wifi_sta_pass"] | "";
    if (strlen(staPass) > 0) strlcpy(wifiStaPass, staPass, EEPROM_PASS_LEN);

    cfgTcpPort  = doc["tcp_port"]  | cfgTcpPort;
    cfgPilModu  = doc["pil_modu"]  | cfgPilModu;

    int d = doc["basamak"] | outputDecimals;
    if (d >= 0 && d <= 6) outputDecimals = d;
    logarithmicOutput = doc["logaritmik"] | logarithmicOutput;
    float ld = doc["log_dekad"] | logDecades;
    if (ld >= 1.0f && ld <= 5.0f) logDecades = ld;

    const char* mod = doc["mod"] | "";
    if (strcmp(mod, "DINAMIK") == 0) calibMode = 1;
    else if (strcmp(mod, "STABIL") == 0) calibMode = 0;

    dualOutputActive = doc["dual_cikti"] | dualOutputActive;
    loggingEnabled   = doc["logging_enabled"] | loggingEnabled;
    strlcpy(cfgLogFile, doc["log_file"] | cfgLogFile, sizeof(cfgLogFile));

    printStatusMessage(calibMode, "CFG_LOADED_FROM_SD");
}

/*
 * appendToCSV
 * -----------
 * Tek bir ölçüm satırını SD karttaki CSV dosyasına ekler.
 * sdAutoLog false ise bu fonksiyon hiçbir şey yapmaz.
 * Her çağrıda SD.open/close yapılır — bu yavaştır, ancak güçten
 * düşme durumunda veri kaybını önler. sdAutoLog'u dikkatli kullanın.
 */
void appendToCSV(uint16_t r, uint16_t g, uint16_t b, uint16_t c,
                 float pr, float pg, float pb, float lux, int colorTemp) {
    if (!sdCardAvailable || !sdCardMounted || !sdAutoLog) return;

    File f = SD.open(CSV_FILENAME, FILE_WRITE);
    if (!f) return;

    const char* stateNames[] = {"R", "G", "B", "L"};
    const char* modeStr = (calibMode == 0) ? "STABIL" : "DINAMIK";

    f.printf("%lu,%d,%d,%d,%d,%.*f,%.*f,%.*f,%.*f,%d,"
             "%.*f,%.*f,%.*f,%.*f,%s,%s\n",
             millis(), r, g, b, c,
             outputDecimals, applyOutputScale(pr),
             outputDecimals, applyOutputScale(pg),
             outputDecimals, applyOutputScale(pb),
             outputDecimals, lux, colorTemp,
             outputDecimals, wR,
             outputDecimals, wG,
             outputDecimals, wB,
             outputDecimals, wL,
             stateNames[currentState], modeStr);
    f.close();
}

/*
 * exportBufferToSD
 * ----------------
 * RAM tamponundaki tüm örnekleri ayrı bir dosyaya yazar.
 * Dosya adı: /export_<millis>.csv
 * SD kart yoksa veya tampon boşsa hata mesajı gönderir.
 * Büyük tamponlarda her 50 satırda bir flush yapılır.
 */
void exportBufferToSD() {
    if (!sdCardAvailable || !sdCardMounted) {
        printErrorMessage(calibMode, "SD_CARD_NOT_FOUND");
        return;
    }
    if (histCount == 0) {
        printErrorMessage(calibMode, "BUFFER_EMPTY");
        return;
    }

    char filename[32];
    snprintf(filename, sizeof(filename), "/export_%lu.csv", millis());

    File f = SD.open(filename, FILE_WRITE);
    if (!f) {
        printErrorMessage(calibMode, "CANNOT_CREATE_FILE");
        return;
    }

    f.println("timestamp_ms,raw_r,raw_g,raw_b,raw_c,"
              "proc_r,proc_g,proc_b,lux,color_temp_k,"
              "wR,wG,wB,wL,state,mode");

    const char* stateNames[] = {"R", "G", "B", "L"};
    const char* modeStr = (calibMode == 0) ? "STABIL" : "DINAMIK";
    int startIdx = (histIndex - histCount + MAX_HISTORY_SECONDS) % MAX_HISTORY_SECONDS;

    for (int i = 0; i < histCount; i++) {
        int idx = (startIdx + i) % MAX_HISTORY_SECONDS;

        float lux;
        int   colorTemp;
        calculateLuxAndTemp(histRawR[idx], histRawG[idx],
                            histRawB[idx], histRawC[idx], lux, colorTemp);

        // Gerçek zaman damgası bilinmediğinden örnekleme indeksi * 1000 ms kullanılır
        f.printf("%lu,%d,%d,%d,%d,%.*f,%.*f,%.*f,%.*f,%d,"
                 "%.*f,%.*f,%.*f,%.*f,%s,%s\n",
                 (unsigned long)(i * 1000),
                 histRawR[idx], histRawG[idx], histRawB[idx], histRawC[idx],
                 outputDecimals, applyOutputScale(histR[idx]),
                 outputDecimals, applyOutputScale(histG[idx]),
                 outputDecimals, applyOutputScale(histB[idx]),
                 outputDecimals, lux, colorTemp,
                 outputDecimals, wR,
                 outputDecimals, wG,
                 outputDecimals, wB,
                 outputDecimals, wL,
                 stateNames[currentState], modeStr);

        if (i % 50 == 0) f.flush();
    }
    f.close();

    char meta[64];
    snprintf(meta, sizeof(meta), "EXPORTED_TO_SD,rows=%d,file=%s", histCount, filename);
    printStatusMessage(calibMode, meta);
}

// ============================================================
// ========== OKUMA FONKSİYONLARI ============================
// ============================================================

/*
 * sendSingleReading
 * -----------------
 * Tek anlık ölçüm. secim ile anlık mod override edilebilir.
 * Format: OUT_SINGLE
 */
void sendSingleReading(int mode) {
    float cR, cG, cB;
    getCalibratedColor(cR, cG, cB, mode);
    printStandardOutput(OUT_SINGLE, mode, cR, cG, cB);
}

/*
 * sendAverageReading
 * ------------------
 * Son 'seconds' saniyelik işlenmiş değerlerin ortalamasını gönderir.
 * seconds, histCount ile kırpılır — yetersiz veri varsa hata döner.
 * Format: OUT_AVERAGE, meta: "interval=<n>"
 */
void sendAverageReading(int seconds, int mode) {
    int calcSeconds = (seconds > histCount) ? histCount : seconds;

    if (calcSeconds == 0) {
        printErrorMessage(mode, "INSUFFICIENT_DATA");
        return;
    }

    float sumR = 0, sumG = 0, sumB = 0;
    for (int i = 0; i < calcSeconds; i++) {
        int idx = histIndex - 1 - i;
        if (idx < 0) idx += MAX_HISTORY_SECONDS;
        sumR += histR[idx];
        sumG += histG[idx];
        sumB += histB[idx];
    }

    char meta[32];
    snprintf(meta, sizeof(meta), "interval=%d", calcSeconds);
    printStandardOutput(OUT_AVERAGE, mode,
                        sumR / calcSeconds, sumG / calcSeconds, sumB / calcSeconds, meta);
}

/*
 * sendSingleDualReading
 * ---------------------
 * Tek anlık ölçüm — hem ham hem işlenmiş.
 * Format: OUT_SINGLE (dual), meta: "single_dual"
 */
void sendSingleDualReading(int mode) {
    uint16_t r, g, b, c;
    tcs.getRawData(&r, &g, &b, &c);

    float cR, cG, cB;
    getCalibratedColor(cR, cG, cB, mode);

    printDualOutput(OUT_SINGLE, mode, r, g, b, c, cR, cG, cB, "single_dual");
}

/*
 * sendAverageDualReading
 * ----------------------
 * Son 'seconds' saniyelik hem ham hem işlenmiş ortalamayı gönderir.
 * Ham değerlerin toplamı uint32_t ile tutulur (taşma önlemi).
 * Format: OUT_AVERAGE (dual), meta: "average_dual,interval=<n>"
 */
void sendAverageDualReading(int seconds, int mode) {
    int calcSeconds = (seconds > histCount) ? histCount : seconds;

    if (calcSeconds == 0) {
        printErrorMessage(mode, "INSUFFICIENT_DATA");
        return;
    }

    float    sumProcR = 0, sumProcG = 0, sumProcB = 0;
    uint32_t sumRawR  = 0, sumRawG  = 0, sumRawB  = 0, sumRawC = 0;

    for (int i = 0; i < calcSeconds; i++) {
        int idx = histIndex - 1 - i;
        if (idx < 0) idx += MAX_HISTORY_SECONDS;

        sumProcR += histR[idx];
        sumProcG += histG[idx];
        sumProcB += histB[idx];

        sumRawR += histRawR[idx];
        sumRawG += histRawG[idx];
        sumRawB += histRawB[idx];
        sumRawC += histRawC[idx];
    }

    char meta[64];
    snprintf(meta, sizeof(meta), "average_dual,interval=%d", calcSeconds);

    printDualOutput(OUT_AVERAGE, mode,
                    (uint16_t)(sumRawR / calcSeconds),
                    (uint16_t)(sumRawG / calcSeconds),
                    (uint16_t)(sumRawB / calcSeconds),
                    (uint16_t)(sumRawC / calcSeconds),
                    sumProcR / calcSeconds,
                    sumProcG / calcSeconds,
                    sumProcB / calcSeconds,
                    meta);
}

/*
 * startLiveStream
 * ---------------
 * Canlı akışı başlatır. testModeActive = true yapar.
 * Global calibMode bu komutla değişir (kalıcı).
 */
void startLiveStream(int mode) {
    testModeActive = true;
    calibMode      = mode;

    char meta[48];
    snprintf(meta, sizeof(meta), "LIVE_START_MODE=%s", (mode == 0) ? "STABIL" : "DINAMIK");
    printStatusMessage(mode, meta);
}

/*
 * startLiveDualStream
 * -------------------
 * Dual canlı akışı başlatır. testModeActive = true yapar.
 * Global calibMode bu komutla değişir (kalıcı).
 */
void startLiveDualStream(int mode) {
    testModeActive = true;
    calibMode      = mode;

    char meta[64];
    snprintf(meta, sizeof(meta), "LIVE_DUAL_START,mode=%s", (mode == 0) ? "STABIL" : "DINAMIK");
    printStatusMessage(mode, meta);
}

/*
 * stopLiveStream
 * --------------
 * Canlı akışı durdurur. testModeActive = false yapar.
 */
void stopLiveStream() {
    testModeActive = false;
    printStatusMessage(calibMode, "LIVE_STOP");
}

// ============================================================
// ========== FULL VERİ (TUM / ALL) ==========================
// ============================================================

/*
 * sendFullData
 * ------------
 * Tek seferde anlık ölçüm + 60s/300s/900s ortalamaları + sistem
 * durumunu gönderir.
 *
 * Ortalamalar matematiksel: histCount kadar örnek kullanılır,
 * üst sınırlar 60, 300, 900 ile kırpılır. Tampon dolmamışsa
 * gerçek örnek sayısı kadar ortalama hesaplanır.
 *
 * Format: timestamp;FULL;mode;<anlık ham+işlenmiş+lüks+katsayı+state>
 *         ;<60s ort>;<300s ort>;<900s ort>;<tampon istatistikleri>
 *
 * NOT: Bu satır standart type enum dışındadır ("FULL" string kullanır)
 *      — mevcut PC ayrıştırıcısıyla uyumluluk için korunmuştur.
 */
void sendFullData() {
    uint16_t r, g, b, c;
    tcs.getRawData(&r, &g, &b, &c);

    float proc_r, proc_g, proc_b;
    getCalibratedColor(proc_r, proc_g, proc_b, calibMode);

    float lux;
    int   colorTemp;
    calculateLuxAndTemp(r, g, b, c, lux, colorTemp);

    const char* stateNames[] = {"R", "G", "B", "L"};
    const char* modeStr      = (calibMode == 0) ? "STABIL" : "DINAMIK";

    // --- Ortalama hesapları (matematiksel, hardcoded değil) ---
    int cnt60  = min(60,  histCount);
    int cnt300 = min(300, histCount);
    int cnt900 = min(900, histCount);

    float avg60_r  = 0, avg60_g  = 0, avg60_b  = 0;
    float avg300_r = 0, avg300_g = 0, avg300_b = 0;
    float avg900_r = 0, avg900_g = 0, avg900_b = 0;

    for (int i = 0; i < cnt900; i++) {
        int idx = (histIndex - 1 - i + MAX_HISTORY_SECONDS) % MAX_HISTORY_SECONDS;
        float vr = histR[idx], vg = histG[idx], vb = histB[idx];

        if (i < cnt60)  { avg60_r  += vr; avg60_g  += vg; avg60_b  += vb; }
        if (i < cnt300) { avg300_r += vr; avg300_g += vg; avg300_b += vb; }
        avg900_r += vr; avg900_g += vg; avg900_b += vb;
    }

    if (cnt60  > 0) { avg60_r  /= cnt60;  avg60_g  /= cnt60;  avg60_b  /= cnt60;  }
    if (cnt300 > 0) { avg300_r /= cnt300; avg300_g /= cnt300; avg300_b /= cnt300; }
    if (cnt900 > 0) { avg900_r /= cnt900; avg900_g /= cnt900; avg900_b /= cnt900; }

    // --- Çıktı (broadcastLine ile Serial+TCP+BLE'ye) ---
    unsigned long ts = millis();
    char buf[640];
    snprintf(buf, sizeof(buf),
        "%lu;FULL;%d;"
        "%d;%d;%d;%d;"
        "%.*f;%.*f;%.*f;"
        "%.*f;%d;"
        "%.*f;%.*f;%.*f;%.*f;"
        "%s;%s;"
        "%.*f;%.*f;%.*f;"
        "%.*f;%.*f;%.*f;"
        "%.*f;%.*f;%.*f;"
        "%d;%d;%d;"
        "%.*f;%d;%d;%d;%d",
        ts, calibMode,
        r, g, b, c,
        outputDecimals, applyOutputScale(proc_r),
        outputDecimals, applyOutputScale(proc_g),
        outputDecimals, applyOutputScale(proc_b),
        outputDecimals, lux, colorTemp,
        outputDecimals, wR, outputDecimals, wG,
        outputDecimals, wB, outputDecimals, wL,
        stateNames[currentState], modeStr,
        outputDecimals, applyOutputScale(avg60_r),
        outputDecimals, applyOutputScale(avg60_g),
        outputDecimals, applyOutputScale(avg60_b),
        outputDecimals, applyOutputScale(avg300_r),
        outputDecimals, applyOutputScale(avg300_g),
        outputDecimals, applyOutputScale(avg300_b),
        outputDecimals, applyOutputScale(avg900_r),
        outputDecimals, applyOutputScale(avg900_g),
        outputDecimals, applyOutputScale(avg900_b),
        histCount, MAX_HISTORY_SECONDS,
        (histCount * 100) / MAX_HISTORY_SECONDS,
        outputDecimals, maxObserved,
        sdCardAvailable ? 1 : 0,
        sdAutoLog       ? 1 : 0,
        testModeActive  ? 1 : 0,
        dualOutputActive? 1 : 0
    );
    broadcastLine(String(buf));
}

// ============================================================
// ========== DONANIM — ENCODER ISR ==========================
// ============================================================

/*
 * encoderISR
 * ----------
 * CLK FALLING kenarında tetiklenen donanım kesmesi.
 *
 * Parazit filtresi: 15 ms altındaki darbeler yok sayılır.
 * Hız uyarlamalı adım:
 *   deltaT < 40 ms  → adım = 0.20  (hızlı çevirme)
 *   deltaT < 80 ms  → adım = 0.10  (orta hız)
 *   deltaT >= 80 ms → adım = 0.05  (yavaş/hassas)
 *
 * Yön: DT LOW → negatif adım (sola), HIGH → pozitif adım (sağa)
 * Aralık: [-1.0, +1.0] — hard clamp uygulanır
 *
 * encoderMoved bayrağı loop()'a aktivite bildirmek için kullanılır.
 */
void encoderISR() {
    unsigned long currentMillis = millis();
    unsigned long deltaT        = currentMillis - lastPulseTime;

    if (deltaT < 15) return; // Parazit filtresi

    int   dtState = digitalRead(ENC_DT_PIN);
    float step    = 0.05f;

    if      (deltaT < 40) step = 0.20f;
    else if (deltaT < 80) step = 0.10f;

    if (dtState == LOW) step = -step;

    float tempVal = 0.0f;
    switch (currentState) {
        case STATE_R: tempVal = wR + step; break;
        case STATE_G: tempVal = wG + step; break;
        case STATE_B: tempVal = wB + step; break;
        case STATE_L: tempVal = wL + step; break;
    }

    if (tempVal >  1.0f) tempVal =  1.0f;
    if (tempVal < -1.0f) tempVal = -1.0f;

    switch (currentState) {
        case STATE_R: wR = tempVal; break;
        case STATE_G: wG = tempVal; break;
        case STATE_B: wB = tempVal; break;
        case STATE_L: wL = tempVal; break;
    }

    lastPulseTime = currentMillis;
    encoderMoved  = true;
}

// ============================================================
// ========== LED GÖRSELLEŞTİRME ============================
// ============================================================

/*
 * updateLEDs
 * ----------
 * Mevcut state için seçili kanalın (wR/wG/wB/wL) değerini
 * NeoPixel şeridine yansıtır.
 *
 * norm = (w + 1) / 2  → [0.0, 1.0] aralığına taşır
 * numLeds = 1 + norm * (NEO_COUNT-1)  → kaç LED yanar
 * br = 30 + norm*225  → parlaklık (30–255)
 * br = 10 + norm*245  → saf renk parlaklığı (10=koyu, 255=tam parlak)
 *
 * Kanal renkleri: R=(br,0,0)  G=(0,br,0)  B=(0,0,br)  L=(br,br,br)
 * Beyaz karışımı yoktur — renk tonu sabit kalır, yalnızca parlaklık değişir.
 */
void updateLEDs() {
    if (staLedConnecting || staLedGreenUntil > 0) return;
    float val = 0.0f;
    if      (currentState == STATE_R) val = wR;
    else if (currentState == STATE_G) val = wG;
    else if (currentState == STATE_B) val = wB;
    else                              val = wL;

    float norm    = (val + 1.0f) / 2.0f;
    int   numLeds = (int)(norm * (NEO_COUNT - 1)) + 1;
    int   br      = 10 + (int)(norm * 245);  // 10 (koyu) → 255 (parlak)

    strip.clear();
    for (int i = 0; i < numLeds; i++) {
        if      (currentState == STATE_R) strip.setPixelColor(i, strip.Color(br,  0,   0 ));
        else if (currentState == STATE_G) strip.setPixelColor(i, strip.Color(0,   br,  0 ));
        else if (currentState == STATE_B) strip.setPixelColor(i, strip.Color(0,   0,   br));
        else                              strip.setPixelColor(i, strip.Color(br,  br,  br));
    }
    strip.show();
    ledsActive = true;
}

// ============================================================
// ========== KOMUT AYRIŞTIRICILARI ==========================
// ============================================================

/*
 * extractModeFromCommand
 * ----------------------
 * Komutun başında "STABIL_" veya "DINAMIK_" öneki varsa öneki
 * soyar ve useMode'u ayarlar. Önek yoksa useMode = calibMode.
 *
 * Örn: "STABIL_OKU_S30" → cmd="OKU_S30", useMode=0
 *      "DINAMIK_OKU_0"  → cmd="OKU_0",   useMode=1
 *      "OKU_S30"        → cmd="OKU_S30",  useMode=calibMode
 *
 * UYARI: Global calibMode değiştirilmez — sadece useMode döner.
 */
void extractModeFromCommand(String &cmd, int &useMode) {
    useMode = calibMode; // varsayılan

    if (cmd.startsWith("STABIL_")) {
        useMode = 0;
        cmd = cmd.substring(7);
    } else if (cmd.startsWith("DINAMIK_")) {
        useMode = 1;
        cmd = cmd.substring(8);
    }
}

/*
 * parseTimeParameter
 * ------------------
 * OKU_S<n>  → saniye cinsinden n değeri döner
 * OKU_<m>   → dakikayı saniyeye çevirir (m * 60), üst sınır 900 sn
 * OKU_0     → 0 döner (tek anlık okuma)
 *
 * Üst sınır MAX_HISTORY_SECONDS (900) ile kırpılır.
 * seconds referans parametresi olarak döner.
 */
void parseTimeParameter(const String &cmd, int &seconds) {
    seconds = 0;

    if (cmd == "OKU_0") {
        seconds = 0;
    } else if (cmd.startsWith("OKU_S")) {
        // "OKU_S" = 5 karakter, sonrası sayı
        seconds = cmd.substring(5).toInt();
        if (seconds > MAX_HISTORY_SECONDS) seconds = MAX_HISTORY_SECONDS;
    } else if (cmd.startsWith("OKU_")) {
        // "OKU_" = 4 karakter, sonrası dakika
        int minutes = cmd.substring(4).toInt();
        seconds = minutes * 60;
        if (seconds > MAX_HISTORY_SECONDS) seconds = MAX_HISTORY_SECONDS;
    }
}

/*
 * parseDualTimeParameter
 * ----------------------
 * DUAL_OKU_S<n> komutları için saniye değeri ayıklar.
 * "DUAL_OKU_S" = 10 karakter.
 */
void parseDualTimeParameter(const String &cmd, int &seconds) {
    seconds = 0;
    // "DUAL_OKU_S" 10 karakter
    int idx = cmd.indexOf("_S");
    if (idx >= 0) {
        seconds = cmd.substring(idx + 2).toInt();
        if (seconds > MAX_HISTORY_SECONDS) seconds = MAX_HISTORY_SECONDS;
    }
}

// ============================================================
// ========== YARDIMCI KOMUT FONKSİYONLARI ===================
// ============================================================

/*
 * setCalibrationMode
 * ------------------
 * Global calibMode'u değiştirir ve durum mesajı gönderir.
 * Geçersiz değer için hata mesajı üretir.
 */
void setCalibrationMode(int mode) {
    if (mode == 0) {
        calibMode = 0;
        printStatusMessage(calibMode, "MODE=STABIL");
    } else if (mode == 1) {
        calibMode = 1;
        printStatusMessage(calibMode, "MODE=DINAMIK");
    } else {
        printErrorMessage(calibMode, "INVALID_MODE");
    }
}

/*
 * showCurrentMode
 * ---------------
 * Mevcut global modu seri porta bildirir.
 */
void showCurrentMode() {
    char meta[32];
    snprintf(meta, sizeof(meta), "CURRENT_MODE=%s", (calibMode == 0) ? "STABIL" : "DINAMIK");
    printStatusMessage(calibMode, meta);
}

/*
 * showCoefficients
 * ----------------
 * wR/wG/wB/wL değerlerini seri porta gönderir.
 */
void showCoefficients() {
    char meta[64];
    snprintf(meta, sizeof(meta), "wR=%.*f,wG=%.*f,wB=%.*f,wL=%.*f",
             outputDecimals, wR, outputDecimals, wG,
             outputDecimals, wB, outputDecimals, wL);
    printStatusMessage(calibMode, meta);
}

/*
 * resetCoefficients
 * -----------------
 * Tüm kalibrasyon katsayılarını ve dinamik maksimumu başlangıç
 * değerlerine döndürür, LED'leri günceller.
 */
void resetCoefficients() {
    wR = wG = wB = wL = 0.0f;
    maxObserved = 1000.0f;
    updateLEDs();
    printStatusMessage(calibMode, "COEFFICIENTS_RESET");
}

/*
 * showBufferStatus
 * ----------------
 * Tampon doluluk bilgisini seri porta gönderir.
 */
void showBufferStatus() {
    char meta[64];
    snprintf(meta, sizeof(meta), "count=%d,capacity=%d,usage=%d%%",
             histCount, MAX_HISTORY_SECONDS,
             (histCount * 100) / MAX_HISTORY_SECONDS);
    printStatusMessage(calibMode, meta);
}

/*
 * clearBuffer
 * -----------
 * Tampon sayaçlarını sıfırlar (veri silinmez, üzerine yazılır).
 */
void clearBuffer() {
    histCount = 0;
    histIndex = 0;
    printStatusMessage(calibMode, "BUFFER_CLEARED");
}

/*
 * showSDStatus
 * ------------
 * SD kart bağlantı durumunu ve otomatik kayıt durumunu bildirir.
 */
void showSDStatus() {
    char meta[64];
    snprintf(meta, sizeof(meta), "available=%d,mounted=%d,auto_log=%d",
             sdCardAvailable, sdCardMounted, sdAutoLog ? 1 : 0);
    printStatusMessage(calibMode, meta);
}

/*
 * showHelp
 * --------
 * Tüm komutları HELP_REQUESTED durum mesajının ardından düz metin
 * olarak listeler. Düz metin satırları standart format dışındadır
 * ancak sadece insan okuyucusu için tasarlanmıştır; ayrıştırıcı
 * bu satırları tip kontrolüyle ayırt edebilir.
 */
void showHelp() {
    printStatusMessage(calibMode, "HELP_REQUESTED");
#if defined(USE_TINYUSB)
    Serial.println("\n=== PICOLOR KOMUTLARI ===");
    Serial.println("KIMSIN                   kimlik");
    Serial.println("VERSIYON / VERSION       firmware surumu");
    Serial.println("DURUM / STATUS           sistem durumu (WiFi, BLE, ayarlar)");
    Serial.println("MOD                      mevcut modu goster");
    Serial.println("MOD_STABIL               stabil moda gec (kalici)");
    Serial.println("MOD_DINAMIK              dinamik moda gec (kalici)");
    Serial.println("KATSAYILAR / COEFF       katsayi goster");
    Serial.println("SIFIRLA / RESET          katsayilari sifirla");
    Serial.println("TAMPON / BUFFER          tampon doluluk");
    Serial.println("TAMPON_SIL / BUFFER_CLEAR tampon temizle");
    Serial.println("TUM / ALL                tam veri paketi");
    Serial.println("SD_DURUM / SD_STATUS     SD kart durumu");
    Serial.println("SD_AKTAR / SD_EXPORT     tamponu SD'ye aktar");
    Serial.println("SDCARD_YAZ_AKTIF         otomatik CSV kayit AC");
    Serial.println("SDCARD_YAZ_PASIF         otomatik CSV kayit KAPAT");
    Serial.println("");
    Serial.println("OKU                      canli akis baslat");
    Serial.println("OKU_STOP                 canli akis durdur");
    Serial.println("OKU_0                    tek okuma");
    Serial.println("OKU_S<n>                 son n saniye ortalamasi");
    Serial.println("OKU_<m>                  son m dakika ortalamasi");
    Serial.println("RAW                      ham sensor degerleri");
    Serial.println("");
    Serial.println("STABIL_OKU[_0/_S<n>/_<m>]  stabil mod override");
    Serial.println("DINAMIK_OKU[_0/_S<n>/_<m>] dinamik mod override");
    Serial.println("");
    Serial.println("DUAL_MODE_ON / OFF       dual cikti modu");
    Serial.println("DUAL_OKU                 tek dual okuma");
    Serial.println("DUAL_OKU_S<n>            n saniyelik dual ort.");
    Serial.println("STABIL_DUAL_OKU[_S<n>]   stabil dual override");
    Serial.println("DINAMIK_DUAL_OKU[_S<n>]  dinamik dual override");
    Serial.println("DUAL_AKIS                canli dual akis");
    Serial.println("STABIL_DUAL_AKIS / DINAMIK_DUAL_AKIS");
    Serial.println("");
    Serial.println("--- CIKTI FORMAT ---");
    Serial.println("BASAMAK_<n>              ondalik basamak say. (0-6)");
    Serial.println("LOGARITMIK               Weber-Fechner log olcek ac");
    Serial.println("LOGARITMIK_<n>           log olcek + dekad ayarla (1.0-5.0)");
    Serial.println("LINEER                   lineer olcege don");
    Serial.println("VARSAYILAN / DEFAULT     fabrika ayarlarina don (1 basamak, lineer)");
    Serial.println("OLCEK / SCALE            mevcut format ayarlarini goster");
    Serial.println("");
    Serial.println("YARDIM / HELP            bu ekran");
    Serial.println("");
    Serial.println("--- WIFI AP (modulun kendi agi) ---");
    Serial.println("WIFI_AP_SSID=<adi>       AP ag adini RAM'e yaz (bu oturum)");
    Serial.println("WIFI_AP_PASS=<sifre>     AP sifresini RAM'e yaz (bu oturum)");
    Serial.println("WIFI_AP_YENILE           AP'yi yeni bilgilerle yeniden baslat");
    Serial.println("--- WIFI STA (ev/ofis modemi) ---");
    Serial.println("WIFI_STA_SSID=<ag_adi>   Modem ag adini RAM'e yaz (bu oturum)");
    Serial.println("WIFI_STA_PASS=<sifre>    Modem sifresini RAM'e yaz (bu oturum)");
    Serial.println("WIFI_STA_BAGLAN          Mevcut RAM bilgileriyle modeme baglan");
    Serial.println("WIFI_STA_KES             Modem baglantisini kes (AP etkilenmez)");
    Serial.println("WIFI_STA_KAYDET          STA bilgilerini EEPROM'a kaydet (SD yok ise)");
    Serial.println("WIFI_STA_SIFIRLA         EEPROM'daki STA bilgilerini sil");
    Serial.println("WIFI_BILGI               AP ve STA durumunu goster");
    Serial.println("========================\n");
#endif
}

/*
 * showDurum
 * ---------
 * Sistem durumunu birkaç STATUS satırı halinde tüm kanallara gönderir:
 * firmware sürümü, SD kart, ölçek ayarları, AP bilgisi, STA durumu, BLE.
 */
void showDurum() {
    char meta[160];

    // Firmware
    printStatusMessage(calibMode, "VER=" FIRMWARE_VERSION);

    // SD kart
    printStatusMessage(calibMode, sdCardMounted ? "SD=MOUNTED" : "SD=NONE");

    // Ölçek ayarları
    snprintf(meta, sizeof(meta), "DEC=%d,LOG=%s,LOG_DEKAD=%.1f,MOD=%s,DUAL=%s",
             outputDecimals,
             logarithmicOutput ? "ON" : "OFF",
             logDecades,
             calibMode == 0 ? "STABIL" : "DINAMIK",
             dualOutputActive ? "ON" : "OFF");
    printStatusMessage(calibMode, meta);

    // WiFi AP (her zaman açık — doğrudan bağlantı için)
    snprintf(meta, sizeof(meta), "AP_SSID=%s,AP_IP=%s,PORT=%d",
             cfgWifiApSSID,
             WiFi.softAPIP().toString().c_str(),
             cfgTcpPort);
    printStatusMessage(calibMode, meta);

    // WiFi STA (ev ağı — akıllı ev entegrasyonu için)
    if (strlen(wifiStaSSID) > 0 && WiFi.status() == WL_CONNECTED) {
        snprintf(meta, sizeof(meta), "STA_SSID=%s,STA_IP=%s",
                 wifiStaSSID, WiFi.localIP().toString().c_str());
    } else if (strlen(wifiStaSSID) > 0) {
        snprintf(meta, sizeof(meta), "STA_SSID=%s,STA=CONNECTING", wifiStaSSID);
    } else {
        strcpy(meta, "STA=NO_CREDENTIALS");
    }
    printStatusMessage(calibMode, meta);

    // BLE
    snprintf(meta, sizeof(meta), "BLE=%s", bleConnected ? "CONNECTED" : "ADVERTISING");
    printStatusMessage(calibMode, meta);
}

// ============================================================
// ========== KOMUT SATIRINI İŞLE (tüm kanallar için) =========
// ============================================================

/*
 * processCommandLine
 * ------------------
 * Ham bir satırı (büyük harf dönüşümü ve token bölme dahil) işler.
 * Serial, TCP ve BLE kanallarından gelen satırlar bu fonksiyona gelir.
 * username: "serial" | "anonymous" | kullanıcı adı
 * currentUsername global'ı güncellenir — broadcastLine bunu kullanır.
 */
void processCommandLine(String line, const String& username, const char* clientType = "serial") {
    currentUsername = username;
    line.trim();
    if (line.length() == 0) { currentUsername = "serial"; return; }

    if (loggingEnabled) {
        String logLine = line;
        String logUpper = line;
        logUpper.toUpperCase();
        if (logUpper.startsWith("WIFI_STA_PASS=")) logLine = "WIFI_STA_PASS=***";
        if (logUpper.startsWith("WIFI_AP_PASS="))  logLine = "WIFI_AP_PASS=***";
        appendUserLog(username, clientType, logLine);
    }

    // SSID/şifre değerleri case-sensitive — toUpperCase'den önce işle
    {
        String upper = line;
        upper.toUpperCase();
        if (upper.startsWith("WIFI_AP_SSID=")) {
            setWiFiApSSID(line.substring(13));
            currentUsername = "serial";
            return;
        }
        if (upper.startsWith("WIFI_AP_PASS=")) {
            setWiFiApPass(line.substring(13));
            currentUsername = "serial";
            return;
        }
        if (upper.startsWith("WIFI_STA_SSID=")) {
            setWiFiStaSSID(line.substring(14));
            currentUsername = "serial";
            return;
        }
        if (upper.startsWith("WIFI_STA_PASS=")) {
            setWiFiStaPass(line.substring(14));
            currentUsername = "serial";
            return;
        }
        if (upper.startsWith("TCP_PORT=")) {
            int p = line.substring(9).toInt();
            if (p > 0 && p < 65536) changeTcpPort((uint16_t)p);
            currentUsername = "serial";
            return;
        }
        if (upper.startsWith("WIRELESS_ENABLED=")) {
            String val = upper.substring(17);
            val.trim();
            if (val == "0" || val == "FALSE" || val == "OFF") disableWiFi();
            else if (val == "1" || val == "TRUE" || val == "ON") enableWiFi();
            currentUsername = "serial";
            return;
        }
    }

    line.toUpperCase();
    if (line.length() == 0) { currentUsername = "serial"; return; }

    String tokens[16];
    int    tokenCount = 0;

    int start = 0;
    int len   = line.length();
    for (int i = 0; i <= len && tokenCount < 16; i++) {
        char c = (i < len) ? line.charAt(i) : ' ';
        if (c == ' ' || c == ',') {
            if (i > start) tokens[tokenCount++] = line.substring(start, i);
            start = i + 1;
        }
    }

    // 1. geçiş: ayar komutları
    for (int i = 0; i < tokenCount; i++) {
        if (!isReadCommand(tokens[i])) processCommand(tokens[i]);
    }
    // 2. geçiş: okuma komutları
    for (int i = 0; i < tokenCount; i++) {
        if (isReadCommand(tokens[i])) processCommand(tokens[i]);
    }

    currentUsername = "serial"; // bir sonraki serial çağrısı için varsayılanı sıfırla
}

// ============================================================
// ========== ANA SERIAL KOMUT İŞLEYİCİ =====================
// ============================================================

/*
 * processCommand
 * --------------
 * Tek bir komutu (zaten büyük harfe çevrilmiş, trim edilmiş) işler.
 * handleSerialCommands tarafından her token için ayrı ayrı çağrılır.
 */
void processCommand(String cmd) {
    // --- Mod önekini ayıkla (STABIL_ / DINAMIK_) ---
    int useMode;
    extractModeFromCommand(cmd, useMode);

    // ====================================================
    // TEMEL KOMUTLAR
    // ====================================================
    if (cmd == "KIMSIN") {
        printStatusMessage(calibMode, "IDENTITY=PICOLOR_" FIRMWARE_VERSION);
    }
    else if (cmd == "VERSIYON" || cmd == "VERSION") {
        printStatusMessage(calibMode, "VERSION=" FIRMWARE_VERSION);
    }
    else if (cmd == "DURUM" || cmd == "STATUS") {
        showDurum();
    }
    else if (cmd == "MOD") {
        showCurrentMode();
    }
    else if (cmd == "MOD_STABIL") {
        setCalibrationMode(0);
    }
    else if (cmd == "MOD_DINAMIK") {
        setCalibrationMode(1);
    }

    // ====================================================
    // KALİBRASYON
    // ====================================================
    else if (cmd == "KATSAYILAR" || cmd == "COEFF") {
        showCoefficients();
    }
    else if (cmd == "SIFIRLA" || cmd == "RESET") {
        resetCoefficients();
    }

    // ====================================================
    // TAMPON
    // ====================================================
    else if (cmd == "TAMPON" || cmd == "BUFFER") {
        showBufferStatus();
    }
    else if (cmd == "TAMPON_SIL" || cmd == "BUFFER_CLEAR") {
        clearBuffer();
    }

    // ====================================================
    // TAM VERİ PAKETİ
    // ====================================================
    else if (cmd == "TUM" || cmd == "ALL") {
        testModeActive = false;
        sendFullData();
    }

    // ====================================================
    // SD KART
    // ====================================================
    else if (cmd == "SD_DURUM" || cmd == "SD_STATUS") {
        showSDStatus();
    }
    else if (cmd == "SD_AKTAR" || cmd == "SD_EXPORT") {
        exportBufferToSD();
    }
    else if (cmd == "SDCARD_YAZ_AKTIF") {
        sdAutoLog = true;
        printStatusMessage(calibMode, "SD_AUTO_LOG=ON");
    }
    else if (cmd == "SDCARD_YAZ_PASIF") {
        sdAutoLog = false;
        printStatusMessage(calibMode, "SD_AUTO_LOG=OFF");
    }

    // ====================================================
    // CANLI AKIŞ
    // ====================================================
    else if (cmd == "OKU_STOP") {
        stopLiveStream();
    }
    else if (cmd == "OKU") {
        startLiveStream(useMode);
    }

    // ====================================================
    // HAM VERİ
    // ====================================================
    else if (cmd == "RAW") {
        testModeActive = false;
        uint16_t r, g, b, c;
        tcs.getRawData(&r, &g, &b, &c);
        // İlk satır: RGB
        printStandardOutput(OUT_RAW, calibMode, (float)r, (float)g, (float)b);
        // İkinci satır: clear channel meta olarak
        char meta[16];
        snprintf(meta, sizeof(meta), "c=%d", c);
        printStandardOutput(OUT_RAW, calibMode, (float)r, (float)g, (float)b, meta);
    }

    // ====================================================
    // TEK ve ORTALAMA OKUMALAR (OKU_0 / OKU_S<n> / OKU_<m>)
    // useMode ile anlık override desteklenir.
    // ====================================================
    else if (cmd == "OKU_0" || cmd.startsWith("OKU_S") || cmd.startsWith("OKU_")) {
        testModeActive = false;
        int seconds;
        parseTimeParameter(cmd, seconds);

        if (seconds == 0) {
            sendSingleReading(useMode);
        } else {
            sendAverageReading(seconds, useMode);
        }
    }

    // ====================================================
    // DUAL MOD KONTROLÜ
    // ====================================================
    else if (cmd == "DUAL_MODE_ON") {
        dualOutputActive = true;
        printStatusMessage(calibMode, "DUAL_MODE_ENABLED");
    }
    else if (cmd == "DUAL_MODE_OFF") {
        dualOutputActive = false;
        printStatusMessage(calibMode, "DUAL_MODE_DISABLED");
    }

    // ====================================================
    // DUAL CANLI AKIŞ
    // ====================================================
    else if (cmd == "DUAL_AKIS") {
        startLiveDualStream(useMode);
    }

    // ====================================================
    // DUAL TEK / ORTALAMA OKUMA (DUAL_OKU / DUAL_OKU_S<n>)
    // useMode ile anlık override desteklenir.
    // "DUAL_OKU_S" = 10 karakter; substring(10).toInt() doğrudur.
    // ====================================================
    else if (cmd == "DUAL_OKU") {
        testModeActive = false;
        sendSingleDualReading(useMode);
    }
    else if (cmd.startsWith("DUAL_OKU_S")) {
        testModeActive = false;
        int seconds = cmd.substring(10).toInt();
        if (seconds > MAX_HISTORY_SECONDS) seconds = MAX_HISTORY_SECONDS;
        if (seconds > 0) {
            sendAverageDualReading(seconds, useMode);
        } else {
            sendSingleDualReading(useMode);
        }
    }

    // ====================================================
    // ÇIKTI FORMAT KONTROLÜ
    // ====================================================
    else if (cmd.startsWith("BASAMAK_")) {
        int n = cmd.substring(8).toInt();
        if (n >= 0 && n <= 6) {
            outputDecimals = n;
            char meta[32];
            snprintf(meta, sizeof(meta), "DECIMALS=%d", outputDecimals);
            printStatusMessage(calibMode, meta);
        } else {
            printErrorMessage(calibMode, "INVALID_DECIMAL_VALUE");
        }
    }
    else if (cmd.startsWith("LOGARITMIK_")) {
        float d = cmd.substring(11).toFloat();
        if (d >= 1.0f && d <= 5.0f) {
            logarithmicOutput = true;
            logDecades = d;
            char meta[48];
            snprintf(meta, sizeof(meta), "SCALE=LOGARITMIK,decades=%.1f", logDecades);
            printStatusMessage(calibMode, meta);
        } else {
            printErrorMessage(calibMode, "INVALID_DECADES_VALUE");
        }
    }
    else if (cmd == "LOGARITMIK") {
        logarithmicOutput = true;
        char meta[48];
        snprintf(meta, sizeof(meta), "SCALE=LOGARITMIK,decades=%.1f", logDecades);
        printStatusMessage(calibMode, meta);
    }
    else if (cmd == "LINEER") {
        logarithmicOutput = false;
        printStatusMessage(calibMode, "SCALE=LINEER");
    }
    else if (cmd == "VARSAYILAN" || cmd == "DEFAULT") {
        outputDecimals    = 1;
        logarithmicOutput = false;
        logDecades        = 3.0f;
        printStatusMessage(calibMode, "OUTPUT=DEFAULT,decimals=1,scale=LINEER");
    }
    else if (cmd == "OLCEK" || cmd == "SCALE") {
        char meta[64];
        snprintf(meta, sizeof(meta), "decimals=%d,scale=%s,decades=%.1f",
                 outputDecimals,
                 logarithmicOutput ? "LOGARITMIK" : "LINEER",
                 logDecades);
        printStatusMessage(calibMode, meta);
    }

    // ====================================================
    // YARDIM
    // ====================================================
    else if (cmd == "YARDIM" || cmd == "HELP") {
        showHelp();
    }

    // ====================================================
    // WiFi AP — modülün kendi ağı
    // ====================================================
    else if (cmd == "WIFI_AP_YENILE") {
        restartAP();
    }

    // ====================================================
    // WiFi STA — ev/ofis modemi
    // ====================================================
    else if (cmd == "WIFI_STA_BAGLAN") {
        reconnectWiFiSTA();
    }
    else if (cmd == "WIFI_STA_KES") {
        WiFi.disconnect();
        printStatusMessage(calibMode, "STA_DISCONNECTED");
    }
    else if (cmd == "WIFI_STA_KAYDET") {
        saveWiFiCredentials();
        printStatusMessage(calibMode, "STA_SAVED_TO_EEPROM");
    }
    else if (cmd == "WIFI_STA_SIFIRLA") {
        clearWiFiCredentials();
    }

    // ====================================================
    // WiFi GENEL
    // ====================================================
    else if (cmd == "WIFI_BILGI") {
        showWiFiBilgi();
    }

    // ====================================================
    // BİLİNMEYEN KOMUT
    // ====================================================
    else {
        printErrorMessage(calibMode, "UNKNOWN_COMMAND");
    }
}

/*
 * isReadCommand
 * -------------
 * Token sensör verisi üreten bir okuma komutu mu?
 * STABIL_/DINAMIK_ önekleri görmezden gelinir.
 * handleSerialCommands'ın iki geçişli sıralaması için kullanılır.
 */
bool isReadCommand(const String &token) {
    String t = token;
    if      (t.startsWith("STABIL_"))  t = t.substring(7);
    else if (t.startsWith("DINAMIK_")) t = t.substring(8);

    if (t == "OKU" || (t.startsWith("OKU_") && t != "OKU_STOP")) return true;
    if (t == "RAW")                                               return true;
    if (t == "DUAL_OKU" || t.startsWith("DUAL_OKU_S") || t == "DUAL_AKIS") return true;
    if (t == "TUM" || t == "ALL")                                return true;
    return false;
}

/*
 * handleSerialCommands
 * --------------------
 * Seri porttan bir satır okur ve processCommandLine'a iletir.
 * İki geçişli sıralama (ayar önce, okuma sonra) processCommandLine içinde.
 */
void handleSerialCommands() {
#if defined(USE_TINYUSB)
    if (!Serial || Serial.available() == 0) return;
    String line = Serial.readStringUntil('\n');
    processCommandLine(line, "serial");
#endif
}

// ============================================================
// ========== LOOP YARDIMCI FONKSİYONLARI ===================
// ============================================================

/*
 * handleButtonPress
 * -----------------
 * Encoder butonunu her loop iterasyonunda polling ile kontrol eder.
 * Kısa basış (>50ms, <LONG_PRESS_TIME): state döngüsü R→G→B→L→R,
 *   uyku süresi beklemeSuresiTiklama'ya ayarlanır, LED'ler güncellenir.
 * Uzun basış (>=LONG_PRESS_TIME): BUTTON_LONG_PRESS bildirimi.
 */
void handleButtonPress(unsigned long currentMillis,
                       bool &buttonDown,
                       unsigned long &lastButtonPressRef) {
    int sw = digitalRead(ENC_SW_PIN);

    if (sw == LOW) {
        if (!buttonDown) {
            buttonDown           = true;
            lastButtonPressRef   = currentMillis;
        }
    } else {
        if (buttonDown) {
            unsigned long duration = currentMillis - lastButtonPressRef;

            if (duration >= (unsigned long)LONG_PRESS_TIME) {
                printStatusMessage(calibMode, "BUTTON_LONG_PRESS");
            } else if (duration > 50) {
                currentState     = (State)((currentState + 1) % 4);
                gecerliUykuSuresi = (unsigned long)(beklemeSuresiTiklama * 1000);
                lastActivityTime = currentMillis;
                updateLEDs();
            }
            buttonDown = false;
        }
    }
}

/*
 * handleEncoder
 * -------------
 * ISR'nin set ettiği encoderMoved bayrağını kontrol eder.
 * Bayrak aktifse uyku süresini ve aktivite zamanını günceller,
 * LED'leri yeniler.
 */
void handleEncoder() {
    if (encoderMoved) {
        encoderMoved      = false;
        lastActivityTime  = millis();
        gecerliUykuSuresi = (unsigned long)(beklemeSuresiCevirme * 1000);
        updateLEDs();
    }
}

/*
 * handleSleepMode
 * ---------------
 * Son aktiviteden bu yana gecerliUykuSuresi ms geçmişse LED'leri söndürür.
 */
void handleSleepMode(unsigned long currentMillis) {
    if (ledsActive && (currentMillis - lastActivityTime > gecerliUykuSuresi)) {
        strip.clear();
        strip.show();
        ledsActive = false;
    }
}

/*
 * handleDataSampling
 * ------------------
 * Her 1000 ms'de bir sensörden ham veri alır, kalibrasyon uygular
 * ve dairesel tampona yazar.
 *
 * sdAutoLog true ve SD kart mevcutsa appendToCSV çağrılır.
 * appendToCSV içinde sdAutoLog kontrolü de yapılır (çift güvence).
 */
void handleDataSampling(unsigned long currentMillis) {
    if (currentMillis - lastSampleTime < 1000) return;
    lastSampleTime = currentMillis;

    uint16_t r, g, b, c;
    tcs.getRawData(&r, &g, &b, &c);

    float cR, cG, cB;
    getCalibratedColor(cR, cG, cB, calibMode);

    histR[histIndex] = cR;
    histG[histIndex] = cG;
    histB[histIndex] = cB;

    histRawR[histIndex] = r;
    histRawG[histIndex] = g;
    histRawB[histIndex] = b;
    histRawC[histIndex] = c;

    histIndex = (histIndex + 1) % MAX_HISTORY_SECONDS;
    if (histCount < MAX_HISTORY_SECONDS) histCount++;

    // Otomatik SD kayıt — sadece sdAutoLog aktifse çalışır
    if (sdAutoLog && sdCardAvailable) {
        float lux;
        int   colorTemp;
        calculateLuxAndTemp(r, g, b, c, lux, colorTemp);
        appendToCSV(r, g, b, c, cR, cG, cB, lux, colorTemp);
    }
}

/*
 * handleTestMode
 * --------------
 * Canlı akış aktifse (testModeActive == true) her 300 ms'de bir
 * veri satırı gönderir.
 *
 * dualOutputActive true ise printDualOutput, false ise
 * printStandardOutput (OUT_LIVE) kullanılır.
 * Her iki durumda da useMode olarak global calibMode kullanılır.
 */
void handleTestMode(unsigned long currentMillis) {
    static unsigned long lastTestPrint = 0;
    if (!testModeActive) return;
    if (currentMillis - lastTestPrint < 300) return;

    lastTestPrint = currentMillis;

    if (dualOutputActive) {
        uint16_t r, g, b, c;
        tcs.getRawData(&r, &g, &b, &c);
        float cR, cG, cB;
        getCalibratedColor(cR, cG, cB, calibMode);
        printDualOutput(OUT_LIVE, calibMode, r, g, b, c, cR, cG, cB, "live_dual");
    } else {
        float cR, cG, cB;
        getCalibratedColor(cR, cG, cB, calibMode);
        printStandardOutput(OUT_LIVE, calibMode, cR, cG, cB);
    }
}

// ============================================================
// ========== KABLOSUZ FONKSİYONLARI =========================
// ============================================================

/*
 * loadWiFiCredentials
 * -------------------
 * EEPROM'dan STA kimlik bilgilerini okur. Magic byte yoksa (hiç kaydedilmemişse)
 * .ino sabitlerindeki DEFAULT_WIFI_STA_SSID/PASS değerleri korunur.
 */
void loadWiFiCredentials() {
    if (EEPROM.read(0) != EEPROM_MAGIC) {
        // EEPROM'da kayıtlı kimlik yok — .ino'daki DEFAULT_WIFI_STA_SSID/PASS geçerli kalır
        return;
    }
    // EEPROM'da geçerli kayıt var — öncelik EEPROM'da, default değerin üzerine yazar
    for (int i = 0; i < EEPROM_SSID_LEN; i++)
        wifiStaSSID[i] = (char)EEPROM.read(EEPROM_SSID_OFFSET + i);
    for (int i = 0; i < EEPROM_PASS_LEN; i++)
        wifiStaPass[i] = (char)EEPROM.read(EEPROM_PASS_OFFSET + i);
    wifiStaSSID[EEPROM_SSID_LEN - 1] = '\0';
    wifiStaPass[EEPROM_PASS_LEN - 1]  = '\0';
}

/*
 * saveWiFiCredentials
 * -------------------
 * Mevcut wifiStaSSID/wifiStaPass değerlerini EEPROM'a yazar.
 * WIFI_STA_KAYDET komutu tarafından çağrılır — döngüsel çağrıdan kaçının.
 */
void saveWiFiCredentials() {
    EEPROM.write(0, EEPROM_MAGIC);
    for (int i = 0; i < EEPROM_SSID_LEN; i++)
        EEPROM.write(EEPROM_SSID_OFFSET + i, (uint8_t)wifiStaSSID[i]);
    for (int i = 0; i < EEPROM_PASS_LEN; i++)
        EEPROM.write(EEPROM_PASS_OFFSET + i, (uint8_t)wifiStaPass[i]);
    EEPROM.commit();
}

// --- STA kimlik bilgileri: yalnızca RAM (bu oturum) ---
// Kalıcı kayıt için: config.json (SD kart) veya WIFI_STA_KAYDET (EEPROM)

/*
 * setWiFiStaSSID / setWiFiStaPass
 * --------------------------------
 * Ev modemi kimlik bilgilerini yalnızca RAM'e yazar (bu oturum).
 * WIFI_STA_SSID= ve WIFI_STA_PASS= komutları tarafından çağrılır.
 */
void setWiFiStaSSID(const String& ssid) {
    strncpy(wifiStaSSID, ssid.c_str(), EEPROM_SSID_LEN - 1);
    wifiStaSSID[EEPROM_SSID_LEN - 1] = '\0';
    char meta[48];
    snprintf(meta, sizeof(meta), "STA_SSID=%s,KAYNAK=RAM", wifiStaSSID);
    printStatusMessage(calibMode, meta);
}

void setWiFiStaPass(const String& pass) {
    strncpy(wifiStaPass, pass.c_str(), EEPROM_PASS_LEN - 1);
    wifiStaPass[EEPROM_PASS_LEN - 1] = '\0';
    printStatusMessage(calibMode, "STA_PASS=RAM");
}

// --- AP kimlik bilgileri: yalnızca RAM (bu oturum) ---
// Kalıcı kayıt için: config.json (SD kart)

/*
 * setWiFiApSSID / setWiFiApPass
 * ------------------------------
 * AP kimlik bilgilerini yalnızca RAM'e yazar (bu oturum).
 * WIFI_AP_SSID= ve WIFI_AP_PASS= komutları tarafından çağrılır.
 * Değişikliğin etkili olması için WIFI_AP_YENILE komutu gerekir.
 */
void setWiFiApSSID(const String& ssid) {
    ssid.toCharArray(cfgWifiApSSID, sizeof(cfgWifiApSSID));
    char meta[48];
    snprintf(meta, sizeof(meta), "AP_SSID=%s,KAYNAK=RAM", cfgWifiApSSID);
    printStatusMessage(calibMode, meta);
}

void setWiFiApPass(const String& pass) {
    pass.toCharArray(cfgWifiApPass, sizeof(cfgWifiApPass));
    printStatusMessage(calibMode, "AP_PASS=RAM");
}

/*
 * restartAP
 * ---------
 * Mevcut RAM'deki AP SSID/şifre değerleriyle AP'yi yeniden başlatır.
 * Not: softAPConfig arduino-pico 5.6.0'da güvenilir çalışmıyor;
 *      gerçek IP her zaman 192.168.4.1 olarak atanır.
 */
void restartAP() {
    WiFi.softAPConfig(IPAddress(192,168,42,1),
                      IPAddress(192,168,42,1),
                      IPAddress(255,255,255,0));
    WiFi.softAP(cfgWifiApSSID, cfgWifiApPass);
    { uint32_t _t = millis(); while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0) && millis() - _t < 15000) delay(10); }
    char meta[80];
    snprintf(meta, sizeof(meta), "AP_RESTARTED,SSID=%s,IP=%s,PORT=%d",
             cfgWifiApSSID, WiFi.softAPIP().toString().c_str(), cfgTcpPort);
    printStatusMessage(calibMode, meta);
}

/*
 * changeTcpPort
 * -------------
 * TCP sunucusunu durdurur ve yeni portta yeniden başlatır.
 * DEFAULT_TCP_PORT için global g_tcpServer nesnesi kullanılır;
 * farklı port için heap'te yeni WiFiServer oluşturulur.
 */
void changeTcpPort(uint16_t newPort) {
    if (tcpServer) tcpServer->stop();
    // Sadece heap ile oluşturulmuşsa sil; global nesneyi (g_tcpServer) silme
    if (tcpServer && tcpServer != &g_tcpServer) {
        delete tcpServer;
        tcpServer = nullptr;
    }
    cfgTcpPort = newPort;
    if (cfgTcpPort == DEFAULT_TCP_PORT) {
        tcpServer = &g_tcpServer;
    } else {
        tcpServer = new WiFiServer(cfgTcpPort);
    }
    tcpServer->begin();
    char meta[48];
    snprintf(meta, sizeof(meta), "TCP_PORT_CHANGED=%d", cfgTcpPort);
    printStatusMessage(calibMode, meta);
}

/*
 * sanitizeHostname
 * ----------------
 * DHCP hostname must only contain letters, digits, and hyphens (RFC 1123).
 * Converts UTF-8 extended/Turkish chars to ASCII base, replaces _ and space
 * with -, and drops everything else. Falls back to "picolor" if result is empty.
 */
static void sanitizeHostname(const char *src, char *dst, size_t dstMax) {
    size_t di = 0;
    for (size_t si = 0; src[si] && di < dstMax - 1; ) {
        uint8_t b = (uint8_t)src[si];
        char out = 0;
        if (b < 0x80) {
            if ((b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9')) out = (char)b;
            else if (b == '_' || b == ' ') out = '-';
            else if (b == '-') out = '-';
            si++;
        } else if ((b & 0xE0) == 0xC0 && (uint8_t)src[si+1]) {
            uint8_t b2 = (uint8_t)src[si+1];
            if (b == 0xC3) {
                // U+00C0-U+00FF: Latin-1 Supplement
                switch (b2) {
                    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: out='A'; break;
                    case 0x87: out='C'; break;
                    case 0x88: case 0x89: case 0x8A: case 0x8B: out='E'; break;
                    case 0x8C: case 0x8D: case 0x8E: case 0x8F: out='I'; break;
                    case 0x91: out='N'; break;
                    case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: out='O'; break;
                    case 0x99: case 0x9A: case 0x9B: case 0x9C: out='U'; break;
                    case 0x9D: out='Y'; break;
                    case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: out='a'; break;
                    case 0xA7: out='c'; break;
                    case 0xA8: case 0xA9: case 0xAA: case 0xAB: out='e'; break;
                    case 0xAC: case 0xAD: case 0xAE: case 0xAF: out='i'; break;
                    case 0xB1: out='n'; break;
                    case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: out='o'; break;
                    case 0xB9: case 0xBA: case 0xBB: case 0xBC: out='u'; break;
                    case 0xBD: out='y'; break;
                    default: break;
                }
            } else if (b == 0xC4) {
                // U+0100-U+017F: Latin Extended-A (Turkish: Ğ=9E ğ=9F İ=B0 ı=B1)
                switch (b2) {
                    case 0x9E: out='G'; break; // Ğ
                    case 0x9F: out='g'; break; // ğ
                    case 0xB0: out='I'; break; // İ
                    case 0xB1: out='i'; break; // ı
                    default:   out = (b2 & 1) ? (char)((b2 - 0x80) / 2 + 'a') : 0; break;
                }
            } else if (b == 0xC5) {
                // U+0140-U+017F cont. (Turkish: Ş=9E ş=9F)
                switch (b2) {
                    case 0x9E: out='S'; break; // Ş
                    case 0x9F: out='s'; break; // ş
                    default:   break;
                }
            }
            si += 2;
        } else if ((b & 0xF0) == 0xE0) { si += 3; }  // 3-byte — skip
        else if ((b & 0xF8) == 0xF0)   { si += 4; }  // 4-byte — skip
        else                            { si++;      }
        if (out && di < dstMax - 1) dst[di++] = out;
    }
    while (di > 0 && dst[di-1] == '-') di--;  // trim trailing hyphens
    dst[di] = '\0';
    if (di == 0) { strncpy(dst, "picolor", dstMax - 1); dst[dstMax-1] = '\0'; }
}

/*
 * applyHostnameBeforeBegin
 * ------------------------
 * netif_set_hostname() pointer KOPYALAMAZ — sadece adresi saklar.
 * Yerel char dizisi kullanmak dangling pointer yaratır: WiFi.begin() sonrası
 * DHCP asenkron çalıştığında dizi çoktan yok olmuş olur ve lwIP çöp okur.
 * g_wifiHostname global olduğundan ömrü programla birlikte devam eder.
 */
static void applyHostnameBeforeBegin() {
    if (g_wifiHostname[0] == '\0') {
        sanitizeHostname(DEFAULT_DEVICE_NAME, g_wifiHostname, sizeof(g_wifiHostname));
    }
    WiFi.setHostname(g_wifiHostname);
    cyw43_arch_lwip_begin();
    struct netif *n;
    NETIF_FOREACH(n) { netif_set_hostname(n, g_wifiHostname); }
    cyw43_arch_lwip_end();
}

/*
 * applyDhcpHostname
 * -----------------
 * STA netif'i WiFi.begin() sırasında oluşturulur — applyHostnameBeforeBegin()
 * o anda NETIF_FOREACH ile bulamaz çünkü henüz mevcut değildir.
 * Bu yüzden bağlantı kurulduktan SONRA STA netif'i IP ile bulup hostname
 * ayarlanır, ardından dhcp_renew() ile modemin client tablosunu güncelliyoruz.
 */
static void applyDhcpHostname() {
    if (g_hostnameApplied) return;
    if (WiFi.status() != WL_CONNECTED) return;
    u32_t staIPu32 = (u32_t)(uint32_t)WiFi.localIP();
    if (staIPu32 == 0) {
        printStatusMessage(calibMode, "DHCP_HN:LOCAL_IP_ZERO");
        return;
    }
    char dbg[96];
    snprintf(dbg, sizeof(dbg), "DHCP_HN:SEARCH,IP=%s,HN=%s",
             WiFi.localIP().toString().c_str(), g_wifiHostname);
    printStatusMessage(calibMode, dbg);
    cyw43_arch_lwip_begin();
    struct netif *sta = NULL;
    { struct netif *n; NETIF_FOREACH(n) { if (ip4_addr_get_u32(netif_ip4_addr(n)) == staIPu32) { sta = n; break; } } }
    if (sta) {
        netif_set_hostname(sta, g_wifiHostname);
        dhcp_stop(sta);
        dhcp_start(sta);
        g_hostnameApplied = true;
        snprintf(dbg, sizeof(dbg), "DHCP_HN:APPLIED,HN=%s",
                 sta->hostname ? sta->hostname : "(null)");
        cyw43_arch_lwip_end();
        printStatusMessage(calibMode, dbg);
    } else {
        cyw43_arch_lwip_end();
        printStatusMessage(calibMode, "DHCP_HN:STA_NETIF_NOT_FOUND");
    }
}

/*
 * reconnectWiFiSTA
 * ----------------
 * WIFI_STA_BAGLAN komutunu işler: kimlik bilgisi varsa WiFi.begin() çağrılır.
 * Otomatik periyodik yeniden bağlantı için handleWiFiReconnect() kullanılır.
 */
void reconnectWiFiSTA() {
    if (strlen(wifiStaSSID) > 0) {
        g_hostnameApplied = false;
        staReconnectAttempts = 0;  // manuel bağlantıda sayacı sıfırla
        applyHostnameBeforeBegin();
        WiFi.begin(wifiStaSSID, wifiStaPass);
        printStatusMessage(calibMode, "WIFI_STA_RECONNECTING");
    } else {
        printStatusMessage(calibMode, "WIFI_STA_NO_CREDENTIALS");
    }
}

/*
 * showWiFiBilgi
 * -------------
 * WIFI_BILGI komutunu işler: AP SSID/IP/port ve STA SSID/IP/durum
 * bilgilerini iki ayrı STATUS satırı olarak gönderir.
 */
void showWiFiBilgi() {
    char meta[120];
    if (strlen(wifiStaSSID) > 0) {
        if (WiFi.status() == WL_CONNECTED) {
            snprintf(meta, sizeof(meta), "STA_SSID=%s,STA_PASS=***,STA_IP=%s",
                     wifiStaSSID, WiFi.localIP().toString().c_str());
        } else {
            snprintf(meta, sizeof(meta), "STA_SSID=%s,STA_PASS=***,STA=CONNECTING", wifiStaSSID);
        }
    } else {
        strcpy(meta, "STA_SSID=<not_set>");
    }
    printStatusMessage(calibMode, meta);

    snprintf(meta, sizeof(meta), "AP_SSID=%s,AP_IP=%s,PORT=%d",
             cfgWifiApSSID, WiFi.softAPIP().toString().c_str(), cfgTcpPort);
    printStatusMessage(calibMode, meta);
}

/*
 * clearWiFiCredentials
 * --------------------
 * EEPROM'daki magic byte'ı silerek STA kimlik bilgilerini geçersiz kılar,
 * RAM'deki değerleri .ino sabitlerinden sıfırlar ve bağlanmayı dener.
 */
void clearWiFiCredentials() {
    EEPROM.write(0, 0x00);
    EEPROM.commit();
    // EEPROM silindi — .ino'daki default değerlere geri dön
    strlcpy(wifiStaSSID, DEFAULT_WIFI_STA_SSID, sizeof(wifiStaSSID));
    strlcpy(wifiStaPass, DEFAULT_WIFI_STA_PASS, sizeof(wifiStaPass));
    printStatusMessage(calibMode, "WIFI_CREDENTIALS_CLEARED");
    // Default doluysa hemen bağlanmayı dene
    reconnectWiFiSTA();
}

/*
 * setupWiFi
 * ---------
 * AP'yi başlatır, STA bağlantısını tetikler ve TCP sunucusunu açar.
 * Sıralama önemlidir: AP önce hazır olmalı, ardından WiFi.begin() çağrılmalı
 * (çakışma sorunu — arduino-pico 5.6.0).
 * STA bağlantısı başlarsa NeoPixel kırmızı yanarak handleWiFiReconnect()'i bekler.
 */
void setupWiFi() {
    // Hostname'i CYW43 başlamadan önce arduino-pico WiFi sınıfına kaydet.
    // arduino-pico bu değeri saklar; STA netifi DHCP başlatmadan önce uygular.
    if (g_wifiHostname[0] == '\0') {
        sanitizeHostname(DEFAULT_DEVICE_NAME, g_wifiHostname, sizeof(g_wifiHostname));
    }
    WiFi.setHostname(g_wifiHostname);

    // AP başlat — CYW43 default IP 192.168.4.1 kullanır (softAPConfig arduino-pico 5.6.0'da çalışmıyor)
    WiFi.softAP(cfgWifiApSSID, cfgWifiApPass);

    { uint32_t _t = millis(); while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0) && millis() - _t < 15000) delay(10); }

    // STA: AP tamamen hazır olduktan sonra başlat — WiFi.begin() AP'yi bozabilir
    // (arduino-pico 5.6.0'da softAP + WiFi.begin() çakışma sorunu)
    if (cfgWifiStaAuto && strlen(wifiStaSSID) > 0 && WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
        applyHostnameBeforeBegin();
        WiFi.begin(wifiStaSSID, wifiStaPass);
        printStatusMessage(calibMode, "WIFI_STA_CONNECTING");
        // STA bağlantısı başladı — kırmızı LED hemen yak (RGB blink'lerden önce görünsün)
        staLedConnecting      = true;
        staLedConnectingStart = millis();
        for (int i = 0; i < NEO_COUNT; i++) strip.setPixelColor(i, strip.Color(40, 0, 0));
        strip.show();
    }

    // TCP sunucusu — global nesneyle başlat (arduino-pico'da en güvenilir yöntem)
    tcpServer = &g_tcpServer;
    tcpServer->begin();
    // JSON'dan farklı port geldiyse runtime değişim uygula
    if (cfgTcpPort != DEFAULT_TCP_PORT) {
        changeTcpPort(cfgTcpPort);
    }

    // mDNS: picolor.local → TCP erişimi için (çift çağrıya karşı korunmalı)
    static bool mdnsStarted = false;
    if (!mdnsStarted) { MDNS.begin("picolor"); mdnsStarted = true; }

    // tcpRxLen dizisini sıfırla
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) tcpRxLen[i] = 0;

    char apMeta[96];
    snprintf(apMeta, sizeof(apMeta), "WIFI_AP_STARTED,IP=%s,PORT=%d",
             WiFi.softAPIP().toString().c_str(), cfgTcpPort);
    printStatusMessage(calibMode, apMeta);
}

/*
 * disableWiFi / enableWiFi
 * ------------------------
 * WIRELESS_ENABLED=0/1 komutu bu fonksiyonları çağırır.
 * disableWiFi: tüm TCP bağlantılarını kapatır, AP ve STA'yı durdurur.
 * enableWiFi : wifiEnabled bayrağını true yapar ve setupWiFi()'ı yeniden çağırır.
 */
void disableWiFi() {
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (tcpClients[i]) tcpClients[i].stop();
    }
    if (tcpServer) tcpServer->stop();
    WiFi.disconnect();
    WiFi.softAPdisconnect(true);
    wifiEnabled = false;
    printStatusMessage(calibMode, "WIFI_DISABLED");
}

void enableWiFi() {
    wifiEnabled = true;
    setupWiFi();
    printStatusMessage(calibMode, "WIFI_ENABLED");
}

// --- BTstack BLE callbacks ---

/*
 * onBLEDeviceConnected
 * --------------------
 * BTstack bağlantı callback'i. Başarılı bağlantıda bleConnected = true,
 * merkez cihazın handle'ı bleCentralHandle'a kaydedilir.
 */
static void onBLEDeviceConnected(BLEStatus status, BLEDevice *device) {
    if (status == BLE_STATUS_OK) {
        bleConnected    = true;
        bleRxLen        = 0;
        bleCentralHandle = device->getHandle();
    }
}

/*
 * onBLEDeviceDisconnected
 * -----------------------
 * Bağlantı koptuğunda durumu sıfırlar ve reklamı yeniden başlatır.
 */
static void onBLEDeviceDisconnected(BLEDevice *device) {
    bleConnected     = false;
    bleRxLen         = 0;
    bleHasPendingCmd = false;
    bleCentralHandle = HCI_CON_HANDLE_INVALID;
    BTstack.startAdvertising();
}

/*
 * onBLECharacteristicWrite
 * ------------------------
 * NUS RX karakteristiğine yazıldığında tetiklenir (BTstack callback).
 * Gelen byte'ları bleRxBuf'a biriktirir; '\n' görünce tam satırı
 * blePendingCmd'e koyar ve bleHasPendingCmd bayrağını set eder.
 * Komut loop()'ta (handleBLEClients) işlenir — callback içinde değil.
 */
static int onBLECharacteristicWrite(uint16_t handle,
                                    uint8_t *data, uint16_t size) {
    if (handle != (uint16_t)nusRxHandle) return 0;
    for (int i = 0; i < (int)size && bleRxLen < (int)sizeof(bleRxBuf) - 1; i++) {
        char c = (char)data[i];
        bleRxBuf[bleRxLen++] = c;
        if (c == '\n') {
            bleRxBuf[bleRxLen] = '\0';
            String line(bleRxBuf);
            bleRxLen = 0;
            line.trim();
            if (line.length() == 0) continue;
            String username = "anonymous";
            if (line.startsWith("USER:") || line.startsWith("user:")) {
                int spaceIdx = line.indexOf(' ', 5);
                if (spaceIdx > 5) {
                    username = line.substring(5, spaceIdx);
                    line     = line.substring(spaceIdx + 1);
                }
            }
            // processCommandLine'ı callback'ten değil loop'tan çağır
            blePendingCmd      = line;
            blePendingUser     = username;
            bleHasPendingCmd   = true;
        }
    }
    return 0;
}

// --- BLE setup & send ---

/*
 * setupBLE
 * --------
 * BTstack'i başlatır, NUS servisini (Nordic UART) GATT tablosuna ekler
 * ve reklamı başlatır. TX/RX handle'ları global değişkenlere kaydedilir.
 */
void setupBLE() {
    BTstack.setBLEDeviceConnectedCallback(onBLEDeviceConnected);
    BTstack.setBLEDeviceDisconnectedCallback(onBLEDeviceDisconnected);
    BTstack.setGATTCharacteristicWrite(onBLECharacteristicWrite);

    BTstack.setup(DEFAULT_DEVICE_NAME);

    BTstack.addGATTService(new UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"));
    nusTxHandle = BTstack.addGATTCharacteristicDynamic(
        new UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"),
        ATT_PROPERTY_READ | ATT_PROPERTY_NOTIFY, 0);
    nusRxHandle = BTstack.addGATTCharacteristicDynamic(
        new UUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"),
        ATT_PROPERTY_WRITE_WITHOUT_RESPONSE | ATT_PROPERTY_WRITE, 0);

    BTstack.startAdvertising();
    printStatusMessage(calibMode, "BLE_NUS_STARTED");
}

/*
 * bleSendLine
 * -----------
 * NUS TX karakteristiğine bir satır + '\n' gönderir.
 * Bağlı istemci yoksa sessizce döner.
 */
void bleSendLine(const String& line) {
    if (!bleConnected || bleCentralHandle == HCI_CON_HANDLE_INVALID) return;
    String data = line + "\n";
    att_server_notify(bleCentralHandle, nusTxHandle,
        (uint8_t*)data.c_str(), (uint16_t)data.length());
}

/*
 * handleWiFiReconnect
 * -------------------
 * Her loop() iterasyonunda üç görevi yürütür:
 * 1. STA bağlandıysa kırmızı LED'i söndürür, 1s yeşil yakar.
 * 2. 30s içinde bağlantı kurulamazsa 3x kırmızı blink → LED söndürme.
 * 3. Her 30s'de bir STA'yı kontrol eder; kopuksa yeniden bağlanır.
 *    cfgPilModu true ise 3 başarısız denemeden sonra bağlantı denemesi durur.
 */
void handleWiFiReconnect(unsigned long currentMillis) {
    // İlk STA bağlantısı LED tespiti — her döngüde hızlı kontrol
    if (staLedConnecting && WiFi.status() == WL_CONNECTED) {
        staLedConnecting      = false;
        staLedConnectingStart = 0;
        for (int i = 0; i < NEO_COUNT; i++) strip.setPixelColor(i, strip.Color(0, 40, 0));
        strip.show();
        staLedGreenUntil = currentMillis + 1000;
        applyDhcpHostname(); // STA bağlantısı kuruldu — DHCP hostname uygula
    }
    // Bağlantı 30 sn içinde kurulamazsa: 3x kırmızı blink, söndür
    if (staLedConnecting && staLedConnectingStart > 0 &&
        currentMillis - staLedConnectingStart >= 30000UL) {
        staLedConnecting      = false;
        staLedConnectingStart = 0;
        for (int b = 0; b < 3; b++) {
            for (int i = 0; i < NEO_COUNT; i++) strip.setPixelColor(i, strip.Color(40, 0, 0));
            strip.show(); delay(200);
            strip.clear(); strip.show(); delay(200);
        }
        updateLEDs();
    }
    if (staLedGreenUntil > 0 && currentMillis >= staLedGreenUntil) {
        staLedGreenUntil = 0;
        updateLEDs();
    }

    // Periyodik yeniden bağlantı — 30 saniyede bir
    static unsigned long lastSTACheck = 0;
    if (!wifiEnabled || !cfgWifiStaAuto || strlen(wifiStaSSID) == 0) return;
    if (currentMillis - lastSTACheck < 30000UL) return;
    lastSTACheck = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
        staReconnectAttempts = 0;
        applyDhcpHostname(); // bağlıysa hostname uygulandı mı kontrol et
    } else {
        int maxAttempts = cfgPilModu ? 3 : 10;
        if (staReconnectAttempts >= maxAttempts) return;
        g_hostnameApplied = false;
        applyHostnameBeforeBegin();
        WiFi.begin(wifiStaSSID, wifiStaPass);
        staReconnectAttempts++;
        char msg[48];
        snprintf(msg, sizeof(msg), "WIFI_STA_RECONNECTING,ATTEMPT=%d/%d",
                 staReconnectAttempts, maxAttempts);
        printStatusMessage(calibMode, msg);
    }
}

/*
 * handleTCPClients
 * ----------------
 * Yeni TCP bağlantılarını kabul eder (maks MAX_TCP_CLIENTS); doluysa reddeder.
 * Bağlı her istemciden karakter karakter satır biriktirir, '\n' gelince
 * processCommandLine()'a iletir. USER:<kullanıcı> öneki ayrıştırılır.
 */
void handleTCPClients() {
    if (!wifiEnabled || !tcpServer) return;
    WiFiClient newClient = tcpServer->accept();
    if (newClient) {
        bool accepted = false;
        for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
            if (!tcpClients[i] || !tcpClients[i].connected()) {
                tcpClients[i] = newClient;
                tcpRxLen[i]   = 0;
                accepted = true;
                // Bağlanan istemciye hoş geldin bildirimi
                String welcome = String(millis()) + ";4;" + String(calibMode)
                               + ";0;0;0;TCP_CONNECTED=PICOLOR_" FIRMWARE_VERSION;
                tcpClients[i].println(welcome);
                // Serial ve diğer kanallara bildir
                currentUsername = "tcp";
                printStatusMessage(calibMode, "TCP_CLIENT_CONNECTED");
                break;
            }
        }
        if (!accepted) {
            newClient.stop(); // doluysa reddet
            printStatusMessage(calibMode, "TCP_CLIENT_REJECTED_FULL");
        }
    }

    // Bağlı istemcilerden gelen veriyi işle
    for (int i = 0; i < MAX_TCP_CLIENTS; i++) {
        if (!tcpClients[i] || !tcpClients[i].connected()) continue;

        while (tcpClients[i].available()) {
            char c = tcpClients[i].read();
            if (c == '\n' || tcpRxLen[i] >= (int)sizeof(tcpRxBuf[i]) - 1) {
                tcpRxBuf[i][tcpRxLen[i]] = '\0';
                String line(tcpRxBuf[i]);
                tcpRxLen[i] = 0;
                line.trim();
                if (line.length() == 0) continue;

                // USER:username prefix ayrıştır
                String username = "anonymous";
                if (line.startsWith("USER:") || line.startsWith("user:")) {
                    int spaceIdx = line.indexOf(' ', 5);
                    if (spaceIdx > 5) {
                        username = line.substring(5, spaceIdx);
                        line     = line.substring(spaceIdx + 1);
                    }
                }
                processCommandLine(line, username, "tcp");
            } else {
                tcpRxBuf[i][tcpRxLen[i]++] = c;
            }
        }
    }
}

/*
 * handleBLEClients
 * ----------------
 * BTstack'in dahili döngüsünü çalıştırır ve varsa bekleyen BLE komutunu işler.
 * Komut onBLECharacteristicWrite callback'inden blePendingCmd'e aktarılmıştır.
 */
void handleBLEClients() {
    BTstack.loop();
    if (bleHasPendingCmd) {
        bleHasPendingCmd = false;
        processCommandLine(blePendingCmd, blePendingUser, "ble");
    }
}

/*
 * appendUserLog
 * -------------
 * loggingEnabled true ise komutu SD karttaki cfgLogFile'a ekler.
 * Şifre içeren satırlar processCommandLine tarafından maskelenerek iletilir.
 */
void appendUserLog(const String& username, const char* clientType, const String& command) {
    if (!loggingEnabled || !sdCardAvailable || !sdCardMounted) return;
    File f = SD.open(cfgLogFile, FILE_WRITE);
    if (!f) return;
    f.printf("%lu,%s,%s,%s\n", millis(),
             username.c_str(), clientType, command.c_str());
    f.close();
}

// ============================================================
// ========== SETUP ==========================================
// ============================================================

void setup() {
    // Serial — her modda setup'ın ilk adımı (No USB: Serial1; TinyUSB: SerialUSB)
    Serial.begin(115200);
#if defined(USE_TINYUSB)
    { uint32_t t = millis(); while (!Serial && millis() - t < 3000) {} }
#endif
    serialReady = true;

    // TCS LED (aktif-LOW) başlangıçta kapalı
    pinMode(TCS_LED_PIN, OUTPUT);
    digitalWrite(TCS_LED_PIN, LOW);

    // I2C — RP2350 üzerinde SDA=4, SCL=5
    Wire.setSDA(4);
    Wire.setSCL(5);
    Wire.begin();

    if (!tcs.begin()) {
        printErrorMessage(calibMode, "TCS_INIT_FAIL");
    } else {
        printStatusMessage(calibMode, "TCS_OK");
    }

    // NeoPixel
    strip.begin();
    strip.show();
    printStatusMessage(calibMode, "NEOPIXEL_OK");

    // Encoder pinleri
    pinMode(ENC_CLK_PIN, INPUT_PULLUP);
    pinMode(ENC_DT_PIN,  INPUT_PULLUP);
    pinMode(ENC_SW_PIN,  INPUT_PULLUP);

    // CLK düşen kenarda kesme — bu satır kesinlikle kaldırılmamalı
    attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), encoderISR, FALLING);
    printStatusMessage(calibMode, "ENCODER_IRQ_OK");

    // SD kart
    initSDCard();

    // EEPROM önce yükle (düşük öncelik)
    EEPROM.begin(EEPROM_SIZE);
    loadWiFiCredentials();

    // JSON config EEPROM'u geçersiz kılar (yüksek öncelik)
    loadJsonConfig();

    setupWiFi();

    setupBLE();

    // İlk örnekleme ve LED güncelleme
    lastSampleTime  = millis();
    lastActivityTime = millis();
    handleDataSampling(millis());
    updateLEDs();

    printStatusMessage(calibMode, "SYSTEM_STARTED");

    // Boot tamamlandı: R → G → B (100 ms arayla)
    for (int c = 0; c < 3; c++) {
        strip.clear();
        for (int i = 0; i < NEO_COUNT; i++) {
            if      (c == 0) strip.setPixelColor(i, strip.Color(40, 0,  0 ));
            else if (c == 1) strip.setPixelColor(i, strip.Color(0,  40, 0 ));
            else             strip.setPixelColor(i, strip.Color(0,  0,  40));
        }
        strip.show();
        delay(100);
        strip.clear();
        strip.show();
        delay(100);
    }

    // RGB blink'ler LED'i sıfırladı — STA durumuna göre geri yükle
    if (staLedConnecting) {
        if (WiFi.status() == WL_CONNECTED) {
            // RGB blink sırasında bağlandı — doğrudan yeşile geç
            staLedConnecting = false;
            for (int i = 0; i < NEO_COUNT; i++) strip.setPixelColor(i, strip.Color(0, 40, 0));
            strip.show();
            staLedGreenUntil = millis() + 1000;
        } else {
            // Hâlâ bağlanıyor — kırmızıyı geri yak
            for (int i = 0; i < NEO_COUNT; i++) strip.setPixelColor(i, strip.Color(40, 0, 0));
            strip.show();
        }
    }
}

// ============================================================
// ========== LOOP ===========================================
// ============================================================

void loop() {
    unsigned long currentMillis = millis();
    static bool   buttonDown    = false;

    handleButtonPress(currentMillis, buttonDown, lastButtonPress); // 1. Buton
    handleEncoder();                                                // 2. Encoder
    handleSleepMode(currentMillis);                                 // 3. LED uyku
    handleDataSampling(currentMillis);                              // 4. Örnekleme
    handleTestMode(currentMillis);                                  // 5. Canlı akış
    handleSerialCommands();                                         // 6. Seri komutlar
    handleWiFiReconnect(currentMillis);                             // 7. STA yeniden bağlantı
    handleTCPClients();                                             // 8. TCP komutlar
    handleBLEClients();                                             // 9. BLE komutlar
}
