// "No USB" modunda SerialUSB uygulaması derlenmez.
// SdFat'ın PioSdioCard.cpp'si (SDIO sürücüsü) Serial'a referans verir —
// biz SPI kullandığımız için bu kod asla çalışmaz, ama linker sembol arar.
// Bu dosya eksik sembolleri karşılar.
//
// Arduino.h dahil EDİLMEZ — "extern SerialUSB Serial;" çakışmasını önlemek için.
// Linker düzeyinde saf sembol tanımı yeterli.
#if !defined(USE_TINYUSB)

extern "C" {
    // SerialUSB::availableForWrite()
    int  _ZN9SerialUSB17availableForWriteEv(void*)               { return 0; }
    // SerialUSB::operator bool()
    int  _ZN9SerialUSBcvbEv(void*)                               { return 0; }
    // SerialUSB::available()
    int  _ZN9SerialUSB9availableEv(void*)                        { return 0; }
    // SerialUSB::begin(unsigned long)
    void _ZN9SerialUSB5beginEm(void*, unsigned long)             {}
    // SerialUSB::write(unsigned char)  — Print::println bu üzerinden çalışır
    unsigned int _ZN9SerialUSB5writeEh(void*, unsigned char)     { return 1; }
    // SerialUSB::write(const uint8_t*, size_t)
    unsigned int _ZN9SerialUSB5writeEPKhm(void*, const void*, unsigned long) { return 0; }

    // Global "Serial" nesnesi — sıfır başlatılmış 256 bayt.
    // vtable işaretçisi = 0; sanal metot çağrılırsa çöker,
    // ancak SPI SD modunda PioSdioCard hiçbir zaman çağrılmaz.
    __attribute__((section(".data"), aligned(8)))
    char Serial[256] = {};
}

#endif // !USE_TINYUSB
