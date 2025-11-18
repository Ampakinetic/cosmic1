#include "sensor_manager.h"

// Include Adafruit sensor headers here to avoid sensor_t conflicts
#include <Adafruit_BMP280.h>
#include <TinyGPSPlus.h>

// ===========================
// Constructor/Destructor
// ===========================

SensorManager::SensorManager() {
    bmp280 = nullptr;
    gps = nullptr;
    gpsSerial = nullptr;
    
    // Initialize data structures
    currentBMP280Data = {0.0f, 0.0f, 0.0f, 0, false};
    currentGPSData = {{0.0, 0.0, 0.0f, 0, 0.0f, 0, 0, 0, 0}, false};
    
    seaLevelPressure = 101325.0f; // Standard atmospheric pressure
    
    // Initialize timing
    lastBMP280Read = 0;
    lastGPSRead = 0;
    
    // Initialize error counts
    bmp280ErrorCount = 0;
    gpsErrorCount = 0;
}

SensorManager::~SensorManager() {
    end();
}

// ===========================
// Initialization
// ===========================

bool SensorManager::begin() {
    bool success = true;
    
    // Initialize BMP280
    if (!initBMP280()) {
        bmp280ErrorCount++;
        success = false;
    }
    
    // Initialize GPS
    if (!initGPS()) {
        gpsErrorCount++;
        success = false;
    }
    
    return success;
}

void SensorManager::end() {
    if (bmp280) {
        delete bmp280;
        bmp280 = nullptr;
    }
    
    if (gps) {
        delete gps;
        gps = nullptr;
    }
    
    if (gpsSerial) {
        gpsSerial->end();
        gpsSerial = nullptr;
    }
}

// ===========================
// Private Initialization Methods
// ===========================

bool SensorManager::initBMP280() {
    Wire.begin(BMP280_SDA_PIN, BMP280_SCL_PIN);
    
    bmp280 = new Adafruit_BMP280();
    
    if (!bmp280->begin(BMP280_ADDRESS)) {
        if (DEBUG_SENSORS) {
            Serial.println("BMP280: Could not find sensor at 0x76");
        }
        return false;
    }
    
    // Configure BMP280 for balloon use
    bmp280->setSampling(Adafruit_BMP280::MODE_NORMAL,
                       Adafruit_BMP280::SAMPLING_X2,     // Temperature
                       Adafruit_BMP280::SAMPLING_X16,    // Pressure
                       Adafruit_BMP280::FILTER_X16,
                       Adafruit_BMP280::STANDBY_MS_500);
    
    if (DEBUG_SENSORS) {
        Serial.println("BMP280: Initialized successfully");
    }
    
    return true;
}

bool SensorManager::initGPS() {
    // Use UART1 for GPS
    gpsSerial = &Serial1;
    gpsSerial->begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    
    gps = new TinyGPSPlus();
    
    // Configure PPS pin if available
    if (GPS_PPS_PIN != -1) {
        pinMode(GPS_PPS_PIN, INPUT);
    }
    
    if (DEBUG_GPS) {
        Serial.println("GPS: Initialized successfully");
    }
    
    return true;
}

// ===========================
// Data Update Methods
// ===========================

void SensorManager::update() {
    uint32_t currentTime = millis();
    
    // Update BMP280 data
    if (currentTime - lastBMP280Read >= BMP280_READ_INTERVAL_MS) {
        updateBMP280Data();
        lastBMP280Read = currentTime;
    }
    
    // Update GPS data
    if (currentTime - lastGPSRead >= GPS_READ_INTERVAL_MS) {
        updateGPSData();
        lastGPSRead = currentTime;
    }
}

void SensorManager::forceUpdate() {
    updateBMP280Data();
    updateGPSData();
    lastBMP280Read = millis();
    lastGPSRead = millis();
}

void SensorManager::updateBMP280Data() {
    if (!bmp280) {
        bmp280ErrorCount++;
        return;
    }
    
    float pressure = bmp280->readPressure();
    float temperature = bmp280->readTemperature();
    
    if (validateBMP280Data(pressure, temperature)) {
        currentBMP280Data.pressure = pressure;
        currentBMP280Data.temperature = temperature;
        currentBMP280Data.altitude = calculateAltitude(pressure, seaLevelPressure);
        currentBMP280Data.timestamp = millis();
        currentBMP280Data.valid = true;
        
        if (DEBUG_SENSORS) {
            Serial.printf("BMP280: P=%.2fPa, T=%.2f°C, Alt=%.2fm\n", 
                         pressure, temperature, currentBMP280Data.altitude);
        }
    } else {
        currentBMP280Data.valid = false;
        bmp280ErrorCount++;
        
        if (DEBUG_SENSORS) {
            Serial.println("BMP280: Invalid reading");
        }
    }
}

