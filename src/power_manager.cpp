#include "power_manager.h"

// ===========================
// Constructor/Destructor
// ===========================

PowerManager::PowerManager() {
    // Initialize power state
    currentPowerState = PowerState::NORMAL_POWER;
    primaryPowerSource = PowerSource::BATTERY;
    
    // Initialize battery status
    batteryStatus = {
        .voltage = 3.7f,
        .current = 0.0f,
        .capacity = 2000.0f,  // 2000mAh default
        .percentage = 100.0f,
        .temperature = 20.0f,
        .timestamp = 0,
        .charging = false,
        .healthy = true,
        .source = PowerSource::BATTERY
    };
    
    // Initialize consumption tracking
    consumption = {
        .totalCurrent = 0.0f,
        .cameraCurrent = 0.0f,
        .loraCurrent = 0.0f,
        .sensorCurrent = 0.0f,
        .processorCurrent = 0.0f,
        .uptime = 0,
        .totalEnergy = 0.0f
    };
    
    // Initialize power limits
    limits = {
        .criticalVoltage = CRITICAL_VOLTAGE,
        .lowVoltage = LOW_VOLTAGE,
        .normalVoltage = NORMAL_VOLTAGE,
        .maxCurrent = 500.0f,
        .maxTemperature = 60.0f
    };
    
    // Initialize timing
    lastUpdateTime = 0;
    lastStateChange = 0;
    lastVoltageCheck = 0;
    lastCurrentCheck = 0;
    
    // Initialize settings
    powerSavingEnabled = false;
    adaptivePowerEnabled = true;
    emergencyShutdownEnabled = true;
    
    // Initialize callbacks
    onLowBatteryCallback = nullptr;
    onCriticalBatteryCallback = nullptr;
    onPowerStateChangedCallback = nullptr;
    onPowerSourceChangedCallback = nullptr;
    onEmergencyShutdownCallback = nullptr;
}

PowerManager::~PowerManager() {
    end();
}

// ===========================
// Initialization
// ===========================

bool PowerManager::begin() {
    // Initialize ADC for battery monitoring
    analogReadResolution(12);  // 12-bit resolution
    analogSetAttenuation(ADC_11db);  // 11dB attenuation for higher voltage range
    
    // Initialize power control pins
    pinMode(POWER_ENABLE_PIN, OUTPUT);
    // Note: Individual power control pins not available in current hardware design
    // Using global power enable only
    
    // Enable power rails
    digitalWrite(POWER_ENABLE_PIN, HIGH);
    
    // Perform initial readings
    updateBatteryVoltage();
    updateCurrentConsumption();
    calculateBatteryPercentage();
    
    lastUpdateTime = millis();
    
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Initialized successfully");
        printPowerStatus();
    }
    
    return true;
}

void PowerManager::end() {
    // Disable all power rails
    digitalWrite(POWER_ENABLE_PIN, LOW);
}

bool PowerManager::reinitialize() {
    end();
    delay(100);
    return begin();
}

// ===========================
// Power Monitoring
// ===========================

void PowerManager::update() {
    uint32_t currentTime = millis();
    
    // Check if it's time to update voltage
    if (currentTime - lastVoltageCheck >= VOLTAGE_CHECK_INTERVAL) {
        updateBatteryVoltage();
        lastVoltageCheck = currentTime;
    }
    
    // Check if it's time to update current
    if (currentTime - lastCurrentCheck >= CURRENT_CHECK_INTERVAL) {
        updateCurrentConsumption();
        lastCurrentCheck = currentTime;
    }
    
    // Check if it's time to update power state
    if (currentTime - lastStateChange >= STATE_CHECK_INTERVAL) {
        updatePowerState();
        lastStateChange = currentTime;
    }
    
    // Update energy consumption
    updateEnergyConsumption();
    
    // Update uptime
    consumption.uptime = millis() / 1000;
    
    lastUpdateTime = currentTime;
}

void PowerManager::forceUpdate() {
    updateBatteryVoltage();
    updateCurrentConsumption();
    updatePowerState();
    calculateBatteryPercentage();
    updateEnergyConsumption();
}

// ===========================
// Private Update Methods
// ===========================

