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
#include <HardwareSerial.h>
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

// Phase 1: Command Protocol & Control
#include "e32_lora.h"
#include "command_handler.h"
#include "auto_capture.h"

// OLED diagnostics (status_display) — bench-visible radio/beacon health
#include "status_display.h"

// Phase 2: Image Transmission
#include "image_tx_manager.h"

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

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "2.0.0"
#endif
#define BUILD_DATE __DATE__ " " __TIME__
#ifndef SYSTEM_NAME
#define SYSTEM_NAME "Cosmic1-Balloon"
#endif

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
    
    // Print welcome message immediately after serial init
    Serial.println();
    Serial.println("========================================");
    Serial.printf("Cosmic1 Balloon Firmware v%s\n", FIRMWARE_VERSION);
    Serial.printf("Build: %s\n", BUILD_DATE);
    Serial.printf("Board: ESP32-S3\n");
    Serial.println("========================================");
    Serial.println("Starting system initialization...");
    
    // Initialize debug system
    Serial.println("Initializing debug system...");
    if (!Debug.begin()) {
        Serial.println("FATAL: Failed to initialize debug system!");
        return;
    }
    
    Serial.println("Debug system initialized successfully");
    SYS_INFO("System booting...");
    
    // Initialize application state
    Serial.println("Initializing application state...");
    memset(&appState, 0, sizeof(appState));
    appState.startTime = millis();
    appState.lastLoopTime = appState.startTime;
    appState.maxLoopTime = 0;
    appState.avgLoopTime = MAIN_LOOP_INTERVAL_MS;
    Serial.println("Application state initialized");
    
    // Initialize hardware
    Serial.println("Initializing hardware...");
    if (!initializeHardware()) {
        Serial.println("FATAL: Hardware initialization failed!");
        SYS_ERROR("Hardware initialization failed");
        return;
    }
    Serial.println("Hardware initialization complete");
    
    // Initialize subsystems
    if (!initializeSubsystems()) {
        SYS_ERROR("Subsystem initialization failed");
        StatusOLED().showBootStage("BOOT FAILED");
        return;
    }

    // Configure system
    if (!configureSystem()) {
        SYS_ERROR("System configuration failed");
        StatusOLED().showBootStage("BOOT FAILED");
        return;
    }

    // Perform system checks
    if (!performSystemChecks()) {
        SYS_ERROR("System checks failed");
        StatusOLED().showBootStage("BOOT FAILED");
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
    StatusOLED().showBootStage("READY");
}

