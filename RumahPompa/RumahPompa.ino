#define BLYNK_NO_YIELD      // Wajib untuk perbaikan error kompilasi Wi-Fi di ESP32 Core v3.x
#define BLYNK_PRINT Serial  // Mengaktifkan teks laporan bahasa Inggris bersih di monitor

#define BLYNK_TEMPLATE_ID   "TMPLxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Rumah pompa"
#define BLYNK_AUTH_TOKEN    "your-blynk-auth-token"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <time.h>
#include <esp_task_wdt.h>   // Library resmi Watchdog Timer

// --- KONEKSI PIN HARDWARE BEBAN RELAY ESP32 ---
const int PIN_RELAY_SUB       = 13; // Relay 1: Pompa Submersible (Pin A14)
const int PIN_RELAY_SIRAM     = 12; // Relay 2: Pompa Siram Kebun (Pin A15)
const int PIN_RELAY_SOLENOID  = 14; // Relay 3: Kran Solenoid Air Siram (Pin A16)
const int PIN_RELAY_BACKUP1   = 27; // Relay 4: Lampu Dalam Kebun (Pin A17)
const int PIN_RELAY_BACKUP2   = 26; // Relay 5: Lampu Luar Bedeng (Pin A19)
const int PIN_RELAY_CADANGAN1 = 23; // Relay 6: Tombol Cadangan 1 (Pin MISO) -> TAMBAHAN BARU
const int PIN_RELAY_CADANGAN2 = 18; // Relay 7: Tombol Cadangan 2 (Pin SCK)  -> TAMBAHAN BARU

// --- KONEKSI PIN HARDWARE SENSOR 3 KABEL (PIN AMAN) ---
const int PIN_SUMUR_LOW    = 32; // Batas bawah air sumur sibel
const int PIN_SUMUR_HIGH   = 4;  // Batas atas air sumur sibel
const int PIN_TANDON_BAWAH = 33; // Batas bawah air tandon
const int PIN_TANDON_ATAS  = 25; // Batas atas air tandon

// --- KONFIGURASI PIN VIRTUAL BLYNK (ANDROID) ---
#define VPIN_MONITOR_SUB      V1  
#define VPIN_MONITOR_SIRAM    V2  
#define VPIN_MONITOR_SOLENOID V3  
#define VPIN_STATUS_SUMUR     V4  
#define VPIN_STATUS_TANDON    V5  
#define VPIN_KONTROL_SUB      V6  
#define VPIN_KONTROL_SIRAM    V7  
#define VPIN_KONTROL_BACKUP1  V8  
#define VPIN_KONTROL_BACKUP2  V9  

// --- KONFIGURASI INTEGRASI BARU ---
const char* ssid = "your-wifi-name";
const char* password = "your-wifi-password";
unsigned long previousMillisWifi = 0;
const long intervalRecheckWifi = 30000; // Cek status koneksi setiap 30 detik
const int WDT_TIMEOUT = 15;             // Toleransi freeze maksimal 15 detik sebelum restart fisik

// --- VARIABEL PENYIMPAN WAKTU JADWAL JALUR INTERNET ---
int jamPagiMulai = 7,  menitPagiMulai = 0,  durasiPagiMenit = 15;
int jamSoreMulai = 16, menitSoreMulai = 30, durasiSoreMenit = 15;

// Variabel Status Kendali Operasional
bool modeManualSub = false;
bool modeManualSiram = false;
bool statusPompaSub = false;
bool statusPompaSiram = false;
bool statusSolenoid = false;

BlynkTimer timer;

// --- PENGAMBILAN DATA KONTROL MANUAL DARI ANDROID ---
BLYNK_WRITE(VPIN_KONTROL_SUB) { modeManualSub = param.asInt(); }
BLYNK_WRITE(VPIN_KONTROL_SIRAM) { modeManualSiram = param.asInt(); }

BLYNK_WRITE(VPIN_KONTROL_BACKUP1) {
  int statusLampuDalam = param.asInt();
  digitalWrite(PIN_RELAY_BACKUP1, statusLampuDalam == 1 ? LOW : HIGH); // Relay 4
}
BLYNK_WRITE(VPIN_KONTROL_BACKUP2) {
  int statusLampuLuar = param.asInt();
  digitalWrite(PIN_RELAY_BACKUP2, statusLampuLuar == 1 ? LOW : HIGH); // Relay 5
}

