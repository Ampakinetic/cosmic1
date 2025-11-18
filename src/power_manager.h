#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include "balloon_config.h"
#include "sensor_pins.h"

// ===========================
// Power Management Data Structures
// ===========================

enum class PowerState : uint8_t {
    FULL_POWER = 0,
    NORMAL_POWER = 1,
    LOW_POWER = 2,
    CRITICAL_POWER = 3,
    EMERGENCY_POWER = 4
};

enum class PowerSource : uint8_t {
    BATTERY = 0,
    SOLAR = 1,
    BACKUP = 2,
    UNKNOWN = 3
};

struct BatteryStatus {
    float voltage;           // Battery voltage in volts
    float current;           // Current draw in mA
    float capacity;          // Remaining capacity in mAh
    float percentage;        // Battery level percentage
    float temperature;       // Battery temperature in Celsius
    uint32_t timestamp;     // Last update timestamp
    bool charging;           // True if charging
    bool healthy;            // True if battery is healthy
    PowerSource source;      // Current power source
};

struct PowerConsumption {
    float totalCurrent;      // Total current draw in mA
    float cameraCurrent;     // Camera current in mA
    float loraCurrent;      // LoRa current in mA
    float sensorCurrent;     // Sensors current in mA
    float processorCurrent;  // Processor current in mA
    uint32_t uptime;        // System uptime in seconds
    float totalEnergy;       // Total energy consumed in Wh
};

struct PowerLimits {
    float criticalVoltage;   // Critical voltage threshold
    float lowVoltage;        // Low voltage threshold
    float normalVoltage;     // Normal voltage threshold
    float maxCurrent;       // Maximum allowed current
    float maxTemperature;   // Maximum battery temperature
};

// ===========================
// Power Manager Class
// ===========================

class PowerManager {
private:
    // Power status
    PowerState currentPowerState;
    PowerSource primaryPowerSource;
    BatteryStatus batteryStatus;
    PowerConsumption consumption;
    PowerLimits limits;
    
    // Timing and monitoring
    uint32_t lastUpdateTime;
    uint32_t lastStateChange;
    uint32_t lastVoltageCheck;
    uint32_t lastCurrentCheck;
    
    // Power management settings
    bool powerSavingEnabled;
    bool adaptivePowerEnabled;
    bool emergencyShutdownEnabled;
    
    // Monitoring intervals (milliseconds)
    static const uint32_t VOLTAGE_CHECK_INTERVAL = 5000;    // 5 seconds
    static const uint32_t CURRENT_CHECK_INTERVAL = 1000;    // 1 second
    static const uint32_t STATE_CHECK_INTERVAL = 10000;     // 10 seconds
    static const uint32_t STATUS_UPDATE_INTERVAL = 30000;    // 30 seconds
    
    // Voltage thresholds
    static constexpr float CRITICAL_VOLTAGE = 3.2f;
    static constexpr float LOW_VOLTAGE = 3.4f;
    static constexpr float NORMAL_VOLTAGE = 3.7f;
    
    // Current consumption estimates (mA)
    static constexpr float CAMERA_CURRENT = 200.0f;
    static constexpr float LORA_TX_CURRENT = 120.0f;
    static constexpr float LORA_RX_CURRENT = 15.0f;
    static constexpr float SENSOR_CURRENT = 50.0f;
    static constexpr float PROCESSOR_CURRENT = 100.0f;
    
    // Private methods
    void updateBatteryVoltage();
    void updateCurrentConsumption();
    void updatePowerState();
    void calculateBatteryPercentage();
    void updateEnergyConsumption();
    bool checkBatteryHealth();
    void applyPowerSaving();
    void emergencyPowerOff();
    float readBatteryVoltage();
    float readBatteryCurrent();
    float readBatteryTemperature();
    void controlPowerRails();
    
public:
    PowerManager();
    ~PowerManager();
    
    // Initialization
    bool begin();
    void end();
    bool reinitialize();
    
