// ============================================================
// DERLEME ZAMANI AYARLARI
// ============================================================
// Aşağıdaki 4 ayar çalışma zamanında değiştirilemez.
// Diğer tüm ayarlar SD karttaki config.txt üzerinden yapılır.
// ============================================================

#define WIRELESS_ENABLED  true   // WiFi + BLE aç/kapat
#define LOGGING_ENABLED   false  // Kullanıcı komutu logu (SD kart gerektirir)
#define MAX_TCP_CLIENTS   4      // Eş zamanlı TCP istemci sayısı (dizi boyutu)
#define DEFAULT_TCP_PORT  8266   // TCP port — yalnızca buradan değiştirilebilir

// !! EEPROM / FLASH YAZMA LİMİTİ UYARISI !!
// RP2040 flash belleği yaklaşık 100.000 silme döngüsüne sahiptir.
// EEPROM yalnızca WIFI_STA_KAYDET komutuyla yazılır; döngüsel yazma yapılmaz.