// --- BARIS INTERKONEKSI BARU UNTUK 2 SAKELAR CADANGAN ---
BLYNK_WRITE(V12) { 
  int statusCadangan1 = param.asInt();
  digitalWrite(PIN_RELAY_CADANGAN1, statusCadangan1 == 1 ? LOW : HIGH); // Relay 6
}
BLYNK_WRITE(V13) { 
  int statusCadangan2 = param.asInt();
  digitalWrite(PIN_RELAY_CADANGAN2, statusCadangan2 == 1 ? LOW : HIGH); // Relay 7
}

// --- MENERIMA DATA JADWAL JAM PENYIRAMAN BARU LANGSUNG DARI HP ANDROID ---
BLYNK_WRITE(V10) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    jamPagiMulai = t.getStartHour();
    menitPagiMulai = t.getStartMinute();
    Serial.printf("[JADWAL] Jam Siram Pagi Diperbarui HP: %02d:%02d\n", jamPagiMulai, menitPagiMulai);
  }
}

BLYNK_WRITE(V11) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    jamSoreMulai = t.getStartHour();
    menitSoreMulai = t.getStartMinute();
    Serial.printf("[JADWAL] Jam Siram Sore Diperbarui HP: %02d:%02d\n", jamSoreMulai, menitSoreMulai);
  }
}

// --- FUNGSI UTAMA KONTROL LOGIKA WATER MANAGEMENT ---
void prosesLogikaAir() {
  // Membaca data elektroda sensor (LOW artinya sensor menyentuh/terendam air)
  bool sumurKosong = (digitalRead(PIN_SUMUR_LOW) == HIGH);  
  bool sumurPenuh  = (digitalRead(PIN_SUMUR_HIGH) == LOW);  
  bool tandonKosong = (digitalRead(PIN_TANDON_BAWAH) == HIGH); 
  bool tandonPenuh  = (digitalRead(PIN_TANDON_ATAS) == LOW);   

  // A. LOGIKA UTAMA PROTEKSI DRY-RUNNING & REFILL SUMUR SIBEL
  if (sumurKosong) {
    statusPompaSub = false;
  } 
  else if (modeManualSub) {
    statusPompaSub = true;
  } 
  else {
    if (tandonKosong && sumurPenuh) {
      statusPompaSub = true;  
    }
    if (tandonPenuh) {
      statusPompaSub = false; 
    }
  }
  digitalWrite(PIN_RELAY_SUB, statusPompaSub ? LOW : HIGH);

  // B. LOGIKA POMPA SIRAM & KRAN SOLENOID (Sinkronisasi Jam Otomatis)
  struct tm timeinfo;
  bool rtcValid = getLocalTime(&timeinfo);
  bool waktuSiramPagi = false;
  bool waktuSiramSore = false;

  if (rtcValid) {
    int menitSekarang    = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
    int menitMulaiPagi   = (jamPagiMulai * 60) + menitPagiMulai;
    int menitSelesaiPagi = menitMulaiPagi + durationPagiMenit; // Mengikuti variabel typo bawaan Anda

    int menitMulaiSore   = (jamSoreMulai * 60) + menitSoreMulai;
    int menitSelesaiSore = menitMulaiSore + durasiSoreMenit;

    waktuSiramPagi = (menitSekarang >= menitMulaiPagi && menitSekarang < menitSelesaiPagi);
    waktuSiramSore = (menitSekarang >= menitMulaiSore && menitSekarang < menitSelesaiSore);
  }

  // Proteksi Pompa Siram (Mati otomatis jika air tandon berada di bawah batas bawah)
  if (tandonKosong && !modeManualSiram) {
    statusPompaSiram = false;
    statusSolenoid   = false;
  }
  else if (modeManualSiram) {
    statusPompaSiram = true;
    statusSolenoid   = true;
  }
  else {
    if (waktuSiramPagi || waktuSiramSore) {
      statusPompaSiram = true;
      statusSolenoid   = true; 
    } else {
      statusPompaSiram = false;
      statusSolenoid   = false; 
    }
  }
  digitalWrite(PIN_RELAY_SIRAM, statusPompaSiram ? LOW : HIGH);
  digitalWrite(PIN_RELAY_SOLENOID, statusSolenoid ? LOW : HIGH);

  // C. PENGIRIMAN TELEMETRI STATUS REAL-TIME KE HP (Hanya dikirim jika Blynk terhubung)
  if (WiFi.status() == WL_CONNECTED && Blynk.connected()) {
    Blynk.virtualWrite(VPIN_MONITOR_SUB, statusPompaSub ? 255 : 0);
    Blynk.virtualWrite(VPIN_MONITOR_SIRAM, statusPompaSiram ? 255 : 0);
    Blynk.virtualWrite(VPIN_MONITOR_SOLENOID, statusSolenoid ? 255 : 0);

    if (sumurKosong) {
      Blynk.virtualWrite(VPIN_STATUS_SUMUR, "SUMUR KERING (PROTEKSI)");
    } else if (sumurPenuh) {
      Blynk.virtualWrite(VPIN_STATUS_SUMUR, "SUMUR PENUH / AMAN");
    } else {
      Blynk.virtualWrite(VPIN_STATUS_SUMUR, "PENGISIAN DEBIT AIR");
    }

    if (tandonPenuh) {
      Blynk.virtualWrite(VPIN_STATUS_TANDON, "PENUH (100%)");
    } else if (tandonKosong) {
      Blynk.virtualWrite(VPIN_STATUS_TANDON, "KOSONG / REFILL (0%)");
    } else {
      Blynk.virtualWrite(VPIN_STATUS_TANDON, "TERISI SEBAGIAN (MID)");
    }
  }
}

