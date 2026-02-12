/**
 * Multi-Vitals Health Monitoring System - IMPROVED VERSION
 * ESP32 Firmware for HR, SpO2, and Temperature Monitoring
 * 
 * Improvements:
 * - Watchdog timer for system reliability
 * - WiFi auto-reconnection with exponential backoff
 * - Enhanced sensor fusion with weighted averaging
 * - Optimized memory usage
 * - NTP time synchronization
 * - LCD update optimization (change detection)
 * - Improved error handling and recovery
 * - Configurable calibration mode
 * - Serial diagnostic commands
 * - Better resource management
 * 
 * Version: 3.1 (Improved)
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
#include <esp_task_wdt.h>
#include <time.h>
#include <cmath>
#include "MAX30105.h"
#include "heartRate.h"

// ==================== VERSION INFO ====================
#define FIRMWARE_VERSION "3.1-IMPROVED"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

// ==================== PIN DEFINITIONS ====================
#define SDA_PIN 21
#define SCL_PIN 22
#define DS18B20_PIN 4
#define SEN11574_PIN 34        // ADC1_CH6 - Analog PPG sensor
#define STATUS_LED 2           // Built-in LED
#define BUTTON_START 18        // Start/Resume button (active LOW)
#define BUTTON_STOP 19         // Stop/Pause button (active LOW)

// ==================== CONFIGURATION ====================
const char* WIFI_SSID = "cybergenii";
const char* WIFI_PASSWORD = "12341234";
String API_BASE_URL = "https://xenophobic-netta-cybergenii-1584fde7.koyeb.app";
const char* VITALS_ENDPOINT = "/health/vitals";

// NTP Configuration
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 0;
const int DAYLIGHT_OFFSET_SEC = 0;

// Timing Configuration
#define SENSOR_READ_INTERVAL 2      // ms for SEN11574
#define VITALS_UPDATE_INTERVAL 1000 // ms
#define CLOUD_SYNC_INTERVAL 5000    // ms
#define LCD_UPDATE_INTERVAL 500     // ms
#define WIFI_RECONNECT_INTERVAL 30000 // ms
#define WATCHDOG_TIMEOUT 10         // seconds

// Sensor Fusion Configuration
#define MIN_QUALITY_THRESHOLD 50
#define EXCELLENT_QUALITY_THRESHOLD 80
#define SENSOR_FUSION_WEIGHT 0.7    // Weight for higher quality sensor

// WiFi Reconnection
#define WIFI_MAX_RETRIES 5
#define WIFI_RETRY_BASE_DELAY 1000  // ms, will use exponential backoff

// ==================== HARDWARE OBJECTS ====================
OneWire oneWire(DS18B20_PIN);
DallasTemperature dallas(&oneWire);
LiquidCrystal_I2C lcd(0x27, 20, 4);
Preferences preferences;
MAX30105 max30102;

// ==================== MONITORING STATE ====================
enum MonitoringState {
    STATE_IDLE,
    STATE_MONITORING,
    STATE_PAUSED,
    STATE_CALIBRATING
};

// ==================== GLOBAL VARIABLES ====================
String deviceID;
int currentScreen = 0;
int lastDisplayedScreen = -1;  // For change detection
unsigned long lastScreenChange = 0;
bool manualScreenLock = false;
MonitoringState monitoringState = STATE_IDLE;
String monitoringStateStr = "idle";

// WiFi reconnection
unsigned long lastWiFiCheck = 0;
int wifiRetryCount = 0;
bool wifiReconnecting = false;

// NTP sync
bool timeInitialized = false;
unsigned long bootTimestamp = 0;

// ==================== VITAL SIGNS STRUCTURE ====================
struct VitalSigns {
    int heartRate = 0;
    int hrQuality = 0;
    int spo2 = 0;
    int spo2Quality = 0;
    float temperature = 36.5;
    bool tempEstimated = false;
    String tempSource = "UNKNOWN";
    String hrSource = "NONE";
    String spo2Source = "NONE";
    
    bool hasAlert = false;
    String alertMessage = "";
    bool isCriticalAlert = false;
    
    // For change detection
    bool hasChanged = true;
};

VitalSigns currentVitals;
VitalSigns lastDisplayedVitals;  // For LCD optimization

// ==================== PULSE SENSOR CLASS (SEN-11574) - IMPROVED ====================
class PulseSensor {
private:
    static const int SAMPLE_RATE = 500;
    static const int WINDOW_SIZE = 100;
    static const int MAX_SIGNAL = 4095;
    
    int signalBuffer[WINDOW_SIZE];
    int bufferIndex = 0;
    bool bufferFilled = false;
    
    unsigned long lastBeatTime = 0;
    int bpm = 0;
    int rrIntervals[10];
    int rrIndex = 0;
    
    float dcComponent = 0;
    float acComponent = 0;
    float spo2 = 0;
    
    int signalQuality = 0;
    int threshold = 2000;
    int adaptiveThreshold = 2000;
    
    unsigned long lastSampleTime = 0;
    unsigned long lastThresholdUpdate = 0;
    
    // Calibration
    bool isCalibrating = false;
    int calibrationSamples = 0;
    long calibrationSum = 0;
    
    // Signal statistics for better quality assessment
    float signalMean = 0;
    float signalStdDev = 0;
    
public:
    void begin() {
        pinMode(SEN11574_PIN, INPUT);
        memset(signalBuffer, 0, sizeof(signalBuffer));
        bufferFilled = false;
    }
    
    void startCalibration() {
        isCalibrating = true;
        calibrationSamples = 0;
        calibrationSum = 0;
        Serial.println("SEN11574: Calibration started");
    }
    
    void stopCalibration() {
        if (calibrationSamples > 0) {
            int avgBaseline = calibrationSum / calibrationSamples;
            threshold = avgBaseline + 200;  // Set threshold above baseline
            Serial.printf("SEN11574: Calibration complete. Threshold: %d\n", threshold);
        }
        isCalibrating = false;
    }
    
    void update() {
        unsigned long now = millis();
        if (now - lastSampleTime < 2) return;
        lastSampleTime = now;
        
        // Read with error checking
        int rawSignal = analogRead(SEN11574_PIN);
        if (rawSignal < 0 || rawSignal > MAX_SIGNAL) {
            rawSignal = signalBuffer[bufferIndex];  // Use last valid value
        }
        
        // Calibration mode
        if (isCalibrating) {
            calibrationSum += rawSignal;
            calibrationSamples++;
            return;
        }
        
        // Store in circular buffer
        signalBuffer[bufferIndex] = rawSignal;
        bufferIndex = (bufferIndex + 1) % WINDOW_SIZE;
        if (bufferIndex == 0) bufferFilled = true;
        
        // Only process when buffer has enough data
        if (!bufferFilled) return;
        
        // Detect heartbeat
        detectBeat(rawSignal);
        
        // Calculate SpO2
        calculateSpO2(rawSignal);
        
        // Update signal quality
        updateSignalQuality();
        
        // Auto-adjust threshold
        if (now - lastThresholdUpdate > 1000) {
            adjustThreshold();
            lastThresholdUpdate = now;
        }
    }
    
    void detectBeat(int signal) {
        static int lastSignal = 0;
        static bool risingEdge = false;
        static int peakValue = 0;
        static unsigned long peakTime = 0;
        
        // Enhanced peak detection with refractory period
        const unsigned long REFRACTORY_PERIOD = 300;  // ms
        
        if (signal > adaptiveThreshold && lastSignal <= adaptiveThreshold) {
            risingEdge = true;
            peakValue = signal;
            peakTime = millis();
        }
        
        if (risingEdge && signal > peakValue) {
            peakValue = signal;
            peakTime = millis();
        }
        
        if (risingEdge && signal < adaptiveThreshold) {
            // Beat detected
            unsigned long now = millis();
            unsigned long beatInterval = now - lastBeatTime;
            
            // Validate with refractory period (30-200 BPM range)
            if (beatInterval > REFRACTORY_PERIOD && beatInterval < 2000 && lastBeatTime > 0) {
                int newBpm = 60000 / beatInterval;
                
                // Smooth BPM changes (reject outliers)
                if (bpm == 0 || abs(newBpm - bpm) < 20) {
                    bpm = newBpm;
                    
                    // Store RR interval
                    rrIntervals[rrIndex] = beatInterval;
                    rrIndex = (rrIndex + 1) % 10;
                    
                    lastBeatTime = now;
                }
            } else if (lastBeatTime == 0) {
                lastBeatTime = now;
            }
            
            risingEdge = false;
        }
        
        lastSignal = signal;
    }
    
    void adjustThreshold() {
        if (!bufferFilled) return;
        
        // Calculate statistics
        long sum = 0;
        int minVal = MAX_SIGNAL, maxVal = 0;
        
        for (int i = 0; i < WINDOW_SIZE; i++) {
            int val = signalBuffer[i];
            sum += val;
            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
        }
        
        signalMean = sum / (float)WINDOW_SIZE;
        
        // Calculate standard deviation
        float variance = 0;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            float diff = signalBuffer[i] - signalMean;
            variance += diff * diff;
        }
        signalStdDev = sqrt(variance / WINDOW_SIZE);
        
        // Adaptive threshold based on signal characteristics
        int range = maxVal - minVal;
        if (range > 100) {
            adaptiveThreshold = minVal + (range * 0.65);
        } else {
            adaptiveThreshold = signalMean + signalStdDev;
        }
        
        // Smooth threshold changes
        threshold = threshold * 0.9 + adaptiveThreshold * 0.1;
    }
    
    void calculateSpO2(int signal) {
        // Enhanced SpO2 with better DC filtering
        const float DC_ALPHA = 0.98;  // Slower DC tracking
        const float AC_ALPHA = 0.1;
        
        dcComponent = dcComponent * DC_ALPHA + signal * (1.0 - DC_ALPHA);
        
        float instantAC = signal - dcComponent;
        acComponent = acComponent * AC_ALPHA + fabs(instantAC) * (1.0 - AC_ALPHA);
        
        if (dcComponent > 100 && acComponent > 5) {
            float ratio = acComponent / dcComponent;
            
            // Improved empirical formula with clamping
            spo2 = constrain(110 - 25 * ratio, 70, 100);
        } else {
            spo2 = 0;
        }
    }
    
    void updateSignalQuality() {
        if (!bufferFilled) {
            signalQuality = 0;
            return;
        }
        
        // Multi-factor quality assessment
        int qualityScore = 0;
        
        // Factor 1: Signal amplitude (0-40 points)
        int minVal = MAX_SIGNAL, maxVal = 0;
        for (int i = 0; i < WINDOW_SIZE; i++) {
            if (signalBuffer[i] < minVal) minVal = signalBuffer[i];
            if (signalBuffer[i] > maxVal) maxVal = signalBuffer[i];
        }
        int amplitude = maxVal - minVal;
        
        if (amplitude > 1000) qualityScore += 40;
        else if (amplitude > 500) qualityScore += 30;
        else if (amplitude > 200) qualityScore += 20;
        else qualityScore += 10;
        
        // Factor 2: Signal consistency (0-30 points)
        if (signalStdDev > 50 && signalStdDev < 500) {
            qualityScore += 30;
        } else if (signalStdDev > 30) {
            qualityScore += 15;
        }
        
        // Factor 3: Recent beat detection (0-30 points)
        unsigned long timeSinceLastBeat = millis() - lastBeatTime;
        if (timeSinceLastBeat < 1500) {
            qualityScore += 30;
        } else if (timeSinceLastBeat < 3000) {
            qualityScore += 15;
        } else {
            bpm = 0;  // Invalidate old BPM
        }
        
        signalQuality = constrain(qualityScore, 0, 100);
    }
    
    int getBPM() {
        if (millis() - lastBeatTime > 3000) return 0;
        return constrain(bpm, 30, 200);
    }
    
    int getSpO2() {
        if (signalQuality < MIN_QUALITY_THRESHOLD) return 0;
        return (int)spo2;
    }
    
    int getSignalQuality() {
        return signalQuality;
    }
    
    int getSpO2Quality() {
        if (signalQuality > 70) return 90;
        if (signalQuality > 50) return 60;
        return 30;
    }
    
    bool isValidReading() {
        return signalQuality > MIN_QUALITY_THRESHOLD && 
               (millis() - lastBeatTime < 3000);
    }
    
    void reset() {
        bpm = 0;
        spo2 = 0;
        signalQuality = 0;
        lastBeatTime = 0;
        bufferFilled = false;
        bufferIndex = 0;
    }
};

// ==================== MAX30102 SENSOR CLASS - IMPROVED ====================
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
    uint32_t lastIRValue = 0;
    
    int spo2Value = 0;
    int spo2Quality = 0;
    
    // Signal quality metrics
    float irDC = 0;
    float redDC = 0;
    
    // Adaptive thresholding
    uint32_t peakThreshold = 0;
    uint32_t troughThreshold = 0;
    
public:
    MAX30102Sensor(MAX30105& max) : sensor(&max) {}
    
    bool begin() {
        if (!sensor->begin(Wire, I2C_SPEED_STANDARD)) {
            Serial.println("MAX30102 not found!");
            available = false;
            return false;
        }
        
        // Optimized configuration
        byte ledBrightness = 60;
        byte sampleAverage = 4;
        byte ledMode = 2;           // Red + IR
        byte sampleRate = 100;
        int pulseWidth = 411;
        int adcRange = 4096;
        
        sensor->setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
        sensor->setPulseAmplitudeRed(0x0A);
        sensor->setPulseAmplitudeGreen(0);
        
        available = true;
        Serial.println("MAX30102 initialized successfully!");
        return true;
    }
    
    void update() {
        if (!available) return;
        
        lastIRValue = irValue;
        irValue = sensor->getIR();
        redValue = sensor->getRed();
        
        // Update DC components with low-pass filter
        const float DC_ALPHA = 0.95;
        irDC = irDC * DC_ALPHA + irValue * (1.0 - DC_ALPHA);
        redDC = redDC * DC_ALPHA + redValue * (1.0 - DC_ALPHA);
        
        // Check if finger is detected
        if (irValue < 50000) {
            beatsPerMinute = 0;
            beatAvg = 0;
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        // Update adaptive thresholds
        updateThresholds();
        
        // Detect heartbeat
        if (checkForBeat(irValue)) {
            long delta = millis() - lastBeat;
            lastBeat = millis();
            
            beatsPerMinute = 60 / (delta / 1000.0);
            
            // Validate and smooth BPM
            if (beatsPerMinute < 255 && beatsPerMinute > 20) {
                // Reject outliers
                if (beatAvg == 0 || abs(beatsPerMinute - beatAvg) < 30) {
                    rates[rateSpot++] = (byte)beatsPerMinute;
                    rateSpot %= RATE_SIZE;
                    
                    // Calculate weighted average
                    beatAvg = 0;
                    for (byte x = 0; x < RATE_SIZE; x++) {
                        beatAvg += rates[x];
                    }
                    beatAvg /= RATE_SIZE;
                }
            }
        }
        
        calculateSpO2();
    }
    
    void updateThresholds() {
        // Adaptive threshold based on recent signal history
        static uint32_t recentPeak = 0;
        static uint32_t recentTrough = 0xFFFFFFFF;
        
        if (irValue > recentPeak) recentPeak = irValue;
        if (irValue < recentTrough) recentTrough = irValue;
        
        // Decay thresholds over time
        static unsigned long lastDecay = 0;
        if (millis() - lastDecay > 1000) {
            recentPeak = recentPeak * 0.95;
            recentTrough = recentTrough * 1.05;
            if (recentTrough > irValue) recentTrough = irValue;
            lastDecay = millis();
        }
        
        peakThreshold = recentTrough + (recentPeak - recentTrough) * 0.6;
    }
    
    void calculateSpO2() {
        if (irValue < 50000 || redValue < 50000) {
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        // Calculate AC/DC ratios for both wavelengths
        float irAC = irValue - irDC;
        float redAC = redValue - redDC;
        
        if (irDC > 1000 && redDC > 1000) {
            float ratioRMS = (redAC / redDC) / (irAC / irDC);
            
            // Improved empirical formula
            spo2Value = constrain((int)(110 - 25 * ratioRMS), 70, 100);
            
            // Quality based on signal strength and stability
            if (irValue > 100000 && llabs((int32_t)(irValue - lastIRValue)) < 5000) {
                spo2Quality = 95;
            } else if (irValue > 75000) {
                spo2Quality = 80;
            } else if (irValue > 50000) {
                spo2Quality = 60;
            } else {
                spo2Quality = 30;
            }
        } else {
            spo2Value = 0;
            spo2Quality = 0;
        }
    }
    
    bool checkForBeat(uint32_t sample) {
        static uint32_t lastSample = 0;
        static bool risingEdge = false;
        static unsigned long lastBeatTime = 0;
        
        // Detect rising edge crossing threshold
        if (sample > peakThreshold && lastSample <= peakThreshold) {
            risingEdge = true;
        }
        
        if (risingEdge && sample < peakThreshold) {
            unsigned long now = millis();
            // Debounce with refractory period
            if (now - lastBeatTime > 300) {
                lastBeatTime = now;
                risingEdge = false;
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
        if (beatAvg > 0 && irValue > 80000) return 95;
        if (beatAvg > 0) return 75;
        return 40;
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
    
    void reset() {
        beatAvg = 0;
        beatsPerMinute = 0;
        spo2Value = 0;
        memset(rates, 0, sizeof(rates));
        rateSpot = 0;
    }
};

// ==================== TEMPERATURE SENSOR CLASS - IMPROVED ====================
class TemperatureSensor {
private:
    DallasTemperature& ds18b20;
    bool sensorAvailable = true;
    unsigned long lastCheck = 0;
    float restingHR = 70.0;
    float lastValidTemp = 36.5;
    int consecutiveFailures = 0;
    
public:
    TemperatureSensor(DallasTemperature& sensor) : ds18b20(sensor) {}
    
    void begin() {
        ds18b20.begin();
        restingHR = preferences.getFloat("resting_hr", 70.0);
        
        // Initial sensor check
        ds18b20.requestTemperatures();
        delay(100);
        float temp = ds18b20.getTempCByIndex(0);
        
        if (temp > 30.0 && temp < 45.0 && temp != -127.0) {
            sensorAvailable = true;
            lastValidTemp = temp;
            Serial.println("DS18B20: Available");
        } else {
            sensorAvailable = false;
            Serial.println("DS18B20: Not detected, using estimation");
        }
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
            delay(100);
            float temp = ds18b20.getTempCByIndex(0);
            
            // Validate reading
            if (temp > 30.0 && temp < 45.0 && temp != -127.0) {
                // Additional validation: reject sudden changes > 2°C
                if (abs(temp - lastValidTemp) < 2.0 || consecutiveFailures > 5) {
                    sensorAvailable = true;
                    lastValidTemp = temp;
                    consecutiveFailures = 0;
                    result.celsius = temp;
                    result.isEstimated = false;
                    result.source = "DS18B20";
                    lastCheck = millis();
                    return result;
                }
            }
            
            consecutiveFailures++;
            if (consecutiveFailures > 3) {
                sensorAvailable = false;
            }
            
            lastCheck = millis();
        }
        
        // Use last valid reading if recent
        if (sensorAvailable && millis() - lastCheck < 30000) {
            result.celsius = lastValidTemp;
            result.isEstimated = false;
            result.source = "DS18B20";
            return result;
        }
        
        // Fallback: Enhanced Liebermeister's Rule
        if (currentHR > 0) {
            float hrDelta = currentHR - restingHR;
            result.celsius = 36.5 + (hrDelta / 10.0);
        } else {
            result.celsius = lastValidTemp;  // Use last known value
        }
        
        result.isEstimated = true;
        result.source = "ESTIMATED";
        result.celsius = constrain(result.celsius, 35.0, 42.0);
        
        return result;
    }
    
    void setRestingHR(float hr) {
        restingHR = constrain(hr, 40.0, 100.0);
        preferences.putFloat("resting_hr", hr);
        Serial.printf("Resting HR set to: %.1f\n", hr);
    }
    
    bool isSensorAvailable() {
        return sensorAvailable;
    }
    
    float getLastValidTemp() {
        return lastValidTemp;
    }
};

// ==================== GLOBAL SENSOR INSTANCES ====================
PulseSensor pulseSensor;
MAX30102Sensor max30102Sensor(max30102);
TemperatureSensor tempSensor(dallas);

// ==================== ENHANCED SENSOR FUSION ====================
struct FusedReading {
    int value;
    int quality;
    String source;
};

FusedReading fuseSensorReadings(int value1, int quality1, const char* source1,
                                 int value2, int quality2, const char* source2) {
    FusedReading result;
    
    // Both sensors have good quality - use weighted average
    if (quality1 >= MIN_QUALITY_THRESHOLD && quality2 >= MIN_QUALITY_THRESHOLD) {
        float weight1 = quality1 / 100.0;
        float weight2 = quality2 / 100.0;
        float totalWeight = weight1 + weight2;
        
        result.value = (value1 * weight1 + value2 * weight2) / totalWeight;
        result.quality = (quality1 + quality2) / 2;
        result.source = String(source1) + "+" + String(source2);
        return result;
    }
    
    // Only first sensor has good quality
    if (quality1 >= MIN_QUALITY_THRESHOLD && value1 > 0) {
        result.value = value1;
        result.quality = quality1;
        result.source = source1;
        return result;
    }
    
    // Only second sensor has good quality
    if (quality2 >= MIN_QUALITY_THRESHOLD && value2 > 0) {
        result.value = value2;
        result.quality = quality2;
        result.source = source2;
        return result;
    }
    
    // No valid readings
    result.value = 0;
    result.quality = 0;
    result.source = "NONE";
    return result;
}

// ==================== ALERT CHECKING - ENHANCED ====================
void checkAlerts() {
    currentVitals.hasAlert = false;
    currentVitals.isCriticalAlert = false;
    currentVitals.alertMessage = "";
    
    // CRITICAL: Severe Hypoxia (SpO2 < 90%)
    if (currentVitals.spo2 > 0 && currentVitals.spo2Quality > 50 && currentVitals.spo2 < 90) {
        currentVitals.hasAlert = true;
        currentVitals.isCriticalAlert = true;
        currentVitals.alertMessage = "CRITICAL: SpO2 LOW!";
        return;
    }
    
    // WARNING: No sensor readings
    if (currentVitals.heartRate == 0) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "No HR detected";
        return;
    }
    
    // WARNING: Low SpO2 (90-94%)
    if (currentVitals.spo2 > 0 && currentVitals.spo2Quality > 50 && currentVitals.spo2 < 95) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Low SpO2: " + String(currentVitals.spo2) + "%";
        return;
    }
    
    // WARNING: Tachycardia
    if (currentVitals.heartRate > 100 && currentVitals.hrQuality > 50) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "High HR: " + String(currentVitals.heartRate);
        return;
    }
    
    // WARNING: Bradycardia
    if (currentVitals.heartRate < 50 && currentVitals.heartRate > 0 && currentVitals.hrQuality > 50) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Low HR: " + String(currentVitals.heartRate);
        return;
    }
    
    // WARNING: Fever
    if (currentVitals.temperature > 38.0 && !currentVitals.tempEstimated) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Fever: " + String(currentVitals.temperature, 1) + "C";
        return;
    }
    
    // INFO: Temperature estimation active
    if (currentVitals.tempEstimated) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Temp Estimated";
        return;
    }
}

// ==================== BUTTON HANDLERS - IMPROVED ====================
class ButtonHandler {
private:
    int pin;
    bool lastState;
    unsigned long lastDebounceTime;
    bool currentState;
    static const unsigned long DEBOUNCE_DELAY = 50;
    
public:
    ButtonHandler(int p) : pin(p), lastState(HIGH), lastDebounceTime(0), currentState(HIGH) {}
    
    void begin() {
        pinMode(pin, INPUT_PULLUP);
    }
    
    
    bool isPressed() {
        bool reading = digitalRead(pin);
        
        // Update debounce timer on any state change
        if (reading != lastState) {
            lastDebounceTime = millis();
            lastState = reading;
        }
        
        // Check if button state has been stable long enough
        if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
            // Detect falling edge (button press)
            if (reading == LOW && currentState == HIGH) {
                currentState = LOW;
                return true;
            }
            // Detect rising edge (button release)
            else if (reading == HIGH && currentState == LOW) {
                currentState = HIGH;
            }
        }
        
        return false;
    }
};

ButtonHandler startButton(BUTTON_START);
ButtonHandler stopButton(BUTTON_STOP);

void handleButtons() {
    if (startButton.isPressed()) {
        if (monitoringState == STATE_IDLE || monitoringState == STATE_PAUSED) {
            monitoringState = STATE_MONITORING;
            monitoringStateStr = "monitoring";
            Serial.println("→ State: MONITORING");
            
            // Flash status LED
            digitalWrite(STATUS_LED, HIGH);
            delay(50);
            digitalWrite(STATUS_LED, LOW);
        }
    }
    
    if (stopButton.isPressed()) {
        if (monitoringState == STATE_MONITORING) {
            monitoringState = STATE_PAUSED;
            monitoringStateStr = "paused";
            Serial.println("→ State: PAUSED");
            
            // Flash status LED twice
            for (int i = 0; i < 2; i++) {
                digitalWrite(STATUS_LED, HIGH);
                delay(50);
                digitalWrite(STATUS_LED, LOW);
                delay(50);
            }
        } else if (monitoringState == STATE_PAUSED) {
            monitoringState = STATE_IDLE;
            monitoringStateStr = "idle";
            Serial.println("→ State: IDLE");
        }
    }
}

// ==================== LCD DISPLAY - OPTIMIZED ====================
bool vitalsChanged() {
    return currentVitals.heartRate != lastDisplayedVitals.heartRate ||
           currentVitals.spo2 != lastDisplayedVitals.spo2 ||
           abs(currentVitals.temperature - lastDisplayedVitals.temperature) > 0.1 ||
           currentVitals.hasAlert != lastDisplayedVitals.hasAlert ||
           currentVitals.alertMessage != lastDisplayedVitals.alertMessage ||
           currentVitals.hrSource != lastDisplayedVitals.hrSource ||
           monitoringState != lastDisplayedScreen;
}

void updateLCD() {
    // Only update if screen changed or vitals changed
    if (currentScreen == lastDisplayedScreen && !vitalsChanged()) {
        return;
    }
    
    lcd.clear();
    
    switch(currentScreen) {
        case 0: // Main screen
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
            lcd.print("HR:");
            lcd.print(currentVitals.hrSource.substring(0, 8));
            lcd.setCursor(11, 2);
            lcd.print(WiFi.status() == WL_CONNECTED ? "WiFi:Y" : "WiFi:N");
            
            lcd.setCursor(0, 3);
            if (monitoringState == STATE_MONITORING) {
                lcd.print("[MON] ");
            } else if (monitoringState == STATE_PAUSED) {
                lcd.print("[PAUSE]");
            } else if (monitoringState == STATE_CALIBRATING) {
                lcd.print("[CAL] ");
            } else {
                lcd.print("[IDLE] ");
            }
            
            if (currentVitals.hasAlert && monitoringState != STATE_CALIBRATING) {
                lcd.print(currentVitals.alertMessage.substring(0, 13));
            }
            break;
            
        case 1: // Sensor status
            lcd.setCursor(0, 0);
            lcd.print("Sensor Status:");
            
            lcd.setCursor(0, 1);
            lcd.print("HR: Q");
            lcd.print(currentVitals.hrQuality);
            lcd.print(" (");
            lcd.print(currentVitals.hrSource.substring(0, 8));
            lcd.print(")");
            
            lcd.setCursor(0, 2);
            lcd.print("O2: Q");
            lcd.print(currentVitals.spo2Quality);
            lcd.print(" (");
            lcd.print(currentVitals.spo2Source.substring(0, 8));
            lcd.print(")");
            
            lcd.setCursor(0, 3);
            lcd.print("T: ");
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
            } else {
                lcd.print("Monitor vitals");
            }
            
            lcd.setCursor(0, 3);
            lcd.print("Auto-rotate 10s");
            break;
            
        case 3: // System info
            lcd.setCursor(0, 0);
            lcd.print("System Info:");
            
            lcd.setCursor(0, 1);
            lcd.print("FW: ");
            lcd.print(FIRMWARE_VERSION);
            
            lcd.setCursor(0, 2);
            lcd.print("Up: ");
            lcd.print(millis() / 1000 / 60);
            lcd.print("m RSSI:");
            lcd.print(WiFi.RSSI());
            
            lcd.setCursor(0, 3);
            lcd.print("Free: ");
            lcd.print(ESP.getFreeHeap());
            lcd.print("b");
            break;
    }
    
    lastDisplayedScreen = currentScreen;
    lastDisplayedVitals = currentVitals;
}

// ==================== VITAL SIGNS UPDATE - ENHANCED ====================
void updateVitals() {
    // Get readings from both sensors
    int max30102_hr = max30102Sensor.getBPM();
    int max30102_spo2 = max30102Sensor.getSpO2();
    int max30102_hrQuality = max30102Sensor.getHRQuality();
    int max30102_spo2Quality = max30102Sensor.getSpO2Quality();
    
    int sen11574_hr = pulseSensor.getBPM();
    int sen11574_spo2 = pulseSensor.getSpO2();
    int sen11574_hrQuality = pulseSensor.getSignalQuality();
    int sen11574_spo2Quality = pulseSensor.getSpO2Quality();
    
    // Fuse heart rate readings
    FusedReading hr = fuseSensorReadings(
        max30102_hr, max30102_hrQuality, "MAX30102",
        sen11574_hr, sen11574_hrQuality, "SEN11574"
    );
    currentVitals.heartRate = hr.value;
    currentVitals.hrQuality = hr.quality;
    currentVitals.hrSource = hr.source;
    
    // Fuse SpO2 readings
    FusedReading spo2 = fuseSensorReadings(
        max30102_spo2, max30102_spo2Quality, "MAX30102",
        sen11574_spo2, sen11574_spo2Quality, "SEN11574"
    );
    currentVitals.spo2 = spo2.value;
    currentVitals.spo2Quality = spo2.quality;
    currentVitals.spo2Source = spo2.source;
    
    // Get temperature
    auto tempReading = tempSensor.getTemperature(currentVitals.heartRate);
    currentVitals.temperature = tempReading.celsius;
    currentVitals.tempEstimated = tempReading.isEstimated;
    currentVitals.tempSource = tempReading.source;
    
    currentVitals.hasChanged = true;
}

// ==================== CLOUD SYNC - IMPROVED ====================
void sendToCloud() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    http.setTimeout(5000);  // 5 second timeout
    
    String url = API_BASE_URL + String(VITALS_ENDPOINT);
    
    if (!http.begin(url)) {
        Serial.println("✗ HTTP begin failed");
        return;
    }
    
    http.addHeader("Content-Type", "application/json");
    
    // Build JSON payload
    DynamicJsonDocument doc(1024);
    doc["device_id"] = deviceID;
    
    // Use NTP time if available, otherwise use millis
    if (timeInitialized) {
        time_t now;
        time(&now);
        doc["timestamp"] = now;
    } else {
        doc["timestamp"] = bootTimestamp + (millis() / 1000);
    }
    
    JsonObject vitals = doc.createNestedObject("vitals");
    
    JsonObject hr = vitals.createNestedObject("heart_rate");
    hr["bpm"] = currentVitals.heartRate;
    hr["signal_quality"] = currentVitals.hrQuality;
    hr["is_valid"] = (currentVitals.heartRate > 0 && currentVitals.hrQuality > MIN_QUALITY_THRESHOLD);
    hr["source"] = currentVitals.hrSource;
    
    JsonObject spo2 = vitals.createNestedObject("spo2");
    spo2["percent"] = currentVitals.spo2;
    spo2["signal_quality"] = currentVitals.spo2Quality;
    spo2["is_valid"] = (currentVitals.spo2 > 0 && currentVitals.spo2Quality > MIN_QUALITY_THRESHOLD);
    spo2["source"] = currentVitals.spo2Source;
    
    JsonObject temp = vitals.createNestedObject("temperature");
    temp["celsius"] = currentVitals.temperature;
    temp["source"] = currentVitals.tempSource;
    temp["is_estimated"] = currentVitals.tempEstimated;
    
    JsonObject sys = doc.createNestedObject("system");
    sys["wifi_rssi"] = WiFi.RSSI();
    sys["uptime_seconds"] = millis() / 1000;
    sys["monitoring_state"] = monitoringStateStr;
    sys["free_heap"] = ESP.getFreeHeap();
    sys["firmware_version"] = FIRMWARE_VERSION;
    
    if (currentVitals.hasAlert) {
        JsonArray alerts = doc.createNestedArray("alerts");
        JsonObject alert = alerts.createNestedObject();
        
        if (currentVitals.isCriticalAlert) {
            alert["type"] = "critical_hypoxia";
            alert["severity"] = "critical";
        } else if (currentVitals.tempEstimated) {
            alert["type"] = "temp_estimated";
            alert["severity"] = "info";
        } else {
            alert["type"] = "threshold_exceeded";
            alert["severity"] = "warning";
        }
        alert["message"] = currentVitals.alertMessage;
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.POST(jsonString);
    
    if (httpCode == 200 || httpCode == 201) {
        Serial.println("✓ Data sent to cloud");
        digitalWrite(STATUS_LED, HIGH);
        delay(30);
        digitalWrite(STATUS_LED, LOW);
    } else if (httpCode > 0) {
        Serial.printf("✗ HTTP error: %d\n", httpCode);
    } else {
        Serial.printf("✗ Connection error: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
}

// ==================== WIFI MANAGEMENT - IMPROVED ====================
void connectWiFi() {
    Serial.println("\n========== WiFi Connection ==========");
    Serial.print("SSID: ");
    Serial.println(WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    WiFi.disconnect();
    delay(100);
    
    Serial.print("Connecting");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✓ WiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        Serial.print("RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        
        // Initialize NTP
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        time_t now;
        if (time(&now) > 0) {
            timeInitialized = true;
            bootTimestamp = now - (millis() / 1000);
            Serial.println("✓ NTP time synchronized");
        }
    } else {
        Serial.println("✗ WiFi connection failed");
        Serial.println("Will retry automatically");
    }
    Serial.println("====================================\n");
}

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED && !wifiReconnecting) {
        unsigned long now = millis();
        
        if (now - lastWiFiCheck > WIFI_RECONNECT_INTERVAL) {
            wifiReconnecting = true;
            Serial.println("WiFi disconnected, reconnecting...");
            
            // Exponential backoff
            int delay_ms = WIFI_RETRY_BASE_DELAY * (1 << min(wifiRetryCount, 4));
            delay(delay_ms);
            
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 10) {
                delay(500);
                attempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("✓ WiFi reconnected");
                wifiRetryCount = 0;
            } else {
                wifiRetryCount++;
                Serial.printf("✗ Reconnection failed (retry %d)\n", wifiRetryCount);
            }
            
            lastWiFiCheck = now;
            wifiReconnecting = false;
        }
    } else if (WiFi.status() == WL_CONNECTED) {
        wifiRetryCount = 0;
    }
}

// ==================== SCREEN AUTO-ROTATE ====================
void handleScreenRotation() {
    if (manualScreenLock) return;
    
    if (millis() - lastScreenChange > 10000) {
        currentScreen = (currentScreen + 1) % 4;
        lastScreenChange = millis();
    }
    
    // Skip alert screen if no alert
    if (currentScreen == 2 && !currentVitals.hasAlert) {
        currentScreen = (currentScreen + 1) % 4;
    }
}

// ==================== SERIAL COMMANDS ====================
void handleSerialCommands() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        
        if (cmd == "help") {
            Serial.println("\n=== Available Commands ===");
            Serial.println("help - Show this menu");
            Serial.println("status - Show system status");
            Serial.println("vitals - Show current vitals");
            Serial.println("reset - Reset sensors");
            Serial.println("calibrate - Start calibration mode");
            Serial.println("setresting <bpm> - Set resting HR");
            Serial.println("wifi - WiFi status");
            Serial.println("restart - Restart device");
            Serial.println("========================\n");
        }
        else if (cmd == "status") {
            Serial.println("\n=== System Status ===");
            Serial.printf("Firmware: %s\n", FIRMWARE_VERSION);
            Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
            Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
            Serial.printf("State: %s\n", monitoringStateStr.c_str());
            Serial.println("===================\n");
        }
        else if (cmd == "vitals") {
            Serial.println("\n=== Current Vitals ===");
            Serial.printf("HR: %d BPM (Q:%d, Source:%s)\n", 
                currentVitals.heartRate, currentVitals.hrQuality, currentVitals.hrSource.c_str());
            Serial.printf("SpO2: %d%% (Q:%d, Source:%s)\n", 
                currentVitals.spo2, currentVitals.spo2Quality, currentVitals.spo2Source.c_str());
            Serial.printf("Temp: %.1f°C (%s)\n", 
                currentVitals.temperature, currentVitals.tempSource.c_str());
            if (currentVitals.hasAlert) {
                Serial.printf("Alert: %s\n", currentVitals.alertMessage.c_str());
            }
            Serial.println("====================\n");
        }
        else if (cmd == "reset") {
            Serial.println("Resetting sensors...");
            pulseSensor.reset();
            max30102Sensor.reset();
            Serial.println("✓ Sensors reset");
        }
        else if (cmd == "calibrate") {
            Serial.println("Starting 10-second calibration...");
            Serial.println("Please keep finger still on sensor");
            monitoringState = STATE_CALIBRATING;
            pulseSensor.startCalibration();
            delay(10000);
            pulseSensor.stopCalibration();
            monitoringState = STATE_IDLE;
            Serial.println("✓ Calibration complete");
        }
        else if (cmd.startsWith("setresting ")) {
            float hr = cmd.substring(11).toFloat();
            if (hr >= 40 && hr <= 100) {
                tempSensor.setRestingHR(hr);
                Serial.printf("✓ Resting HR set to %.1f\n", hr);
            } else {
                Serial.println("✗ Invalid HR (must be 40-100)");
            }
        }
        else if (cmd == "wifi") {
            Serial.println("\n=== WiFi Status ===");
            Serial.printf("Status: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
                Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
            }
            Serial.println("==================\n");
        }
        else if (cmd == "restart") {
            Serial.println("Restarting...");
            delay(1000);
            ESP.restart();
        }
        else {
            Serial.println("Unknown command. Type 'help' for commands.");
        }
    }
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    delay(100);
    
    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║  Multi-Vitals Health Monitor v3.1     ║");
    Serial.println("║  Enhanced Edition                      ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.printf("Build: %s %s\n\n", BUILD_DATE, BUILD_TIME);
    
    // Enable watchdog
    esp_task_wdt_init(WATCHDOG_TIMEOUT, true);
    esp_task_wdt_add(NULL);
    
    // Initialize hardware
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    
    startButton.begin();
    stopButton.begin();
    pinMode(SEN11574_PIN, INPUT);
    analogSetAttenuation(ADC_11db);
    
    // LCD
    Wire.begin(SDA_PIN, SCL_PIN);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VitalWatch v3.1");
    lcd.setCursor(0, 1);
    lcd.print("Enhanced Edition");
    lcd.setCursor(0, 2);
    lcd.print("Initializing...");
    
    // Preferences
    preferences.begin("health", false);
    deviceID = "HEALTH_DEVICE_001";
    
    // Initialize sensors
    Serial.println("Initializing sensors...");
    
    Serial.print("- DS18B20 Temperature... ");
    dallas.begin();
    tempSensor.begin();
    Serial.println("OK");
    
    Serial.print("- MAX30102 (I2C)... ");
    if (max30102Sensor.begin()) {
        Serial.println("OK");
        lcd.setCursor(0, 2);
        lcd.print("MAX30102: OK       ");
    } else {
        Serial.println("FAILED");
        lcd.setCursor(0, 2);
        lcd.print("MAX30102: FAIL     ");
    }
    delay(1000);
    
    Serial.print("- SEN11574 (Analog)... ");
    pulseSensor.begin();
    Serial.println("OK");
    
    // WiFi
    lcd.setCursor(0, 3);
    lcd.print("WiFi connecting...  ");
    connectWiFi();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ready!");
    lcd.setCursor(0, 1);
    lcd.print("Press START button");
    lcd.setCursor(0, 2);
    lcd.print("Type 'help' in serial");
    delay(2000);
    
    lastScreenChange = millis();
    
    Serial.println("\n✓ Setup complete");
    Serial.println("Type 'help' for commands\n");
    
    // Feed watchdog
    esp_task_wdt_reset();
}

// ==================== MAIN LOOP ====================
void loop() {
    static unsigned long lastSensorRead = 0;
    static unsigned long lastCloudSync = 0;
    static unsigned long lastLCDUpdate = 0;
    static unsigned long lastVitalUpdate = 0;
    
    // Feed watchdog
    esp_task_wdt_reset();
    
    // Handle serial commands
    handleSerialCommands();
    
    // Handle button input
    handleButtons();
    
    // Check WiFi connection
    checkWiFiConnection();
    
    // Update sensors when monitoring
    if (monitoringState == STATE_MONITORING) {
        // MAX30102 update
        max30102Sensor.update();
        
        // SEN11574 update at 500Hz
        if (millis() - lastSensorRead >= SENSOR_READ_INTERVAL) {
            pulseSensor.update();
            lastSensorRead = millis();
        }
        
        // Update vitals every second
        if (millis() - lastVitalUpdate >= VITALS_UPDATE_INTERVAL) {
            updateVitals();
            checkAlerts();
            lastVitalUpdate = millis();
        }
        
        // Cloud sync
        if (millis() - lastCloudSync >= CLOUD_SYNC_INTERVAL) {
            if (WiFi.status() == WL_CONNECTED) {
                sendToCloud();
            }
            lastCloudSync = millis();
        }
    }
    
    // Update LCD
    if (millis() - lastLCDUpdate >= LCD_UPDATE_INTERVAL) {
        updateLCD();
        lastLCDUpdate = millis();
    }
    
    // Handle screen rotation
    handleScreenRotation();
    
    // Small delay to prevent tight loop
    delay(1);
}
