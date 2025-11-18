#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <Wire.h>

// Forward declaration to avoid sensor_t conflicts
// We'll include actual headers in the .cpp file where needed
class Adafruit_BMP280;
class TinyGPSPlus;

#include "balloon_config.h"
#include "sensor_pins.h"
#include "common_types.h"

// ===========================
// Sensor Data Structures
// ===========================

struct BMP280Data {
    float pressure;        // Pressure in Pascals
    float temperature;     // Temperature in Celsius
    float altitude;        // Calculated altitude in meters
    uint32_t timestamp;    // Timestamp in milliseconds
    bool valid;           // Data validity flag
};

// GPSData - defined in common_types.h
// Additional GPS-specific fields for sensor manager
struct SensorGPSData : public GPSData {
    bool locked;          // GPS lock status
    bool valid;
    int timestamp;
};

// ===========================
// Sensor Manager Class
// ===========================

class SensorManager {
private:
    Adafruit_BMP280* bmp280;
    TinyGPSPlus* gps;
    
    // BMP280 data
    BMP280Data currentBMP280Data;
    float seaLevelPressure;    // Sea level pressure for altitude calculation
    
    // GPS data
    SensorGPSData currentGPSData;
    HardwareSerial* gpsSerial;
    
    // Timing
    uint32_t lastBMP280Read;
    uint32_t lastGPSRead;
    
    // Error tracking
    uint32_t bmp280ErrorCount;
    uint32_t gpsErrorCount;
    
    // Private methods
    bool initBMP280();
    bool initGPS();
    float calculateAltitude(float pressure, float seaLevelPressure);
    void updateBMP280Data();
    void updateGPSData();
    bool validateBMP280Data(float pressure, float temperature);
    bool validateGPSData();

public:
    SensorManager();
    ~SensorManager();
    
    // Initialization
    bool begin();
    void end();
    
    // Data updates
    void update();
    void forceUpdate();
    
    // Data access
    BMP280Data getBMP280Data() const { return currentBMP280Data; }
    GPSData getGPSData() const { return currentGPSData; }
    
    // Status methods
    bool isBMP280Ready() const;
    bool isGPSReady() const;
    bool isGPSLocked() const;
    
    // Configuration
    void setSeaLevelPressure(float pressure) { seaLevelPressure = pressure; }
    float getSeaLevelPressure() const { return seaLevelPressure; }
    
    // Error handling
    uint32_t getBMP280ErrorCount() const { return bmp280ErrorCount; }
    uint32_t getGPSErrorCount() const { return gpsErrorCount; }
    void resetErrorCounts();
    
    // Debug
    void printBMP280Data() const;
    void printGPSData() const;
    void printStatus() const;
};

// ===========================
// Global Instance
// ===========================

extern SensorManager& Sensors();

#endif // SENSOR_MANAGER_H
