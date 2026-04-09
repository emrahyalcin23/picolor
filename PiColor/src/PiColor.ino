/*
 * PiColor - Smart ambient light & color analyzer System
 * Copyright (c) 2026 Emrah YALÇIN
 * * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */
#include <Wire.h>
#include <Adafruit_TCS34725.h>
#include <Adafruit_NeoPixel.h>

// --- PIN TANIMLAMALARI ---
#define TCS_LED_PIN 15 
#define ENC_CLK_PIN 16 
#define ENC_DT_PIN 17  
#define ENC_SW_PIN 18  
#define NEO_PIN 28     
#define NEO_COUNT 8    
#define LONG_PRESS_TIME 1000 

// --- KAZANÇ VE HASSASİYET TANIMLAMALARI ---
// Seçenekler: 1, 4, 16, 60 (TCS34725 donanımsal limitleri)
#define KAZANC_ORANI 4 

// Entegrasyon süresi (Işık toplama süresi): 
// TCS34725_INTEGRATIONTIME_50MS, _154MS, _700MS gibi...
#define ENTEGRASYON_SURESI TCS34725_INTEGRATIONTIME_50MS

// --- ÇIKTI NORMALİZASYON SABİTLERİ ---
// Teorik maksimum kalibre değer: ham_maks × maks_renk_katsayısı × maks_ışık_katsayısı
// = 65535 × 2.0 × 2.0 = 262140  (wR/wG/wB ve wL'nin tümü +1.0 olduğunda)
#define NORM_INPUT_MAX  (65535.0f * 2.0f * 2.0f)  // = 262140.0
#define NORM_OUTPUT_MIN 0.0f
#define NORM_OUTPUT_MAX 100.0f

// Sensör nesnesini makrolar ile başlatıyoruz
Adafruit_TCS34725 tcs = Adafruit_TCS34725(ENTEGRASYON_SURESI, 
                        (KAZANC_ORANI == 1)  ? TCS34725_GAIN_1X :
                        (KAZANC_ORANI == 16) ? TCS34725_GAIN_16X :
                        (KAZANC_ORANI == 60) ? TCS34725_GAIN_60X : 
                                               TCS34725_GAIN_4X);

// Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

enum State { STATE_R, STATE_G, STATE_B, STATE_L };
State currentState = STATE_R;

// --- KATSAYILAR VE DONANIM DEĞİŞKENLERİ ---
volatile float wR = 0.0, wG = 0.0, wB = 0.0, wL = 0.0;
volatile unsigned long lastPulseTime = 0; 
volatile bool encoderMoved = false;

// --- ZAMAN VE UYKU DEĞİŞKENLERİ ---
float beklemeSuresiCevirme = 1.0; 
float beklemeSuresiTiklama = 3.0; 
unsigned long gecerliUykuSuresi = 1000; 
unsigned long lastActivityTime = 0;
unsigned long lastButtonPress = 0;
bool ledsActive = false;

// --- VERİ HAVUZU (15 DAKİKALIK RAM TAMPONU) ---
#define MAX_HISTORY_SECONDS 900 
float histR[MAX_HISTORY_SECONDS];
float histG[MAX_HISTORY_SECONDS];
float histB[MAX_HISTORY_SECONDS];
int histIndex = 0;       // Sıradaki yazılacak konum
int histCount = 0;       // Havuzda biriken toplam saniye miktarı (İlk açılışta 900'e kadar artar)
unsigned long lastSampleTime = 0;

// --- ÇALIŞMA MODLARI ---
bool testModeActive = false; // Başlangıçta sessiz modda bekle

// --- YARDIMCI FONKSİYON: KALİBRE EDİLMİŞ VERİYİ HESAPLA ---
void getCalibratedColor(float &cR, float &cG, float &cB) {
    uint16_t r, g, b, c;
    tcs.getRawData(&r, &g, &b, &c);

    float l_factor = 1.0 + wL; if (l_factor < 0.05) l_factor = 0.05;
    float r_factor = 1.0 + wR; if (r_factor < 0.05) r_factor = 0.05;
    float g_factor = 1.0 + wG; if (g_factor < 0.05) g_factor = 0.05;
    float b_factor = 1.0 + wB; if (b_factor < 0.05) b_factor = 0.05;

    // cR = (r * r_factor) * l_factor;
    // cG = (g * g_factor) * l_factor;
    // cB = (b * b_factor) * l_factor;
    
    // Ham değeri kalibre et, ardından 0-100 aralığına normalize et
    // Referans maks = NORM_INPUT_MAX (262140), aşanlar NORM_OUTPUT_MAX'a kesilir
    cR = (r * r_factor) * l_factor / NORM_INPUT_MAX * NORM_OUTPUT_MAX;
    cG = (g * g_factor) * l_factor / NORM_INPUT_MAX * NORM_OUTPUT_MAX;
    cB = (b * b_factor) * l_factor / NORM_INPUT_MAX * NORM_OUTPUT_MAX;
 
    if (cR > NORM_OUTPUT_MAX) cR = NORM_OUTPUT_MAX;
    if (cG > NORM_OUTPUT_MAX) cG = NORM_OUTPUT_MAX;
    if (cB > NORM_OUTPUT_MAX) cB = NORM_OUTPUT_MAX;
    if (cR < NORM_OUTPUT_MIN) cR = NORM_OUTPUT_MIN;
    if (cG < NORM_OUTPUT_MIN) cG = NORM_OUTPUT_MIN;
    if (cB < NORM_OUTPUT_MIN) cB = NORM_OUTPUT_MIN;
}

