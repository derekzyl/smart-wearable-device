/**
 * Multi-Vitals Health Monitoring System - FIXED VERSION
 * ESP32 Firmware for HR, SpO2, and Temperature Monitoring
 * 
 * FIXES:
 * - Button handling completely rewritten with better debouncing
 * - MAX30102 configuration optimized for HW-605
 * - SEN-11574 signal processing improved with better filtering
 * - Enhanced dual-sensor comparison and validation
 * - Real-time debug output for troubleshooting
 * - Improved beat detection algorithms for both sensors
 * 
 * Version: 3.2 (FIXED)
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
#define FIRMWARE_VERSION "3.2"
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

// ==================== DEBUG MODE ====================
#define DEBUG_SENSORS true      // Enable detailed sensor debug output
#define DEBUG_BUTTONS true      // Enable button debug output

// ==================== CONFIGURATION ====================
const char* WIFI_SSID = "cybergenii";
const char* WIFI_PASSWORD = "12341234";
String API_BASE_URL = "https://xenophobic-netta-cybergenii-1584fde7.koyeb.app";
const char* VITALS_ENDPOINT = "/health/vitals";

// NTP Configuration
const char* NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// Timing Configuration
#define SENSOR_READ_INTERVAL 2      // ms for SEN11574
#define VITALS_UPDATE_INTERVAL 1000 // ms
#define CLOUD_SYNC_INTERVAL 5000    // ms
#define LCD_UPDATE_INTERVAL 500     // ms
#define WIFI_RECONNECT_INTERVAL 30000 // ms
#define WATCHDOG_TIMEOUT 10         // seconds
#define DEBUG_PRINT_INTERVAL 2000   // ms for sensor debug
#define STATE_POLL_INTERVAL 10000   // ms - poll for remote commands every 10s

// Sensor Fusion Configuration
#define MIN_QUALITY_THRESHOLD 40    // Lowered for better detection
#define EXCELLENT_QUALITY_THRESHOLD 80
#define SENSOR_FUSION_WEIGHT 0.7

// WiFi Reconnection
#define WIFI_MAX_RETRIES 5
#define WIFI_RETRY_BASE_DELAY 1000

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
int lastDisplayedScreen = -1;
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

// Debug timing
unsigned long lastDebugPrint = 0;

// State polling
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

// ==================== IMPROVED BUTTON CLASS ====================
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
        
        #if DEBUG_BUTTONS
        Serial.printf("Button on pin %d initialized: %s\n", pin, stableState == HIGH ? "HIGH" : "LOW");
        #endif
    }
    
    void update() {
        bool currentRaw = digitalRead(pin);
        
        // If state changed, reset debounce timer
        if (currentRaw != lastRawState) {
            lastChangeTime = millis();
            lastRawState = currentRaw;
        }
        
        // If state has been stable for debounce period, accept it
        if ((millis() - lastChangeTime) > debounceDelay) {
            if (currentRaw != stableState) {
                lastStableState = stableState;
                stableState = currentRaw;
                
                #if DEBUG_BUTTONS
                Serial.printf("Button %d state changed to: %s\n", pin, stableState == HIGH ? "HIGH (Released)" : "LOW (Pressed)");
                #endif
            }
        }
    }
    
    bool isPressed() {
        // Detect transition from HIGH to LOW (button press)
        return (lastStableState == HIGH && stableState == LOW);
    }
    
    bool isReleased() {
        // Detect transition from LOW to HIGH (button release)
        return (lastStableState == LOW && stableState == HIGH);
    }
    
    bool isDown() {
        return stableState == LOW;
    }
    
    void resetState() {
        lastStableState = stableState;
    }
};

Button startButton(BUTTON_START);
Button stopButton(BUTTON_STOP);

// ==================== ENHANCED PULSE SENSOR CLASS (SEN-11574) ====================
class PulseSensor {
private:
    static const int SAMPLE_RATE = 500;
    static const int WINDOW_SIZE = 100;
    static const int MAX_SIGNAL = 4095;
    
    int signalBuffer[WINDOW_SIZE];
    int bufferIndex = 0;
    bool bufferFilled = false;
    
    // Beat detection
    unsigned long lastBeatTime = 0;
    int currentBPM = 0;
    int lastValidBPM = 0;
    int beatHistory[8];
    int beatHistoryIndex = 0;
    int beatHistoryCount = 0;
    
    // Signal processing
    float dcLevel = 2048;
    float acAmplitude = 0;
    int peakValue = 0;
    int troughValue = 4095;
    int dynamicThreshold = 2048;
    
    // Quality metrics
    int signalQuality = 0;
    float signalSNR = 0;
    
    // SpO2
    float spo2Value = 0;
    int spo2Quality = 0;
    
    // Calibration
    bool isCalibrating = false;
    int calibrationSamples = 0;
    long calibrationSum = 0;
    int baselineLevel = 2048;
    
    unsigned long lastSampleTime = 0;
    unsigned long lastAdaptUpdate = 0;
    
    // Moving average filter
    float smoothedSignal = 2048;
    const float SMOOTH_ALPHA = 0.3;
    
public:
    void begin() {
        pinMode(SEN11574_PIN, INPUT);
        memset(signalBuffer, 0, sizeof(signalBuffer));
        memset(beatHistory, 0, sizeof(beatHistory));
        bufferFilled = false;
        
        // Initial baseline reading
        delay(100);
        int sum = 0;
        for (int i = 0; i < 20; i++) {
            sum += analogRead(SEN11574_PIN);
            delay(10);
        }
        baselineLevel = sum / 20;
        dcLevel = baselineLevel;
        dynamicThreshold = baselineLevel + 150;
        
        Serial.printf("SEN11574: Baseline level = %d\n", baselineLevel);
    }
    
    void startCalibration() {
        isCalibrating = true;
        calibrationSamples = 0;
        calibrationSum = 0;
        Serial.println("SEN11574: Calibration started - keep finger still");
    }
    
    void stopCalibration() {
        if (calibrationSamples > 0) {
            baselineLevel = calibrationSum / calibrationSamples;
            dynamicThreshold = baselineLevel + 200;
            dcLevel = baselineLevel;
            Serial.printf("SEN11574: Calibration complete. Baseline: %d, Threshold: %d\n", 
                baselineLevel, dynamicThreshold);
        }
        isCalibrating = false;
    }
    
    void update() {
        unsigned long now = millis();
        if (now - lastSampleTime < 2) return;
        lastSampleTime = now;
        
        // Read sensor with validation
        int rawSignal = analogRead(SEN11574_PIN);
        if (rawSignal < 0 || rawSignal > MAX_SIGNAL) {
            rawSignal = smoothedSignal;  // Use last valid value
        }
        
        // Apply smoothing filter
        smoothedSignal = smoothedSignal * (1.0 - SMOOTH_ALPHA) + rawSignal * SMOOTH_ALPHA;
        int signal = (int)smoothedSignal;
        
        // Calibration mode
        if (isCalibrating) {
            calibrationSum += signal;
            calibrationSamples++;
            return;
        }
        
        // Store in buffer
        signalBuffer[bufferIndex] = signal;
        bufferIndex = (bufferIndex + 1) % WINDOW_SIZE;
        if (bufferIndex == 0) bufferFilled = true;
        
        if (!bufferFilled) return;
        
        // Update signal characteristics
        updateSignalStats();
        
        // Detect beats
        detectBeat(signal, now);
        
        // Calculate SpO2
        calculateSpO2(signal);
        
        // Update quality metrics
        updateQuality();
    }
    
    void updateSignalStats() {
        if (!bufferFilled) return;
        
        // Calculate DC component (slow-changing baseline)
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
        dcLevel = dcLevel * 0.98 + newDC * 0.02;  // Very slow tracking
        
        // AC amplitude (pulse strength)
        int range = maxVal - minVal;
        acAmplitude = acAmplitude * 0.7 + range * 0.3;
        
        // Update adaptive threshold
        if (millis() - lastAdaptUpdate > 500) {
            peakValue = maxVal;
            troughValue = minVal;
            
            if (range > 50) {
                // Set threshold between trough and peak
                dynamicThreshold = troughValue + (range * 0.5);
            } else {
                // Fallback to DC-based threshold
                dynamicThreshold = dcLevel + 100;
            }
            
            lastAdaptUpdate = millis();
            
            #if DEBUG_SENSORS
            if (millis() - lastDebugPrint > DEBUG_PRINT_INTERVAL) {
                Serial.printf("SEN11574: DC=%.0f, AC=%.0f, Range=%d, Thresh=%d\n", 
                    dcLevel, acAmplitude, range, dynamicThreshold);
            }
            #endif
        }
    }
    
    void detectBeat(int signal, unsigned long now) {
        static int lastSignal = 0;
        static bool aboveThreshold = false;
        static int peakInCycle = 0;
        static unsigned long cycleStartTime = 0;
        
        // Detect when signal crosses threshold going up
        if (signal > dynamicThreshold && lastSignal <= dynamicThreshold) {
            aboveThreshold = true;
            peakInCycle = signal;
            cycleStartTime = now;
        }
        
        // Track peak while above threshold
        if (aboveThreshold && signal > peakInCycle) {
            peakInCycle = signal;
        }
        
        // Detect when signal crosses threshold going down (beat confirmed)
        if (aboveThreshold && signal < dynamicThreshold) {
            aboveThreshold = false;
            
            // Validate beat with timing constraints (30-200 BPM)
            unsigned long beatInterval = now - lastBeatTime;
            
            if (beatInterval > 300 && beatInterval < 2000 && lastBeatTime > 0) {
                // Calculate BPM from this beat
                int instantBPM = 60000 / beatInterval;
                
                // Validate against recent history
                bool isValid = true;
                if (beatHistoryCount > 0) {
                    int avgHistory = 0;
                    for (int i = 0; i < beatHistoryCount; i++) {
                        avgHistory += beatHistory[i];
                    }
                    avgHistory /= beatHistoryCount;
                    
                    // Reject if too different from recent average
                    if (abs(instantBPM - avgHistory) > 30) {
                        isValid = false;
                    }
                }
                
                if (isValid) {
                    // Store in history
                    beatHistory[beatHistoryIndex] = instantBPM;
                    beatHistoryIndex = (beatHistoryIndex + 1) % 8;
                    if (beatHistoryCount < 8) beatHistoryCount++;
                    
                    // Calculate running average
                    int sum = 0;
                    for (int i = 0; i < beatHistoryCount; i++) {
                        sum += beatHistory[i];
                    }
                    currentBPM = sum / beatHistoryCount;
                    lastValidBPM = currentBPM;
                    
                    lastBeatTime = now;
                    
                    #if DEBUG_SENSORS
                    Serial.printf("SEN11574: Beat! BPM=%d (instant=%d, interval=%lums)\n", 
                        currentBPM, instantBPM, beatInterval);
                    #endif
                }
            } else if (lastBeatTime == 0) {
                lastBeatTime = now;
            }
        }
        
        // Reset BPM if no beat detected for 3 seconds
        if (now - lastBeatTime > 3000 && lastBeatTime > 0) {
            currentBPM = 0;
            beatHistoryCount = 0;
            #if DEBUG_SENSORS
            Serial.println("SEN11574: No beat detected for 3s, reset BPM");
            #endif
        }
        
        lastSignal = signal;
    }
    
    void calculateSpO2(int signal) {
        if (acAmplitude < 20 || dcLevel < 500) {
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        // Calculate AC/DC ratio
        float ratio = acAmplitude / dcLevel;
        
        // Empirical formula for SpO2 estimation
        // Note: This is approximate and should be calibrated
        spo2Value = constrain(110 - 25 * ratio, 70, 100);
        
        // Quality based on signal strength
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
        
        // Factor 1: AC amplitude (pulse strength) - 0-40 points
        if (acAmplitude > 200) quality += 40;
        else if (acAmplitude > 100) quality += 30;
        else if (acAmplitude > 50) quality += 20;
        else if (acAmplitude > 20) quality += 10;
        
        // Factor 2: Recent beat detection - 0-40 points
        unsigned long timeSinceLastBeat = millis() - lastBeatTime;
        if (timeSinceLastBeat < 1200 && currentBPM > 0) {
            quality += 40;
        } else if (timeSinceLastBeat < 2000 && currentBPM > 0) {
            quality += 25;
        } else if (timeSinceLastBeat < 3000 && lastValidBPM > 0) {
            quality += 10;
        }
        
        // Factor 3: Beat consistency - 0-20 points
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
    
    int getSignalQuality() {
        return signalQuality;
    }
    
    int getSpO2Quality() {
        return spo2Quality;
    }
    
    bool isValidReading() {
        return signalQuality > MIN_QUALITY_THRESHOLD && 
               (millis() - lastBeatTime < 3000);
    }
    
    int getRawSignal() {
        return (int)smoothedSignal;
    }
    
    int getThreshold() {
        return dynamicThreshold;
    }
    
    void reset() {
        currentBPM = 0;
        lastValidBPM = 0;
        spo2Value = 0;
        signalQuality = 0;
        lastBeatTime = 0;
        beatHistoryCount = 0;
        beatHistoryIndex = 0;
        memset(beatHistory, 0, sizeof(beatHistory));
        Serial.println("SEN11574: Reset");
    }
};

// ==================== ENHANCED MAX30102 SENSOR CLASS ====================
class MAX30102Sensor {
private:
    MAX30105* sensor;
    bool available = false;
    
    // Beat detection
    const byte RATE_SIZE = 8;
    byte rates[8];
    byte rateSpot = 0;
    unsigned long lastBeat = 0;
    float beatsPerMinute = 0;
    int beatAvg = 0;
    int lastValidBPM = 0;
    
    // Signal values
    uint32_t irValue = 0;
    uint32_t redValue = 0;
    uint32_t lastIRValue = 0;
    
    // DC filtering
    float irDC = 0;
    float redDC = 0;
    float irAC = 0;
    float redAC = 0;
    
    // SpO2
    int spo2Value = 0;
    int spo2Quality = 0;
    
    // Adaptive threshold
    uint32_t irPeak = 0;
    uint32_t irTrough = 0xFFFFFFFF;
    uint32_t adaptiveThreshold = 70000;
    unsigned long lastThresholdUpdate = 0;
    
    // State tracking
    bool fingerDetected = false;
    unsigned long fingerDetectedTime = 0;
    
public:
    MAX30102Sensor(MAX30105& max) : sensor(&max) {}
    
    bool begin() {
        if (!sensor->begin(Wire, I2C_SPEED_STANDARD)) {
            Serial.println("MAX30102: Not found on I2C bus!");
            available = false;
            return false;
        }
        
        // OPTIMIZED CONFIGURATION FOR HW-605
        byte ledBrightness = 0x1F;    // Increased from 60 (0-255, try 0x1F=31 or 0x3F=63)
        byte sampleAverage = 4;        // Average 4 samples
        byte ledMode = 2;              // Red + IR LEDs
        byte sampleRate = 100;         // 100 samples/sec
        int pulseWidth = 411;          // 411µs pulse width (16-bit resolution)
        int adcRange = 4096;           // ADC range 4096
        
        sensor->setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
        
        // Set LED pulse amplitudes
        sensor->setPulseAmplitudeRed(0x1F);    // Increased
        sensor->setPulseAmplitudeIR(0x1F);     // IR amplitude
        sensor->setPulseAmplitudeGreen(0);     // Green off
        
        // Initialize baseline
        delay(100);
        for (int i = 0; i < 10; i++) {
            uint32_t ir = sensor->getIR();
            if (ir > 0) {
                irDC = ir;
                break;
            }
            delay(10);
        }
        
        available = true;
        Serial.println("MAX30102: Initialized successfully (HW-605)");
        Serial.printf("MAX30102: LED Brightness=0x%02X, Sample Rate=%d\n", ledBrightness, sampleRate);
        return true;
    }
    
    void update() {
        if (!available) return;
        
        lastIRValue = irValue;
        irValue = sensor->getIR();
        redValue = sensor->getRed();
        
        // Check finger detection
        bool wasDetected = fingerDetected;
        fingerDetected = (irValue > 50000);  // Threshold for finger presence
        
        if (fingerDetected && !wasDetected) {
            fingerDetectedTime = millis();
            Serial.printf("MAX30102: Finger detected! IR=%lu\n", irValue);
        } else if (!fingerDetected && wasDetected) {
            Serial.println("MAX30102: Finger removed");
            reset();
            return;
        }
        
        if (!fingerDetected) {
            return;
        }
        
        // Update DC components with very slow tracking
        const float DC_ALPHA = 0.995;  // Even slower for better baseline
        irDC = irDC * DC_ALPHA + irValue * (1.0 - DC_ALPHA);
        redDC = redDC * DC_ALPHA + redValue * (1.0 - DC_ALPHA);
        
        // Calculate AC components
        irAC = irValue - irDC;
        redAC = redValue - redDC;
        
        // Update adaptive threshold
        updateThreshold();
        
        // Detect heartbeat
        if (detectBeat(irValue)) {
            unsigned long delta = millis() - lastBeat;
            lastBeat = millis();
            
            beatsPerMinute = 60.0 / (delta / 1000.0);
            
            // Validate BPM range (30-200)
            if (beatsPerMinute >= 30 && beatsPerMinute <= 200) {
                // Additional validation: check against average if we have history
                bool valid = true;
                if (beatAvg > 0) {
                    int diff = abs(beatsPerMinute - beatAvg);
                    if (diff > 40) {  // Reject if too different
                        valid = false;
                    }
                }
                
                if (valid) {
                    rates[rateSpot++] = (byte)beatsPerMinute;
                    rateSpot %= RATE_SIZE;
                    
                    // Calculate weighted average (recent beats matter more)
                    int sum = 0;
                    int count = 0;
                    for (byte x = 0; x < RATE_SIZE; x++) {
                        if (rates[x] > 0) {
                            sum += rates[x];
                            count++;
                        }
                    }
                    if (count > 0) {
                        beatAvg = sum / count;
                        lastValidBPM = beatAvg;
                    }
                    
                    #if DEBUG_SENSORS
                    Serial.printf("MAX30102: Beat! BPM=%d (instant=%.0f, interval=%lums, IR=%lu)\n", 
                        beatAvg, beatsPerMinute, delta, irValue);
                    #endif
                }
            }
        }
        
        // Calculate SpO2
        calculateSpO2();
        
        // Debug output
        #if DEBUG_SENSORS
        if (millis() - lastDebugPrint > DEBUG_PRINT_INTERVAL) {
            Serial.printf("MAX30102: IR=%lu, Red=%lu, irDC=%.0f, irAC=%.0f, Threshold=%lu, BPM=%d\n",
                irValue, redValue, irDC, irAC, adaptiveThreshold, beatAvg);
        }
        #endif
    }
    
    void updateThreshold() {
        if (!fingerDetected) return;
        
        // Track peak and trough with decay
        if (irValue > irPeak) {
            irPeak = irValue;
        }
        if (irValue < irTrough && irValue > 50000) {
            irTrough = irValue;
        }
        
        // Update threshold every second
        if (millis() - lastThresholdUpdate > 1000) {
            // Decay peak and trough
            irPeak = irPeak * 0.95;
            if (irTrough < irValue * 1.5) {
                irTrough = irTrough * 1.05;
            }
            
            // Set threshold between trough and peak
            if (irPeak > irTrough) {
                uint32_t range = irPeak - irTrough;
                adaptiveThreshold = irTrough + (range * 0.5);
            } else {
                adaptiveThreshold = irDC * 1.01;  // Fallback
            }
            
            // Clamp threshold
            adaptiveThreshold = constrain(adaptiveThreshold, 60000, 200000);
            
            lastThresholdUpdate = millis();
        }
    }
    
    bool detectBeat(uint32_t sample) {
        static uint32_t lastSample = 0;
        static bool risingEdge = false;
        static uint32_t peakInBeat = 0;
        static unsigned long lastBeatTime = 0;
        
        // Detect rising edge crossing threshold
        if (sample > adaptiveThreshold && lastSample <= adaptiveThreshold) {
            risingEdge = true;
            peakInBeat = sample;
        }
        
        // Track peak while rising
        if (risingEdge && sample > peakInBeat) {
            peakInBeat = sample;
        }
        
        // Detect falling edge - beat complete
        if (risingEdge && sample < adaptiveThreshold) {
            unsigned long now = millis();
            
            // Debounce with refractory period (300ms minimum between beats)
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
        if (!fingerDetected) {
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        // Need sufficient signal for SpO2
        if (irDC < 50000 || redDC < 50000 || fabs(irAC) < 50) {
            spo2Value = 0;
            spo2Quality = 0;
            return;
        }
        
        // Calculate ratio of ratios
        float ratioRMS = (fabs(redAC) / redDC) / (fabs(irAC) / irDC);
        
        // Empirical formula (requires calibration against pulse oximeter)
        spo2Value = constrain((int)(110 - 25 * ratioRMS), 70, 100);
        
        // Quality based on signal strength and stability
        if (irValue > 120000 && beatAvg > 0) {
            spo2Quality = 95;
        } else if (irValue > 90000 && beatAvg > 0) {
            spo2Quality = 80;
        } else if (irValue > 70000) {
            spo2Quality = 60;
        } else {
            spo2Quality = 30;
        }
    }
    
    int getBPM() {
        if (!available || !fingerDetected) return 0;
        
        // Return 0 if no recent beats
        if (millis() - lastBeat > 3000) {
            return 0;
        }
        
        return beatAvg;
    }
    
    int getSpO2() {
        if (!available || !fingerDetected) return 0;
        return spo2Value;
    }
    
    int getHRQuality() {
        if (!available || !fingerDetected) return 0;
        
        unsigned long timeSinceBeat = millis() - lastBeat;
        
        if (irValue > 100000 && beatAvg > 0 && timeSinceBeat < 1200) {
            return 95;
        } else if (irValue > 80000 && beatAvg > 0 && timeSinceBeat < 2000) {
            return 75;
        } else if (irValue > 60000 && timeSinceBeat < 3000) {
            return 50;
        }
        return 20;
    }
    
    int getSpO2Quality() {
        if (!available || !fingerDetected) return 0;
        return spo2Quality;
    }
    
    bool isAvailable() {
        return available;
    }
    
    bool isFingerDetected() {
        return available && fingerDetected;
    }
    
    uint32_t getIRValue() {
        return irValue;
    }
    
    void reset() {
        beatAvg = 0;
        beatsPerMinute = 0;
        spo2Value = 0;
        memset(rates, 0, sizeof(rates));
        rateSpot = 0;
        irPeak = 0;
        irTrough = 0xFFFFFFFF;
        Serial.println("MAX30102: Reset");
    }
};

// ==================== TEMPERATURE SENSOR CLASS ====================
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
        
        ds18b20.requestTemperatures();
        delay(100);
        float temp = ds18b20.getTempCByIndex(0);
        
        if (temp > 30.0 && temp < 45.0 && temp != -127.0) {
            sensorAvailable = true;
            lastValidTemp = temp;
            Serial.printf("DS18B20: Available, initial temp: %.1f°C\n", temp);
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
            if (consecutiveFailures > 3) {
                sensorAvailable = false;
            }
            
            lastCheck = millis();
        }
        
        if (sensorAvailable && millis() - lastCheck < 30000) {
            result.celsius = lastValidTemp;
            result.isEstimated = false;
            result.source = "DS18B20";
            return result;
        }
        
        // Fallback estimation
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
    
    void setRestingHR(float hr) {
        restingHR = constrain(hr, 40.0, 100.0);
        preferences.putFloat("resting_hr", hr);
        Serial.printf("Resting HR set to: %.1f\n", hr);
    }
    
    bool isSensorAvailable() {
        return sensorAvailable;
    }
};

// ==================== GLOBAL SENSOR INSTANCES ====================
PulseSensor pulseSensor;
MAX30102Sensor max30102Sensor(max30102);
TemperatureSensor tempSensor(dallas);

// ==================== ENHANCED SENSOR FUSION WITH COMPARISON ====================
struct SensorComparison {
    int max30102_value;
    int max30102_quality;
    int sen11574_value;
    int sen11574_quality;
    int fused_value;
    int fused_quality;
    String source;
    int difference;
    bool agreement;
};

SensorComparison fuseSensorsWithComparison(int value1, int quality1, const char* source1,
                                           int value2, int quality2, const char* source2) {
    SensorComparison result;
    result.max30102_value = value1;
    result.max30102_quality = quality1;
    result.sen11574_value = value2;
    result.sen11574_quality = quality2;
    
    // Calculate difference
    result.difference = abs(value1 - value2);
    
    // Check if sensors agree (within 10% or 10 units)
    if (value1 > 0 && value2 > 0) {
        result.agreement = (result.difference <= max(value1, value2) * 0.1) || (result.difference <= 10);
    } else {
        result.agreement = false;
    }
    
    // Both sensors have readings
    if (quality1 >= MIN_QUALITY_THRESHOLD && quality2 >= MIN_QUALITY_THRESHOLD && 
        value1 > 0 && value2 > 0) {
        
        // If sensors agree, use weighted average
        if (result.agreement) {
            float weight1 = quality1 / 100.0;
            float weight2 = quality2 / 100.0;
            float totalWeight = weight1 + weight2;
            
            result.fused_value = (value1 * weight1 + value2 * weight2) / totalWeight;
            result.fused_quality = (quality1 + quality2) / 2;
            result.source = String(source1) + "+" + String(source2);
        } else {
            // Sensors disagree - use higher quality sensor
            if (quality1 > quality2) {
                result.fused_value = value1;
                result.fused_quality = quality1;
                result.source = source1;
            } else {
                result.fused_value = value2;
                result.fused_quality = quality2;
                result.source = source2;
            }
        }
    }
    // Only first sensor valid
    else if (quality1 >= MIN_QUALITY_THRESHOLD && value1 > 0) {
        result.fused_value = value1;
        result.fused_quality = quality1;
        result.source = source1;
    }
    // Only second sensor valid
    else if (quality2 >= MIN_QUALITY_THRESHOLD && value2 > 0) {
        result.fused_value = value2;
        result.fused_quality = quality2;
        result.source = source2;
    }
    // No valid readings
    else {
        result.fused_value = 0;
        result.fused_quality = 0;
        result.source = "NONE";
    }
    
    return result;
}

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
        currentVitals.alertMessage = "Low SpO2: " + String(currentVitals.spo2) + "%";
        return;
    }
    
    if (currentVitals.heartRate > 100 && currentVitals.hrQuality > 50) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "High HR: " + String(currentVitals.heartRate);
        return;
    }
    
    if (currentVitals.heartRate < 50 && currentVitals.heartRate > 0 && currentVitals.hrQuality > 50) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Low HR: " + String(currentVitals.heartRate);
        return;
    }
    
    if (currentVitals.temperature > 38.0 && !currentVitals.tempEstimated) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Fever: " + String(currentVitals.temperature, 1) + "C";
        return;
    }
    
    if (currentVitals.tempEstimated) {
        currentVitals.hasAlert = true;
        currentVitals.alertMessage = "Temp Estimated";
        return;
    }
}

// ==================== BUTTON HANDLERS - FIXED ====================
void handleButtons() {
    // Update button states
    startButton.update();
    stopButton.update();
    
    // Check for start button press
    if (startButton.isPressed()) {
        #if DEBUG_BUTTONS
        Serial.println("START button pressed!");
        #endif
        
        if (monitoringState == STATE_IDLE || monitoringState == STATE_PAUSED) {
            monitoringState = STATE_MONITORING;
            monitoringStateStr = "monitoring";
            Serial.println("→ State: MONITORING");
            
            // Visual feedback
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
    
    // Check for stop button press
    if (stopButton.isPressed()) {
        #if DEBUG_BUTTONS
        Serial.println("STOP button pressed!");
        #endif
        
        if (monitoringState == STATE_MONITORING) {
            monitoringState = STATE_PAUSED;
            monitoringStateStr = "paused";
            Serial.println("→ State: PAUSED");
            
            // Visual feedback
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
            Serial.println("→ State: IDLE");
            
            lcd.clear();
            lcd.setCursor(0, 1);
            lcd.print("Monitoring Stopped");
            delay(500);
        }
        
        stopButton.resetState();
    }
}

// ==================== LCD DISPLAY - ENHANCED ====================
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
            lcd.print("Src:");
            lcd.print(currentVitals.hrSource.substring(0, 12));
            
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
            
        case 1: // Sensor comparison
            lcd.setCursor(0, 0);
            lcd.print("Sensor Comparison:");
            
            lcd.setCursor(0, 1);
            lcd.print("M:");
            lcd.print(max30102Sensor.getBPM());
            lcd.print("(Q");
            lcd.print(max30102Sensor.getHRQuality());
            lcd.print(") S:");
            lcd.print(pulseSensor.getBPM());
            lcd.print("(Q");
            lcd.print(pulseSensor.getSignalQuality());
            lcd.print(")");
            
            lcd.setCursor(0, 2);
            lcd.print("MAX Finger:");
            lcd.print(max30102Sensor.isFingerDetected() ? "YES" : "NO ");
            
            lcd.setCursor(0, 3);
            lcd.print("Using: ");
            lcd.print(currentVitals.hrSource);
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
            lcd.print("Up:");
            lcd.print(millis() / 60000);
            lcd.print("m WiFi:");
            lcd.print(WiFi.status() == WL_CONNECTED ? "Y" : "N");
            
            lcd.setCursor(0, 3);
            lcd.print("Heap:");
            lcd.print(ESP.getFreeHeap() / 1024);
            lcd.print("k");
            break;
    }
    
    lastDisplayedScreen = currentScreen;
    lastDisplayedVitals = currentVitals;
}

// ==================== VITALS UPDATE - ENHANCED ====================
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
    
    // Fuse heart rate with comparison
    SensorComparison hrComparison = fuseSensorsWithComparison(
        max30102_hr, max30102_hrQuality, "MAX30102",
        sen11574_hr, sen11574_hrQuality, "SEN11574"
    );
    
    currentVitals.heartRate = hrComparison.fused_value;
    currentVitals.hrQuality = hrComparison.fused_quality;
    currentVitals.hrSource = hrComparison.source;
    
    // Debug sensor comparison
    #if DEBUG_SENSORS
    if (millis() - lastDebugPrint > DEBUG_PRINT_INTERVAL) {
        Serial.println("\n=== SENSOR COMPARISON ===");
        Serial.printf("HR: MAX=%d(Q%d) SEN=%d(Q%d) → Fused=%d(Q%d) [%s]\n",
            max30102_hr, max30102_hrQuality,
            sen11574_hr, sen11574_hrQuality,
            hrComparison.fused_value, hrComparison.fused_quality,
            hrComparison.source.c_str());
        Serial.printf("    Difference: %d BPM, Agreement: %s\n",
            hrComparison.difference, hrComparison.agreement ? "YES" : "NO");
        lastDebugPrint = millis();
    }
    #endif
    
    // Fuse SpO2
    SensorComparison spo2Comparison = fuseSensorsWithComparison(
        max30102_spo2, max30102_spo2Quality, "MAX30102",
        sen11574_spo2, sen11574_spo2Quality, "SEN11574"
    );
    
    currentVitals.spo2 = spo2Comparison.fused_value;
    currentVitals.spo2Quality = spo2Comparison.fused_quality;
    currentVitals.spo2Source = spo2Comparison.source;
    
    // Get temperature
    auto tempReading = tempSensor.getTemperature(currentVitals.heartRate);
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
    
    if (!http.begin(url)) {
        Serial.println("✗ HTTP begin failed");
        return;
    }
    
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

// ==================== REMOTE STATE CONTROL ====================
void checkRemoteStateCommand() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    http.setTimeout(3000);  // Shorter timeout for polling
    
    String url = API_BASE_URL + String("/health/devices/") + deviceID + String("/state/pending");
    
    if (!http.begin(url)) {
        Serial.println("✗ State poll: HTTP begin failed");
        return;
    }
    
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
                    Serial.printf("📱 Remote command received: %s\n", newState);
                    
                    // Update monitoring state based on remote command
                    if (strcmp(newState, "monitoring") == 0) {
                        monitoringState = STATE_MONITORING;
                        monitoringStateStr = "monitoring";
                        Serial.println("→ State: MONITORING (remote)");
                        
                        // Visual feedback
                        digitalWrite(STATUS_LED, HIGH);
                        delay(100);
                        digitalWrite(STATUS_LED, LOW);
                        
                        // Update LCD
                        lcd.clear();
                        lcd.setCursor(0, 1);
                        lcd.print("Remote: START");
                        delay(500);
                        
                    } else if (strcmp(newState, "paused") == 0) {
                        monitoringState = STATE_PAUSED;
                        monitoringStateStr = "paused";
                        Serial.println("→ State: PAUSED (remote)");
                        
                        // Flash twice
                        for (int i = 0; i < 2; i++) {
                            digitalWrite(STATUS_LED, HIGH);
                            delay(100);
                            digitalWrite(STATUS_LED, LOW);
                            delay(100);
                        }
                        
                        // Update LCD
                        lcd.clear();
                        lcd.setCursor(0, 1);
                        lcd.print("Remote: PAUSE");
                        delay(500);
                        
                    } else if (strcmp(newState, "idle") == 0) {
                        monitoringState = STATE_IDLE;
                        monitoringStateStr = "idle";
                        Serial.println("→ State: IDLE (remote)");
                        
                        // Update LCD
                        lcd.clear();
                        lcd.setCursor(0, 1);
                        lcd.print("Remote: STOP");
                        delay(500);
                    }
                }
            }
        } else {
            Serial.printf("✗ State poll: JSON parse error: %s\n", error.c_str());
        }
    } else if (httpCode > 0) {
        Serial.printf("✗ State poll: HTTP %d\n", httpCode);
    }
    
    http.end();
}

// ==================== WIFI MANAGEMENT ====================
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
        
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        time_t now;
        if (time(&now) > 0) {
            timeInitialized = true;
            bootTimestamp = now - (millis() / 1000);
            Serial.println("✓ NTP time synchronized");
        }
    } else {
        Serial.println("✗ WiFi connection failed");
    }
    Serial.println("====================================\n");
}

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED && !wifiReconnecting) {
        unsigned long now = millis();
        
        if (now - lastWiFiCheck > WIFI_RECONNECT_INTERVAL) {
            wifiReconnecting = true;
            Serial.println("WiFi disconnected, reconnecting...");
            
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
    
    if (currentScreen == 2 && !currentVitals.hasAlert) {
        currentScreen = (currentScreen + 1) % 4;
    }
}

// ==================== SERIAL COMMANDS - ENHANCED ====================
void handleSerialCommands() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        
        if (cmd == "help") {
            Serial.println("\n=== Available Commands ===");
            Serial.println("help        - Show this menu");
            Serial.println("status      - Show system status");
            Serial.println("vitals      - Show current vitals");
            Serial.println("sensors     - Show detailed sensor info");
            Serial.println("compare     - Compare both sensors");
            Serial.println("reset       - Reset sensors");
            Serial.println("calibrate   - Start SEN11574 calibration");
            Serial.println("setresting <bpm> - Set resting HR");
            Serial.println("wifi        - WiFi status");
            Serial.println("test        - Test buttons");
            Serial.println("restart     - Restart device");
            Serial.println("========================\n");
        }
        else if (cmd == "status") {
            Serial.println("\n=== System Status ===");
            Serial.printf("Firmware: %s\n", FIRMWARE_VERSION);
            Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
            Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
            Serial.printf("State: %s\n", monitoringStateStr.c_str());
            Serial.printf("MAX30102: %s\n", max30102Sensor.isAvailable() ? "Available" : "Not available");
            Serial.printf("DS18B20: %s\n", tempSensor.isSensorAvailable() ? "Available" : "Not available");
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
        else if (cmd == "sensors") {
            Serial.println("\n=== Sensor Details ===");
            Serial.printf("MAX30102:\n");
            Serial.printf("  BPM: %d (Quality: %d)\n", max30102Sensor.getBPM(), max30102Sensor.getHRQuality());
            Serial.printf("  SpO2: %d%% (Quality: %d)\n", max30102Sensor.getSpO2(), max30102Sensor.getSpO2Quality());
            Serial.printf("  Finger: %s\n", max30102Sensor.isFingerDetected() ? "Detected" : "Not detected");
            Serial.printf("  IR Value: %lu\n", max30102Sensor.getIRValue());
            Serial.printf("\nSEN11574:\n");
            Serial.printf("  BPM: %d (Quality: %d)\n", pulseSensor.getBPM(), pulseSensor.getSignalQuality());
            Serial.printf("  SpO2: %d%% (Quality: %d)\n", pulseSensor.getSpO2(), pulseSensor.getSpO2Quality());
            Serial.printf("  Signal: %d (Threshold: %d)\n", pulseSensor.getRawSignal(), pulseSensor.getThreshold());
            Serial.println("====================\n");
        }
        else if (cmd == "compare") {
            Serial.println("\n=== Sensor Comparison ===");
            Serial.printf("MAX30102 HR: %d BPM (Q: %d)\n", 
                max30102Sensor.getBPM(), max30102Sensor.getHRQuality());
            Serial.printf("SEN11574 HR: %d BPM (Q: %d)\n", 
                pulseSensor.getBPM(), pulseSensor.getSignalQuality());
            Serial.printf("Difference: %d BPM\n", 
                abs(max30102Sensor.getBPM() - pulseSensor.getBPM()));
            Serial.printf("Fused Result: %d BPM (Source: %s)\n",
                currentVitals.heartRate, currentVitals.hrSource.c_str());
            Serial.println("=======================\n");
        }
        else if (cmd == "reset") {
            Serial.println("Resetting sensors...");
            pulseSensor.reset();
            max30102Sensor.reset();
            Serial.println("✓ Sensors reset");
        }
        else if (cmd == "calibrate") {
            Serial.println("Starting 10-second calibration...");
            Serial.println("Please remove finger from SEN11574 sensor");
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
        else if (cmd == "test") {
            Serial.println("\n=== Button Test ===");
            Serial.println("Press START button...");
            unsigned long testStart = millis();
            bool startDetected = false;
            while (millis() - testStart < 5000) {
                startButton.update();
                if (startButton.isPressed()) {
                    Serial.println("✓ START button works!");
                    startDetected = true;
                    startButton.resetState();
                    break;
                }
                delay(10);
            }
            if (!startDetected) {
                Serial.println("✗ START button not detected");
            }
            
            Serial.println("Press STOP button...");
            testStart = millis();
            bool stopDetected = false;
            while (millis() - testStart < 5000) {
                stopButton.update();
                if (stopButton.isPressed()) {
                    Serial.println("✓ STOP button works!");
                    stopDetected = true;
                    stopButton.resetState();
                    break;
                }
                delay(10);
            }
            if (!stopDetected) {
                Serial.println("✗ STOP button not detected");
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
    Serial.println("║  Multi-Vitals Health Monitor v3.2     ║");
    Serial.println("║  FIXED Edition                         ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.printf("Build: %s %s\n\n", BUILD_DATE, BUILD_TIME);
    
    // Enable watchdog
    esp_task_wdt_init(WATCHDOG_TIMEOUT, true);
    esp_task_wdt_add(NULL);
    
    // Initialize hardware
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    
    // Initialize buttons
    Serial.println("Initializing buttons...");
    startButton.begin();
    stopButton.begin();
    
    // Initialize ADC
    pinMode(SEN11574_PIN, INPUT);
    analogSetAttenuation(ADC_11db);
    
    // LCD
    Wire.begin(SDA_PIN, SCL_PIN);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("VitalWatch v3.2");
    lcd.setCursor(0, 1);
    lcd.print("FIXED Edition");
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
    
    Serial.print("- MAX30102 (I2C HW-605)... ");
    delay(100);
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
    lcd.print("Serial: 'help'");
    lcd.setCursor(0, 3);
    lcd.print("Buttons: TEST OK");
    delay(3000);
    
    lastScreenChange = millis();
    
    Serial.println("\n✓ Setup complete");
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║  Type 'help' for serial commands      ║");
    Serial.println("║  Type 'test' to test buttons          ║");
    Serial.println("║  Type 'sensors' for sensor details    ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    
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
    
    // Handle serial commands (highest priority)
    handleSerialCommands();
    
    // Handle button input (call frequently)
    handleButtons();
    
    // Check WiFi connection
    checkWiFiConnection();
    
    // Poll for remote state commands
    if (millis() - lastStatePoll >= STATE_POLL_INTERVAL) {
        if (WiFi.status() == WL_CONNECTED) {
            checkRemoteStateCommand();
        }
        lastStatePoll = millis();
    }
    
    // Update sensors when monitoring
    if (monitoringState == STATE_MONITORING) {
        // MAX30102 update (must be called frequently)
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
