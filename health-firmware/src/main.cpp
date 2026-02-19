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
const char* WIFI_SSID = "cybergenii";
const char* WIFI_PASSWORD = "12341234";
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
#define WATCHDOG_TIMEOUT 30
#define STATE_POLL_INTERVAL 10000

#define MIN_QUALITY_THRESHOLD 40
#define WIFI_MAX_RETRIES 5
#define WIFI_RETRY_BASE_DELAY 1000

// MAX30102 I2C address (standard; some modules allow 0x57 or 0x58 via ADDR pin)
#define MAX30102_I2C_ADDR 0x57
// IR: finger present when reflected IR is above this.
#define MAX30102_FINGER_THRESHOLD 4000
// RED: finger present when RED is above this (finger on ~200k+, removed ~6k).
#define MAX30102_FINGER_THRESHOLD_RED 15000
// 18-bit max = 262143. Above this we treat as saturated (no pulse visible).
#define MAX30102_SATURATED 250000

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
    int lastGoodRaw;
    
    int peakValue;
    int troughValue;
    unsigned long lastAdaptUpdate;
    
    int signalQuality;
    float spo2Value;
    int spo2Quality;
    int lastValidBPM;
    int lastValidSpO2;
    
    unsigned long rawPeakTimes[8];
    int rawPeakCount;
    int rawPeakIndex;
    bool rawPeakZone;
    int rawPeakZoneMax;
    unsigned long rawPeakZoneMaxTime;
    int bpmFromRaw;
    
