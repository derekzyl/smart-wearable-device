/**
 * Smart Health Watch - ESP32-S3 Firmware
 * 
 * Main firmware for wearable health monitoring device
 * Features:
 * - Heart rate & SpO₂ monitoring (MAX30102)
 * - Body temperature sensing (MAX30205)
 * - Motion tracking (MPU6050 IMU)
 * - OLED display (SSD1306)
 * - Bluetooth Low Energy (BLE) data streaming
 * - Wi-Fi sync to backend API
 * - Low-power deep sleep modes
 */

#include <Arduino.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Time.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "Adafruit_MPU6050.h"
#include "MAX30205.h"

// ==================== Hardware Pin Definitions ====================
#define I2C_SDA 21
#define I2C_SCL 22
#define OLED_RESET -1
#define BUTTON_1 0
#define BUTTON_2 35
#define VIBRATION_MOTOR 25
#define CHARGING_LED 26
#define STATUS_LED 27

// ==================== Display Configuration ====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== Sensor Objects ====================
MAX30105 particleSensor;
Adafruit_MPU6050 mpu;
MAX30205 tempSensor;

// ==================== BLE Configuration ====================
#define SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define CHAR_HEART_RATE_UUID "00002a37-0000-1000-8000-00805f9b34fb"
#define CHAR_SPO2_UUID "12345678-1234-1234-1234-123456789abd"
#define CHAR_TEMPERATURE_UUID "12345678-1234-1234-1234-123456789abe"
#define CHAR_IMU_UUID "12345678-1234-1234-1234-123456789abf"
#define CHAR_BATTERY_UUID "00002a19-0000-1000-8000-00805f9b34fb"

BLEServer* pServer = nullptr;
BLECharacteristic* pCharHeartRate = nullptr;
BLECharacteristic* pCharSpO2 = nullptr;
BLECharacteristic* pCharTemperature = nullptr;
BLECharacteristic* pCharIMU = nullptr;
BLECharacteristic* pCharBattery = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;

// ==================== Sensor Data Structures ====================
struct VitalsData {
  float heartRate = 0.0;
  float spo2 = 0.0;
  float temperature = 0.0;
  float batteryLevel = 100.0;
  uint32_t timestamp = 0;
};

struct IMUData {
  float accelX = 0.0;
  float accelY = 0.0;
  float accelZ = 0.0;
  float gyroX = 0.0;
  float gyroY = 0.0;
  float gyroZ = 0.0;
  uint32_t steps = 0;
};

VitalsData vitals;
IMUData imuData;

// ==================== Heart Rate Calculation ====================
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;

// ==================== Wi-Fi Configuration ====================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* backendUrl = "http://your-backend-api.com/api/v1/vitals/upload";

// ==================== Power Management ====================
const unsigned long DEEP_SLEEP_INTERVAL = 15 * 60 * 1000; // 15 minutes
unsigned long lastSyncTime = 0;
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000; // 1 second

// ==================== RTC ====================
ESP32Time rtc;

// ==================== BLE Server Callbacks ====================
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    digitalWrite(STATUS_LED, HIGH);
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    digitalWrite(STATUS_LED, LOW);
  }
};

// ==================== Function Prototypes ====================
void initSensors();
void initDisplay();
void initBLE();
void initWiFi();
void updateHeartRate();
void updateSpO2();
void updateTemperature();
void updateIMU();
void updateDisplay();
void sendBLEData();
void syncToBackend();
float readBatteryLevel();
void vibrate(int duration);
void enterDeepSleep();

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize pins
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(VIBRATION_MOTOR, OUTPUT);
  pinMode(CHARGING_LED, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);

  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000); // Fast I2C (400kHz)

  // Initialize sensors
  initSensors();
  
  // Initialize display
  initDisplay();
  
  // Initialize BLE
  initBLE();
  
  // Initialize WiFi (optional, for backend sync)
  // initWiFi();

  // Initialize RTC
  rtc.setTime(0); // Will be synced from backend/NTP

  Serial.println("Smart Health Watch initialized!");
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println("Health Watch");
  display.setTextSize(1);
  display.setCursor(20, 45);
  display.println("Initializing...");
  display.display();
  delay(2000);
}

