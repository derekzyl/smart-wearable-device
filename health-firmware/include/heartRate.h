/**
 * Heart Rate Detection Algorithm
 * Based on SparkFun MAX3010x library
 */

#ifndef HEARTRATE_H
#define HEARTRATE_H

#include <Arduino.h>

const int RATE_SIZE = 4; // Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; // Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; // Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

// Calculates the heart rate based on IR value
bool checkForBeat(long sample) {
  static long last = 0;
  static bool wasPeak = false;
  static long lastPeakTime = 0;
  static long lastValleyTime = 0;
  
  // Detect peaks and valleys
  if (sample > last && !wasPeak) {
    wasPeak = true;
    lastPeakTime = millis();
  } else if (sample < last && wasPeak) {
    wasPeak = false;
    lastValleyTime = millis();
    
    // Calculate time between peaks
    long beatTime = lastPeakTime - lastValleyTime;
    if (beatTime > 300 && beatTime < 2000) { // Valid beat range (30-200 bpm)
      beatsPerMinute = 60000.0 / beatTime;
      lastBeat = millis();
      return true;
    }
  }
  
  last = sample;
  return false;
}

#endif


