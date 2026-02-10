/**
 * Configuration Header
 * Adjustable settings for health monitoring system
 */

#ifndef CONFIG_H
#define CONFIG_H

// ==================== WIFI CONFIGURATION ====================
// Update these with your actual credentials
#define WIFI_SSID "cybergenii"
#define WIFI_PASSWORD "12341234"

// ==================== API CONFIGURATION ====================
// Update this with your backend server IP/domain
#define API_BASE_URL "https://xenophobic-netta-cybergenii-1584fde7.koyeb.app"
#define VITALS_ENDPOINT "/health/vitals"

// ==================== SENSOR CALIBRATION ====================
// Default resting heart rate (can be updated via app)
#define DEFAULT_RESTING_HR 70

// Battery voltage divider ratio (adjust based on your circuit)
#define BATTERY_VOLTAGE_DIVIDER 2.0

// Battery voltage range (in millivolts)
#define BATTERY_MIN_VOLTAGE 3400  // 3.4V (empty)
#define BATTERY_MAX_VOLTAGE 4200  // 4.2V (full)

// ==================== TIMING CONFIGURATION ====================
#define PULSE_SAMPLE_RATE_MS 2      // 500Hz sampling
#define VITAL_UPDATE_RATE_MS 1000   // 1 second
#define LCD_UPDATE_RATE_MS 500      // 0.5 seconds
#define CLOUD_SYNC_RATE_MS 5000     // 5 seconds
#define SCREEN_ROTATE_MS 10000      // 10 seconds

// ==================== ALERT THRESHOLDS ====================
#define THRESHOLD_SPO2_CRITICAL 90   // Critical hypoxia
#define THRESHOLD_SPO2_LOW 95        // Low oxygen
#define THRESHOLD_HR_HIGH 100        // Tachycardia
#define THRESHOLD_HR_LOW 50          // Bradycardia
#define THRESHOLD_TEMP_HIGH 38.0     // Fever (Celsius)
#define THRESHOLD_TEMP_LOW 35.5      // Hypothermia (Celsius)
#define THRESHOLD_BATTERY_LOW 20     // Low battery (%)

// ==================== LCD I2C ADDRESSES ====================
// Try 0x27 first, if that doesn't work, try 0x3F
#define LCD_I2C_ADDRESS 0x27
// #define LCD_I2C_ADDRESS 0x3F    // Uncomment if 0x27 doesn't work

// ==================== SIGNAL QUALITY THRESHOLDS ====================
#define MIN_SIGNAL_QUALITY_HR 50     // Minimum for valid HR
#define MIN_SIGNAL_QUALITY_SPO2 50   // Minimum for valid SpO2

#endif // CONFIG_H
