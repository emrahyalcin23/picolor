#pragma once
// "No USB" modunda Serial (USB CDC) tanımsız olur.
// SdFat ve diğer kütüphanelerin Serial referansları Serial1 (UART) olarak yönlendirilir.
// Serial1 başlatılmadıysa çıktı sessizce düşer — çökme olmaz.
#if !defined(USE_TINYUSB)
  #define Serial Serial1
#endif