void PowerManager::updateBatteryVoltage() {
    batteryStatus.voltage = readBatteryVoltage();
    batteryStatus.timestamp = millis();
    
    // Check charging status (simplified - in real implementation would use charger IC)
    batteryStatus.charging = (batteryStatus.voltage > 4.0f);
    
    // Determine power source
    PowerSource oldSource = primaryPowerSource;
    if (batteryStatus.charging) {
        primaryPowerSource = PowerSource::SOLAR;
    } else {
        primaryPowerSource = PowerSource::BATTERY;
    }
    
    // Notify if power source changed
    if (oldSource != primaryPowerSource && onPowerSourceChangedCallback) {
        onPowerSourceChangedCallback(oldSource, primaryPowerSource);
    }
}

void PowerManager::updateCurrentConsumption() {
    // Read actual current if available, otherwise estimate
    float actualCurrent = readBatteryCurrent();
    
    if (actualCurrent > 0) {
        consumption.totalCurrent = actualCurrent;
    } else {
        // Estimate based on active components
        consumption.totalCurrent = PROCESSOR_CURRENT + SENSOR_CURRENT;
        
        // Add LoRa current (transmit vs receive)
        consumption.loraCurrent = LORA_RX_CURRENT;  // Assume receive most of the time
        consumption.totalCurrent += consumption.loraCurrent;
        
        // Camera power control not available in current hardware
        // Assume camera is always enabled when power is on
        consumption.cameraCurrent = CAMERA_CURRENT;
        consumption.totalCurrent += consumption.cameraCurrent;
    }
    
    consumption.processorCurrent = PROCESSOR_CURRENT;
    consumption.sensorCurrent = SENSOR_CURRENT;
    
    batteryStatus.current = consumption.totalCurrent;
}

void PowerManager::updatePowerState() {
    PowerState oldState = currentPowerState;
    
    // Determine power state based on voltage and percentage
    if (batteryStatus.voltage <= limits.criticalVoltage || batteryStatus.percentage <= 5.0f) {
        currentPowerState = PowerState::EMERGENCY_POWER;
    } else if (batteryStatus.voltage <= limits.lowVoltage || batteryStatus.percentage <= 15.0f) {
        currentPowerState = PowerState::CRITICAL_POWER;
    } else if (batteryStatus.voltage <= limits.normalVoltage || batteryStatus.percentage <= 30.0f) {
        currentPowerState = PowerState::LOW_POWER;
    } else if (batteryStatus.percentage >= 80.0f) {
        currentPowerState = PowerState::FULL_POWER;
    } else {
        currentPowerState = PowerState::NORMAL_POWER;
    }
    
    // Handle state changes
    if (oldState != currentPowerState) {
        if (onPowerStateChangedCallback) {
            onPowerStateChangedCallback(oldState, currentPowerState);
        }
        
        // Apply power saving based on new state
        if (adaptivePowerEnabled) {
            applyPowerSaving();
        }
        
        // Handle critical situations
        if (currentPowerState == PowerState::CRITICAL_POWER) {
            handleCriticalBattery();
        } else if (currentPowerState == PowerState::EMERGENCY_POWER) {
            triggerEmergencyShutdown();
        } else if (oldState >= PowerState::LOW_POWER && currentPowerState < PowerState::LOW_POWER) {
            handlePowerRecovery();
        }
    }
    
    // Check for low battery warnings
    if (batteryStatus.percentage <= 20.0f && onLowBatteryCallback) {
        onLowBatteryCallback(batteryStatus.voltage, batteryStatus.percentage);
    }
    
    if (batteryStatus.percentage <= 5.0f && onCriticalBatteryCallback) {
        onCriticalBatteryCallback(batteryStatus.voltage, batteryStatus.percentage);
    }
}

void PowerManager::calculateBatteryPercentage() {
    // Simple linear mapping from voltage to percentage
    batteryStatus.percentage = voltageToPercentage(batteryStatus.voltage);
    
    // Constrain to valid range
    if (batteryStatus.percentage > 100.0f) batteryStatus.percentage = 100.0f;
    if (batteryStatus.percentage < 0.0f) batteryStatus.percentage = 0.0f;
    
    // Update estimated capacity
    batteryStatus.capacity = 2000.0f * (batteryStatus.percentage / 100.0f);
}

