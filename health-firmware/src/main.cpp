/**
 * Multi-Vitals Health Monitoring System
 * ESP32 Firmware for HR, SpO2, and Temperature Monitoring
 * 
 * Features:
 * - Dual Heart Rate sensors: MAX30102 (I2C) + SEN11574 (Analog PPG)
 * - Dual SpO2 monitoring with sensor fusion/fallback
 * - Temperature with DS18B20 failover to Liebermeister's Rule
 * - 20x4 LCD display with 4 screens
 * - WiFi cloud sync to /health/vitals endpoint
 * - Critical alert LED system
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
#include "MAX30105.h"
#include "heartRate.h"

// ==================== PIN DEFINITIONS ====================
#define SDA_PIN 21
#define SCL_PIN 22
#define DS18B20_PIN 4
#define SEN11574_PIN 34        // ADC1_CH6 - Analog PPG sensor
#define STATUS_LED 2           // Built-in LED
#define BUTTON_START 32        // Start/Resume button (active LOW) - Safe GPIO
#define BUTTON_STOP 33         // Stop/Pause button (active LOW) - Safe GPIO
// MAX30102 uses I2C (SDA_PIN, SCL_PIN)

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
MAX30105 max30102;  // MAX30102 sensor object

// ==================== MONITORING STATE ====================
enum MonitoringState {
    STATE_IDLE,
    STATE_MONITORING,
    STATE_PAUSED
};

// ==================== GLOBAL VARIABLES ====================
String deviceID;
int currentScreen = 0;
unsigned long lastScreenChange = 0;
bool manualScreenLock = false;
MonitoringState monitoringState = STATE_IDLE;
String monitoringStateStr = "idle";

// ==================== VITAL SIGNS STRUCTURE ====================
struct VitalSigns {
    int heartRate = 0;
    int hrQuality = 0;
    int spo2 = 0;
    int spo2Quality = 0;
    float temperature = 36.5;
    bool tempEstimated = false;
    String tempSource = "UNKNOWN";
    String hrSource = "NONE";      // "MAX30102", "SEN11574", or "NONE"
    String spo2Source = "NONE";    // "MAX30102", "SEN11574", or "NONE"
    
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

// ==================== MAX30102 SENSOR CLASS ====================
class MAX30102Sensor {
private:
    MAX30105* sensor;
    bool available = false;
    
    const byte RATE_SIZE = 4;
    byte rates[4];
    byte rateSpot = 0;
    long lastBeat = 0;
    float beatsPerMinute = 0;
    int beatAvg = 0;
    
    uint32_t irValue = 0;
    uint32_t redValue = 0;
    
    int spo2Value = 0;
    int spo2Quality = 0;
    
public:
    MAX30102Sensor(MAX30105& max) : sensor(&max) {}
    
    bool begin() {
        // Initialize sensor
        if (!sensor->begin(Wire, I2C_SPEED_STANDARD)) {
            Serial.println("MAX30102 not found!");
            available = false;
            return false;
        }
        
        // Configure sensor
        byte ledBrightness = 60;   // 0-255
        byte sampleAverage = 4;     // 1, 2, 4, 8, 16, 32
        byte ledMode = 2;           // 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
        byte sampleRate = 100;      // 50, 100, 200, 400, 800, 1000, 1600, 3200
        int pulseWidth = 411;       // 69, 118, 215, 411
        int adcRange = 4096;        // 2048, 4096, 8192, 16384
        
        sensor->setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
        sensor->setPulseAmplitudeRed(0x0A);   // Turn Red LED to low to indicate sensor is running
        sensor->setPulseAmplitudeGreen(0);     // Turn off Green LED
        
        available = true;
        Serial.println("MAX30102 initialized successfully!");
        return true;
    }
    
    void update() {
        if (!available) return;
        
        // Read sensor values
        irValue = sensor->getIR();
        redValue = sensor->getRed();
        
        // Check if finger is detected (IR value > threshold)
        if (irValue < 50000) {
            beatsPerMinute = 0;
            beatAvg = 0;
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        // Detect heartbeat using IR value
        if (checkForBeat(irValue)) {
            // We found a beat!
            long delta = millis() - lastBeat;
            lastBeat = millis();
            
            beatsPerMinute = 60 / (delta / 1000.0);
            
            // Validate BPM (30-200 range)
            if (beatsPerMinute < 255 && beatsPerMinute > 20) {
                rates[rateSpot++] = (byte)beatsPerMinute;
                rateSpot %= RATE_SIZE;
                
                // Calculate average
                beatAvg = 0;
                for (byte x = 0; x < RATE_SIZE; x++) {
                    beatAvg += rates[x];
                }
                beatAvg /= RATE_SIZE;
            }
        }
        
        // Calculate SpO2
        calculateSpO2();
    }
    
    void calculateSpO2() {
        if (irValue < 50000 || redValue < 50000) {
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        // Calculate R value (ratio of ratios)
        // R = (AC_red / DC_red) / (AC_ir / DC_ir)
        // Simplified SpO2 = 110 - 25*R
        
        float ratio = (float)redValue / (float)irValue;
        
        // Empirical formula for SpO2
        spo2Value = (int)(110 - 25 * ratio);
        
        // Clamp to valid range
        if (spo2Value > 100) spo2Value = 100;
        if (spo2Value < 70) spo2Value = 70;
        
        // Quality based on signal strength
        if (irValue > 100000) {
            spo2Quality = 95;
        } else if (irValue > 75000) {
            spo2Quality = 80;
        } else if (irValue > 50000) {
            spo2Quality = 60;
        } else {
            spo2Quality = 30;
        }
    }
    
    bool checkForBeat(uint32_t sample) {
        // Simple beat detection - looking for peaks
        static uint32_t lastSample = 0;
        static uint32_t peak = 0;
        static uint32_t trough = 0;
        static bool risingEdge = false;
        static unsigned long lastBeatTime = 0;
        
        // Update peak and trough
        if (sample > peak) peak = sample;
        if (sample < trough || trough == 0) trough = sample;
        
        // Detect rising edge
        uint32_t threshold = trough + (peak - trough) * 0.6;
        
        if (sample > threshold && lastSample <= threshold) {
            risingEdge = true;
        }
        
        if (risingEdge && sample < threshold) {
            // Beat detected
            unsigned long now = millis();
            if (now - lastBeatTime > 300) {  // Debounce (min 200 BPM)
                lastBeatTime = now;
                risingEdge = false;
                
                // Reset peak/trough gradually
                peak = peak * 0.95;
                trough = trough * 1.05 + sample * 0.05;
                
                lastSample = sample;
                return true;
            }
            risingEdge = false;
        }
        
        lastSample = sample;
        return false;
    }
    
    int getBPM() {
        if (!available) return 0;
        return beatAvg;
    }
    
    int getSpO2() {
        if (!available) return 0;
        return spo2Value;
    }
    
    int getHRQuality() {
        if (!available) return 0;
        if (irValue < 50000) return 0;
        if (beatAvg > 0) return 90;
        return 50;
    }
    
    int getSpO2Quality() {
        if (!available) return 0;
        return spo2Quality;
    }
    
    bool isAvailable() {
        return available;
    }
    
    bool isFingerDetected() {
        return available && irValue > 50000;
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
MAX30102Sensor max30102Sensor(max30102);
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
}

// ==================== BUTTON HANDLERS ====================
void handleButtons() {
    static unsigned long lastStartPress = 0;
    static unsigned long lastStopPress = 0;
    const unsigned long DEBOUNCE_DELAY = 50;
    
    // Read buttons (active LOW)
    bool startPressed = (digitalRead(BUTTON_START) == LOW);
    bool stopPressed = (digitalRead(BUTTON_STOP) == LOW);
    
    // Handle START button
    if (startPressed && (millis() - lastStartPress > DEBOUNCE_DELAY)) {
        lastStartPress = millis();
        
        if (monitoringState == STATE_IDLE || monitoringState == STATE_PAUSED) {
            monitoringState = STATE_MONITORING;
            monitoringStateStr = "monitoring";
            Serial.println("→ State: MONITORING");
            
            // Flash status LED
            digitalWrite(STATUS_LED, HIGH);
            delay(100);
            digitalWrite(STATUS_LED, LOW);
        }
    }
    
    // Handle STOP button
    if (stopPressed && (millis() - lastStopPress > DEBOUNCE_DELAY)) {
        lastStopPress = millis();
        
        if (monitoringState == STATE_MONITORING) {
            monitoringState = STATE_PAUSED;
            monitoringStateStr = "paused";
            Serial.println("→ State: PAUSED");
            
            // Flash status LED twice
            for (int i = 0; i < 2; i++) {
                digitalWrite(STATUS_LED, HIGH);
                delay(100);
                digitalWrite(STATUS_LED, LOW);
                delay(100);
            }
        }
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
            lcd.print("HR Src: ");
            lcd.print(currentVitals.hrSource);
            lcd.setCursor(10, 2);
            lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi:Y" : "WiFi:N");
            
            lcd.setCursor(0, 3);
            // Show monitoring state
            if (monitoringState == STATE_MONITORING) {
                lcd.print("[MON] ");
            } else if (monitoringState == STATE_PAUSED) {
                lcd.print("[PAUSE] ");
            } else {
                lcd.print("[IDLE] ");
            }
            
            // Show alert if any
            if (currentVitals.hasAlert) {
                lcd.print(currentVitals.alertMessage.substring(0, 12));
            } else {
                lcd.print("Ready");
            }
            break;
            
        case 1: // Sensor status
            lcd.setCursor(0, 0);
            lcd.print("Sensor Status:");
            
            lcd.setCursor(0, 1);
            lcd.print("HR:  ");
            lcd.print(currentVitals.hrQuality > 50 ? "Good" : "Poor");
            lcd.print(" (");
            lcd.print(currentVitals.hrSource);
            lcd.print(")");
            
            lcd.setCursor(0, 2);
            lcd.print("SpO2:");
            lcd.print(currentVitals.spo2Quality > 50 ? "Good" : "Poor");
            lcd.print(" (");
            lcd.print(currentVitals.spo2Source);
            lcd.print(")");
            
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
    // Dual-sensor fusion for Heart Rate and SpO2
    // Priority: MAX30102 (more accurate) > SEN11574 (fallback)
    
    int max30102_hr = max30102Sensor.getBPM();
    int max30102_spo2 = max30102Sensor.getSpO2();
    int max30102_hrQuality = max30102Sensor.getHRQuality();
    int max30102_spo2Quality = max30102Sensor.getSpO2Quality();
    
    int sen11574_hr = pulseSensor.getBPM();
    int sen11574_spo2 = pulseSensor.getSpO2();
    int sen11574_hrQuality = pulseSensor.getSignalQuality();
    int sen11574_spo2Quality = pulseSensor.getSpO2Quality();
    
    // Heart Rate Selection
    if (max30102_hrQuality >= 60 && max30102_hr > 0) {
        // Use MAX30102 if quality is good
        currentVitals.heartRate = max30102_hr;
        currentVitals.hrQuality = max30102_hrQuality;
        currentVitals.hrSource = "MAX30102";
    } else if (sen11574_hrQuality >= 50 && sen11574_hr > 0) {
        // Fallback to SEN11574
        currentVitals.heartRate = sen11574_hr;
        currentVitals.hrQuality = sen11574_hrQuality;
        currentVitals.hrSource = "SEN11574";
    } else {
        // No valid readings
        currentVitals.heartRate = 0;
        currentVitals.hrQuality = 0;
        currentVitals.hrSource = "NONE";
    }
    
    // SpO2 Selection
    if (max30102_spo2Quality >= 60 && max30102_spo2 > 0) {
        // Use MAX30102 for SpO2 (more accurate with dual LED)
        currentVitals.spo2 = max30102_spo2;
        currentVitals.spo2Quality = max30102_spo2Quality;
        currentVitals.spo2Source = "MAX30102";
    } else if (sen11574_spo2Quality >= 50 && sen11574_spo2 > 0) {
        // Fallback to SEN11574 (less accurate, single wavelength)
        currentVitals.spo2 = sen11574_spo2;
        currentVitals.spo2Quality = sen11574_spo2Quality;
        currentVitals.spo2Source = "SEN11574";
    } else {
        // No valid readings
        currentVitals.spo2 = 0;
        currentVitals.spo2Quality = 0;
        currentVitals.spo2Source = "NONE";
    }
    
    // Temperature with failover
    auto tempReading = tempSensor.getTemperature(currentVitals.heartRate);
    currentVitals.temperature = tempReading.celsius;
    currentVitals.tempEstimated = tempReading.isEstimated;
    currentVitals.tempSource = tempReading.source;
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
    hr["source"] = currentVitals.hrSource;
    
    JsonObject spo2 = vitals.createNestedObject("spo2");
    spo2["percent"] = currentVitals.spo2;
    spo2["signal_quality"] = currentVitals.spo2Quality;
    spo2["is_valid"] = (currentVitals.spo2 > 0 && currentVitals.spo2Quality > 50);
    spo2["source"] = currentVitals.spo2Source;
    
    JsonObject temp = vitals.createNestedObject("temperature");
    temp["celsius"] = currentVitals.temperature;
    temp["source"] = currentVitals.tempSource;
    temp["is_estimated"] = currentVitals.tempEstimated;
    
    JsonObject sys = doc.createNestedObject("system");
    sys["wifi_rssi"] = WiFi.RSSI();
    sys["uptime_seconds"] = millis() / 1000;
    sys["monitoring_state"] = monitoringStateStr;
    
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
    // Status LED indicates monitoring state
    // (LED feedback is now handled in button handlers)
}

// ==================== WIFI CONNECTION ====================
void connectWiFi() {
    Serial.println("\n========== WiFi Diagnostics ==========");
    Serial.print("SSID: ");
    Serial.println(WIFI_SSID);
    Serial.print("Password length: ");
    Serial.println(strlen(WIFI_PASSWORD));
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        
        // Print status every 5 attempts
        if (attempts % 5 == 0) {
            Serial.print("\n[Attempt ");
            Serial.print(attempts);
            Serial.print("] Status: ");
            Serial.print(WiFi.status());
            Serial.print(" - ");
            
            switch(WiFi.status()) {
                case WL_IDLE_STATUS: Serial.print("IDLE"); break;
                case WL_NO_SSID_AVAIL: Serial.print("NO_SSID_AVAILABLE"); break;
                case WL_SCAN_COMPLETED: Serial.print("SCAN_COMPLETED"); break;
                case WL_CONNECTED: Serial.print("CONNECTED"); break;
                case WL_CONNECT_FAILED: Serial.print("CONNECT_FAILED"); break;
                case WL_CONNECTION_LOST: Serial.print("CONNECTION_LOST"); break;
                case WL_DISCONNECTED: Serial.print("DISCONNECTED"); break;
                default: Serial.print("UNKNOWN"); break;
            }
            Serial.println();
        }
        
        attempts++;
    }
    
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✓ WiFi connected successfully!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
    } else {
        Serial.println("✗ WiFi connection FAILED!");
        Serial.print("Final status: ");
        Serial.println(WiFi.status());
        Serial.println("\nTroubleshooting:");
        Serial.println("1. Check SSID is correct");
        Serial.println("2. Check password is correct");
        Serial.println("3. Check router is in range");
        Serial.println("4. Check 2.4GHz WiFi is enabled");
    }
    Serial.println("======================================\n");
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
    pinMode(BUTTON_START, INPUT_PULLUP);  // Active LOW with internal pullup
    pinMode(BUTTON_STOP, INPUT_PULLUP);   // Active LOW with internal pullup
    pinMode(SEN11574_PIN, INPUT);
    
    // Configure ADC
    analogSetAttenuation(ADC_11db);  // 0-3.3V range
    
    // LCD
    Wire.begin(SDA_PIN, SCL_PIN);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VitalWatch v3.0");
    lcd.setCursor(0, 1);
    lcd.print("HR+SpO2 w/ Buttons");
    lcd.setCursor(0, 2);
    lcd.print("Initializing...");
    
    // Preferences
    preferences.begin("health", false);
    
    // Hardcoded device ID
    deviceID = "HEALTH_DEVICE_001";
    
    // Initialize sensors
    Serial.println("\n--- Initializing Sensors ---");
    
    Serial.print("DS18B20 Temperature... ");
    dallas.begin();
    tempSensor.begin();
    Serial.println("OK");
    
    Serial.print("MAX30102 (I2C)... ");
    if (max30102Sensor.begin()) {
        Serial.println("OK");
        lcd.setCursor(0, 2);
        lcd.print("MAX30102: OK");
    } else {
        Serial.println("FAILED (will use SEN11574)");
        lcd.setCursor(0, 2);
        lcd.print("MAX30102: FAIL");
    }
    delay(1000);
    
    Serial.print("SEN11574 (Analog)... ");
    pulseSensor.begin();
    Serial.println("OK");
    Serial.println("--- Sensor Init Complete ---\n");
    
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
    
    
    Serial.println("\n===== Multi-Vitals Health Monitor =====");
    Serial.println("Device ID: " + deviceID);
    Serial.println("Backend: " + API_BASE_URL + VITALS_ENDPOINT);
    Serial.print("WiFi Status: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "NOT CONNECTED");
    Serial.println("Setup complete. Starting main loop...");
    Serial.println("=======================================");
}

// ==================== MAIN LOOP ====================
void loop() {
    static unsigned long lastSensorRead = 0;
    static unsigned long lastCloudSync = 0;
    static unsigned long lastLCDUpdate = 0;
    static unsigned long lastVitalUpdate = 0;
    
    // Handle button input (always active)
    handleButtons();
    
    // Only update sensors when in MONITORING state
    if (monitoringState == STATE_MONITORING) {
        // Read pulse sensors
        // MAX30102 update (called frequently as it has internal buffering)
        max30102Sensor.update();
        
        // SEN11574 update at 500Hz (every 2ms)
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
        
        // Cloud sync every 5 seconds
        if (millis() - lastCloudSync >= 5000 && WiFi.status() == WL_CONNECTED) {
            sendToCloud();
            lastCloudSync = millis();
        }
    }
    
    // Update LCD every 500ms (always active)
    if (millis() - lastLCDUpdate >= 500) {
        updateLCD();
        lastLCDUpdate = millis();
    }
    
    // Handle screen rotation
    handleScreenRotation();
    
    // Update LEDs
    updateLEDs();
}