// ==================== Main Loop ====================
void loop() {
  unsigned long currentMillis = millis();

  // Update sensors
  updateHeartRate();
  updateSpO2();
  updateTemperature();
  updateIMU();
  readBatteryLevel();

  // Update display
  if (currentMillis - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplay();
    lastDisplayUpdate = currentMillis;
  }

  // Send BLE data if connected
  if (deviceConnected) {
    sendBLEData();
  }

  // Handle BLE connection state
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  // Sync to backend periodically (every 15 minutes)
  if (WiFi.status() == WL_CONNECTED && 
      (currentMillis - lastSyncTime >= DEEP_SLEEP_INTERVAL || lastSyncTime == 0)) {
    syncToBackend();
    lastSyncTime = currentMillis;
  }

  // Check for abnormal readings and trigger alerts
  if (vitals.heartRate > 120 || vitals.heartRate < 50) {
    vibrate(200);
  }
  if (vitals.spo2 < 90) {
    vibrate(500);
  }
  if (vitals.temperature > 38.0 || vitals.temperature < 35.0) {
    vibrate(300);
  }

  // Button handling
  if (digitalRead(BUTTON_1) == LOW) {
    // Toggle display mode
    delay(200); // Debounce
  }

  delay(100); // Small delay for stability
}

// ==================== Initialize Sensors ====================
void initSensors() {
  Serial.println("Initializing sensors...");

  // MAX30102 Heart Rate & SpO₂ Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found. Please check wiring/power.");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);
  Serial.println("MAX30102 initialized");

  // MPU6050 IMU
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Please check wiring.");
    while (1);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("MPU6050 initialized");

  // MAX30205 Temperature Sensor
  if (!tempSensor.begin()) {
    Serial.println("MAX30205 not found. Please check wiring.");
    while (1);
  }
  Serial.println("MAX30205 initialized");
}

// ==================== Initialize Display ====================
void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }
  display.clearDisplay();
  display.display();
  Serial.println("OLED display initialized");
}

// ==================== Initialize BLE ====================
void initBLE() {
  BLEDevice::init("Smart Health Watch");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Heart Rate Characteristic
  pCharHeartRate = pService->createCharacteristic(
    CHAR_HEART_RATE_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharHeartRate->addDescriptor(new BLE2902());

  // SpO₂ Characteristic
  pCharSpO2 = pService->createCharacteristic(
    CHAR_SPO2_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharSpO2->addDescriptor(new BLE2902());

  // Temperature Characteristic
  pCharTemperature = pService->createCharacteristic(
    CHAR_TEMPERATURE_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharTemperature->addDescriptor(new BLE2902());

  // IMU Data Characteristic
  pCharIMU = pService->createCharacteristic(
    CHAR_IMU_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharIMU->addDescriptor(new BLE2902());

  // Battery Level Characteristic
  pCharBattery = pService->createCharacteristic(
    CHAR_BATTERY_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharBattery->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("BLE initialized - Waiting for client connection...");
}

// ==================== Initialize WiFi ====================
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed. Continuing with BLE only.");
  }
}

// ==================== Update Heart Rate ====================
void updateHeartRate() {
  long irValue = particleSensor.getIR();
  
  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60 / (delta / 1000.0);
    
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++) {
        beatAvg += rates[x];
      }
      beatAvg /= RATE_SIZE;
    }
  }
  
  vitals.heartRate = beatsPerMinute > 20 ? beatsPerMinute : beatAvg;
  vitals.timestamp = rtc.getEpoch();
}

// ==================== Update SpO₂ ====================
void updateSpO2() {
  long redValue = particleSensor.getRed();
  long irValue = particleSensor.getIR();
  
  // Simple SpO₂ calculation (simplified algorithm)
  // Real implementation would use more sophisticated PPG analysis
  float ratio = (float)redValue / (float)irValue;
  vitals.spo2 = 110.0 - 25.0 * ratio;
  
  // Clamp to valid range
  if (vitals.spo2 > 100) vitals.spo2 = 100;
  if (vitals.spo2 < 70) vitals.spo2 = 95; // Default if invalid
}

// ==================== Update Temperature ====================
void updateTemperature() {
  vitals.temperature = tempSensor.getTemperature();
}

// ==================== Update IMU ====================
void updateIMU() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  imuData.accelX = a.acceleration.x;
  imuData.accelY = a.acceleration.y;
  imuData.accelZ = a.acceleration.z;
  imuData.gyroX = g.gyro.x;
  imuData.gyroY = g.gyro.y;
  imuData.gyroZ = g.gyro.z;
  
  // Simple step detection (magnitude threshold)
  float magnitude = sqrt(a.acceleration.x*a.acceleration.x + 
                         a.acceleration.y*a.acceleration.y + 
                         a.acceleration.z*a.acceleration.z);
  static float lastMagnitude = 0;
  static unsigned long lastStepTime = 0;
  
  if (abs(magnitude - lastMagnitude) > 2.0 && 
      (millis() - lastStepTime) > 300) {
    imuData.steps++;
    lastStepTime = millis();
  }
  lastMagnitude = magnitude;
}