void PowerManager::updateEnergyConsumption() {
    static uint32_t lastEnergyUpdate = 0;
    uint32_t currentTime = millis();
    
    if (lastEnergyUpdate == 0) {
        lastEnergyUpdate = currentTime;
        return;
    }
    
    // Calculate time delta in hours
    float deltaTime = (currentTime - lastEnergyUpdate) / 3600000.0f;
    
    // Calculate energy consumed (Wh)
    float power = (batteryStatus.voltage * consumption.totalCurrent) / 1000.0f;  // Convert to watts
    consumption.totalEnergy += power * deltaTime;
    
    lastEnergyUpdate = currentTime;
}

bool PowerManager::checkBatteryHealth() {
    // Check temperature
    if (batteryStatus.temperature > limits.maxTemperature) {
        batteryStatus.healthy = false;
        return false;
    }
    
    // Check for voltage instability (simplified check)
    static float lastVoltage = 0.0f;
    static uint32_t lastHealthCheck = 0;
    
    uint32_t currentTime = millis();
    if (currentTime - lastHealthCheck > 60000) {  // Check every minute
        float voltageDiff = abs(batteryStatus.voltage - lastVoltage);
        
        // Large voltage swings indicate battery issues
        if (voltageDiff > 0.5f && !batteryStatus.charging) {
            batteryStatus.healthy = false;
        } else {
            batteryStatus.healthy = true;
        }
        
        lastVoltage = batteryStatus.voltage;
        lastHealthCheck = currentTime;
    }
    
    return batteryStatus.healthy;
}

// ===========================
// Hardware Interface
// ===========================

float PowerManager::readBatteryVoltage() {
    // Read from ADC pin (assuming voltage divider)
    int rawValue = analogRead(BATTERY_SENSE_PIN);
    
    // Convert ADC reading to voltage
    // Assuming 3.3V reference and voltage divider ratio
    float voltage = (rawValue / 4095.0f) * 3.3f * 2.0f;  // 2.0 = voltage divider ratio
    
    return voltage;
}

float PowerManager::readBatteryCurrent() {
    // Current sensing not available in current hardware
    // Return estimated value based on system state
    return -1.0f;  // Negative indicates not available
}

float PowerManager::readBatteryTemperature() {
    // Temperature sensing not available in current hardware
    // Return estimated value based on system state
    return 20.0f;  // Default temperature
}

// ===========================
// Power Control
// ===========================

void PowerManager::setPowerState(PowerState state) {
    if (state == currentPowerState) {
        return;
    }
    
    PowerState oldState = currentPowerState;
    currentPowerState = state;
    
    if (onPowerStateChangedCallback) {
        onPowerStateChangedCallback(oldState, state);
    }
    
    applyPowerSaving();
}

void PowerManager::applyPowerSaving() {
    switch (currentPowerState) {
        case PowerState::FULL_POWER:
            // All systems at full power
            // Individual power control not available - using global power only
            // Set processor to maximum frequency
            setCpuFrequencyMhz(240);
            break;
            
        case PowerState::NORMAL_POWER:
            // Normal operation with some optimizations
            // Individual power control not available - using global power only
            // Reduce processor frequency slightly
            setCpuFrequencyMhz(160);
            break;
            
        case PowerState::LOW_POWER:
            // Reduced camera usage, lower processor frequency
            // Individual power control not available - using global power only
            setCpuFrequencyMhz(80);
            
            // Notify other systems to enter low power mode
            if (DEBUG_POWER) {
                Serial.println("Power Manager: Entering low power mode");
            }
            break;
            
        case PowerState::CRITICAL_POWER:
            // Minimal operation - only essential systems
            // Individual power control not available - using global power only
            setCpuFrequencyMhz(40);
            
            // Notify other systems to enter critical power mode
            if (DEBUG_POWER) {
                Serial.println("Power Manager: Entering critical power mode");
            }
            break;
            
        case PowerState::EMERGENCY_POWER:
            // Emergency shutdown preparation
            handleCriticalBattery();
            break;
    }
}

void PowerManager::controlPowerRails() {
    // This method can be used for more fine-grained power rail control
    // Implementation depends on specific hardware design
}

