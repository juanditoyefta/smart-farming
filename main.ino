#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ModbusMaster.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <UrlEncode.h>

String phoneNumber = "6288227689571"; //country_code + phone number
String apiKey = "&apikey=3204650";

// Definisi WiFi
const char* ssid = "YEFTA";
const char* password = "085705652088";

// Definisi MQTT
const char* mqttServer = "8.215.56.179";
const int mqttPort = 1883;
const char* mqttUser = "edspert_batch2";
const char* mqttPassword = "edspert_batch2";
const char* mqttTopicNumbers = "edspert/juandito/data/numbers";
const char* mqttTopicTexts = "edspert/juandito/data/texts";

// Definisi Pin
#define LDR_PIN 33
#define SOIL_MOISTURE_PIN 35
#define WATER_SENSOR_PIN 34
#define SHT20_RX_PIN 16
#define SHT20_TX_PIN 17
#define SDA_PIN 21
#define SCL_PIN 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Output Pin
#define LED_PIN 2
#define BUZZER_PIN 15
#define PUMP_PIN 13
#define FAN_PIN 12

// Threshold
#define MOISTURE_THRESHOLD 1500
#define WATER_THRESHOLD 500
#define TEMPERATURE_THRESHOLD 34.0

// Inisialisasi komponen
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ModbusMaster node;
WiFiClient espClient;
PubSubClient client(espClient);

// Fungsi intensitas cahaya (Lux)
float intensitasCahaya(int adcValue) {
  const float GAMMA = 0.7;
  const float RL10 = 33;
  float voltage = adcValue / 4095.0 * 3.3;
  float resistance = (3.3 - voltage > 0) ? (2000 * voltage / (3.3 - voltage)) : 1; // Hindari pembagian nol
  if (resistance <= 0) resistance = 1; // Pastikan resistance valid
  float lux = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1.0 / GAMMA));
  return lux;
}

// Fungsi untuk terhubung ke WiFi
void connectToWiFi() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Menghubungkan ke WiFi...");
  display.display();

  Serial.print("Menghubungkan ke WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi terhubung");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Terhubung!");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
}

// Fungsi untuk terhubung ke MQTT broker
void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke MQTT...");
    if (client.connect("ESP32_Client", mqttUser, mqttPassword)) {
      Serial.println("Terhubung ke MQTT");
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" Coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Inisialisasi OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED gagal diinisialisasi"));
    while (true);
  }
  display.clearDisplay();
  display.display();

  // Inisialisasi WiFi dan MQTT
  connectToWiFi();
  client.setServer(mqttServer, mqttPort);

  // Inisialisasi Modbus
  Serial2.begin(9600, SERIAL_8N1, SHT20_RX_PIN, SHT20_TX_PIN);
  node.begin(1, Serial2);

  // Inisialisasi pin sensor dan output
  pinMode(LDR_PIN, INPUT);
  pinMode(SOIL_MOISTURE_PIN, INPUT);
  pinMode(WATER_SENSOR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
}

// Threshold baru untuk kebocoran air (sesuaikan nilainya sesuai kebutuhan)
#define LEAKAGE_THRESHOLD 100