// --- SETUP UTAMA ---
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Inisialisasi Hardware Output (Relay 1 sampai Relay 7)
  pinMode(PIN_RELAY_SUB, OUTPUT);
  pinMode(PIN_RELAY_SIRAM, OUTPUT);
  pinMode(PIN_RELAY_SOLENOID, OUTPUT);
  pinMode(PIN_RELAY_BACKUP1, OUTPUT);
  pinMode(PIN_RELAY_BACKUP2, OUTPUT);
  pinMode(PIN_RELAY_CADANGAN1, OUTPUT); // Diaktifkan
  pinMode(PIN_RELAY_CADANGAN2, OUTPUT); // Diaktifkan

  // Kondisi Awal: Semua relay MATI (Active High off = HIGH)
  digitalWrite(PIN_RELAY_SUB, HIGH);
  digitalWrite(PIN_RELAY_SIRAM, HIGH);
  digitalWrite(PIN_RELAY_SOLENOID, HIGH);
  digitalWrite(PIN_RELAY_BACKUP1, HIGH);
  digitalWrite(PIN_RELAY_BACKUP2, HIGH);
  digitalWrite(PIN_RELAY_CADANGAN1, HIGH); // Diaktifkan awal OFF
  digitalWrite(PIN_RELAY_CADANGAN2, HIGH); // Diaktifkan awal OFF

  // Inisialisasi Hardware Input (Sensor Elektroda)
  pinMode(PIN_SUMUR_LOW, INPUT_PULLUP);
  pinMode(PIN_SUMUR_HIGH, INPUT_PULLUP);
  pinMode(PIN_TANDON_BAWAH, INPUT_PULLUP);
  pinMode(PIN_TANDON_ATAS, INPUT_PULLUP);

  // --- KOMPATIBILITAS OTOMATIS WATCHDOG TIMER ---
  Serial.println("\n[SYSTEM] Mengonfigurasi Watchdog Timer...");
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT * 1000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
  #else
    esp_task_wdt_init(WDT_TIMEOUT, true);
  #endif
  esp_task_wdt_add(NULL); // Daftarkan loop utama ke pengawasan WDT

  // --- KONEKSI Wi-Fi AWAL ---
  Serial.println("[SISTEM] Mencoba masuk ke jaringan Wi-Fi...");
  WiFi.begin(ssid, password); 

  int timeoutCounter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    timeoutCounter++;
    if(timeoutCounter > 20) { // Jika 10 detik gagal, loloskan agar logika offline lokal tetap jalan
      Serial.println("\n[SISTEM] Wi-Fi Lewat Batas Waktu. Berjalan Mode Offline.");
      break;
    }
  }

  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[SISTEM] Wi-Fi Sukses Terhubung!");
    Serial.print("[SISTEM] Alamat IP ESP32: ");
    Serial.println(WiFi.localIP());
  }

  // Menghubungkan ke Blynk Cloud
  Blynk.config(BLYNK_AUTH_TOKEN, "sgp1.blynk.cloud", 80);
  Blynk.connect();

  // Mengaktifkan Jam NTP Dunia Indonesia (GMT+7)
  configTime(25200, 0, "id.pool.ntp.org"); 

  // Menjalankan pengecekan sensor berkala setiap 2 detik
  timer.setInterval(2000L, prosesLogikaAir);
}

// --- LOOP UTAMA ---
void loop() {
  esp_task_wdt_reset(); // Memberi makan Watchdog tanda sistem normal bekerja

  // Menjalankan Blynk hanya jika koneksi internet terjalin
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }
  timer.run();
}