// ===========================
// Component Power Control
// ===========================

void PowerManager::enableCamera(bool enable) {
    // Individual power control not available in current hardware
    // Using global power control only
    if (enable) {
        consumption.cameraCurrent = CAMERA_CURRENT;
    } else {
        consumption.cameraCurrent = 0.0f;
    }
}

void PowerManager::enableLoRa(bool enable) {
    // Individual power control not available in current hardware
    // Using global power control only
    if (enable) {
        consumption.loraCurrent = LORA_RX_CURRENT;
    } else {
        consumption.loraCurrent = 0.0f;
    }
}

void PowerManager::enableSensors(bool enable) {
    // Individual power control not available in current hardware
    // Using global power control only
    if (enable) {
        consumption.sensorCurrent = SENSOR_CURRENT;
    } else {
        consumption.sensorCurrent = 0.0f;
    }
}

void PowerManager::setProcessorFrequency(uint32_t frequency) {
    setCpuFrequencyMhz(frequency / 1000000);
    
    // Update current estimate based on frequency
    float frequencyRatio = (float)frequency / 240000000.0f;  // Ratio to 240MHz
    consumption.processorCurrent = PROCESSOR_CURRENT * frequencyRatio;
}

// ===========================
// Sleep and Wake Management
// ===========================

void PowerManager::enterDeepSleep(uint32_t durationMs) {
    if (DEBUG_POWER) {
        Serial.printf("Power Manager: Entering deep sleep for %lu ms\n", durationMs);
    }
    
    // Configure wake-up sources
    esp_sleep_enable_timer_wakeup(durationMs * 1000);  // Convert to microseconds
    
    // Enter deep sleep
    esp_deep_sleep_start();
}

void PowerManager::enterLightSleep(uint32_t durationMs) {
    if (DEBUG_POWER) {
        Serial.printf("Power Manager: Entering light sleep for %lu ms\n", durationMs);
    }
    
    // Configure light sleep
    esp_sleep_enable_timer_wakeup(durationMs * 1000);
    
    // Enter light sleep
    esp_light_sleep_start();
}

void PowerManager::wakeup() {
    // This is called after waking from sleep
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Waking up from sleep");
    }
    
    // Reinitialize systems
    forceUpdate();
}

bool PowerManager::isSleeping() const {
    return false;  // Simple implementation - would need sleep state tracking
}

// ===========================
// Emergency Handling
// ===========================

void PowerManager::triggerEmergencyShutdown() {
    if (!emergencyShutdownEnabled) {
        return;
    }
    
    if (DEBUG_POWER) {
        Serial.println("Power Manager: TRIGGERING EMERGENCY SHUTDOWN");
    }
    
    // Notify callback
    if (onEmergencyShutdownCallback) {
        onEmergencyShutdownCallback("Critical battery level");
    }
    
    // Disable non-essential systems
    // Individual power control not available in current hardware
    // Using global power control only
    
    // Send emergency message via LoRa if possible
    // This would be handled by the communication system
    
    // Enter deep sleep or shutdown after delay
    delay(5000);  // Allow time for emergency transmission
    enterDeepSleep(3600000);  // Sleep for 1 hour
}

void PowerManager::handleCriticalBattery() {
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Handling critical battery level");
    }
    
    // Disable camera and sensors
    // Individual power control not available in current hardware
    // Using global power control only
    
    // Set minimum processor frequency
    setCpuFrequencyMhz(20);
    
    // Keep only LoRa active for emergency communications
    // Individual power control not available - using software control only
}

void PowerManager::handleLowBattery() {
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Handling low battery level");
    }
    
    // Apply aggressive power saving
    powerSavingEnabled = true;
    
    // Reduce camera usage
    // Individual power control not available - using software control only
    
    // Lower processor frequency
    setCpuFrequencyMhz(80);
}

void PowerManager::handlePowerRecovery() {
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Power recovered, restoring normal operation");
    }
    
    // Restore normal power settings
    powerSavingEnabled = false;
    
    // Re-enable systems gradually
    // Individual power control not available - using software control only
    
    // Restore processor frequency
    setCpuFrequencyMhz(160);
}