    // Power monitoring
    void update();
    void forceUpdate();
    BatteryStatus getBatteryStatus() const { return batteryStatus; }
    PowerConsumption getConsumption() const { return consumption; }
    PowerState getPowerState() const { return currentPowerState; }
    PowerSource getPowerSource() const { return primaryPowerSource; }
    
    // Power control
    void setPowerState(PowerState state);
    void enablePowerSaving(bool enable) { powerSavingEnabled = enable; }
    void enableAdaptivePower(bool enable) { adaptivePowerEnabled = enable; }
    void enableEmergencyShutdown(bool enable) { emergencyShutdownEnabled = enable; }
    
    bool isPowerSavingEnabled() const { return powerSavingEnabled; }
    bool isAdaptivePowerEnabled() const { return adaptivePowerEnabled; }
    bool isEmergencyShutdownEnabled() const { return emergencyShutdownEnabled; }
    
    // Battery management
    bool isBatteryHealthy() const { return batteryStatus.healthy; }
    bool isCharging() const { return batteryStatus.charging; }
    float getBatteryVoltage() const { return batteryStatus.voltage; }
    float getBatteryPercentage() const { return batteryStatus.percentage; }
    float getBatteryTemperature() const { return batteryStatus.temperature; }
    
    // Power consumption
    float getTotalCurrent() const { return consumption.totalCurrent; }
    float getEstimatedRuntime() const;  // Estimated runtime in hours
    float getPowerEfficiency() const;   // Power efficiency percentage
    
    // Power limits
    void setPowerLimits(const PowerLimits& newLimits);
    PowerLimits getPowerLimits() const { return limits; }
    bool isWithinLimits() const;
    
    // Component power control
    void enableCamera(bool enable);
    void enableLoRa(bool enable);
    void enableSensors(bool enable);
    void setProcessorFrequency(uint32_t frequency);
    
    // Sleep and wake management
    void enterDeepSleep(uint32_t durationMs);
    void enterLightSleep(uint32_t durationMs);
    void wakeup();
    bool isSleeping() const;
    
    // Emergency handling
    void triggerEmergencyShutdown();
    void handleCriticalBattery();
    void handleLowBattery();
    void handlePowerRecovery();
    
    // Calibration and diagnostics
    bool calibrateVoltage();
    bool calibrateCurrent();
    void resetEnergyCounter();
    void runDiagnostics();
    
    // Status and debugging
    void printPowerStatus() const;
    void printBatteryStatus() const;
    void printConsumptionStatus() const;
    void printPowerLimits() const;
    void printSystemState() const;
    
    // Event callbacks
    void (*onLowBatteryCallback)(float voltage, float percentage);
    void (*onCriticalBatteryCallback)(float voltage, float percentage);
    void (*onPowerStateChangedCallback)(PowerState oldState, PowerState newState);
    void (*onPowerSourceChangedCallback)(PowerSource oldSource, PowerSource newSource);
    void (*onEmergencyShutdownCallback)(const char* reason);
    
    // Callback registration
    void setLowBatteryCallback(void (*callback)(float, float));
    void setCriticalBatteryCallback(void (*callback)(float, float));
    void setPowerStateChangedCallback(void (*callback)(PowerState, PowerState));
    void setPowerSourceChangedCallback(void (*callback)(PowerSource, PowerSource));
    void setEmergencyShutdownCallback(void (*callback)(const char*));
};

// ===========================
// Global Instance
// ===========================

extern PowerManager& PowerMgr();

// ===========================
// Utility Functions
// ===========================

const char* powerStateToString(PowerState state);
const char* powerSourceToString(PowerSource source);
float voltageToPercentage(float voltage, float maxVoltage = 4.2f);
float calculatePowerEfficiency(float outputPower, float inputPower);
uint32_t estimateRuntime(float batteryCapacity, float currentDraw);

#endif // POWER_MANAGER_H
