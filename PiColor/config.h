// ============================================================
// ÖZELLIK ANAHTARLARI — Derleme zamanı bayrakları
// ============================================================
// Bu dosyada yalnızca derleme zamanında (compile-time) belirlenmesi
// gereken bayraklar yer alır.
//
// Çalışma zamanı (runtime) ayarları SD karttaki config.txt
// dosyasından okunur (bkz. loadSDConfig). SD kart yoksa veya
// config.txt bulunamazsa aşağıdaki DEFAULT_* değerleri kullanılır.
// ============================================================

// Derleme zamanı özellik bayrakları
#define WIRELESS_ENABLED  true   // WiFi+BLE aktif/pasif — default: aktif
#define LOGGING_ENABLED   false  // Kullanıcı logu aktif/pasif — default: pasif
                                 // SD kart takılı   → komutlar log_file yoluna kaydedilir
                                 // SD kart takılı değil → loglama otomatik devre dışı

// Dizi boyutu — derleme zamanında sabit olmalı (bellek düzeni)
#define MAX_TCP_CLIENTS   4

// ============================================================
// VARSAYILAN DEĞERLER — config.txt yoksa bu değerler kullanılır
// ============================================================
#define DEFAULT_WIFI_AP_SSID  "PiColor"
#define DEFAULT_WIFI_AP_PASS  "picolor123"
#define DEFAULT_TCP_PORT      8266   // YALNIZCA buradan degistirilebilir (runtime'da degil)
#define DEFAULT_LOG_FILE      "/user_log.csv"

// ============================================================
// !! EEPROM / FLASH YAZMA LİMİTİ UYARISI !!
//
// RP2040 flash belleği yaklaşık 100.000 blok silme döngüsüne
// sahiptir. Bu nedenle ayarları EEPROM'a (flash emülasyonu)
// sık sık yazmaktan kaçının.
//
// EEPROM bu projede YALNIZCA WiFi STA kimlik bilgilerini
// (WIFI_SSID= / WIFI_PASS= komutları) kalıcı saklamak için
// kullanılmaktadır — ve yalnızca kullanıcı bu komutları
// verdiğinde yazılır (önyükleme başına değil).
//
// Çalışma zamanı yapılandırması (SSID, şifre, port vb.)
// SD karttaki config.txt üzerinden yönetilmeli; EEPROM'a
// döngüsel olarak yazılmamalıdır.
// ============================================================