// ===========================
// Power Limits
// ===========================

void PowerManager::setPowerLimits(const PowerLimits& newLimits) {
    limits = newLimits;
    
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Power limits updated");
        printPowerLimits();
    }
}

bool PowerManager::isWithinLimits() const {
    return (batteryStatus.voltage >= limits.criticalVoltage &&
            consumption.totalCurrent <= limits.maxCurrent &&
            batteryStatus.temperature <= limits.maxTemperature);
}

// ===========================
// Battery Management
// ===========================

float PowerManager::getEstimatedRuntime() const {
    if (consumption.totalCurrent <= 0) {
        return 0;
    }
    
    float remainingCapacity = batteryStatus.capacity;  // mAh
    return remainingCapacity / consumption.totalCurrent;  // hours
}

float PowerManager::getPowerEfficiency() const {
    // Simple efficiency calculation based on voltage drop under load
    float noLoadVoltage = 4.2f;  // Assume full charge voltage
    float currentVoltage = batteryStatus.voltage;
    
    if (noLoadVoltage <= 0) {
        return 0;
    }
    
    return (currentVoltage / noLoadVoltage) * 100.0f;
}

// ===========================
// Calibration and Diagnostics
// ===========================

bool PowerManager::calibrateVoltage() {
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Calibrating voltage measurement");
    }
    
    // Simple calibration - would need reference voltage
    // This is a placeholder implementation
    float measuredVoltage = readBatteryVoltage();
    
    // Store calibration factor if needed
    // Implementation depends on specific hardware
    
    return true;
}

bool PowerManager::calibrateCurrent() {
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Calibrating current measurement");
    }
    
    // Simple calibration - would need known load
    // This is a placeholder implementation
    
    return true;
}

void PowerManager::resetEnergyCounter() {
    consumption.totalEnergy = 0.0f;
    
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Energy counter reset");
    }
}

void PowerManager::runDiagnostics() {
    if (DEBUG_POWER) {
        Serial.println("Power Manager: Running diagnostics");
    }
    
    // Check battery health
    batteryStatus.healthy = checkBatteryHealth();
    
    // Check power limits
    bool withinLimits = isWithinLimits();
    
    // Check power rail status - individual power control not available
    bool globalPower = (digitalRead(POWER_ENABLE_PIN) == HIGH);
    
    Serial.println("=== Power Diagnostics ===");
    Serial.printf("Battery Healthy: %s\n", batteryStatus.healthy ? "Yes" : "No");
    Serial.printf("Within Limits: %s\n", withinLimits ? "Yes" : "No");
    Serial.printf("Global Power: %s\n", globalPower ? "On" : "Off");
    Serial.printf("Camera Current: %.1f mA\n", consumption.cameraCurrent);
    Serial.printf("LoRa Current: %.1f mA\n", consumption.loraCurrent);
    Serial.printf("Sensor Current: %.1f mA\n", consumption.sensorCurrent);
}

// ===========================
// Callback Registration
// ===========================

void PowerManager::setLowBatteryCallback(void (*callback)(float, float)) {
    onLowBatteryCallback = callback;
}

void PowerManager::setCriticalBatteryCallback(void (*callback)(float, float)) {
    onCriticalBatteryCallback = callback;
}

void PowerManager::setPowerStateChangedCallback(void (*callback)(PowerState, PowerState)) {
    onPowerStateChangedCallback = callback;
}

void PowerManager::setPowerSourceChangedCallback(void (*callback)(PowerSource, PowerSource)) {
    onPowerSourceChangedCallback = callback;
}

void PowerManager::setEmergencyShutdownCallback(void (*callback)(const char*)) {
    onEmergencyShutdownCallback = callback;
}

// ===========================
// Debug Methods
// ===========================

void PowerManager::printPowerStatus() const {
    Serial.println("=== Power Manager Status ===");
    Serial.printf("Power State: %s\n", powerStateToString(currentPowerState));
    Serial.printf("Power Source: %s\n", powerSourceToString(primaryPowerSource));
    Serial.printf("Power Saving: %s\n", powerSavingEnabled ? "Enabled" : "Disabled");
    Serial.printf("Adaptive Power: %s\n", adaptivePowerEnabled ? "Enabled" : "Disabled");
    Serial.printf("Emergency Shutdown: %s\n", emergencyShutdownEnabled ? "Enabled" : "Disabled");
}