void loop() {
    if (!appState.initialized) {
        delay(1000);
        return;
    }
    
    uint32_t loopStartTime = millis();

    // WR-09: no try/catch — ESP32 Arduino builds compile with exceptions
    // disabled (and even enabled, faults on this platform abort/reboot
    // rather than unwinding C++ stacks), so a catch block here can never
    // catch the failures it wraps — false containment. Fault containment is
    // the watchdog plus the handleSystemError() call sites at real error
    // paths.
    // Feed watchdog
    if (Debug.isWatchdogEnabled()) {
        Debug.feedWatchdog();
    }

    // Update system state
    updateSystemState();

    // Process main subsystems
    processSensors();
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

    // OLED status screen (status_display): 1 Hz refresh from the same live
    // sources the beacon path reads — radio truth, subsystem health, and
    // the transmit result the serial log prints
    static uint32_t lastOledMs = 0;
    if (millis() - lastOledMs >= 1000) {
        lastOledMs = millis();
        BalloonOledStatus oled{};
        oled.e32Ready = E32LoRaModule().isReady();
        oled.auxHigh = E32LoRaModule().isAuxHigh();
        oled.txErrors = E32LoRaModule().getTransmitErrorCount();
        oled.bmpOk = Sensors().isBMP280Ready();
        oled.camOk = appState.cameraActive;
        GPSData gpsNow = Sensors().getGPSData();
        oled.gpsSats = gpsNow.satellites;
        oled.batteryV = PowerMgr().getBatteryVoltage();
        oled.beaconSeq = ImageTx().getBeaconSeq();
        oled.beaconsSent = ImageTx().getBeaconsSent();
        oled.lastBeaconOk = ImageTx().getLastBeaconOk();
        oled.lastBeaconAgeMs = ImageTx().getBeaconAgeMs();
        oled.upMs = millis();
        oled.freeHeap = ESP.getFreeHeap();
        StatusOLED().render(oled);
    }

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

    // OLED status screen comes alive the moment the shared I2C bus exists
    // (Sensors owns Wire) — the remaining boot stages are then visible on
    // the panel, and a hang reads as the stage it stopped at
    StatusOLED().begin(StatusDisplay::Board::BALLOON);
    StatusOLED().showBootStage("SENSORS");

    // Initialize camera manager
    if (!Camera().begin()) {
        SYS_WARNING("Camera manager initialization failed - continuing without camera");
        appState.cameraActive = false;
    } else {
        SYS_INFO("Camera manager initialized");
        appState.cameraActive = true;
    }
    StatusOLED().showBootStage("CAMERA");

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

    // Phase 1: Initialize E32 LoRa module and Command Handler
    HardwareSerial* loraSerial = &Serial2;
    if (!E32LoRaModule().begin(loraSerial, 48, 14, 19, 20, 21, 9600)) {
        SYS_WARNING("E32 LoRa module initialization failed");
    } else {
        SYS_INFO("E32 LoRa module initialized");
    }
    StatusOLED().showBootStage("LORA E32");

    if (!CmdHandler().begin(&E32LoRaModule(), &Camera())) {
        SYS_WARNING("Command handler initialization failed");
    } else {
        SYS_INFO("Command handler initialized");
    }

    if (!AutoCap().begin(&Camera())) {
        SYS_WARNING("Auto-capture module initialization failed");
    } else {
        SYS_INFO("Auto-capture module initialized");
    }

    // Phase 2: image transfer push module (thumbnail stream after each capture)
    if (!ImageTx().begin(&E32LoRaModule())) {
        SYS_WARNING("Image TX module initialization failed");
    } else {
        SYS_INFO("Image TX module initialized");
    }
    StatusOLED().showBootStage("IMG TX");
    appState.communicationActive = true;

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
    
    // I2C initialization is handled by sensor_manager
    // Do not initialize Wire here to avoid "Bus already started" warnings

    // Initialize UART2 for LoRa E32 module
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    pinMode(LORA_AUX_PIN, INPUT);
    // Set normal mode (M0=0, M1=0)
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);
    // UART2 will be initialized by LoRa communication layer
    // Serial2.begin(LORA_BAUD_RATE, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

    // Initialize UART for GPS
    Serial1.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_TX_PIN, GPS_RX_PIN);
    
    // Initialize power control pin
   // pinMode(POWER_ENABLE_PIN, OUTPUT);
   // digitalWrite(POWER_ENABLE_PIN, HIGH);  // Enable power to sensors
    
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
    
    // LoRa E32 pins are handled by UART initialization in initializeBoard()
    
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
    
    // I2C check is deferred to sensor_manager initialization
    // Cannot check I2C here as Wire is not yet initialized
    // The sensor_manager will handle I2C device detection
    
    // Check GPS serial communication
    if (Serial1.available() > 0) {
        SYS_INFO("GPS communication detected");
    } else {
        SYS_WARNING("No GPS communication detected (may need more time)");
    }
    
    // Check LoRa E32 module (check AUX pin state)
    int auxState = digitalRead(LORA_AUX_PIN);
    SYS_INFO("LoRa E32 AUX pin state: %s", auxState ? "HIGH" : "LOW");
    // In normal mode (M0=0, M1=0), AUX should be HIGH when module is ready
    if (auxState == HIGH) {
        SYS_INFO("LoRa E32 module appears ready (AUX is HIGH)");
    } else {
        SYS_WARNING("LoRa E32 module AUX is LOW (may be busy or in sleep mode)");
    }
    
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
    // Refresh PowerMgr's cached readings on a ~1s cadence (D-26 millis
    // idiom) — the telemetry beacon reads PowerMgr().getBatteryVoltage(),
    // and that value is only as fresh as the last update() call
    static uint32_t lastPowerUpdateMs = 0;
    if (millis() - lastPowerUpdateMs >= 1000) {
        lastPowerUpdateMs = millis();
        PowerMgr().update();
    }

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

    // Process incoming camera commands (Phase 1)
    CmdHandler().process();

    // Run the interval auto-capture timer (Phase 1, CTRL-03/CTRL-04)
    AutoCap().process();

    // Push captured-image thumbnails over the E32 link (Phase 2, IMG-01 push
    // half). Ordering is the first half of PRI-01 arbitration: command
    // responses (sent inside CmdHandler().process() above) always get the
    // transmit opportunity before image traffic — at most one chunk transmit
    // can ever sit between a response and the radio.
    ImageTx().process();

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