// --- DONANIMSAL KESME (ENCODER ISR) ---
void encoderISR() {
    unsigned long currentMillis = millis();
    unsigned long deltaT = currentMillis - lastPulseTime;
    
    if (deltaT < 15) return; // 15ms Katı Parazit Filtresi

    int dtState = digitalRead(ENC_DT_PIN);
    float step = 0.05;

    if (deltaT < 40) step = 0.20;
    else if (deltaT < 80) step = 0.10;

    if (dtState == LOW) step = -step;

    float tempVal = 0.0;
    switch (currentState) {
        case STATE_R: tempVal = wR + step; break;
        case STATE_G: tempVal = wG + step; break;
        case STATE_B: tempVal = wB + step; break;
        case STATE_L: tempVal = wL + step; break;
    }

    if (tempVal > 1.0) tempVal = 1.0;
    if (tempVal < -1.0) tempVal = -1.0;

    switch (currentState) {
        case STATE_R: wR = tempVal; break;
        case STATE_G: wG = tempVal; break;
        case STATE_B: wB = tempVal; break;
        case STATE_L: wL = tempVal; break;
    }
    
    lastPulseTime = currentMillis;
    encoderMoved = true;
}

void setup() {
    Serial.begin(115200);
    
    pinMode(TCS_LED_PIN, OUTPUT);
    digitalWrite(TCS_LED_PIN, LOW);

    Wire.setSDA(4);
    Wire.setSCL(5);
    Wire.begin();
    if(!tcs.begin()) {
        Serial.println("Sensör Bulunamadı!");
    }

    strip.begin();
    strip.show();

    pinMode(ENC_CLK_PIN, INPUT_PULLUP);
    pinMode(ENC_DT_PIN, INPUT_PULLUP);
    pinMode(ENC_SW_PIN, INPUT_PULLUP);
    
    attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), encoderISR, FALLING);
}

