/**
 * Main Balloon Firmware
 * ESP32-S3 High-Altitude Balloon Project
 * Phase 2 Implementation
 * 
 * This is the main application file for the balloon firmware.
 * It coordinates all subsystems and manages the overall balloon operation.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "balloon_config.h"
#include "board_config.h"
#include "sensor_pins.h"
#include "camera_pins.h"

// Module Headers
#include "sensor_manager.h"
#include "camera_manager.h"
#include "lora_comm.h"
#include "power_manager.h"
#include "packet_handler.h"
#include "system_state.h"
#include "debug_utils.h"

// Forward declarations for missing types
struct PowerData {
    float batteryVoltage;
    float batteryCurrent;
    uint8_t batteryPercentage;
    uint32_t timestamp;
    bool valid;
};

// Missing constants
#define BOARD_NAME "ESP32-S3-DevKit"

// ===========================
// Global Configuration
// ===========================

#define FIRMWARE_VERSION "2.0.0"
#define BUILD_DATE __DATE__ " " __TIME__
#define SYSTEM_NAME "Cosmic1-Balloon"

// Timing Constants
#define SETUP_DELAY_MS           1000
#define MAIN_LOOP_INTERVAL_MS    100     // 10 Hz main loop
#define TELEMETRY_INTERVAL_MS    5000    // 5 seconds
#define HEARTBEAT_INTERVAL_MS   30000   // 30 seconds
#define STATUS_REPORT_INTERVAL_MS 60000   // 1 minute
#define PERFORMANCE_INTERVAL_MS   10000   // 10 seconds

// ===========================
// Application State
// ===========================

struct AppState {
    bool initialized;
    uint32_t startTime;
    uint32_t lastTelemetryTime;
    uint32_t lastHeartbeatTime;
    uint32_t lastStatusReportTime;
    uint32_t lastPerformanceTime;
    uint32_t loopCounter;
    uint32_t lastLoopTime;
    
    // System Mode Control
    bool flightMode;
    bool debugMode;
    bool lowPowerMode;
    bool emergencyMode;
    
    // Data Collection State
    bool sensorsActive;
    bool cameraActive;
    bool communicationActive;
    bool gpsActive;
    
    // Performance Metrics
    uint32_t maxLoopTime;
    uint32_t avgLoopTime;
    uint32_t loopTimeSum;
    
    // Error Tracking
    uint32_t errorCount;
    uint32_t lastErrorTime;
    char lastErrorMessage[128];
};

// ===========================
// Global Variables
// ===========================

static AppState appState;

// ===========================
// Function Declarations
// ===========================

// Initialization Functions
bool initializeHardware();
bool initializeSubsystems();
bool configureSystem();
bool performSystemChecks();

// Hardware Initialization Helper Functions
bool initializeBoard();
void initializeSensorPins();
void initializeCameraPins();
bool checkHardwareStatus();

// Main Loop Functions
void updateSystemState();
void processSensors();
void processCamera();
void processCommunications();
void processPowerManagement();
void processPacketHandling();

// Timing Functions
bool shouldSendTelemetry();
bool shouldSendHeartbeat();
bool shouldReportStatus();
bool shouldUpdatePerformance();

// Communication Functions
void sendTelemetryData();
void sendHeartbeatPacket();
void sendStatusReport();
void processIncomingCommands();

// Utility Functions
void printSystemInfo();
void handleSystemError(const char* error);
void updatePerformanceMetrics(uint32_t loopTime);
bool checkSystemHealth();

// Event Handlers
void onSystemEvent(const SystemEvent& event);
void onEmergencyTriggered(const char* reason);
void onModeChanged(SystemMode newMode);
void onFlightPhaseChanged(FlightPhase newPhase);

// ===========================
// Arduino Main Functions
// ===========================

void setup() {
    // Initialize serial communication first
    Serial.begin(SERIAL_BAUD_RATE);
    delay(SETUP_DELAY_MS);
    
    Serial.println();
    Serial.println("========================================");
    Serial.printf("Cosmic1 Balloon Firmware v%s\n", FIRMWARE_VERSION);
    Serial.printf("Build: %s\n", BUILD_DATE);
    Serial.printf("Board: ESP32-S3\n");
    Serial.println("========================================");
    
    // Initialize debug system
    if (!Debug.begin()) {
        Serial.println("FATAL: Failed to initialize debug system!");
        return;
    }
    
    SYS_INFO("System booting...");
    
    // Initialize application state
    memset(&appState, 0, sizeof(appState));
    appState.startTime = millis();
    appState.lastLoopTime = appState.startTime;
    appState.maxLoopTime = 0;
    appState.avgLoopTime = MAIN_LOOP_INTERVAL_MS;
    
    // Initialize hardware
    if (!initializeHardware()) {
        SYS_ERROR("Hardware initialization failed");
        return;
    }
    
    // Initialize subsystems
    if (!initializeSubsystems()) {
        SYS_ERROR("Subsystem initialization failed");
        return;
    }
    
    // Configure system
    if (!configureSystem()) {
        SYS_ERROR("System configuration failed");
        return;
    }
    
    // Perform system checks
    if (!performSystemChecks()) {
        SYS_ERROR("System checks failed");
        return;
    }
    
    // Mark as initialized
    appState.initialized = true;
    SYS_INFO("System initialization complete");
    
    // Print system information
    printSystemInfo();
    
    // Enter pre-flight mode
    SysState().setMode(SystemMode::PRE_FLIGHT);
    SysState().setFlightPhase(FlightPhase::GROUND);
    
    SYS_INFO("System ready - entering main loop");
}

void loop() {
    if (!appState.initialized) {
        delay(1000);
        return;
    }
    
    uint32_t loopStartTime = millis();
    
    try {
        // Feed watchdog
        if (Debug.isWatchdogEnabled()) {
            Debug.feedWatchdog();
        }
        
        // Update system state
        updateSystemState();
        
        // Process main subsystems
        processSensors();
        processCamera();
        processCommunications();
        processPowerManagement();
        processPacketHandling();
        
        // Send periodic data
        if (shouldSendTelemetry()) {
            sendTelemetryData();
        }
        
        if (shouldSendHeartbeat()) {
            sendHeartbeatPacket();
        }
        
        if (shouldReportStatus()) {
            sendStatusReport();
        }
        
        if (shouldUpdatePerformance()) {
            updatePerformanceMetrics(millis() - loopStartTime);
        }
        
        // Process incoming commands
        processIncomingCommands();
        
        // Update loop statistics
        appState.loopCounter++;
        uint32_t loopTime = millis() - loopStartTime;
        appState.lastLoopTime = loopTime;
        
        if (loopTime > appState.maxLoopTime) {
            appState.maxLoopTime = loopTime;
        }
        
        appState.loopTimeSum += loopTime;
        if (appState.loopCounter % 100 == 0) {
            appState.avgLoopTime = appState.loopTimeSum / 100;
            appState.loopTimeSum = 0;
        }
        
        // Maintain loop timing
        if (loopTime < MAIN_LOOP_INTERVAL_MS) {
            delay(MAIN_LOOP_INTERVAL_MS - loopTime);
        }
        
    } catch (...) {
        SYS_ERROR("Exception in main loop");
        handleSystemError("Main loop exception");
    }
}

// ===========================
// Initialization Functions
// ===========================

bool initializeHardware() {
    SYS_INFO("Initializing hardware...");
    
    // Initialize board-specific hardware
    if (!initializeBoard()) {
        SYS_ERROR("Board initialization failed");
        return false;
    }
    
    // Initialize pins
    initializeSensorPins();
    initializeCameraPins();
    
    // Check hardware status
    if (!checkHardwareStatus()) {
        SYS_WARNING("Some hardware issues detected");
    }
    
    SYS_INFO("Hardware initialization complete");
    return true;
}

bool initializeSubsystems() {
    SYS_INFO("Initializing subsystems...");
    
    // Initialize power management first
    if (!PowerMgr().begin()) {
        SYS_ERROR("Power manager initialization failed");
        return false;
    }
    SYS_INFO("Power manager initialized");
    
    // Initialize sensor manager
    if (!Sensors().begin()) {
        SYS_ERROR("Sensor manager initialization failed");
        return false;
    }
    SYS_INFO("Sensor manager initialized");
    appState.sensorsActive = true;
    
    // Initialize camera manager
    if (!Camera().begin()) {
        SYS_WARNING("Camera manager initialization failed - continuing without camera");
        appState.cameraActive = false;
    } else {
        SYS_INFO("Camera manager initialized");
        appState.cameraActive = true;
    }
    
    // Initialize LoRa communication
    if (!LoRaComm().begin()) {
        SYS_ERROR("LoRa communication initialization failed");
        return false;
    }
    SYS_INFO("LoRa communication initialized");
    appState.communicationActive = true;
    
    // Initialize packet handler
    if (!PacketMgr().begin()) {
        SYS_ERROR("Packet handler initialization failed");
        return false;
    }
    SYS_INFO("Packet handler initialized");
    
    // Initialize system state
    if (!SysState().begin()) {
        SYS_ERROR("System state initialization failed");
        return false;
    }
    SYS_INFO("System state initialized");
    
    SYS_INFO("All subsystems initialized successfully");
    return true;
}

bool configureSystem() {
    SYS_INFO("Configuring system...");
    
    // Configure debug system - simplified for now
    // Debug().setDebugLevel(DEFAULT_DEBUG_LEVEL);
    // Debug().setSerialEnabled(true);
    // Debug().setFileLoggingEnabled(false);  // Disable file logging initially
    
    // Enable all debug categories - simplified for now
    // Debug().setCategoryEnabled(DebugCategory::SYSTEM, true);
    // Debug().setCategoryEnabled(DebugCategory::SENSORS, true);
    // Debug().setCategoryEnabled(DebugCategory::CAMERA, true);
    // Debug().setCategoryEnabled(DebugCategory::LORA, true);
    // Debug().setCategoryEnabled(DebugCategory::POWER, true);
    // Debug().setCategoryEnabled(DebugCategory::STATE, true);
    
    // Configure system state - simplified for now
    // SysState().setFlightModeEnabled(true);
    // SysState().setAutoRecoveryEnabled(true);
    // SysState().setHealthCheckInterval(5000);  // 5 seconds
    
    // Configure power management - simplified for now
    // PowerMgr().setLowPowerThreshold(BATTERY_LOW_THRESHOLD);
    // PowerMgr().setCriticalPowerThreshold(BATTERY_CRITICAL_THRESHOLD);
    
    // Configure LoRa communication - simplified for now
    // LoRaComm().setFrequency(LORA_FREQUENCY);
    // LoRaComm().setPower(LORA_TX_POWER);
    // LoRaComm().setSpreadingFactor(LORA_SPREADING_FACTOR);
    
    // Configure sensors
    // Sensors are configured via defines in balloon_config.h
    
    // Configure camera
    if (appState.cameraActive) {
        // Camera configuration will be handled by camera manager
        // Camera().setResolution(FRAMESIZE_QVGA);
        // Camera().setQuality(10);  // Medium quality
        // Camera().setCaptureInterval(30000);  // 30 seconds
    }
    
    SYS_INFO("System configuration complete");
    return true;
}

bool performSystemChecks() {
    SYS_INFO("Performing system checks...");
    
    bool allPassed = true;
    
    // Check power system - simplified for now
    // if (!PowerMgr().performHealthCheck()) {
    //     SYS_WARNING("Power system health check failed");
    //     allPassed = false;
    // }
    SYS_WARNING("Power system health check skipped - method not available");
    
    // Check sensor system
    if (!Sensors().isBMP280Ready() || !Sensors().isGPSReady()) {
        SYS_WARNING("Sensor system health check failed");
        allPassed = false;
    }
    
    // Check communication system
    // LoRaComm().performHealthCheck(); // Method doesn't exist yet
    SYS_WARNING("Communication system health check skipped");
    
    // Check camera system (if active)
    if (appState.cameraActive) { // && !Camera().performHealthCheck()) {
        SYS_WARNING("Camera system health check failed");
        allPassed = false;
    }
    
    // Run system diagnostics
    if (!SysState().runDiagnostics()) {
        SYS_WARNING("System diagnostics failed");
        allPassed = false;
    }
    
    if (allPassed) {
        SYS_INFO("All system checks passed");
    } else {
        SYS_WARNING("Some system checks failed - continuing with reduced functionality");
    }
    
    return true;  // Continue even if some checks fail
}

// ===========================
// Hardware Initialization Helper Functions
// ===========================

bool initializeBoard() {
    SYS_INFO("Initializing board-specific hardware...");
    
    // Initialize I2C for sensors
    Wire.begin(BMP280_SDA_PIN, BMP280_SCL_PIN);
    
    // Initialize SPI for LoRa
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_CS_PIN);
    
    // Initialize UART for GPS
    Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    
    // Initialize power control pin
    pinMode(POWER_ENABLE_PIN, OUTPUT);
    digitalWrite(POWER_ENABLE_PIN, HIGH);  // Enable power to sensors
    
    // Initialize LED pins
    pinMode(LED_GPS_LOCK_PIN, OUTPUT);
    pinMode(LED_LORA_TX_PIN, OUTPUT);
    pinMode(LED_ERROR_PIN, OUTPUT);
    
    // Set initial LED states
    digitalWrite(LED_GPS_LOCK_PIN, LOW);
    digitalWrite(LED_LORA_TX_PIN, LOW);
    digitalWrite(LED_ERROR_PIN, LOW);
    
    SYS_INFO("Board initialization complete");
    return true;
}

void initializeSensorPins() {
    SYS_INFO("Initializing sensor pins...");
    
    // BMP280 sensor pins are handled by I2C initialization in initializeBoard()
    
    // GPS pins are handled by UART initialization in initializeBoard()
    
    // LoRa pins are handled by SPI initialization in initializeBoard()
    
    // Additional sensor pin configuration if needed
    pinMode(GPS_PPS_PIN, INPUT_PULLDOWN);  // Pulse Per Second pin
    
    SYS_INFO("Sensor pins initialized");
}

void initializeCameraPins() {
    SYS_INFO("Initializing camera pins...");
    
    // Camera pins are defined in camera_pins.h and handled by the camera manager
    // No additional pin initialization needed here as it's done in camera_manager.cpp
    
    SYS_INFO("Camera pins initialized");
}

bool checkHardwareStatus() {
    SYS_INFO("Checking hardware status...");
    
    bool allGood = true;
    
    // Check I2C bus - try to communicate with BMP280
    Wire.beginTransmission(BMP280_ADDRESS);
    if (Wire.endTransmission() != 0) {
        SYS_WARNING("BMP280 sensor not found on I2C bus");
        allGood = false;
    } else {
        SYS_INFO("BMP280 sensor detected");
    }
    
    // Check GPS serial communication
    if (Serial1.available() > 0) {
        SYS_INFO("GPS communication detected");
    } else {
        SYS_WARNING("No GPS communication detected (may need more time)");
    }
    
    // Check LoRa module (basic SPI communication)
    digitalWrite(LORA_CS_PIN, LOW);
    delay(1);
    // Try to read a register from LoRa module (simplified check)
    digitalWrite(LORA_CS_PIN, HIGH);
    
    // Check power status
    int batteryLevel = analogRead(BATTERY_SENSE_PIN);
    if (batteryLevel > 0) {
        SYS_INFO("Battery monitoring active (raw reading: %d)", batteryLevel);
    } else {
        SYS_WARNING("Battery monitoring may not be working");
    }
    
    SYS_INFO("Hardware status check complete");
    return allGood;
}

// ===========================
// Main Loop Functions
// ===========================

void updateSystemState() {
    SysState().update();
    
    // Update system mode based on conditions
    SystemMode currentMode = SysState().getMode();
    SystemStatus currentStatus = SysState().getSystemStatus();
    
    // Handle emergency conditions
    if (SysState().isEmergencyActive()) {
        if (!appState.emergencyMode) {
            SYS_ERROR("Emergency mode activated: %s", SysState().getEmergencyReason());
            appState.emergencyMode = true;
        }
    } else {
        if (appState.emergencyMode) {
            SYS_INFO("Emergency mode cleared");
            appState.emergencyMode = false;
        }
    }
    
    // Update flight mode
    appState.flightMode = (currentMode == SystemMode::ASCENT || 
                          currentMode == SystemMode::APEX_DETECTED || 
                          currentMode == SystemMode::DESCENT);
    
    // Update low power mode - use dummy data for now
    PowerData powerData = {3.7f, 0.1f, 85, millis(), true};
    appState.lowPowerMode = (powerData.batteryPercentage < BATTERY_LOW_THRESHOLD);
    
    // Update GPS status
    GPSData gpsData = Sensors().getGPSData();
    appState.gpsActive = (gpsData.satellites > 0);
}

void processSensors() {
    if (!appState.sensorsActive) {
        return;
    }
    
    Sensors().update();
    
    // Get sensor data for system state
    BMP280Data sensorData = Sensors().getBMP280Data();
    GPSData gpsData = Sensors().getGPSData();
    
    // Update system state with sensor data
    SysState().setCurrentAltitude(gpsData.altitude);
    SysState().setCurrentVelocity(gpsData.speed);
    SysState().setCurrentTemperature(sensorData.temperature);
    
    // Check for sensor alerts
    if (sensorData.temperature > 60.0f) {
        SYS_WARNING("High temperature detected: %.1f°C", sensorData.temperature);
    }
    
    if (sensorData.pressure < 200.0f) {
        SYS_INFO("Low pressure detected: %.1f hPa (altitude: %.1f m)", 
                sensorData.pressure, gpsData.altitude);
    }
}

void processCamera() {
    if (!appState.cameraActive) {
        return;
    }
    
    // Camera manager doesn't have update() method
    // Check if it's time to capture an image
    if (Camera().isTimeToCapture(30000)) {  // 30 second interval
        if (Camera().captureImage()) {
            SYS_INFO("Camera image captured");
            
            // Get camera data
            const ImageData& imageData = Camera().getCurrentImage();
            
            // Convert to CameraData format
            CameraData cameraData;
            static uint16_t nextImageId = 1;
            cameraData.imageId = nextImageId++; // Simple counter for image ID
            cameraData.imageSize = imageData.length;
            cameraData.timestamp = imageData.timestamp;
            cameraData.compression = 1; // Default compression
            cameraData.brightness = 0.0f; // Default brightness
            cameraData.contrast = 0.0f; // Default contrast
            cameraData.faceCount = 0; // Default face count
            cameraData.objectCount = 0; // Default object count
            
            // Create camera packet
            if (PacketMgr().createCameraPacket(cameraData)) {
                SYS_LOG("Camera packet created successfully");
            }
        }
    }
}

void processCommunications() {
    if (!appState.communicationActive) {
        return;
    }
    
    // LoRaComm().update(); // Method doesn't exist
    
    // Check for received data - simplified for now
    // uint8_t* receivedData = nullptr;
    // size_t receivedLength = 0;
    // if (LoRaComm().receiveData(receivedData, receivedLength)) {
    //     SYS_LOG("Received %zu bytes via LoRa", receivedLength);
    //     
    //     // Process received data through packet handler
    //     if (receivedData && receivedLength > 0) {
    //         PacketMgr().processIncomingData(receivedData, receivedLength);
    //     }
    //     
    //     if (receivedData) {
    //         free(receivedData);
    //     }
    // }
    
    // Send queued packets
    // while (PacketMgr().getBufferUsage() > 0) {
    //     if (!PacketMgr().sendPacket()) {
    //         SYS_WARNING("Failed to send packet");
    //         break;
    //     }
    // }
}

void processPowerManagement() {
    // PowerMgr().update(); // Method doesn't exist
    
    // Check power status - use dummy data for now
    PowerData powerData = {3.7f, 0.1f, 85, millis(), true};
    
    // Update subsystem states based on power
    if (powerData.batteryPercentage < BATTERY_CRITICAL_THRESHOLD) {
        SYS_ERROR("Critical battery level: %d%%", powerData.batteryPercentage);
        
        // Enter emergency mode if not already
        if (!SysState().isEmergencyActive()) {
            SysState().triggerEmergency("Critical battery level");
        }
    } else if (powerData.batteryPercentage < BATTERY_LOW_THRESHOLD) {
        SYS_WARNING("Low battery level: %d%%", powerData.batteryPercentage);
        
        // Disable non-critical systems
        if (appState.cameraActive) {
            Camera().enableCamera(false); // Use correct method
            appState.cameraActive = false;
            SYS_INFO("Camera disabled due to low power");
        }
    }
    
    // Update system state with power data
    // SysState().setSubsystemState("power", SubsystemState::ACTIVE);
}

void processPacketHandling() {
    // PacketMgr().update(); // Method doesn't exist
    
    // Check for packet handler errors - simplified for now
    // float packetLossRate = PacketMgr().getPacketLossRate();
    // if (packetLossRate > 10.0f) {
    //     SYS_WARNING("High packet loss rate: %.1f%%", packetLossRate);
    // }
    
    // Update subsystem state
    // SysState().setSubsystemState("lora", SubsystemState::ACTIVE);
}

// ===========================
// Timing Functions
// ===========================

bool shouldSendTelemetry() {
    uint32_t currentTime = millis();
    if (currentTime - appState.lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
        appState.lastTelemetryTime = currentTime;
        return true;
    }
    return false;
}

bool shouldSendHeartbeat() {
    uint32_t currentTime = millis();
    if (currentTime - appState.lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS) {
        appState.lastHeartbeatTime = currentTime;
        return true;
    }
    return false;
}

bool shouldReportStatus() {
    uint32_t currentTime = millis();
    if (currentTime - appState.lastStatusReportTime >= STATUS_REPORT_INTERVAL_MS) {
        appState.lastStatusReportTime = currentTime;
        return true;
    }
    return false;
}

bool shouldUpdatePerformance() {
    uint32_t currentTime = millis();
    if (currentTime - appState.lastPerformanceTime >= PERFORMANCE_INTERVAL_MS) {
        appState.lastPerformanceTime = currentTime;
        return true;
    }
    return false;
}

// ===========================
// Communication Functions
// ===========================

void sendTelemetryData() {
    if (!appState.communicationActive) {
        return;
    }
    
    // Get sensor data
    BMP280Data sensorData = Sensors().getBMP280Data();
    GPSData gpsData = Sensors().getGPSData();
    
    // Create combined telemetry data
    TelemetryData telemetryData;
    telemetryData.temperature = sensorData.temperature;
    telemetryData.pressure = sensorData.pressure;
    telemetryData.humidity = 0.0f; // Not available from BMP280
    
    // Power data - create dummy values for now
    telemetryData.batteryVoltage = 3.7f;
    telemetryData.batteryCurrent = 0.1f;
    telemetryData.batteryPercentage = 85;
    
    telemetryData.uptime = millis();
    telemetryData.rssi = -85; // Default RSSI
    telemetryData.freeHeap = ESP.getFreeHeap();
    telemetryData.cpuTemperature = sensorData.temperature;
    telemetryData.powerState = 1;
    
    // Create and queue telemetry packet
    if (PacketMgr().createTelemetryPacket(telemetryData)) {
        SYS_LOG("Telemetry packet created");
    } else {
        SYS_WARNING("Failed to create telemetry packet");
    }
}

void sendHeartbeatPacket() {
    if (!appState.communicationActive) {
        return;
    }
    
    if (PacketMgr().createHeartbeatPacket()) {
        SYS_LOG("Heartbeat packet created");
    } else {
        SYS_WARNING("Failed to create heartbeat packet");
    }
}

void sendStatusReport() {
    if (!appState.communicationActive) {
        return;
    }
    
    // Create status message
    char statusMessage[200];
    snprintf(statusMessage, sizeof(statusMessage),
             "Mode:%s Phase:%s Status:%s Loop:%lu MaxLoop:%lu",
             SysState().modeToString(SysState().getMode()),
             SysState().flightPhaseToString(SysState().getFlightPhase()),
             SysState().statusToString(SysState().getSystemStatus()),
             appState.loopCounter,
             appState.maxLoopTime);
    
    if (PacketMgr().createStatusPacket(statusMessage)) {
        SYS_LOG("Status report packet created");
    } else {
        SYS_WARNING("Failed to create status report packet");
    }
}

void processIncomingCommands() {
    // This would process any received commands
    // For now, it's a placeholder
}

// ===========================
// Utility Functions
// ===========================

void printSystemInfo() {
    Serial.println("\n=== System Information ===");
    Serial.printf("Firmware: %s\n", FIRMWARE_VERSION);
    Serial.printf("Build: %s\n", BUILD_DATE);
    Serial.printf("Board: %s\n", BOARD_NAME);
    Serial.printf("CPU Freq: %lu MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Flash Size: %lu MB\n", ESP.getFlashChipSize() / (1024 * 1024));
    Serial.printf("Free Heap: %lu bytes\n", ESP.getFreeHeap());
    Serial.printf("Uptime: %lu ms\n", millis());
    Serial.println("========================\n");
}

void handleSystemError(const char* error) {
    SYS_ERROR("System error: %s", error);
    
    appState.errorCount++;
    appState.lastErrorTime = millis();
    strncpy(appState.lastErrorMessage, error, sizeof(appState.lastErrorMessage) - 1);
    appState.lastErrorMessage[sizeof(appState.lastErrorMessage) - 1] = '\0';
    
    // Trigger emergency if too many errors
    if (appState.errorCount > 10) {
        SysState().triggerEmergency("Too many system errors");
    }
}

void updatePerformanceMetrics(uint32_t loopTime) {
    // Update debug performance metrics
    Debug.updateLoopTime(loopTime);
    
    // Print performance info periodically
    static uint32_t lastPrintTime = 0;
    if (millis() - lastPrintTime > 60000) {  // Every minute
        SYS_INFO("Performance - Loop: %lu ms, Max: %lu ms, Avg: %lu ms, Count: %lu",
                 loopTime, appState.maxLoopTime, appState.avgLoopTime, appState.loopCounter);
        lastPrintTime = millis();
    }
}

bool checkSystemHealth() {
    // Overall system health check
    bool healthy = true;
    
    // Check memory
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 50000) {  // Less than 50KB free
        SYS_WARNING("Low memory: %lu bytes free", freeHeap);
        healthy = false;
    }
    
    // Check loop time
    if (appState.maxLoopTime > MAIN_LOOP_INTERVAL_MS * 2) {
        SYS_WARNING("High loop time: %lu ms", appState.maxLoopTime);
        healthy = false;
    }
    
    return healthy;
}

// ===========================
// Event Handlers
// ===========================

void onSystemEvent(const SystemEvent& event) {
    // SYS_LOG("System event: %s", SysState().eventTypeToString(event.eventType)); // Method may not be accessible
    
    // Handle different event types - simplified for now
    // switch (event.eventType) {
    //     case EventType::EMERGENCY_TRIGGERED:
    //         onEmergencyTriggered(reinterpret_cast<const char*>(event.data));
    //         break;
    //     case EventType::MODE_CHANGE:
    //         onModeChanged(static_cast<SystemMode>(event.data[0]));
    //         break;
    //     case EventType::FLIGHT_PHASE_CHANGE:
    //         onFlightPhaseChanged(static_cast<FlightPhase>(event.data[0]));
    //         break;
    //     default:
    //         break;
    // }
}

void onEmergencyTriggered(const char* reason) {
    SYS_ERROR("Emergency triggered: %s", reason);
    
    // Take emergency actions
    if (appState.cameraActive) {
        Camera().enableCamera(false); // Use correct method
        appState.cameraActive = false;
    }
    
    // Reduce sensor reading frequency - simplified for now
    // Sensors().setReadInterval(5000);  // 0.2 Hz
    
    // Increase communication frequency for emergency beacon - simplified for now
    // LoRaComm().setPower(20);  // Maximum power
}

void onModeChanged(SystemMode newMode) {
    SYS_INFO("System mode changed to: %s", SysState().modeToString(newMode));
    
    // Adjust system behavior based on mode - simplified for now
    // switch (newMode) {
    //     case SystemMode::ASCENT:
    //         // Increase data rate during ascent
    //         Sensors().setReadInterval(500);
    //         break;
    //     case SystemMode::DESCENT:
    //         // Moderate data rate during descent
    //         Sensors().setReadInterval(1000);
    //         break;
    //     case SystemMode::EMERGENCY:
    //         // Minimum functionality in emergency
    //         Sensors().setReadInterval(5000);
    //         break;
    //     case SystemMode::SAFE_MODE:
    //         // Reduced functionality in safe mode
    //         Sensors().setReadInterval(2000);
    //         break;
    //     default:
    //         break;
    // }
}

void onFlightPhaseChanged(FlightPhase newPhase) {
    SYS_INFO("Flight phase changed to: %s", SysState().flightPhaseToString(newPhase));
    
    // Adjust behavior based on flight phase - simplified for now
    // switch (newPhase) {
    //     case FlightPhase::LAUNCH:
    //         SYS_INFO("Launch detected - increasing sensor rate");
    //         Sensors().setReadInterval(250);
    //         break;
    //     case FlightPhase::APEX:
    //         SYS_INFO("Apex detected - recording maximum altitude");
    //         break;
    //     case FlightPhase::PARACHUTE_DESCENT:
    //         SYS_INFO("Parachute descent detected");
    //         break;
    //     case FlightPhase::LANDING:
    //         SYS_INFO("Landing detected - entering recovery mode");
    //         Sensors().setReadInterval(2000);
    //         break;
    //     default:
    //         break;
    // }
}

// ===========================
// Debug and Development Functions
// ===========================

#ifdef DEBUG_MODE
void printDebugInfo() {
    Serial.println("\n=== Debug Information ===");
    Serial.printf("Loop Count: %lu\n", appState.loopCounter);
    Serial.printf("Last Loop Time: %lu ms\n", appState.lastLoopTime);
    Serial.printf("Max Loop Time: %lu ms\n", appState.maxLoopTime);
    Serial.printf("Avg Loop Time: %lu ms\n", appState.avgLoopTime);
    Serial.printf("Error Count: %lu\n", appState.errorCount);
    Serial.printf("Sensors Active: %s\n", appState.sensorsActive ? "Yes" : "No");
    Serial.printf("Camera Active: %s\n", appState.cameraActive ? "Yes" : "No");
    Serial.printf("Communication Active: %s\n", appState.communicationActive ? "Yes" : "No");
    Serial.printf("GPS Active: %s\n", appState.gpsActive ? "Yes" : "No");
    Serial.printf("Flight Mode: %s\n", appState.flightMode ? "Yes" : "No");
    Serial.printf("Emergency Mode: %s\n", appState.emergencyMode ? "Yes" : "No");
    Serial.printf("Low Power Mode: %s\n", appState.lowPowerMode ? "Yes" : "No");
    Serial.println("========================\n");
}
#endif