void SensorManager::updateGPSData() {
    if (!gps || !gpsSerial) {
        gpsErrorCount++;
        return;
    }
    
    // Read GPS data for a short time
    uint32_t timeout = millis() + 100; // 100ms timeout
    while (millis() < timeout && gpsSerial->available()) {
        gps->encode(gpsSerial->read());
    }
    
    if (validateGPSData()) {
        currentGPSData.latitude = gps->location.lat();
        currentGPSData.longitude = gps->location.lng();
        currentGPSData.altitude = gps->altitude.meters();
        currentGPSData.speed = gps->speed.mps();
        currentGPSData.course = gps->course.deg();
        currentGPSData.satellites = gps->satellites.value();
        currentGPSData.hdop = gps->hdop.value();
        currentGPSData.timestamp = millis();
        currentGPSData.valid = true;
        currentGPSData.locked = true;
        
        if (DEBUG_GPS && (millis() - lastGPSRead > 10000)) { // Log every 10 seconds
            Serial.printf("GPS: Lat=%.6f, Lon=%.6f, Sats=%d, HDOP=%.1f\n",
                         currentGPSData.latitude, currentGPSData.longitude,
                         currentGPSData.satellites, currentGPSData.hdop);
        }
    } else {
        currentGPSData.locked = false;
        
        // Keep last valid data but mark as not locked
        if (!currentGPSData.valid) {
            gpsErrorCount++;
            
            if (DEBUG_GPS) {
                Serial.println("GPS: No valid data");
            }
        }
    }
}

// ===========================
// Validation Methods
// ===========================

bool SensorManager::validateBMP280Data(float pressure, float temperature) {
    // Check for valid pressure range (300hPa to 1200hPa)
    if (pressure < 30000.0f || pressure > 120000.0f) {
        return false;
    }
    
    // Check for valid temperature range (-40°C to +85°C)
    if (temperature < -40.0f || temperature > 85.0f) {
        return false;
    }
    
    // Check for NaN values
    if (isnan(pressure) || isnan(temperature)) {
        return false;
    }
    
    return true;
}

bool SensorManager::validateGPSData() {
    // Check if we have a valid location fix
    if (!gps->location.isValid()) {
        return false;
    }
    
    // Check minimum satellites
    if (gps->satellites.value() < GPS_MIN_SATS) {
        return false;
    }
    
    // Check HDOP (should be less than 5.0 for reasonable accuracy)
    if (gps->hdop.isValid() && gps->hdop.value() > 500) { // HDOP scaled by 100
        return false;
    }
    
    return true;
}

// ===========================
// Utility Methods
// ===========================

float SensorManager::calculateAltitude(float pressure, float seaLevelPressure) {
    // Barometric altitude formula
    // h = 44330 * (1 - (P/P0)^(1/5.255))
    return 44330.0f * (1.0f - pow(pressure / seaLevelPressure, 0.190263f));
}

// ===========================
// Status Methods
// ===========================

bool SensorManager::isBMP280Ready() const {
    return bmp280 != nullptr;
}

bool SensorManager::isGPSReady() const {
    return gps != nullptr && gpsSerial != nullptr;
}

bool SensorManager::isGPSLocked() const {
    return currentGPSData.locked;
}

void SensorManager::resetErrorCounts() {
    bmp280ErrorCount = 0;
    gpsErrorCount = 0;
}

// ===========================
// Debug Methods
// ===========================

void SensorManager::printBMP280Data() const {
    Serial.println("=== BMP280 Data ===");
    Serial.printf("Pressure: %.2f Pa\n", currentBMP280Data.pressure);
    Serial.printf("Temperature: %.2f °C\n", currentBMP280Data.temperature);
    Serial.printf("Altitude: %.2f m\n", currentBMP280Data.altitude);
    Serial.printf("Valid: %s\n", currentBMP280Data.valid ? "Yes" : "No");
    Serial.printf("Timestamp: %lu ms\n", currentBMP280Data.timestamp);
    Serial.printf("Sea Level Pressure: %.2f Pa\n", seaLevelPressure);
    Serial.printf("Error Count: %lu\n", bmp280ErrorCount);
}

void SensorManager::printGPSData() const {
    Serial.println("=== GPS Data ===");
    Serial.printf("Latitude: %.6f°\n", currentGPSData.latitude);
    Serial.printf("Longitude: %.6f°\n", currentGPSData.longitude);
    Serial.printf("Altitude: %.2f m\n", currentGPSData.altitude);
    Serial.printf("Speed: %.2f m/s\n", currentGPSData.speed);
    Serial.printf("Course: %.2f°\n", currentGPSData.course);
    Serial.printf("Satellites: %d\n", currentGPSData.satellites);
    Serial.printf("HDOP: %.1f\n", currentGPSData.hdop / 100.0f);
    Serial.printf("Valid: %s\n", currentGPSData.valid ? "Yes" : "No");
    Serial.printf("Locked: %s\n", currentGPSData.locked ? "Yes" : "No");
    Serial.printf("Timestamp: %lu ms\n", currentGPSData.timestamp);
    Serial.printf("Error Count: %lu\n", gpsErrorCount);
}

void SensorManager::printStatus() const {
    Serial.println("=== Sensor Manager Status ===");
    Serial.printf("BMP280 Ready: %s\n", isBMP280Ready() ? "Yes" : "No");
    Serial.printf("GPS Ready: %s\n", isGPSReady() ? "Yes" : "No");
    Serial.printf("GPS Locked: %s\n", isGPSLocked() ? "Yes" : "No");
    Serial.printf("BMP280 Errors: %lu\n", bmp280ErrorCount);
    Serial.printf("GPS Errors: %lu\n", gpsErrorCount);
    Serial.printf("Last BMP280 Read: %lu ms ago\n", millis() - lastBMP280Read);
    Serial.printf("Last GPS Read: %lu ms ago\n", millis() - lastGPSRead);
}
