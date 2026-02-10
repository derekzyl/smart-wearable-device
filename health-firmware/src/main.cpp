/**
 * Multi-Vitals Health Monitoring System
 * ESP32 Firmware for HR, SpO2, and Temperature Monitoring
 * 
 * Features:
 * - Heart Rate monitoring from Sen-11574 PPG sensor
 * - SpO2 (blood oxygen) monitoring from Sen-11574
 * - Temperature with DS18B20 failover to Liebermeister's Rule
 * - 20x4 LCD display with 4 screens
 * - WiFi cloud sync to /health/vitals endpoint
 * - Critical alert LED system
 * - Battery monitoring
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

// ==================== PIN DEFINITIONS ====================
#define SDA_PIN 21
#define SCL_PIN 22
#define DS18B20_PIN 4
#define SEN11574_PIN 34        // ADC1_CH6
#define BATTERY_ADC_PIN 35     // ADC1_CH7
#define STATUS_LED 2           // Built-in LED
#define ALERT_LED 5            // External Red LED

// ==================== CONFIGURATION ====================
const char* WIFI_SSID = "cybergenii";
const char* WIFI_PASSWORD = "12341234";
String API_BASE_URL = "https://xenophobic-netta-cybergenii-1584fde7.koyeb.app";
const char* VITALS_ENDPOINT = "/health/vitals";

// ==================== HARDWARE OBJECTS ====================
OneWire oneWire(DS18B20_PIN);
DallasTemperature dallas(&oneWire);
LiquidCrystal_I2C lcd(0x27, 20, 4);  // Try 0x3F if 0x27 doesn't work
Preferences preferences;

// ==================== GLOBAL VARIABLES ====================
String deviceID;
int currentScreen = 0;
unsigned long lastScreenChange = 0;
bool manualScreenLock = false;

// ==================== VITAL SIGNS STRUCTURE ====================
struct VitalSigns {
    int heartRate = 0;
    int hrQuality = 0;
    int spo2 = 0;
    int spo2Quality = 0;
    float temperature = 36.5;
    bool tempEstimated = false;
    String tempSource = "UNKNOWN";
    int batteryPercent = 100;
    float batteryVoltage = 3.7;
    
    bool hasAlert = false;
    String alertMessage = "";
    bool isCriticalAlert = false;
};

VitalSigns currentVitals;

// ==================== PULSE SENSOR CLASS (SEN-11574) ====================
class PulseSensor {
private:
    static const int SAMPLE_RATE = 500;  // Hz
    static const int WINDOW_SIZE = 100;   // samples (~200ms window)
    
    int signalBuffer[WINDOW_SIZE];
    int bufferIndex = 0;
    
    unsigned long lastBeatTime = 0;
    int bpm = 0;
    int rrIntervals[10];
    int rrIndex = 0;
    
    float dcComponent = 0;
    float acComponent = 0;
    float spo2 = 0;
    
    int signalQuality = 0;
    int threshold = 2000;
    
    unsigned long lastSampleTime = 0;
    
public:
    void begin() {
        pinMode(SEN11574_PIN, INPUT);
        for (int i = 0; i < WINDOW_SIZE; i++) {
            signalBuffer[i] = 0;
        }
    }
    
    void update() {
        // Sample at 500Hz (every 2ms)
        unsigned long now = millis();
        if (now - lastSampleTime < 2) return;
        lastSampleTime = now;
        
        // Read analog value (0-4095 on ESP32)
        int rawSignal = analogRead(SEN11574_PIN);
        
        // Store in circular buffer
        signalBuffer[bufferIndex] = rawSignal;
        bufferIndex = (bufferIndex + 1) % WINDOW_SIZE;
        
        // Detect heartbeat
        detectBeat(rawSignal);
        
        // Calculate SpO2
        calculateSpO2(rawSignal);
        
        // Update signal quality
        updateSignalQuality();
    }
    
    void detectBeat(int signal) {
        static int lastSignal = 0;
        static bool risingEdge = false;
        
        // Peak detection
        if (signal > threshold && lastSignal <= threshold) {
            risingEdge = true;
        }
        
        if (risingEdge && signal < threshold) {
            // Beat detected!
            unsigned long now = millis();
            unsigned long beatInterval = now - lastBeatTime;
            
            // Validate (30-200 BPM range = 300-2000ms interval)
            if (beatInterval > 300 && beatInterval < 2000 && lastBeatTime > 0) {
                bpm = 60000 / beatInterval;
                
                // Store RR interval for HRV
                rrIntervals[rrIndex] = beatInterval;
                rrIndex = (rrIndex + 1) % 10;
                
                lastBeatTime = now;
            } else if (lastBeatTime == 0) {
                lastBeatTime = now;
            }
            
            risingEdge = false;
        }
        
        lastSignal = signal;
        
        // Auto-adjust threshold every second
        static unsigned long lastThresholdUpdate = 0;
        if (millis() - lastThresholdUpdate > 1000) {
            adjustThreshold();
            lastThresholdUpdate = millis();
        }
    }
    
    void adjustThreshold() {
        // Find min/max in buffer
        int minVal = 4095, maxVal = 0;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (signalBuffer[i] < minVal) minVal = signalBuffer[i];
            if (signalBuffer[i] > maxVal) maxVal = signalBuffer[i];
        }
        // Set threshold at 70% of range
        threshold = minVal + (maxVal - minVal) * 0.7;
    }
    
    void calculateSpO2(int signal) {
        // SpO2 estimation from single-wavelength PPG
        // Note: This is an approximation - dual-wavelength is more accurate
        
        // Calculate DC component (baseline) with low-pass filter
        static float dcFilter = 2000;
        dcFilter = dcFilter * 0.95 + signal * 0.05;
        dcComponent = dcFilter;
        
        // Calculate AC component (pulsatile)
        acComponent = signal - dcComponent;
        
        // Calculate AC/DC ratio
        if (dcComponent > 100) {  // Avoid division by zero
            float ratio = abs(acComponent) / dcComponent;
            
            // Empirical formula for SpO2 estimation
            // This is a simplified model - adjust based on calibration
            spo2 = 110 - 25 * ratio;
            
            // Clamp to valid range
            if (spo2 > 100) spo2 = 100;
            if (spo2 < 70) spo2 = 70;
        } else {
            spo2 = 0;  // Invalid reading
        }
    }
    
    void updateSignalQuality() {
        // Calculate signal quality based on:
        // 1. Signal amplitude
        // 2. Consistency of beats
        // 3. Noise level
        
        int minVal = 4095, maxVal = 0;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (signalBuffer[i] < minVal) minVal = signalBuffer[i];
            if (signalBuffer[i] > maxVal) maxVal = signalBuffer[i];
        }
        int amplitude = maxVal - minVal;
        
        // Quality score based on amplitude
        if (amplitude > 1000) {
            signalQuality = 95;
        } else if (amplitude > 500) {
            signalQuality = 75;
        } else if (amplitude > 200) {
            signalQuality = 50;
        } else {
            signalQuality = 25;
        }
        
        // Reduce quality if no recent beats
        if (millis() - lastBeatTime > 3000) {
            signalQuality = 0;
            bpm = 0;  // Invalidate BPM
        }
    }
    
    int getBPM() {
        // Return 0 if no beat in last 3 seconds
        if (millis() - lastBeatTime > 3000) {
            return 0;
        }
        return bpm;
    }
    
    int getSpO2() {
        // Return SpO2 percentage
        if (signalQuality < 50) {
            return 0;  // Signal too poor for reliable SpO2
        }
        return (int)spo2;
    }
    
    int getSignalQuality() {
        return signalQuality;
    }
    
    int getSpO2Quality() {
        // SpO2 needs higher quality signal than HR
        if (signalQuality > 70) return 90;
        if (signalQuality > 50) return 60;
        return 30;
    }
    
    bool isValidReading() {
        return signalQuality > 50 && (millis() - lastBeatTime < 3000);
    }
};

// ==================== TEMPERATURE SENSOR CLASS ====================
class TemperatureSensor {
private:
    DallasTemperature& ds18b20;
    bool sensorAvailable = true;
    unsigned long lastCheck = 0;
    float restingHR = 70.0;
    
public:
    TemperatureSensor(DallasTemperature& sensor) : ds18b20(sensor) {}
    
    void begin() {
        ds18b20.begin();
        // Load resting HR from preferences
        restingHR = preferences.getFloat("resting_hr", 70.0);
    }
    
    struct TempReading {
        float celsius;
        bool isEstimated;
        String source;
    };
    
    TempReading getTemperature(float currentHR) {
        TempReading result;
        
        // Try DS18B20 every 10 seconds
        if (millis() - lastCheck > 10000 || lastCheck == 0) {
            ds18b20.requestTemperatures();
            delay(100);  // Wait for conversion
            float temp = ds18b20.getTempCByIndex(0);
            
            // Validate reading
            if (temp > 30.0 && temp < 45.0 && temp != -127.0) {
                sensorAvailable = true;
                result.celsius = temp;
                result.isEstimated = false;
                result.source = "DS18B20";
                lastCheck = millis();
                return result;
            } else {
                sensorAvailable = false;
            }
            
            lastCheck = millis();
        }
        
        // Fallback: Liebermeister's Rule
        if (!sensorAvailable) {
            result.celsius = 36.5 + ((currentHR - restingHR) / 10.0);
            result.isEstimated = true;
            result.source = "ESTIMATED";
            
            // Clamp to reasonable range
            if (result.celsius < 35.0) result.celsius = 35.0;
            if (result.celsius > 42.0) result.celsius = 42.0;
            
            return result;
        }
        
        // Return last known DS18B20 reading if within 30 seconds
        if (millis() - lastCheck < 30000) {
            float temp = ds18b20.getTempCByIndex(0);
            if (temp > 30.0 && temp < 45.0 && temp != -127.0) {
                result.celsius = temp;
                result.isEstimated = false;
                result.source = "DS18B20";
                return result;
            }
        }
        
        // If all else fails, use estimation
        result.celsius = 36.5 + ((currentHR - restingHR) / 10.0);
        result.isEstimated = true;
        result.source = "ESTIMATED";
        
        if (result.celsius < 35.0) result.celsius = 35.0;
        if (result.celsius > 42.0) result.celsius = 42.0;
        
        return result;
    }
    
    void setRestingHR(float hr) {
        restingHR = hr;
        preferences.putFloat("resting_hr", hr);
    }
    
    bool isSensorAvailable() {
        return sensorAvailable;
    }
};

// ==================== GLOBAL SENSOR INSTANCES ====================
PulseSensor pulseSensor;
TemperatureSensor tempSensor(dallas);

// ==================== ALERT CHECKING ====================
void checkAlerts() {
    currentVitals.hasAlert = false;
    currentVitals.isCriticalAlert = false;
    currentVitals.alertMessage = "";
    
    // Priority 1: CRITICAL - Severe Hypoxia (SpO2 < 90%)
    if (currentVitals.spo2 > 0 && currentVitals.spo2 < 90) {
        currentVitals.hasAlert = true;
        currentVitals.isCriticalAlert = true;
        currentVitals.alertMessage = "CRITICAL: SpO2 LOW!";
        return;
    }
    
    // Priority 2: No sensor readings
    if (currentVitals.heartRate == 0) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "No HR detected";
        return;
    }
    
    // Priority 3: WARNING - Low SpO2 (90-94%)
    if (currentVitals.spo2 > 0 && currentVitals.spo2 < 95) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Low SpO2: " + String(currentVitals.spo2) + "%";
        return;
    }
    
    // Priority 4: High heart rate
    if (currentVitals.heartRate > 100) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "High HR: " + String(currentVitals.heartRate);
        return;
    }
    
    // Priority 5: Low heart rate
    if (currentVitals.heartRate < 50 && currentVitals.heartRate > 0) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Low HR: " + String(currentVitals.heartRate);
        return;
    }
    
    // Priority 6: High temperature/fever
    if (currentVitals.temperature > 38.0) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Fever: " + String(currentVitals.temperature, 1) + "C";
        return;
    }
    
    // Priority 7: Temperature estimation active
    if (currentVitals.tempEstimated) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Temp Estimated";
        return;
    }
    
    // Priority 8: Low battery
    if (currentVitals.batteryPercent < 20) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Low Battery";
        return;
    }
}

// ==================== LCD DISPLAY FUNCTIONS ====================
void updateLCD() {
    lcd.clear();
    
    switch(currentScreen) {
        case 0: // Main screen - Live vitals
            lcd.setCursor(0, 0);
            lcd.print("HR:");
            lcd.print(currentVitals.heartRate);
            lcd.print(" BPM ");
            lcd.print("O2:");
            lcd.print(currentVitals.spo2);
            lcd.print("%");
            
            lcd.setCursor(0, 1);
            lcd.print("Temp: ");
            lcd.print(currentVitals.temperature, 1);
            lcd.print("C");
            if (currentVitals.tempEstimated) {
                lcd.print(" (Est)");
            }
            
            lcd.setCursor(0, 2);
            lcd.print("Batt: ");
            lcd.print(currentVitals.batteryPercent);
            lcd.print("%");
            lcd.setCursor(10, 2);
            lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi:Y" : "WiFi:N");
            
            lcd.setCursor(0, 3);
            if (currentVitals.hasAlert) {
                lcd.print(currentVitals.isCriticalAlert ? "!ALERT! " : "Alert: ");
                lcd.print(currentVitals.alertMessage.substring(0, 12));
            } else {
                lcd.print("Status: Normal");
            }
            break;
            
        case 1: // Sensor status
            lcd.setCursor(0, 0);
            lcd.print("Sensor Status:");
            
            lcd.setCursor(0, 1);
            lcd.print("HR:  ");
            lcd.print(currentVitals.hrQuality > 50 ? "Good" : "Poor");
            lcd.print(" (");
            lcd.print(currentVitals.hrQuality);
            lcd.print("%)");
            
            lcd.setCursor(0, 2);
            lcd.print("SpO2:");
            lcd.print(currentVitals.spo2Quality > 50 ? "Good" : "Poor");
            lcd.print(" (");
            lcd.print(currentVitals.spo2Quality);
            lcd.print("%)");
            
            lcd.setCursor(0, 3);
            lcd.print("Temp: ");
            lcd.print(currentVitals.tempSource);
            break;
            
        case 2: // Alert screen
            lcd.setCursor(0, 0);
            lcd.print(currentVitals.isCriticalAlert ? "CRITICAL ALERT!" : "ALERT!");
            
            lcd.setCursor(0, 1);
            lcd.print(currentVitals.alertMessage);
            
            lcd.setCursor(0, 2);
            if (currentVitals.spo2 < 90 && currentVitals.spo2 > 0) {
                lcd.print("SEEK MEDICAL HELP!");
            }
            
            lcd.setCursor(0, 3);
            lcd.print("[Auto-rotate 10s]");
            break;
            
        case 3: // Historical summary
            lcd.setCursor(0, 0);
            lcd.print("Current Stats:");
            
            lcd.setCursor(0, 1);
            lcd.print("HR: ");
            lcd.print(currentVitals.heartRate);
            lcd.print(" BPM");
            
            lcd.setCursor(0, 2);
            lcd.print("SpO2: ");
            lcd.print(currentVitals.spo2);
            lcd.print("%");
            
            lcd.setCursor(0, 3);
            lcd.print("Temp: ");
            lcd.print(currentVitals.temperature, 1);
            lcd.print("C");
            break;
    }
}


// ==================== VITAL SIGNS UPDATE ====================
void updateVitals() {
    // Heart rate and SpO2
    currentVitals.heartRate = pulseSensor.getBPM();
    currentVitals.hrQuality = pulseSensor.getSignalQuality();
    currentVitals.spo2 = pulseSensor.getSpO2();
    currentVitals.spo2Quality = pulseSensor.getSpO2Quality();
    
    // Temperature with failover
    auto tempReading = tempSensor.getTemperature(currentVitals.heartRate);
    currentVitals.temperature = tempReading.celsius;
    currentVitals.tempEstimated = tempReading.isEstimated;
    currentVitals.tempSource = tempReading.source;
    
    // Battery
    int rawBattery = analogRead(BATTERY_ADC_PIN);
    currentVitals.batteryVoltage = (rawBattery / 4095.0) * 3.3 * 2.0;  // Voltage divider
    currentVitals.batteryPercent = map((int)(currentVitals.batteryVoltage * 100), 340, 420, 0, 100);
    currentVitals.batteryPercent = constrain(currentVitals.batteryPercent, 0, 100);
}

// ==================== CLOUD SYNC ====================
void sendToCloud() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    String url = API_BASE_URL + String(VITALS_ENDPOINT);
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    // Build JSON payload
    StaticJsonDocument<768> doc;
    doc["device_id"] = deviceID;
    doc["timestamp"] = millis() / 1000;  // Simple timestamp (should use NTP in production)
    
    JsonObject vitals = doc.createNestedObject("vitals");
    
    JsonObject hr = vitals.createNestedObject("heart_rate");
    hr["bpm"] = currentVitals.heartRate;
    hr["signal_quality"] = currentVitals.hrQuality;
    hr["is_valid"] = (currentVitals.heartRate > 0);
    
    JsonObject spo2 = vitals.createNestedObject("spo2");
    spo2["percent"] = currentVitals.spo2;
    spo2["signal_quality"] = currentVitals.spo2Quality;
    spo2["is_valid"] = (currentVitals.spo2 > 0 && currentVitals.spo2Quality > 50);
    
    JsonObject temp = vitals.createNestedObject("temperature");
    temp["celsius"] = currentVitals.temperature;
    temp["source"] = currentVitals.tempSource;
    temp["is_estimated"] = currentVitals.tempEstimated;
    
    JsonObject sys = doc.createNestedObject("system");
    sys["battery_percent"] = currentVitals.batteryPercent;
    sys["battery_voltage"] = currentVitals.batteryVoltage;
    sys["wifi_rssi"] = WiFi.RSSI();
    sys["uptime_seconds"] = millis() / 1000;
    
    if (currentVitals.hasAlert) {
        JsonArray alerts = doc.createNestedArray("alerts");
        JsonObject alert = alerts.createNestedObject();
        
        if (currentVitals.isCriticalAlert) {
            alert["type"] = "critical_hypoxia";
        } else if (currentVitals.tempEstimated) {
            alert["type"] = "temp_estimated";
        } else {
            alert["type"] = "threshold_exceeded";
        }
        alert["message"] = currentVitals.alertMessage;
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.POST(jsonString);
    
    if (httpCode == 200) {
        Serial.println("✓ Data sent to /health/vitals");
        digitalWrite(STATUS_LED, HIGH);
        delay(50);
        digitalWrite(STATUS_LED, LOW);
    } else {
        Serial.printf("✗ Error sending: %d\n", httpCode);
    }
    
    http.end();
}

// ==================== LED CONTROL ====================
void updateLEDs() {
    // Alert LED
    if (currentVitals.hasAlert) {
        // Blink faster for critical alerts
        int blinkRate = currentVitals.isCriticalAlert ? 250 : 500;
        
        static unsigned long lastBlink = 0;
        static bool ledState = false;
        if (millis() - lastBlink > blinkRate) {
            ledState = !ledState;
            digitalWrite(ALERT_LED, ledState);
            lastBlink = millis();
        }
    } else {
        digitalWrite(ALERT_LED, LOW);
    }
}

// ==================== WIFI CONNECTION ====================
void connectWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed");
    }
}

// ==================== SCREEN AUTO-ROTATE ====================
void handleScreenRotation() {
    if (manualScreenLock) return;
    
    // Auto-rotate every 10 seconds
    if (millis() - lastScreenChange > 10000) {
        currentScreen = (currentScreen + 1) % 4;
        lastScreenChange = millis();
    }
    
    // If on alert screen and no alert, skip to next screen
    if (currentScreen == 2 && !currentVitals.hasAlert) {
        currentScreen = (currentScreen + 1) % 4;
    }
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    
    // Initialize hardware
    pinMode(STATUS_LED, OUTPUT);
    pinMode(ALERT_LED, OUTPUT);
    pinMode(BATTERY_ADC_PIN, INPUT);
    pinMode(SEN11574_PIN, INPUT);
    
    // Configure ADC
    analogSetAttenuation(ADC_11db);  // 0-3.3V range
    
    // LCD
    Wire.begin(SDA_PIN, SCL_PIN);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VitalWatch v2.0");
    lcd.setCursor(0, 1);
    lcd.print("HR + SpO2 Monitor");
    lcd.setCursor(0, 2);
    lcd.print("Initializing...");
    
    // Preferences
    preferences.begin("health", false);
    
    // Generate device ID
    uint64_t chipid = ESP.getEfuseMac();
    deviceID = "ESP32_" + String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
    deviceID.toUpperCase();
    
    // Initialize sensors
    dallas.begin();
    tempSensor.begin();
    pulseSensor.begin();
    
    // WiFi
    lcd.setCursor(0, 3);
    lcd.print("WiFi connecting...");
    connectWiFi();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ready!");
    lcd.setCursor(0, 1);
    lcd.print("Device: ");
    lcd.print(deviceID.substring(0, 12));
    delay(2000);
    
    lastScreenChange = millis();
    
    Serial.println("===== Multi-Vitals Health Monitor =====");
    Serial.println("Device ID: " + deviceID);
    Serial.println("Backend: " + API_BASE_URL + VITALS_ENDPOINT);
    Serial.println("=====================================");
}

// ==================== MAIN LOOP ====================
void loop() {
    static unsigned long lastSensorRead = 0;
    static unsigned long lastCloudSync = 0;
    static unsigned long lastLCDUpdate = 0;
    static unsigned long lastVitalUpdate = 0;
    
    // Read pulse sensor at 500Hz (every 2ms)
    if (millis() - lastSensorRead >= 2) {
        pulseSensor.update();
        lastSensorRead = millis();
    }
    
    // Update vitals every 1 second
    if (millis() - lastVitalUpdate >= 1000) {
        updateVitals();
        checkAlerts();
        lastVitalUpdate = millis();
    }
    
    // Update LCD every 500ms
    if (millis() - lastLCDUpdate >= 500) {
        updateLCD();
        lastLCDUpdate = millis();
    }
    
    // Cloud sync every 5 seconds
    if (millis() - lastCloudSync >= 5000 && WiFi.status() == WL_CONNECTED) {
        sendToCloud();
        lastCloudSync = millis();
    }
    
    // Handle screen rotation
    handleScreenRotation();
    
    // Update LEDs
    updateLEDs();
}