void loop() {
  if (!client.connected()) {
    connectToMQTT();
  }
  client.loop();

  // Membaca sensor
  int ldrValue = analogRead(LDR_PIN);
  int soilMoistureValue = analogRead(SOIL_MOISTURE_PIN);
  int waterSensorValue = analogRead(WATER_SENSOR_PIN);
  float lux = intensitasCahaya(ldrValue);

  if (isnan(lux)) {
    lux = 0; // Set nilai default jika lux tidak valid
  }

  // Membaca suhu dan kelembapan dari SHT20
  uint8_t result = node.readInputRegisters(0x0001, 1);
  float temperature = (result == node.ku8MBSuccess) ? node.getResponseBuffer(0) / 10.0 : 0;

  result = node.readInputRegisters(0x0002, 1);
  float humidity = (result == node.ku8MBSuccess) ? node.getResponseBuffer(0) / 10.0 : 0;

  // Mengontrol output berdasarkan data sensor
  digitalWrite(FAN_PIN, (temperature > TEMPERATURE_THRESHOLD) ? LOW : HIGH);
  digitalWrite(PUMP_PIN, (soilMoistureValue > MOISTURE_THRESHOLD) ? LOW : HIGH);
  digitalWrite(LED_PIN, (lux < 500) ? HIGH : LOW);

  if (waterSensorValue > LEAKAGE_THRESHOLD) { 
    digitalWrite(BUZZER_PIN, HIGH); // Buzzer berbunyi jika ada kebocoran 
    sendAlert("Ada kebocoran air");
    } else { 
      digitalWrite(BUZZER_PIN, LOW); // Buzzer mati jika tidak ada kebocoran 
    }

  // Interpretasi data untuk teks
  String soilMoistureStatus = (soilMoistureValue > MOISTURE_THRESHOLD) ? "Tanah Kering" : "Tanah Lembab";
  String temperatureStatus = (temperature > TEMPERATURE_THRESHOLD) ? "Suhu Tinggi" : "Suhu Normal";
  String lightStatus = (lux > 1000) ? "Cahaya Terang" : (lux > 500) ? "Cahaya Sedang" : "Cahaya Redup";
  String waterStatus = (waterSensorValue > LEAKAGE_THRESHOLD) ? "Kebocoran Terdeteksi" : "Tidak Ada Kebocoran";

  // Membuat JSON untuk data angka dan teks
  StaticJsonDocument<256> jsonDataNumbers;
  jsonDataNumbers["lux"] = lux;
  jsonDataNumbers["soil_moisture"] = soilMoistureValue;
  jsonDataNumbers["temperature"] = temperature;
  jsonDataNumbers["humidity"] = humidity;
  jsonDataNumbers["water_level"] = waterSensorValue;

  char jsonBufferNumbers[256];
  serializeJson(jsonDataNumbers, jsonBufferNumbers);

  StaticJsonDocument<256> jsonDataTexts;
  jsonDataTexts["lux"] = lightStatus;
  jsonDataTexts["soil_moisture"] = soilMoistureStatus;
  jsonDataTexts["temperature"] = temperatureStatus;
  jsonDataTexts["humidity"] = (humidity > 50.0) ? "Kelembapan Tinggi" : "Kelembapan Normal";
  jsonDataTexts["water_level"] = waterStatus;

  char jsonBufferTexts[256];
  serializeJson(jsonDataTexts, jsonBufferTexts);

  // Mengirim data ke MQTT
  client.publish(mqttTopicNumbers, jsonBufferNumbers);
  client.publish(mqttTopicTexts, jsonBufferTexts);

  // Menampilkan data teks ke OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Data Sensor:");
  display.print("Cahaya: ");
  display.println(lightStatus);
  display.print("Tanah: ");
  display.println(soilMoistureStatus);
  display.print("Suhu: ");
  display.println(temperatureStatus);
  display.print("Kelembapan: ");
  display.println((humidity > 50.0) ? "Tinggi" : "Normal");
  display.print("Air: ");
  display.println(waterStatus);
  display.display();

  delay(5000);
}

void sendAlert(String message) {

  // Data to send with HTTP POST
  String url = "https://api.callmebot.com/whatsapp.php?phone=6288227689571&text=" + phoneNumber + "&apikey=" + apiKey + "&text=" + urlEncode(message);
  HTTPClient http;
  http.begin(url);

  // Specify content-type header
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Send HTTP POST request
  int httpResponseCode = http.POST(url);
  if (httpResponseCode == 200) {
    Serial.print("Message sent successfully");
  }
  else {
    Serial.println("Error sending the message");
    Serial.print("HTTP response code: ");
    Serial.println(httpResponseCode);
  }

  // Free resources
  http.end();
}