void loop() {
    unsigned long currentMillis = millis();
    static bool buttonDown = false;

    // 1. BUTON MANTIĞI
    int sw = digitalRead(ENC_SW_PIN);
    if (sw == LOW) {
        if (!buttonDown) {
            buttonDown = true;
            lastButtonPress = currentMillis;
        }
    } else {
        if (buttonDown) {
            unsigned long duration = currentMillis - lastButtonPress;
            if (duration > LONG_PRESS_TIME) {
                Serial.println(">> UZUN BASILDI (Fonksiyonu İleride Eklenecek)");
            } else if (duration > 50) { 
                currentState = (State)((currentState + 1) % 4);
                gecerliUykuSuresi = beklemeSuresiTiklama * 1000; 
                lastActivityTime = currentMillis;
                updateLEDs();
            }
            buttonDown = false;
        }
    }

    // 2. ENCODER KONTROLÜ
    if (encoderMoved) {
        encoderMoved = false; 
        lastActivityTime = currentMillis; 
        gecerliUykuSuresi = beklemeSuresiCevirme * 1000; 
        updateLEDs(); 
    }

    // 3. DİNAMİK UYKU MODU
    if (ledsActive && (currentMillis - lastActivityTime > gecerliUykuSuresi)) {
        strip.clear();
        strip.show();
        ledsActive = false;
    }

    // 4. VERİ HAVUZUNU DOLDURMA (Her 1 Saniyede Bir)
    if (currentMillis - lastSampleTime >= 1000) {
        lastSampleTime = currentMillis;
        
        float cR, cG, cB;
        getCalibratedColor(cR, cG, cB);
        
        // Veriyi tampona yaz ve indexi ilerlet
        histR[histIndex] = cR;
        histG[histIndex] = cG;
        histB[histIndex] = cB;
        
        histIndex = (histIndex + 1) % MAX_HISTORY_SECONDS;
        if (histCount < MAX_HISTORY_SECONDS) {
            histCount++; // 900 saniye dolana kadar sayacı artır
        }
    }

    // 5. TEST MODU (OKU ile tetiklenir, her 300ms'de veri basar)
    static unsigned long lastTestPrint = 0;
    if (testModeActive && (currentMillis - lastTestPrint > 300)) {
        lastTestPrint = currentMillis;
        float cR, cG, cB;
        getCalibratedColor(cR, cG, cB);
        
        
        // Debug için ; 
        // Serial.print("TEST AKISI => ");
        Serial.print("R:"); Serial.print(cR, 0);
        Serial.print(" G:"); Serial.print(cG, 0);
        Serial.print(" B:"); Serial.println(cB, 0);
    }

    // 6. SERİ HABERLEŞME (BİLGİSAYARDAN GELEN KOMUTLAR)
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toUpperCase(); 

        // YENİ EKLENEN KİMLİK DOĞRULAMA (HANDSHAKE) KOMUTU
        if (cmd == "KIMSIN") {
            Serial.println("BENIM_OZEL_PICOM_V1");
        }
        else if (cmd == "OKU") {
            // Sadece OKU gelirse canlı veri akışını başlat
            testModeActive = true;
            Serial.println(">> CANLI AKIS BASLADI (Durdurmak icin OKU_X veya OKU_0 gonderin)");
        } 
        else if (cmd.startsWith("OKU_")) {
            // Parametreli bir komut gelirse canlı akışı (stream) hemen durdur
            testModeActive = false; 
            
            int reqSeconds = 0;

            // 1. Durum: Saniye bazlı okuma isteniyorsa (Örn: OKU_S30)
            if (cmd.startsWith("OKU_S")) {
                String param = cmd.substring(5); // "OKU_S" kısmı 5 karakter olduğu için 5'ten sonrasını al
                reqSeconds = param.toInt();
                
                // Güvenlik sınırı: Havuzumuz maksimum 900 saniye (15 dk) alabiliyor
                if (reqSeconds > MAX_HISTORY_SECONDS) reqSeconds = MAX_HISTORY_SECONDS; 
            } 
            // 2. Durum: Dakika bazlı okuma isteniyorsa (Örn: OKU_10)
            else {
                String param = cmd.substring(4); // "OKU_" kısmı 4 karakter olduğu için 4'ten sonrasını al
                int minutes = param.toInt();
                
                if (minutes > 15) minutes = 15; // 15 dakika güvenlik sınırı
                reqSeconds = minutes * 60;      // Dakikayı saniyeye çevir
            }

            // --- Bundan sonrası eski kodunla birebir aynı ---

            if (reqSeconds == 0) {
                // OKU_0 veya OKU_S0: Sadece o anki değeri TEK SEFERLİK gönder
                float cR, cG, cB;
                getCalibratedColor(cR, cG, cB);
                Serial.print("ANLIK TEK OKUMA => R:"); Serial.print(cR, 0);
                Serial.print(" G:"); Serial.print(cG, 0);
                Serial.print(" B:"); Serial.println(cB, 0);
            } 
            else {
                // İstenen saniye kadar ortalamayı TEK SEFERLİK gönder
                int calcSeconds = (reqSeconds > histCount) ? histCount : reqSeconds; 
                
                if (calcSeconds == 0) {
                    Serial.println("HATA: Henuz ortalama alinacak kadar veri birikmedi.");
                } else {
                    float sumR = 0, sumG = 0, sumB = 0;
                    
                    for (int i = 0; i < calcSeconds; i++) {
                        int idx = histIndex - 1 - i;
                        if (idx < 0) idx += MAX_HISTORY_SECONDS;
                        
                        sumR += histR[idx];
                        sumG += histG[idx];
                        sumB += histB[idx];
                    }

                    // Debug için;
                    // Serial.print("ORTALAMA (Son "); Serial.print(calcSeconds); Serial.print(" Sn) => ");
                    Serial.print("R:"); Serial.print(sumR / calcSeconds, 0);
                    Serial.print(" G:"); Serial.print(sumG / calcSeconds, 0);
                    Serial.print(" B:"); Serial.println(sumB / calcSeconds, 0);
                }
            }
        }
        else if (cmd == "RAW") {
            testModeActive = true; // Sürekli akış varsa durdur
            uint16_t r, g, b, c;
            tcs.getRawData(&r, &g, &b, &c);
            
            Serial.print("HAM VERI (RAW) => "); // niye bilmiyorum ama ham veri vermiyor. katsayılı veriyor.
            Serial.print("R:"); Serial.print(r);
            Serial.print(" G:"); Serial.print(g);
            Serial.print(" B:"); Serial.print(b);
            Serial.print(" C:"); Serial.println(c);
        }
        else {
            Serial.print("Girebileceğin emirler şunlar: KIMSIN, OKU, OKU_X, RAW");
        }
    }
}

// --- LED GÖRSELLEŞTİRME ---
void updateLEDs() {
    float val = 0.0;
    if (currentState == STATE_R) val = wR;
    else if (currentState == STATE_G) val = wG;
    else if (currentState == STATE_B) val = wB;
    else val = wL;

    float norm = (val + 1.0) / 2.0; 
    int numLeds = (norm * (NEO_COUNT - 1)) + 1;
    
    int br = 30 + (norm * 225);
    int w = norm * 150;

    strip.clear();
    for(int i = 0; i < numLeds; i++) {
        if (currentState == STATE_R)      strip.setPixelColor(i, strip.Color(br, w, w));
        else if (currentState == STATE_G) strip.setPixelColor(i, strip.Color(w, br, w));
        else if (currentState == STATE_B) strip.setPixelColor(i, strip.Color(w, w, br));
        else                              strip.setPixelColor(i, strip.Color(br, br, br));
    }
    strip.show();
    ledsActive = true;
}