// ==================== Update Display ====================
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Heart Rate
  display.setCursor(0, 0);
  display.print("HR: ");
  display.print((int)vitals.heartRate);
  display.println(" bpm");
  
  // SpO₂
  display.setCursor(0, 12);
  display.print("SpO2: ");
  display.print((int)vitals.spo2);
  display.println("%");
  
  // Temperature
  display.setCursor(0, 24);
  display.print("Temp: ");
  display.print(vitals.temperature, 1);
  display.println(" C");
  
  // Steps
  display.setCursor(0, 36);
  display.print("Steps: ");
  display.println(imuData.steps);
  
  // Battery
  display.setCursor(0, 48);
  display.print("Battery: ");
  display.print((int)vitals.batteryLevel);
  display.println("%");
  
  // Connection status
  display.setCursor(90, 0);
  if (deviceConnected) {
    display.println("BLE");
  } else {
    display.println("---");
  }
  
  display.display();
}

// ==================== Send BLE Data ====================
void sendBLEData() {
  if (deviceConnected) {
    // Heart Rate
    uint8_t hrData[2] = {(uint8_t)vitals.heartRate, 0};
    pCharHeartRate->setValue(hrData, 2);
    pCharHeartRate->notify();
    
    // SpO₂
    uint8_t spo2Data[1] = {(uint8_t)vitals.spo2};
    pCharSpO2->setValue(spo2Data, 1);
    pCharSpO2->notify();
    
    // Temperature
    uint8_t tempData[2];
    int16_t tempInt = (int16_t)(vitals.temperature * 100);
    tempData[0] = (uint8_t)(tempInt & 0xFF);
    tempData[1] = (uint8_t)((tempInt >> 8) & 0xFF);
    pCharTemperature->setValue(tempData, 2);
    pCharTemperature->notify();
    
    // IMU Data (JSON format)
    StaticJsonDocument<200> doc;
    doc["ax"] = imuData.accelX;
    doc["ay"] = imuData.accelY;
    doc["az"] = imuData.accelZ;
    doc["gx"] = imuData.gyroX;
    doc["gy"] = imuData.gyroY;
    doc["gz"] = imuData.gyroZ;
    doc["steps"] = imuData.steps;
    String imuJson;
    serializeJson(doc, imuJson);
    pCharIMU->setValue(imuJson.c_str());
    pCharIMU->notify();
    
    // Battery Level
    uint8_t batteryData[1] = {(uint8_t)vitals.batteryLevel};
    pCharBattery->setValue(batteryData, 1);
    pCharBattery->notify();
    
    delay(10); // Small delay to prevent BLE buffer overflow
  }
}

// ==================== Sync to Backend ====================
void syncToBackend() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  HTTPClient http;
  http.begin(backendUrl);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<512> doc;
  doc["user_id"] = "device_001"; // Should be configurable
  doc["timestamp"] = vitals.timestamp;
  doc["heart_rate"] = vitals.heartRate;
  doc["spo2"] = vitals.spo2;
  doc["temperature"] = vitals.temperature;
  doc["steps"] = imuData.steps;
  doc["accel_x"] = imuData.accelX;
  doc["accel_y"] = imuData.accelY;
  doc["accel_z"] = imuData.accelZ;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  if (httpResponseCode > 0) {
    Serial.print("Backend sync successful: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Backend sync failed: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
}

// ==================== Read Battery Level ====================
float readBatteryLevel() {
  // Simple voltage divider reading (if battery connected to ADC)
  // For now, using a placeholder calculation
  // In real implementation, use ADC pin with voltage divider
  int adcValue = analogRead(34); // GPIO34 is ADC1_CH6
  float voltage = (adcValue / 4095.0) * 3.3 * 2; // Assuming 2:1 divider
  vitals.batteryLevel = ((voltage - 3.0) / 1.2) * 100; // 3.0V = 0%, 4.2V = 100%
  
  if (vitals.batteryLevel > 100) vitals.batteryLevel = 100;
  if (vitals.batteryLevel < 0) vitals.batteryLevel = 0;
  
  return vitals.batteryLevel;
}

// ==================== Vibrate ====================
void vibrate(int duration) {
  digitalWrite(VIBRATION_MOTOR, HIGH);
  delay(duration);
  digitalWrite(VIBRATION_MOTOR, LOW);
}

// ==================== Enter Deep Sleep ====================
void enterDeepSleep() {
  Serial.println("Entering deep sleep...");
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(10, 30);
  display.println("Sleeping...");
  display.display();
  delay(1000);
  
  // Configure wake-up sources
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_INTERVAL * 1000); // Convert to microseconds
  
  // Enter deep sleep
  esp_deep_sleep_start();
}