void PowerManager::printBatteryStatus() const {
    Serial.println("=== Battery Status ===");
    Serial.printf("Voltage: %.2f V\n", batteryStatus.voltage);
    Serial.printf("Current: %.1f mA\n", batteryStatus.current);
    Serial.printf("Percentage: %.1f%%\n", batteryStatus.percentage);
    Serial.printf("Capacity: %.1f mAh\n", batteryStatus.capacity);
    Serial.printf("Temperature: %.1f °C\n", batteryStatus.temperature);
    Serial.printf("Charging: %s\n", batteryStatus.charging ? "Yes" : "No");
    Serial.printf("Healthy: %s\n", batteryStatus.healthy ? "Yes" : "No");
    Serial.printf("Estimated Runtime: %.1f hours\n", getEstimatedRuntime());
    Serial.printf("Power Efficiency: %.1f%%\n", getPowerEfficiency());
}

void PowerManager::printConsumptionStatus() const {
    Serial.println("=== Power Consumption ===");
    Serial.printf("Total Current: %.1f mA\n", consumption.totalCurrent);
    Serial.printf("Camera Current: %.1f mA\n", consumption.cameraCurrent);
    Serial.printf("LoRa Current: %.1f mA\n", consumption.loraCurrent);
    Serial.printf("Sensor Current: %.1f mA\n", consumption.sensorCurrent);
    Serial.printf("Processor Current: %.1f mA\n", consumption.processorCurrent);
    Serial.printf("Total Energy: %.2f Wh\n", consumption.totalEnergy);
    Serial.printf("Uptime: %lu seconds\n", consumption.uptime);
}

void PowerManager::printPowerLimits() const {
    Serial.println("=== Power Limits ===");
    Serial.printf("Critical Voltage: %.2f V\n", limits.criticalVoltage);
    Serial.printf("Low Voltage: %.2f V\n", limits.lowVoltage);
    Serial.printf("Normal Voltage: %.2f V\n", limits.normalVoltage);
    Serial.printf("Max Current: %.1f mA\n", limits.maxCurrent);
    Serial.printf("Max Temperature: %.1f °C\n", limits.maxTemperature);
}

void PowerManager::printSystemState() const {
    Serial.println("=== Power System State ===");
    printPowerStatus();
    printBatteryStatus();
    printConsumptionStatus();
}

// ===========================
// Utility Functions
// ===========================

const char* powerStateToString(PowerState state) {
    switch (state) {
        case PowerState::FULL_POWER: return "Full Power";
        case PowerState::NORMAL_POWER: return "Normal Power";
        case PowerState::LOW_POWER: return "Low Power";
        case PowerState::CRITICAL_POWER: return "Critical Power";
        case PowerState::EMERGENCY_POWER: return "Emergency Power";
        default: return "Unknown";
    }
}

const char* powerSourceToString(PowerSource source) {
    switch (source) {
        case PowerSource::BATTERY: return "Battery";
        case PowerSource::SOLAR: return "Solar";
        case PowerSource::BACKUP: return "Backup";
        case PowerSource::UNKNOWN: return "Unknown";
        default: return "Unknown";
    }
}

float voltageToPercentage(float voltage, float maxVoltage) {
    // Simple linear mapping from voltage to percentage
    float percentage = ((voltage - 3.0f) / (maxVoltage - 3.0f)) * 100.0f;
    
    // Constrain to valid range
    if (percentage > 100.0f) percentage = 100.0f;
    if (percentage < 0.0f) percentage = 0.0f;
    
    return percentage;
}

float calculatePowerEfficiency(float outputPower, float inputPower) {
    if (inputPower <= 0) {
        return 0;
    }
    
    return (outputPower / inputPower) * 100.0f;
}

uint32_t estimateRuntime(float batteryCapacity, float currentDraw) {
    if (currentDraw <= 0) {
        return 0;
    }
    
    float runtimeHours = batteryCapacity / currentDraw;
    return (uint32_t)(runtimeHours * 3600);  // Convert to seconds
}