public:
    PulseSensor() : bufferIndex(0), bufferFilled(false), lastBeatTime(0), currentBPM(0),
                    beatHistoryIndex(0), beatHistoryCount(0), dcLevel(2048), acAmplitude(0),
                    dynamicThreshold(2048), baselineLevel(2048), smoothedSignal(2048),
                    smoothAlpha(0.12), lastGoodRaw(2048), peakValue(0), troughValue(4095), lastAdaptUpdate(0),
                    signalQuality(0), spo2Value(0), spo2Quality(0), lastValidBPM(0), lastValidSpO2(0),
                    rawPeakCount(0), rawPeakIndex(0), rawPeakZone(false), rawPeakZoneMax(0), rawPeakZoneMaxTime(0), bpmFromRaw(0) {
        memset(signalBuffer, 0, sizeof(signalBuffer));
        memset(beatHistory, 0, sizeof(beatHistory));
        memset(rawPeakTimes, 0, sizeof(rawPeakTimes));
    }
    
    void begin() {
        pinMode(SEN11574_PIN, INPUT);
        analogReadResolution(12);
#if defined(ESP32)
        analogSetAttenuation(ADC_11db);
#endif
        delay(100);
        int sum = 0;
        int validSamples = 0;
        
        for (int i = 0; i < 50; i++) {
            int reading = analogRead(SEN11574_PIN);
            if (reading >= 0 && reading <= MAX_SIGNAL) {
                sum += reading;
                validSamples++;
            }
            delay(20);
        }
        
        if (validSamples > 0) {
            baselineLevel = sum / validSamples;
            dcLevel = baselineLevel;
            smoothedSignal = baselineLevel;
            dynamicThreshold = baselineLevel + 80;
        } else {
            baselineLevel = 2048;
            dcLevel = 2048;
            smoothedSignal = 2048;
            dynamicThreshold = 2128;
        }
    }
    
    void update() {
        int rawSignal = analogRead(SEN11574_PIN);
        if (rawSignal < 0 || rawSignal > MAX_SIGNAL) return;
        if (rawSignal <= 50 || rawSignal >= MAX_SIGNAL - 50) {
            rawSignal = lastGoodRaw;
        } else {
            lastGoodRaw = rawSignal;
        }
#ifdef DEBUG_SENSORS
        if (millis() % 500 < 5) {
            Serial.print(F("SEN11574: raw="));
            Serial.print(rawSignal);
            Serial.print(F(" dc="));
            Serial.print((int)dcLevel);
            Serial.print(F(" BPM="));
            Serial.println(currentBPM);
        }
#endif
        smoothedSignal = smoothedSignal * (1.0 - smoothAlpha) + rawSignal * smoothAlpha;
        int signal = (int)smoothedSignal;
        
        signalBuffer[bufferIndex] = signal;
        bufferIndex = (bufferIndex + 1) % WINDOW_SIZE;
        if (bufferIndex == 0) bufferFilled = true;
        
        if (!bufferFilled) return;
        
        updateSignalStats();
        detectBeat(signal, millis());
        updateBPMFromRaw(signal, millis());
        calculateSpO2();
        if (spo2Value > 0) lastValidSpO2 = (int)spo2Value;
        updateQuality();
    }
    
    void updateBPMFromRaw(int signal, unsigned long now) {
        int thresh = (int)(dcLevel + 0.35f * (acAmplitude > 20 ? acAmplitude : 20));
        if (signal > thresh) {
            if (!rawPeakZone) rawPeakZone = true;
            if (signal > rawPeakZoneMax) {
                rawPeakZoneMax = signal;
                rawPeakZoneMaxTime = now;
            }
        } else {
            if (rawPeakZone && rawPeakZoneMaxTime > 0) {
                rawPeakTimes[rawPeakIndex] = rawPeakZoneMaxTime;
                rawPeakIndex = (rawPeakIndex + 1) % 8;
                if (rawPeakCount < 8) rawPeakCount++;
                unsigned long lastInterval = 0;
                if (rawPeakCount >= 2) {
                    int prev = (rawPeakIndex - 2 + 8) % 8;
                    lastInterval = rawPeakTimes[(rawPeakIndex - 1 + 8) % 8] - rawPeakTimes[prev];
                }
                if (lastInterval >= 300 && lastInterval <= 2000) {
                    int bpm = (int)(60000 / (long)lastInterval);
                    if (bpm >= 40 && bpm <= 180) {
                        bpmFromRaw = bpm;
                        lastValidBPM = bpm;
                        if (currentBPM == 0) currentBPM = bpm;
                    }
                }
            }
            rawPeakZone = false;
            rawPeakZoneMax = 0;
        }
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
            
            // More aggressive threshold positioning
            if (range > 20) {
                // Position threshold at 40% of the way from trough to peak
                dynamicThreshold = troughValue + (range * 0.40);
            } else if (range > 10) {
                // For smaller signals, be more conservative
                dynamicThreshold = troughValue + (range * 0.35);
            } else {
                // Fallback to DC-based threshold
                dynamicThreshold = dcLevel + 50;
            }
            
            // Ensure threshold is reasonable
            dynamicThreshold = constrain(dynamicThreshold, minVal + 5, maxVal - 5);
            
            lastAdaptUpdate = millis();
        }
    }
    
    void detectBeat(int signal, unsigned long now) {
        static int lastSignal = 0;
        static bool aboveThreshold = false;
        
        // If this is the first reading, initialize lastBeatTime
        if (lastBeatTime == 0) {
            lastBeatTime = now;
            lastSignal = signal;
            return;
        }
        
        // Rising edge: signal crosses threshold from below
        if (signal > dynamicThreshold && lastSignal <= dynamicThreshold) {
            aboveThreshold = true;
        }
        
        // Falling edge: signal goes back below threshold after crossing above
        if (aboveThreshold && signal < dynamicThreshold && lastSignal >= dynamicThreshold) {
            aboveThreshold = false;
            
            unsigned long beatInterval = now - lastBeatTime;
            
            // Valid beat interval: 200ms to 2500ms (24-300 BPM)
            if (beatInterval > 200 && beatInterval < 2500) {
                int instantBPM = 60000 / beatInterval;
                
                bool isValid = true;
                if (beatHistoryCount > 2) {
                    int avgHistory = 0;
                    for (int i = 0; i < beatHistoryCount; i++) {
                        avgHistory += beatHistory[i];
                    }
                    avgHistory /= beatHistoryCount;
                    if (abs(instantBPM - avgHistory) > 50) isValid = false;
                } else {
                    isValid = (instantBPM >= 25 && instantBPM <= 220);
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
                    lastValidBPM = currentBPM;
                    lastBeatTime = now;
                }
            }
        }
        
        if (now - lastBeatTime > 3000 && lastBeatTime > 0) {
            currentBPM = 0;
            beatHistoryCount = 0;
        }
        
        lastSignal = signal;
    }
    
    void calculateSpO2() {
        if (acAmplitude < 10 || dcLevel < 200) {
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        float ratio = acAmplitude / dcLevel;
        spo2Value = constrain(110 - 25 * ratio, 70, 100);
        
        if (acAmplitude > 150 && signalQuality > 50) {
            spo2Quality = 85;
        } else if (acAmplitude > 80 && signalQuality > 30) {
            spo2Quality = 60;
        } else if (acAmplitude > 40) {
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
        
        if (acAmplitude > 150) quality += 40;
        else if (acAmplitude > 80) quality += 30;
        else if (acAmplitude > 40) quality += 20;
        else if (acAmplitude > 15) quality += 10;
        
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
        if (bpmFromRaw > 0 && hasPulseSignal()) quality = (quality < 45) ? 45 : quality;
        
        signalQuality = constrain(quality, 0, 100);
    }
    
    bool hasPulseSignal() {
        return bufferFilled && dcLevel >= 500 && dcLevel <= 3800 && acAmplitude > 12;
    }
    
    int getBPM() {
        if (!hasPulseSignal()) return 0;
        if (currentBPM > 0) return constrain(currentBPM, 25, 220);
        if (bpmFromRaw > 0) return bpmFromRaw;
        return lastValidBPM;
    }
    
    int getLastValidBPM() { return hasPulseSignal() ? lastValidBPM : 0; }
    
    int getSpO2() {
        if (signalQuality >= 20 && spo2Value > 0) return (int)spo2Value;
        if (hasPulseSignal() && lastValidSpO2 > 0) return lastValidSpO2;
        return 0;
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
    int lastValidBPM;
    int lastValidSpO2;
    
    static const int IR_RAW_BUF = 80;
    uint32_t irRawBuf[IR_RAW_BUF];
    unsigned long irRawTimeBuf[IR_RAW_BUF];
    int irRawHead;
    int irRawLen;
    int bpmFromRaw;
    
    int i2cNoDataCount;
    unsigned long i2cLastRecoveryMs;
    int i2cCooldownLeft;
    
    int computeBPMFromRaw() {
        if (irRawLen < (int)(IR_RAW_BUF - 2)) return 0;
        uint32_t minV = 0xFFFFFFFF, maxV = 0;
        for (int i = 0; i < irRawLen; i++) {
            int idx = (irRawHead + i) % IR_RAW_BUF;
            uint32_t v = irRawBuf[idx];
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        if (maxV <= minV || (maxV - minV) < 1000) return 0;
        uint32_t thresh = minV + (maxV - minV) / 3;
        int peakIdx[16];
        int nPeaks = 0;
        for (int i = 1; i < irRawLen - 1 && nPeaks < 16; i++) {
            int idx = (irRawHead + i) % IR_RAW_BUF;
            int idxL = (irRawHead + i - 1) % IR_RAW_BUF;
            int idxR = (irRawHead + i + 1) % IR_RAW_BUF;
            uint32_t v = irRawBuf[idx];
            if (v > thresh && v >= irRawBuf[idxL] && v >= irRawBuf[idxR])
                peakIdx[nPeaks++] = idx;
        }
        if (nPeaks < 2) return 0;
        long intervals[15];
        int nInt = 0;
        for (int i = 1; i < nPeaks; i++) {
            long dt = (long)(irRawTimeBuf[peakIdx[i]] - irRawTimeBuf[peakIdx[i-1]]);
            if (dt >= 300 && dt <= 2000) intervals[nInt++] = dt;
        }
        if (nInt == 0) return 0;
        for (int i = 0; i < nInt - 1; i++)
            for (int j = i + 1; j < nInt; j++)
                if (intervals[j] < intervals[i]) {
                    long t = intervals[i]; intervals[i] = intervals[j]; intervals[j] = t;
                }
        long medianMs = nInt % 2 ? intervals[nInt/2] : (intervals[nInt/2 - 1] + intervals[nInt/2]) / 2;
        int bpm = (int)(60000 / medianMs);
        if (bpm >= 40 && bpm <= 180) return bpm;
        return 0;
    }
    
public:
    MAX30102Sensor(MAX30105& max) : sensor(&max), available(false), rateSpot(0),
                                    lastBeat(0), beatsPerMinute(0), beatAvg(0),
                                    irValue(0), redValue(0), irDC(0), redDC(0),
                                    irAC(0), redAC(0), spo2Value(0), spo2Quality(0),
                                    irPeak(0), irTrough(0xFFFFFFFF), adaptiveThreshold(25000),
                                    lastThresholdUpdate(0), fingerDetected(false),
                                    lastValidBPM(0), lastValidSpO2(0),
                                    irRawHead(0), irRawLen(0), bpmFromRaw(0),
                                    i2cNoDataCount(0), i2cLastRecoveryMs(0), i2cCooldownLeft(0) {
        memset(rates, 0, sizeof(rates));
        memset(irRawBuf, 0, sizeof(irRawBuf));
        memset(irRawTimeBuf, 0, sizeof(irRawTimeBuf));
    }
    
    bool begin() {
        if (!sensor->begin(Wire, I2C_SPEED_STANDARD, MAX30102_I2C_ADDR)) {
            available = false;
            uint8_t partId = sensor->readPartID();
            Serial.println(F("MAX30102: begin() FAILED"));
            Serial.print(F("  Part ID read: 0x"));
            Serial.println(partId, HEX);
            Serial.println(F("  Expected 0x15. If 0x00: no device at 0x57 (check wiring/SDA/SCL)."));
            return false;
        }
        
        byte ledBrightness = 0x7F;   // 25 mA – balance: 0x5F too dim, 0xFF saturates
        byte sampleAverage = 4;
        byte ledMode = 2;   // Red + IR only (MAX30102 has no green)
        byte sampleRate = 100;
        int pulseWidth = 411;
        int adcRange = 4096;
        
        sensor->setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
        sensor->setPulseAmplitudeRed(0x7F);
        sensor->setPulseAmplitudeIR(0x7F);
        sensor->setPulseAmplitudeGreen(0);
        sensor->wakeUp();
        sensor->clearFIFO();
        
        delay(300);
        for (int i = 0; i < 30; i++) {
            sensor->check();
            delay(15);
        }
        uint32_t ir = sensor->getIR();
        if (ir > 0) irDC = ir;
        
        available = true;
        Serial.println(F("MAX30102: init OK"));
        return true;
    }
    
    void update() {
        if (!available) return;
        
        if (i2cCooldownLeft > 0) i2cCooldownLeft--;
        int checkCount = (i2cCooldownLeft > 0) ? 2 : 5;
        for (int i = 0; i < checkCount; i++) {
            sensor->check();
            delay(1);
            esp_task_wdt_reset();
        }
        if (sensor->available() > 0) {
            while (sensor->available() > 1) sensor->nextSample();
            irValue = sensor->getFIFOIR();
            redValue = sensor->getFIFORed();
            sensor->nextSample();
            i2cNoDataCount = 0;
        } else {
            i2cNoDataCount++;
            if (i2cNoDataCount >= 35 && (millis() - i2cLastRecoveryMs) >= 10000) {
                Wire.end();
                delay(80);
                Wire.begin(SDA_PIN, SCL_PIN);
                Wire.setTimeOut(2000);
                Wire.setClock(100000);
                i2cNoDataCount = 0;
                i2cLastRecoveryMs = millis();
                i2cCooldownLeft = 15;
            }
        }
        // When FIFO empty keep previous values; do not block on getIR()/getRed()
        
        bool saturated = (irValue >= MAX30102_SATURATED || redValue >= MAX30102_SATURATED);
        if (saturated) {
            // Don't use saturated readings for DC/AC/beat – signal is flat, BPM would stay 0
            irValue = (irValue >= MAX30102_SATURATED && irDC > 0) ? (uint32_t)irDC : irValue;
            redValue = (redValue >= MAX30102_SATURATED && redDC > 0) ? (uint32_t)redDC : redValue;
        }
        
        bool wasDetected = fingerDetected;
        fingerDetected = (irValue > MAX30102_FINGER_THRESHOLD) && (redValue > MAX30102_FINGER_THRESHOLD_RED); 

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
            irPeak = 0;
            irTrough = 0xFFFFFFFF;
            lastBeat = millis();
            adaptiveThreshold = MAX30102_FINGER_THRESHOLD + 10000;
            lastThresholdUpdate = millis();
        } else if (!fingerDetected && wasDetected) {
            reset();
            return;
        }
        
        if (!fingerDetected) return;
        
        if (irValue < MAX30102_SATURATED) {
            irRawBuf[irRawHead] = irValue;
            irRawTimeBuf[irRawHead] = millis();
            irRawHead = (irRawHead + 1) % IR_RAW_BUF;
            if (irRawLen < IR_RAW_BUF) irRawLen++;
        }
        bpmFromRaw = computeBPMFromRaw();
        if (bpmFromRaw > 0) lastValidBPM = bpmFromRaw;
        
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
            if (beatAvg > 0) lastValidBPM = beatAvg;
        }
        
        calculateSpO2();
        if (spo2Value > 0) lastValidSpO2 = spo2Value;
    }
    
    void updateThreshold() {
        if (!fingerDetected) return;
        
        // Track peak and trough with better initialization
        if (irPeak == 0 || irValue > irPeak) irPeak = irValue;
        if (irTrough == 0xFFFFFFFF || (irValue < irTrough && irValue > MAX30102_FINGER_THRESHOLD)) {
            irTrough = irValue;
        }
        
        if (millis() - lastThresholdUpdate > 500) {
            // Decay peak slowly, allow trough to rise
            if (irPeak > 0) irPeak = irPeak * 0.92;
            if (irTrough < irValue * 1.5 && irTrough != 0xFFFFFFFF) {
                irTrough = irTrough * 1.08;
            }
            
            // Calculate threshold based on peak/trough or DC level
            if (irPeak > 0 && irTrough < 0xFFFFFFFF && irPeak > irTrough) {
                uint32_t range = irPeak - irTrough;
                adaptiveThreshold = irTrough + (range * 0.4);
            } else {
                // Fallback: use DC-based threshold
                adaptiveThreshold = irDC * 1.05;
            }
            
            // Keep threshold in reasonable range
            adaptiveThreshold = constrain(adaptiveThreshold, (uint32_t)MAX30102_FINGER_THRESHOLD, (uint32_t)(irDC + 50000));
            lastThresholdUpdate = millis();
        }
    }
    
    bool detectBeat(uint32_t sample) {
        static uint32_t lastSample = 0;
        static bool risingEdge = false;
        static unsigned long lastBeatTime = 0;
        
        // Initialize lastBeatTime on first call
        if (lastBeatTime == 0) {
            lastBeatTime = millis();
            lastSample = sample;
            return false;
        }
        
        // Rising edge detection: signal crosses threshold from below
        if (sample > adaptiveThreshold && lastSample <= adaptiveThreshold) {
            risingEdge = true;
        }
        
        // Falling edge detection: confirm beat only on downward crossing
        if (risingEdge && sample < adaptiveThreshold && lastSample >= adaptiveThreshold) {
            unsigned long now = millis();
            unsigned long beatInterval = now - lastBeatTime;
            
            // Valid beat interval: 300ms to 2500ms (24-200 BPM)
            if (beatInterval > 300 && beatInterval < 2500) {
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
        if (!fingerDetected || irDC < MAX30102_FINGER_THRESHOLD || redDC < MAX30102_FINGER_THRESHOLD || fabs(irAC) < 30) {
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        float ratioRMS = (fabs(redAC) / redDC) / (fabs(irAC) / irDC);
        spo2Value = constrain((int)(110 - 25 * ratioRMS), 70, 100);
        
        if (irValue > 80000 && beatAvg > 0) {
            spo2Quality = 95;
        } else if (irValue > 50000 && beatAvg > 0) {
            spo2Quality = 80;
        } else if (irValue > 30000) {
            spo2Quality = 60;
        } else if (irValue > MAX30102_FINGER_THRESHOLD) {
            spo2Quality = 40;
        } else {
            spo2Quality = 20;
        }
    }
    
    int getBPM() {
        if (!available || !fingerDetected) return 0;
        if (millis() - lastBeat <= 3000 && beatAvg > 0) return beatAvg;
        if (bpmFromRaw > 0) return bpmFromRaw;
        return lastValidBPM;
    }
    
    int getLastValidBPM() { return fingerDetected ? lastValidBPM : 0; }
    
    int getSpO2() {
        if (available && fingerDetected && spo2Value > 0) return spo2Value;
        if (lastValidSpO2 > 0) return lastValidSpO2;
        return 0;
    }
    
    int getHRQuality() {
        if (!available || !fingerDetected) {
            if (lastValidBPM > 0) return 25;
            return 0;
        }
        if (bpmFromRaw > 0) return 45;
        unsigned long timeSinceBeat = millis() - lastBeat;
        if (irValue > 80000 && beatAvg > 0 && timeSinceBeat < 1200) return 95;
        else if (irValue > 50000 && beatAvg > 0 && timeSinceBeat < 2000) return 75;
        else if (irValue > 30000 && timeSinceBeat < 3000) return 50;
        else if (irValue > MAX30102_FINGER_THRESHOLD) return 30;
        if (lastValidBPM > 0) return 25;
        return 20;
    }
    
    int getSpO2Quality() {
        if (!available || !fingerDetected) {
            if (lastValidSpO2 > 0) return 25;
            return 0;
        }
        if (lastValidSpO2 > 0 && spo2Value == 0) return 25;
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
    static int lastReportedBPM = 0;
    static unsigned long lastReportedBPMTime = 0;
    const unsigned long BPM_HOLD_MS = 15000;
    
    int max30102_hr = max30102Sensor.getBPM();
    int max30102_spo2 = max30102Sensor.getSpO2();
    int max30102_hrQuality = max30102Sensor.getHRQuality();
    int max30102_spo2Quality = max30102Sensor.getSpO2Quality();
    
    int sen11574_hr = pulseSensor.getBPM();
    int sen11574_spo2 = pulseSensor.getSpO2();
    int sen11574_hrQuality = pulseSensor.getSignalQuality();
    int sen11574_spo2Quality = pulseSensor.getSpO2Quality();
    
    bool fingerOnMax = max30102Sensor.isFingerDetected();
    
    if (!fingerOnMax) {
        currentVitals.heartRate = 0;
        currentVitals.hrQuality = 0;
        currentVitals.hrSource = "NONE";
        lastReportedBPM = 0;
    }
    else if (max30102_hr > 0 && max30102_hrQuality >= MIN_QUALITY_THRESHOLD) {
        currentVitals.heartRate = max30102_hr;
        currentVitals.hrQuality = max30102_hrQuality;
        currentVitals.hrSource = "MAX30102";
        lastReportedBPM = max30102_hr;
        lastReportedBPMTime = millis();
    }
    else if (sen11574_hr > 0 && sen11574_hrQuality >= MIN_QUALITY_THRESHOLD) {
        currentVitals.heartRate = sen11574_hr;
        currentVitals.hrQuality = sen11574_hrQuality;
        currentVitals.hrSource = "SEN11574";
        lastReportedBPM = sen11574_hr;
        lastReportedBPMTime = millis();
    }
    else {
        int lastMax = max30102Sensor.getLastValidBPM();
        int lastSen = pulseSensor.getLastValidBPM();
        int fusedBPM = 0;
        if (lastMax > 0 && lastSen > 0)
            fusedBPM = (lastMax + lastSen) / 2;
        else if (lastMax > 0)
            fusedBPM = lastMax;
        else if (lastSen > 0)
            fusedBPM = lastSen;
        if (fusedBPM > 0) {
            currentVitals.heartRate = fusedBPM;
            currentVitals.hrQuality = 25;
            currentVitals.hrSource = (lastMax > 0 && lastSen > 0) ? "Fused" : "Held";
            lastReportedBPM = fusedBPM;
            lastReportedBPMTime = millis();
        } else if (lastReportedBPM > 0 && (millis() - lastReportedBPMTime) < BPM_HOLD_MS) {
            currentVitals.heartRate = lastReportedBPM;
            currentVitals.hrQuality = 25;
            currentVitals.hrSource = "Held";
        } else {
            currentVitals.heartRate = 0;
            currentVitals.hrQuality = 0;
            currentVitals.hrSource = "NONE";
        }
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
    Wire.setTimeOut(2000);
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
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    delay(50);
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
    } else {
        static unsigned long lastPulseIdle = 0;
        if (millis() - lastPulseIdle >= 20) {
            pulseSensor.update();
            lastPulseIdle = millis();
        }
    }
    
    if (millis() - lastLCDUpdate >= LCD_UPDATE_INTERVAL) {
        updateLCD();
        lastLCDUpdate = millis();
    }
    
    handleScreenRotation();
    
    delay(1);
}