#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// --- Konfigurasi WiFi & MQTT ---
const char* ssid = "pocky";
const char* password = "ayamgeprek";
const char* mqtt_server = "broker.emqx.io";

WiFiClient espClient;
PubSubClient client(espClient);
Servo servoPintu;

// --- Definisi Pin ---
#define WATER_SENSOR 35
#define BUZZER 19
#define PINTU_PIN 18

// --- Variabel Kendali Waktu (Millis) ---
unsigned long lastUpdate = 0;
const long interval = 1000;    // Interval baca sensor (1 detik)

unsigned long lastBuzzerMillis = 0;
bool buzzerState = false;

unsigned long lastServoMillis = 0;
const int servoSpeed = 5;     // Kecepatan gerak (ms per derajat)

// --- Variabel Status Logika ---
bool statusManual = false;     // Perintah dari Kodular
int targetPosisi = 0;          // Target sudut servo (0, 90, 180)
int posisiSekarang = 0;        // Posisi aktual servo

// Fungsi Callback MQTT
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  
  Serial.print("Pesan Masuk: ");
  Serial.println(message);

  if (String(topic) == "posttest3/iot/control") {
    if (message == "ON") statusManual = true;
    else if (message == "OFF") statusManual = false;
  }
}

void setup_wifi() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected");
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32_Water_System_Final")) {
      client.subscribe("posttest3/iot/control");
    } else { delay(3000); }
  }
}

String getStatus(int value) {
  if (value < 800) return "Aman";
  else if (value <= 1500) return "Waspada";
  else return "Bahaya";
}

// --- Fungsi Eksekusi Hardware (Non-Blocking) ---

void handleBuzzer(bool aktif, int speed) {
  if (aktif) {
    if (millis() - lastBuzzerMillis >= speed) {
      lastBuzzerMillis = millis();
      buzzerState = !buzzerState;
      digitalWrite(BUZZER, buzzerState);
    }
  } else {
    digitalWrite(BUZZER, LOW);
    buzzerState = false;
  }
}

void handleServo() {
  if (millis() - lastServoMillis >= servoSpeed) {
    lastServoMillis = millis();
    if (posisiSekarang < targetPosisi) {
      posisiSekarang++;
      servoPintu.write(posisiSekarang);
    } else if (posisiSekarang > targetPosisi) {
      posisiSekarang--;
      servoPintu.write(posisiSekarang);
    }
  }
}

void setup() {
  pinMode(BUZZER, OUTPUT);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  servoPintu.attach(PINTU_PIN);
  servoPintu.write(posisiSekarang); // Inisialisasi posisi 0
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop(); 

  unsigned long currentMillis = millis();

  // 1. LOGIKA UTAMA (Berjalan setiap 1 detik)
  if (currentMillis - lastUpdate >= interval) {
    lastUpdate = currentMillis;

    int sensorValue = analogRead(WATER_SENSOR);
    String statusAir = getStatus(sensorValue);
    
    // Logika Hybrid: Sistem aktif jika (Sensor Bahaya/Waspada) ATAU (Manual ON)
    bool statusOtomatis = (statusAir != "Aman");
    bool sistemAktif = (statusOtomatis || statusManual);

    if (sistemAktif) {
      // Prioritas Bahaya atau Paksa ON manual saat Aman
      if (statusAir == "Bahaya" || (statusManual && statusAir == "Aman")) {
        targetPosisi = 180;
        client.publish("posttest3/iot/buzzer", "ON");
        client.publish("posttest3/iot/pintu", "180");
      } 
      else if (statusAir == "Waspada") {
        targetPosisi = 90;
        client.publish("posttest3/iot/buzzer", "OFF");
        client.publish("posttest3/iot/pintu", "90");
      }
    } else {
      // Kondisi Aman dan Manual OFF
      targetPosisi = 0;
      client.publish("posttest3/iot/buzzer", "OFF");
      client.publish("posttest3/iot/pintu", "0");
    }

    // Monitoring
    Serial.printf("Sensor: %d | Status: %s | Manual: %s | Target: %d\n", 
                  sensorValue, statusAir.c_str(), statusManual ? "ON" : "OFF", targetPosisi);

    client.publish("posttest3/iot/nilai", String(sensorValue).c_str());
    client.publish("posttest3/iot/statusair", statusAir.c_str());
  }

  // 2. EKSEKUSI HARDWARE (Berjalan real-time tanpa delay)
  
  // Kontrol Servo
  handleServo();

  // Kontrol Buzzer
  int sensorSekarang = analogRead(WATER_SENSOR);
  String statusSekarang = getStatus(sensorSekarang);
  
  // Buzzer hanya kedip jika Bahaya atau Manual ON saat Aman
  bool perluBunyi = (statusSekarang == "Bahaya" || (statusManual && statusSekarang == "Aman"));
  handleBuzzer(perluBunyi, 1000);
}