/**
 * Multi-Vitals Health Monitoring System - Production Version
 * ESP32 Firmware for HR, SpO2, and Temperature Monitoring
 * 
 * Features:
 * - Dual heart rate sensors (MAX30102 + SEN-11574) with sensor fusion
 * - SpO2 monitoring via MAX30102
 * - Temperature monitoring (DS18B20)
 * - Cloud sync with backend API
 * - LCD display with multiple screens
 * - Physical button controls
 * - Remote state control via API
 * 
 * Version: 4.1 (Production - Fixed)
 * Author: CyberGenii
 * License: MIT
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
#define FIRMWARE_VERSION "4.1"
#define DEBUG_SENSORS 1

// ==================== PIN DEFINITIONS ====================
#define SDA_PIN 21
#define SCL_PIN 22
#define DS18B20_PIN 4
#define SEN11574_PIN 34
#define STATUS_LED 2
#define BUTTON_START 18
#define BUTTON_STOP 19

// ==================== CONFIGURATION ====================
const char* WIFI_SSID = "Lumen";
const char* WIFI_PASSWORD = "oselee99";
String API_BASE_URL = "https://xenophobic-netta-cybergenii-1584fde7.koyeb.app";
const char* VITALS_ENDPOINT = "/health/vitals";

const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

#define SENSOR_READ_INTERVAL 2
#define VITALS_UPDATE_INTERVAL 1000
#define CLOUD_SYNC_INTERVAL 5000.
#define LCD_UPDATE_INTERVAL 500
#define WIFI_RECONNECT_INTERVAL 30000
#define WATCHDOG_TIMEOUT 10
#define STATE_POLL_INTERVAL 10000

#define MIN_QUALITY_THRESHOLD 40
#define WIFI_MAX_RETRIES 5
#define WIFI_RETRY_BASE_DELAY 1000

// ==================== HARDWARE OBJECTS ====================
OneWire oneWire(DS18B20_PIN);
DallasTemperature dallas(&oneWire);
LiquidCrystal_I2C lcd(0x27, 20, 4);
Preferences preferences;
MAX30105 max30102;

// ==================== STATE MANAGEMENT ====================
enum MonitoringState {
    STATE_IDLE,
    STATE_MONITORING,
    STATE_PAUSED
};

String deviceID;
int currentScreen = 0;
int lastDisplayedScreen = -1;
unsigned long lastScreenChange = 0;
MonitoringState monitoringState = STATE_IDLE;
String monitoringStateStr = "idle";

unsigned long lastWiFiCheck = 0;
int wifiRetryCount = 0;
bool wifiReconnecting = false;
bool timeInitialized = false;
unsigned long bootTimestamp = 0;
unsigned long lastStatePoll = 0;

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
    bool hasChanged = true;
};

VitalSigns currentVitals;
VitalSigns lastDisplayedVitals;

// ==================== FORWARD DECLARATIONS ====================
class PulseSensor;
class MAX30102Sensor;
class TemperatureSensor;

// ==================== BUTTON CLASS ====================
class Button {
private:
    uint8_t pin;
    bool lastRawState;
    bool stableState;
    bool lastStableState;
    unsigned long lastChangeTime;
    unsigned long debounceDelay;
    
public:
    Button(uint8_t p, unsigned long debounce = 50) 
        : pin(p), lastRawState(HIGH), stableState(HIGH), 
          lastStableState(HIGH), lastChangeTime(0), debounceDelay(debounce) {}
    
    void begin() {
        pinMode(pin, INPUT_PULLUP);
        lastRawState = digitalRead(pin);
        stableState = lastRawState;
        lastStableState = lastRawState;
    }
    
    void update() {
        bool currentRaw = digitalRead(pin);
        
        if (currentRaw != lastRawState) {
            lastChangeTime = millis();
            lastRawState = currentRaw;
        }
        
        if ((millis() - lastChangeTime) > debounceDelay) {
            if (currentRaw != stableState) {
                lastStableState = stableState;
                stableState = currentRaw;
            }
        }
    }
    
    bool isPressed() {
        return (lastStableState == HIGH && stableState == LOW);
    }
    
    void resetState() {
        lastStableState = stableState;
    }
};

Button startButton(BUTTON_START);
Button stopButton(BUTTON_STOP);

// ==================== PULSE SENSOR CLASS (SEN-11574) ====================
class PulseSensor {
private:
    static const int WINDOW_SIZE = 100;
    static const int MAX_SIGNAL = 4095;
    
    int signalBuffer[WINDOW_SIZE];
    int bufferIndex;
    bool bufferFilled;
    
    unsigned long lastBeatTime;
    int currentBPM;
    int beatHistory[8];
    int beatHistoryIndex;
    int beatHistoryCount;
    
    float dcLevel;
    float acAmplitude;
    int dynamicThreshold;
    int baselineLevel;
    
    float smoothedSignal;
    float smoothAlpha;
    
    int peakValue;
    int troughValue;
    unsigned long lastAdaptUpdate;
    
    int signalQuality;
    float spo2Value;
    int spo2Quality;
    
public:
    PulseSensor() : bufferIndex(0), bufferFilled(false), lastBeatTime(0), currentBPM(0),
                    beatHistoryIndex(0), beatHistoryCount(0), dcLevel(2048), acAmplitude(0),
                    dynamicThreshold(2048), baselineLevel(2048), smoothedSignal(2048),
                    smoothAlpha(0.3), peakValue(0), troughValue(4095), lastAdaptUpdate(0),
                    signalQuality(0), spo2Value(0), spo2Quality(0) {
        memset(signalBuffer, 0, sizeof(signalBuffer));
        memset(beatHistory, 0, sizeof(beatHistory));
    }
    
    void begin() {
        pinMode(SEN11574_PIN, INPUT);
        analogReadResolution(12);
        analogSetWidth(12);
        analogSetAttenuation(ADC_11db);
        
        delay(100);
        int sum = 0;
        int validSamples = 0;
        
        for (int i = 0; i < 50; i++) {
            int reading = analogRead(SEN11574_PIN);
            if (reading > 0) {
                sum += reading;
                validSamples++;
            }
            delay(20);
        }
        
        if (validSamples > 0) {
            baselineLevel = sum / validSamples;
            dcLevel = baselineLevel;
            smoothedSignal = baselineLevel;
            dynamicThreshold = baselineLevel + 100;
        }
    }
    
    void update() {
        int rawSignal = analogRead(SEN11574_PIN);
        if (rawSignal < 0 || rawSignal > MAX_SIGNAL) return;
        
        smoothedSignal = smoothedSignal * (1.0 - smoothAlpha) + rawSignal * smoothAlpha;
        int signal = (int)smoothedSignal;
        
        signalBuffer[bufferIndex] = signal;
        bufferIndex = (bufferIndex + 1) % WINDOW_SIZE;
        if (bufferIndex == 0) bufferFilled = true;
        
        if (!bufferFilled) return;
        
        updateSignalStats();
        detectBeat(signal, millis());
        calculateSpO2();
        updateQuality();
    }
    
    void updateSignalStats() {
        if (!bufferFilled) return;
        
        long sum = 0;
        int minVal = MAX_SIGNAL;
        int maxVal = 0;
        
        for (int i = 0; i < WINDOW_SIZE; i++) {
            int val = signalBuffer[i];
            sum += val;
            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
        }
        
        float newDC = sum / (float)WINDOW_SIZE;
        dcLevel = dcLevel * 0.98 + newDC * 0.02;
        
        int range = maxVal - minVal;
        acAmplitude = acAmplitude * 0.7 + range * 0.3;
        
        if (millis() - lastAdaptUpdate > 500) {
            peakValue = maxVal;
            troughValue = minVal;
            
            if (range > 50) {
                dynamicThreshold = troughValue + (range * 0.5);
            } else {
                dynamicThreshold = dcLevel + 100;
            }
            
            lastAdaptUpdate = millis();
        }
    }
    
    void detectBeat(int signal, unsigned long now) {
        static int lastSignal = 0;
        static bool aboveThreshold = false;
        
        if (signal > dynamicThreshold && lastSignal <= dynamicThreshold) {
            aboveThreshold = true;
        }
        
        if (aboveThreshold && signal < dynamicThreshold) {
            aboveThreshold = false;
            
            unsigned long beatInterval = now - lastBeatTime;
            
            if (beatInterval > 250 && beatInterval < 2500 && lastBeatTime > 0) {
                int instantBPM = 60000 / beatInterval;
                
                bool isValid = true;
                if (beatHistoryCount > 2) {
                    int avgHistory = 0;
                    for (int i = 0; i < beatHistoryCount; i++) {
                        avgHistory += beatHistory[i];
                    }
                    avgHistory /= beatHistoryCount;
                    
                    if (abs(instantBPM - avgHistory) > 40) {
                        isValid = false;
                    }
                } else {
                    isValid = (instantBPM >= 30 && instantBPM <= 200);
                }
                
                if (isValid) {
                    beatHistory[beatHistoryIndex] = instantBPM;
                    beatHistoryIndex = (beatHistoryIndex + 1) % 8;
                    if (beatHistoryCount < 8) beatHistoryCount++;
                    
                    int sum = 0;
                    for (int i = 0; i < beatHistoryCount; i++) {
                        sum += beatHistory[i];
                    }
                    currentBPM = sum / beatHistoryCount;
                    lastBeatTime = now;
                }
            } else if (lastBeatTime == 0) {
                lastBeatTime = now;
            }
        }
        
        if (now - lastBeatTime > 3000 && lastBeatTime > 0) {
            currentBPM = 0;
            beatHistoryCount = 0;
        }
        
        lastSignal = signal;
    }
    
    void calculateSpO2() {
        if (acAmplitude < 20 || dcLevel < 500) {
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        float ratio = acAmplitude / dcLevel;
        spo2Value = constrain(110 - 25 * ratio, 70, 100);
        
        if (acAmplitude > 200 && signalQuality > 60) {
            spo2Quality = 85;
        } else if (acAmplitude > 100 && signalQuality > 40) {
            spo2Quality = 60;
        } else if (acAmplitude > 50) {
            spo2Quality = 40;
        } else {
            spo2Quality = 20;
        }
    }
    
    void updateQuality() {
        if (!bufferFilled) {
            signalQuality = 0;
            return;
        }
        
        int quality = 0;
        
        if (acAmplitude > 200) quality += 40;
        else if (acAmplitude > 100) quality += 30;
        else if (acAmplitude > 50) quality += 20;
        else if (acAmplitude > 20) quality += 10;
        
        unsigned long timeSinceLastBeat = millis() - lastBeatTime;
        if (timeSinceLastBeat < 1200 && currentBPM > 0) {
            quality += 40;
        } else if (timeSinceLastBeat < 2000 && currentBPM > 0) {
            quality += 25;
        } else if (timeSinceLastBeat < 3000) {
            quality += 10;
        }
        
        if (beatHistoryCount >= 4) {
            quality += 20;
        } else if (beatHistoryCount >= 2) {
            quality += 10;
        }
        
        signalQuality = constrain(quality, 0, 100);
    }
    
    int getBPM() {
        unsigned long timeSince = millis() - lastBeatTime;
        if (timeSince > 3000) return 0;
        return constrain(currentBPM, 30, 200);
    }
    
    int getSpO2() {
        if (signalQuality < 30) return 0;
        return (int)spo2Value;
    }
    
    int getSignalQuality() { return signalQuality; }
    int getSpO2Quality() { return spo2Quality; }
    
    void reset() {
        currentBPM = 0;
        spo2Value = 0;
        signalQuality = 0;
        lastBeatTime = 0;
        beatHistoryCount = 0;
        beatHistoryIndex = 0;
        memset(beatHistory, 0, sizeof(beatHistory));
    }
};

// ==================== MAX30102 SENSOR CLASS ====================
class MAX30102Sensor {
private:
    MAX30105* sensor;
    bool available;
    
    static const byte RATE_SIZE = 8;
    byte rates[RATE_SIZE];
    byte rateSpot;
    unsigned long lastBeat;
    float beatsPerMinute;
    int beatAvg;
    
    uint32_t irValue;
    uint32_t redValue;
    
    float irDC;
    float redDC;
    float irAC;
    float redAC;
    
    int spo2Value;
    int spo2Quality;
    
    uint32_t irPeak;
    uint32_t irTrough;
    uint32_t adaptiveThreshold;
    unsigned long lastThresholdUpdate;
    
    bool fingerDetected;
    
public:
    MAX30102Sensor(MAX30105& max) : sensor(&max), available(false), rateSpot(0),
                                    lastBeat(0), beatsPerMinute(0), beatAvg(0),
                                    irValue(0), redValue(0), irDC(0), redDC(0),
                                    irAC(0), redAC(0), spo2Value(0), spo2Quality(0),
                                    irPeak(0), irTrough(0xFFFFFFFF), adaptiveThreshold(70000),
                                    lastThresholdUpdate(0), fingerDetected(false) {
        memset(rates, 0, sizeof(rates));
    }
    
    bool begin() {
        if (!sensor->begin(Wire, I2C_SPEED_STANDARD)) {
            available = false;
            return false;
        }
        
        byte ledBrightness = 0x7F;
        byte sampleAverage = 4;
        byte ledMode = 2;
        byte sampleRate = 100;
        int pulseWidth = 411;
        int adcRange = 4096;
        
        sensor->setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
        sensor->setPulseAmplitudeRed(0x7F);
        sensor->setPulseAmplitudeIR(0x7F);
        sensor->setPulseAmplitudeGreen(0);
        
        delay(100);
        for (int i = 0; i < 20; i++) {
            uint32_t ir = sensor->getIR();
            if (ir > 0) irDC = ir;
            delay(50);
        }
        
        available = true;
        return true;
    }
    
    void update() {
        if (!available) return;
        
        irValue = sensor->getIR();
        redValue = sensor->getRed();
        
        bool wasDetected = fingerDetected;
        // User requested revert to former settings (30000)
        fingerDetected = (irValue > 30000); 

        #ifdef DEBUG_SENSORS
        if (millis() % 200 == 0) {
            Serial.print("MAX30102: IR=");
            Serial.print(irValue);
            Serial.print(", RED=");
            Serial.print(redValue);
            Serial.print(", Detect=");
            Serial.print(fingerDetected);
            Serial.print(", BPM=");
            Serial.println(beatsPerMinute);
        }
        #endif
        
        if (fingerDetected && !wasDetected) {
            memset(rates, 0, sizeof(rates));
            rateSpot = 0;
            beatAvg = 0;
        } else if (!fingerDetected && wasDetected) {
            reset();
            return;
        }
        
        if (!fingerDetected) return;
        
        const float DC_ALPHA = 0.995;
        irDC = irDC * DC_ALPHA + irValue * (1.0 - DC_ALPHA);
        redDC = redDC * DC_ALPHA + redValue * (1.0 - DC_ALPHA);
        
        irAC = irValue - irDC;
        redAC = redValue - redDC;
        
        updateThreshold();
        
        if (detectBeat(irValue)) {
            unsigned long delta = millis() - lastBeat;
            lastBeat = millis();
            
            beatsPerMinute = 60.0 / (delta / 1000.0);
            
            if (beatsPerMinute >= 30 && beatsPerMinute <= 200) {
                bool valid = true;
                if (beatAvg > 0) {
                    int diff = abs((int)beatsPerMinute - beatAvg);
                    if (diff > 40) valid = false;
                }
                
                if (valid) {
                    rates[rateSpot++] = (byte)beatsPerMinute;
                    rateSpot %= RATE_SIZE;
                    
                    int sum = 0;
                    int count = 0;
                    for (byte x = 0; x < RATE_SIZE; x++) {
                        if (rates[x] > 0) {
                            sum += rates[x];
                            count++;
                        }
                    }
                    if (count > 0) beatAvg = sum / count;
                }
            }
        }
        
        calculateSpO2();
    }
    
    void updateThreshold() {
        if (!fingerDetected) return;
        
        if (irValue > irPeak) irPeak = irValue;
        if (irValue < irTrough && irValue > 30000) irTrough = irValue;
        
        if (millis() - lastThresholdUpdate > 1000) {
            irPeak = irPeak * 0.95;
            if (irTrough < irValue * 1.5) {
                irTrough = irTrough * 1.05;
            }
            
            if (irPeak > irTrough) {
                uint32_t range = irPeak - irTrough;
                adaptiveThreshold = irTrough + (range * 0.5);
            } else {
                adaptiveThreshold = irDC * 1.01;
            }
            
            adaptiveThreshold = constrain(adaptiveThreshold, 40000UL, 200000UL);
            lastThresholdUpdate = millis();
        }
    }
    
    bool detectBeat(uint32_t sample) {
        static uint32_t lastSample = 0;
        static bool risingEdge = false;
        static unsigned long lastBeatTime = 0;
        
        if (sample > adaptiveThreshold && lastSample <= adaptiveThreshold) {
            risingEdge = true;
        }
        
        if (risingEdge && sample < adaptiveThreshold) {
            unsigned long now = millis();
            
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
    
    void calculateSpO2() {
        if (!fingerDetected || irDC < 30000 || redDC < 30000 || fabs(irAC) < 50) {
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        float ratioRMS = (fabs(redAC) / redDC) / (fabs(irAC) / irDC);
        spo2Value = constrain((int)(110 - 25 * ratioRMS), 70, 100);
        
        if (irValue > 120000 && beatAvg > 0) {
            spo2Quality = 95;
        } else if (irValue > 90000 && beatAvg > 0) {
            spo2Quality = 80;
        } else if (irValue > 60000) {
            spo2Quality = 60;
        } else if (irValue > 30000) {
            spo2Quality = 40;
        } else {
            spo2Quality = 20;
        }
    }
    
    int getBPM() {
        if (!available || !fingerDetected || millis() - lastBeat > 3000) return 0;
        return beatAvg;
    }
    
    int getSpO2() {
        if (!available || !fingerDetected) return 0;
        return spo2Value;
    }
    
    int getHRQuality() {
        if (!available || !fingerDetected) return 0;
        
        unsigned long timeSinceBeat = millis() - lastBeat;
        
        if (irValue > 100000 && beatAvg > 0 && timeSinceBeat < 1200) return 95;
        else if (irValue > 80000 && beatAvg > 0 && timeSinceBeat < 2000) return 75;
        else if (irValue > 50000 && timeSinceBeat < 3000) return 50;
        else if (irValue > 30000) return 30;
        return 20;
    }
    
    int getSpO2Quality() {
        if (!available || !fingerDetected) return 0;
        return spo2Quality;
    }
    
    bool isAvailable() { return available; }
    bool isFingerDetected() { return available && fingerDetected; }
    
    void reset() {
        beatAvg = 0;
        beatsPerMinute = 0;
        spo2Value = 0;
        memset(rates, 0, sizeof(rates));
        rateSpot = 0;
        irPeak = 0;
        irTrough = 0xFFFFFFFF;
    }
};

// ==================== TEMPERATURE SENSOR CLASS ====================
class TemperatureSensor {
private:
    DallasTemperature& ds18b20;
    bool sensorAvailable;
    unsigned long lastCheck;
    float restingHR;
    float lastValidTemp;
    int consecutiveFailures;
    
public:
    struct TempReading {
        float celsius;
        bool isEstimated;
        String source;
    };
    
    TemperatureSensor(DallasTemperature& sensor) : ds18b20(sensor), sensorAvailable(true),
                                                   lastCheck(0), restingHR(70.0),
                                                   lastValidTemp(36.5), consecutiveFailures(0) {}
    
    void begin() {
        ds18b20.begin();
        restingHR = preferences.getFloat("resting_hr", 70.0);
        
        ds18b20.requestTemperatures();
        delay(100);
        float temp = ds18b20.getTempCByIndex(0);
        
        if (temp > 30.0 && temp < 45.0 && temp != -127.0) {
            sensorAvailable = true;
            lastValidTemp = temp;
        } else {
            sensorAvailable = false;
        }
    }
    
    TempReading getTemperature(float currentHR) {
        TempReading result;
        
        if (millis() - lastCheck > 10000 || lastCheck == 0) {
            ds18b20.requestTemperatures();
            delay(100);
            float temp = ds18b20.getTempCByIndex(0);
            
            if (temp > 30.0 && temp < 45.0 && temp != -127.0) {
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
            if (consecutiveFailures > 3) sensorAvailable = false;
            lastCheck = millis();
        }
        
        if (sensorAvailable && millis() - lastCheck < 30000) {
            result.celsius = lastValidTemp;
            result.isEstimated = false;
            result.source = "DS18B20";
            return result;
        }
        
        if (currentHR > 0) {
            float hrDelta = currentHR - restingHR;
            result.celsius = 36.5 + (hrDelta / 10.0);
        } else {
            result.celsius = lastValidTemp;
        }
        
        result.isEstimated = true;
        result.source = "ESTIMATED";
        result.celsius = constrain(result.celsius, 35.0, 42.0);
        
        return result;
    }
    
    bool isSensorAvailable() { return sensorAvailable; }
};

// ==================== SENSOR INSTANCES ====================
PulseSensor pulseSensor;
MAX30102Sensor max30102Sensor(max30102);
TemperatureSensor tempSensor(dallas);


// ==================== ALERT CHECKING ====================
void checkAlerts() {
    currentVitals.hasAlert = false;
    currentVitals.isCriticalAlert = false;
    currentVitals.alertMessage = "";
    
    if (currentVitals.spo2 > 0 && currentVitals.spo2Quality > 50 && currentVitals.spo2 < 90) {
        currentVitals.hasAlert = true;
        currentVitals.isCriticalAlert = true;
        currentVitals.alertMessage = "CRITICAL: SpO2 LOW!";
        return;
    }
    
    if (currentVitals.heartRate == 0) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "No HR detected";
        return;
    }
    
    if (currentVitals.spo2 > 0 && currentVitals.spo2Quality > 50 && currentVitals.spo2 < 95) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Low SpO2";
        return;
    }
    
    if (currentVitals.heartRate > 100 && currentVitals.hrQuality > 50) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "High HR";
        return;
    }
    
    if (currentVitals.heartRate < 50 && currentVitals.heartRate > 0 && currentVitals.hrQuality > 50) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Low HR";
        return;
    }
    
    if (currentVitals.temperature > 38.0 && !currentVitals.tempEstimated) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Fever";
        return;
    }
}

// ==================== BUTTON HANDLERS ====================
void handleButtons() {
    startButton.update();
    stopButton.update();
    
    if (startButton.isPressed()) {
        if (monitoringState == STATE_IDLE || monitoringState == STATE_PAUSED) {
            monitoringState = STATE_MONITORING;
            monitoringStateStr = "monitoring";
            
            digitalWrite(STATUS_LED, HIGH);
            delay(100);
            digitalWrite(STATUS_LED, LOW);
            
            lcd.clear();
            lcd.setCursor(0, 1);
            lcd.print("Monitoring Started");
            delay(500);
        }
        startButton.resetState();
    }
    
    if (stopButton.isPressed()) {
        if (monitoringState == STATE_MONITORING) {
            monitoringState = STATE_PAUSED;
            monitoringStateStr = "paused";
            
            for (int i = 0; i < 2; i++) {
                digitalWrite(STATUS_LED, HIGH);
                delay(100);
                digitalWrite(STATUS_LED, LOW);
                delay(100);
            }
            
            lcd.clear();
            lcd.setCursor(0, 1);
            lcd.print("Monitoring Paused");
            delay(500);
        } else if (monitoringState == STATE_PAUSED) {
            monitoringState = STATE_IDLE;
            monitoringStateStr = "idle";
            
            lcd.clear();
            lcd.setCursor(0, 1);
            lcd.print("Monitoring Stopped");
            delay(500);
        }
        stopButton.resetState();
    }
}

// ==================== LCD DISPLAY ====================
bool vitalsChanged() {
    return currentVitals.heartRate != lastDisplayedVitals.heartRate ||
           currentVitals.spo2 != lastDisplayedVitals.spo2 ||
           abs(currentVitals.temperature - lastDisplayedVitals.temperature) > 0.1 ||
           currentVitals.hasAlert != lastDisplayedVitals.hasAlert ||
           monitoringState != lastDisplayedScreen;
}

void updateLCD() {
    if (currentScreen == lastDisplayedScreen && !vitalsChanged()) return;
    
    lcd.clear();
    
    switch(currentScreen) {
        case 0:
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
            
            lcd.setCursor(0, 2);
            lcd.print("Src:");
            lcd.print(currentVitals.hrSource.substring(0, 12));
            
            lcd.setCursor(0, 3);
            if (monitoringState == STATE_MONITORING) {
                lcd.print("[MON] ");
            } else if (monitoringState == STATE_PAUSED) {
                lcd.print("[PAUSE]");
            } else {
                lcd.print("[IDLE] ");
            }
            
            if (currentVitals.hasAlert) {
                lcd.print(currentVitals.alertMessage.substring(0, 13));
            }
            break;
            
        case 1:
            lcd.setCursor(0, 0);
            lcd.print("System Status:");
            
            lcd.setCursor(0, 1);
            lcd.print("WiFi:");
            lcd.print(WiFi.status() == WL_CONNECTED ? "OK " : "ERR");
            lcd.print(" Up:");
            lcd.print(millis() / 60000);
            lcd.print("m");
            
            lcd.setCursor(0, 2);
            lcd.print("MAX:");
            lcd.print(max30102Sensor.isFingerDetected() ? "YES" : "NO ");
            lcd.print(" SEN:");
            lcd.print(pulseSensor.getBPM() > 0 ? "YES" : "NO ");
            
            lcd.setCursor(0, 3);
            lcd.print("v");
            lcd.print(FIRMWARE_VERSION);
            break;
    }
    
    lastDisplayedScreen = currentScreen;
    lastDisplayedVitals = currentVitals;
}

// ==================== VITALS UPDATE ====================
void updateVitals() {
    int max30102_hr = max30102Sensor.getBPM();
    int max30102_spo2 = max30102Sensor.getSpO2();
    int max30102_hrQuality = max30102Sensor.getHRQuality();
    int max30102_spo2Quality = max30102Sensor.getSpO2Quality();
    
    int sen11574_hr = pulseSensor.getBPM();
    int sen11574_spo2 = pulseSensor.getSpO2();
    int sen11574_hrQuality = pulseSensor.getSignalQuality();
    int sen11574_spo2Quality = pulseSensor.getSpO2Quality();
    
    // --- HEART RATE PRIORITY: MAX30102 (Primary) with Failover to SEN11574 ---
    // User requested to prioritize MAX30102. Use SEN11574 only if MAX30102 is 0.
    if (max30102_hr > 0 && max30102_hrQuality >= MIN_QUALITY_THRESHOLD) {
        currentVitals.heartRate = max30102_hr;
        currentVitals.hrQuality = max30102_hrQuality;
        currentVitals.hrSource = "MAX30102";
    } 
    else if (sen11574_hr > 0 && sen11574_hrQuality >= MIN_QUALITY_THRESHOLD) {
        currentVitals.heartRate = sen11574_hr;
        currentVitals.hrQuality = sen11574_hrQuality;
        currentVitals.hrSource = "SEN11574";
    } 
    else {
        currentVitals.heartRate = 0;
        currentVitals.hrQuality = 0;
        currentVitals.hrSource = "NONE";
    }
    
    // --- SpO2 PRIORITY: MAX30102 ---
    // MAX30102 is the primary sensor for SpO2
    if (max30102_spo2 > 0 && max30102_spo2Quality >= MIN_QUALITY_THRESHOLD) {
        currentVitals.spo2 = max30102_spo2;
        currentVitals.spo2Quality = max30102_spo2Quality;
        currentVitals.spo2Source = "MAX30102";
    }
    else if (sen11574_spo2 > 0 && sen11574_spo2Quality >= MIN_QUALITY_THRESHOLD) {
        currentVitals.spo2 = sen11574_spo2;
        currentVitals.spo2Quality = sen11574_spo2Quality;
        currentVitals.spo2Source = "SEN11574 (Est)";
    }
    else {
        currentVitals.spo2 = 0;
        currentVitals.spo2Quality = 0;
        currentVitals.spo2Source = "NONE";
    }
    
    // Get temperature
    TemperatureSensor::TempReading tempReading = tempSensor.getTemperature(currentVitals.heartRate);
    currentVitals.temperature = tempReading.celsius;
    currentVitals.tempEstimated = tempReading.isEstimated;
    currentVitals.tempSource = tempReading.source;
    
    currentVitals.hasChanged = true;
}

// ==================== CLOUD SYNC ====================
void sendToCloud() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    http.setTimeout(5000);
    
    String url = API_BASE_URL + String(VITALS_ENDPOINT);
    if (!http.begin(url)) return;
    
    http.addHeader("Content-Type", "application/json");
    
    DynamicJsonDocument doc(1024);
    doc["device_id"] = deviceID;
    
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
        digitalWrite(STATUS_LED, HIGH);
        delay(30);
        digitalWrite(STATUS_LED, LOW);
    }
    
    http.end();
}

// ==================== REMOTE STATE CONTROL ====================
void checkRemoteStateCommand() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    http.setTimeout(3000);
    
    String url = API_BASE_URL + String("/health/devices/") + deviceID + String("/state/pending");
    if (!http.begin(url)) return;
    
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        String response = http.getString();
        
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error) {
            bool hasPending = doc["has_pending"] | false;
            
            if (hasPending) {
                const char* newState = doc["state"];
                
                if (newState != nullptr) {
                    if (strcmp(newState, "monitoring") == 0) {
                        monitoringState = STATE_MONITORING;
                        monitoringStateStr = "monitoring";
                    } else if (strcmp(newState, "paused") == 0) {
                        monitoringState = STATE_PAUSED;
                        monitoringStateStr = "paused";
                    } else if (strcmp(newState, "idle") == 0) {
                        monitoringState = STATE_IDLE;
                        monitoringStateStr = "idle";
                    }
                }
            }
        }
    }
    
    http.end();
}

// ==================== WIFI MANAGEMENT ====================
void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.disconnect();
    delay(100);
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        time_t now;
        time(&now);
        if (now > 0) {
            timeInitialized = true;
            bootTimestamp = now - (millis() / 1000);
        }
    }
}

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED && !wifiReconnecting) {
        unsigned long now = millis();
        
        if (now - lastWiFiCheck > WIFI_RECONNECT_INTERVAL) {
            wifiReconnecting = true;
            
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
                wifiRetryCount = 0;
            } else {
                wifiRetryCount++;
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
    if (millis() - lastScreenChange > 10000) {
        currentScreen = (currentScreen + 1) % 2;
        lastScreenChange = millis();
    }
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    delay(100);
    
    esp_task_wdt_init(WATCHDOG_TIMEOUT, true);
    esp_task_wdt_add(NULL);
    
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    
    startButton.begin();
    stopButton.begin();
    
    pinMode(SEN11574_PIN, INPUT);
    
    Wire.begin(SDA_PIN, SCL_PIN);
    // Wire.setClock(400000); // Reverted to default 100kHz per user request
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VitalWatch v4.1");
    lcd.setCursor(0, 1);
    lcd.print("Production");
    lcd.setCursor(0, 2);
    lcd.print("Initializing...");
    
    preferences.begin("health", false);
    deviceID = "HEALTH_DEVICE_001";
    
    dallas.begin();
    tempSensor.begin();
    
    delay(100);
    max30102Sensor.begin();
    pulseSensor.begin();
    
    lcd.setCursor(0, 3);
    lcd.print("WiFi connecting...");
    connectWiFi();
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ready!");
    lcd.setCursor(0, 1);
    lcd.print("Press START button");
    delay(2000);
    
    lastScreenChange = millis();
    esp_task_wdt_reset();

    // --- I2C SCANNER ---
    Serial.println("Scanning I2C bus...");
    byte count = 0;
    for (byte i = 8; i < 120; i++) {
        Wire.beginTransmission(i);
        if (Wire.endTransmission() == 0) {
            Serial.print("Found I2C device at: 0x");
            Serial.println(i, HEX);
            count++;
        }
    }
    Serial.print("Found ");
    Serial.print(count);
    Serial.println(" device(s).");
    if (count == 0) Serial.println("No I2C devices found\n");
    else Serial.println("done\n");
}

// ==================== MAIN LOOP ====================
void loop() {
    static unsigned long lastSensorRead = 0;
    static unsigned long lastCloudSync = 0;
    static unsigned long lastLCDUpdate = 0;
    static unsigned long lastVitalUpdate = 0;
    
    esp_task_wdt_reset();
    
    handleButtons();
    checkWiFiConnection();
    
    if (millis() - lastStatePoll >= STATE_POLL_INTERVAL) {
        if (WiFi.status() == WL_CONNECTED) {
            checkRemoteStateCommand();
        }
        lastStatePoll = millis();
    }
    
    if (monitoringState == STATE_MONITORING) {
        max30102Sensor.update();
        
        if (millis() - lastSensorRead >= SENSOR_READ_INTERVAL) {
            pulseSensor.update();
            lastSensorRead = millis();
        }
        
        if (millis() - lastVitalUpdate >= VITALS_UPDATE_INTERVAL) {
            updateVitals();
            checkAlerts();
            lastVitalUpdate = millis();
        }
        
        if (millis() - lastCloudSync >= CLOUD_SYNC_INTERVAL) {
            if (WiFi.status() == WL_CONNECTED) {
                sendToCloud();
            }
            lastCloudSync = millis();
        }
    }
    
    if (millis() - lastLCDUpdate >= LCD_UPDATE_INTERVAL) {
        updateLCD();
        lastLCDUpdate = millis();
    }
    
    handleScreenRotation();
    
    delay(1);
}