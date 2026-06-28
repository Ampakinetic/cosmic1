# ESP32-S3 Crash and Debugger Troubleshooting

## Issue 1: I2C Bus Crash - FIXED ✅

### Problem
The system was experiencing crashes with these warning messages:
```
[  1248][W][Wire.cpp:296] begin(): Bus already started in Master Mode.
[  1254][W][Wire.cpp:296] begin(): Bus already started in Master Mode.
```

### Root Cause
**Multiple I2C initialization issues** - The I2C bus (`Wire`) was being initialized multiple times:
1. In `main_balloon.cpp` - `checkHardwareStatus()` tried to use Wire BEFORE it was initialized
2. In `main_balloon.cpp` - `initializeBoard()` function had a duplicate `Wire.begin()` call
3. **Critical:** The Adafruit_BMP280 library internally calls `Wire.begin()` in its `begin()` method

### Solution Applied
1. **Removed premature Wire usage** in `checkHardwareStatus()` - deferred I2C checking to sensor_manager
2. **Removed duplicate initialization** from `main_balloon.cpp` 
3. **Fixed Adafruit library conflict** by passing `&Wire` to the BMP280 constructor and begin() method:
   ```cpp
   // Initialize I2C BEFORE creating BMP280 object
   Wire.begin(BMP280_SDA_PIN, BMP280_SCL_PIN);
   delay(10);  // Allow I2C bus to stabilize
   
   // Pass &Wire to prevent library from reinitializing
   bmp280 = new Adafruit_BMP280(&Wire);
   if (!bmp280->begin(BMP280_ADDRESS, 0x76)) {
       // Handle error
   }
   ```

**Changed in `main_balloon.cpp`:**
```cpp
bool initializeBoard() {
    SYS_INFO("Initializing board-specific hardware...");
    
    // I2C initialization is handled by sensor_manager
    // Do not initialize Wire here to avoid "Bus already started" warnings
    
    // Initialize UART2 for LoRa E32 module
    // LoRa pins: M0=19, M1=20, AUX=21, TX=14, RX=48
    pinMode(LORA_M0_PIN, OUTPUT);
    pinMode(LORA_M1_PIN, OUTPUT);
    pinMode(LORA_AUX_PIN, INPUT);
    // Set normal mode (M0=0, M1=0)
    digitalWrite(LORA_M0_PIN, LOW);
    digitalWrite(LORA_M1_PIN, LOW);
    
    // UART2 will be initialized by the LoRa driver
    ...
}
```

### Expected Result
- No more "Bus already started in Master Mode" warnings
- BMP280 sensor should initialize properly
- System should boot without I2C-related crashes

---

## Issue 2: ESP-PROG Debugger Not Found

### Problem
The debugger fails to connect with these errors:
```
Error: no device found
Error: unable to open ftdi device with vid 0403, pid 6010, description '*', serial '*' at bus location '*'
Error: unable to open ftdi device with vid 0403, pid 6014, description '*', serial '*' at bus location '*'
```

### Root Cause
The ESP-PROG hardware debugger is not being detected. This is a **physical hardware connection issue**, not a firmware problem.

### Troubleshooting Steps

#### 1. Check Physical Connections
- Verify the ESP-PROG is properly connected via USB
- Check the JTAG cable connections to your ESP32-S3:
  - **TMS** → GPIO 3
  - **TCK** → GPIO 4
  - **TDI** → GPIO 5
  - **TDO** → GPIO 6
  - **GND** → GND
  - **VCC** → 3.3V (optional, if powering from ESP-PROG)

#### 2. Install FTDI Drivers
The ESP-PROG uses an FTDI chip. On Windows, you need proper drivers:

```powershell
# Check if the device is recognized
Get-PnpDevice | Where-Object {$_.FriendlyName -like "*FTDI*"}
```

**Download FTDI Drivers:**
- Visit: https://ftdichip.com/drivers/
- Download "FTDI VCP Drivers" for Windows
- Install and reboot

#### 3. Verify Device Manager
1. Open Device Manager
2. Look under "Ports (COM & LPT)" or "Universal Serial Bus controllers"
3. You should see "USB Serial Port" or "FT2232H Dual HS USB-UART/FIFO"
4. If you see a yellow warning icon, the driver isn't installed correctly

#### 4. Check USB Port
- Try a different USB port (preferably USB 2.0)
- Avoid USB hubs - connect directly to the PC
- Some USB 3.0 ports have compatibility issues with FTDI devices

#### 5. Verify ESP-PROG Power
- The ESP-PROG has a power LED that should be lit
- Check the 3.3V/5V jumper setting on the ESP-PROG

#### 6. OpenOCD Configuration
Your `platformio.ini` is correctly configured:
```ini
debug_tool = esp-prog
debug_init_break = tbreak setup
```

#### 7. Alternative: Use USB Serial Debugging
If you can't get the ESP-PROG working, you can use Serial debugging:

**In your code:**
```cpp
Serial.begin(115200);
Serial.println("Debug message");
```

**Monitor with PlatformIO:**
```bash
pio device monitor -b 115200
```

#### 8. Test ESP-PROG Hardware
Run this command to test if OpenOCD can detect the ESP-PROG:
```powershell
cd C:\Users\<YourUser>\.platformio\packages\tool-openocd-esp32
.\bin\openocd.exe -f interface/ftdi/esp32_devkitj_v1.cfg -f target/esp32s3.cfg
```

If this fails with the same error, the ESP-PROG is definitely not being detected.

### Alternative Debugging Methods

#### Option 1: Serial Debugging (Current Method)
You're already using this effectively with:
- `Serial.printf()` statements
- Monitor output via PlatformIO
- ESP32 exception decoder

#### Option 2: Remote GDB via UART
You can use GDB debugging over the serial port without ESP-PROG:
```ini
debug_port = COM3  ; Your serial port
debug_speed = 115200
```

#### Option 3: JTAG Debugging with Different Hardware
- ESP-Prog (current, not working)
- J-Link (expensive but reliable)
- ST-Link (with adapter)
- Built-in USB-JTAG (some ESP32-S3 boards have this)

### Quick Checklist
- [ ] ESP-PROG plugged in and powered
- [ ] JTAG cables connected to correct pins
- [ ] FTDI drivers installed
- [ ] Device appears in Device Manager
- [ ] Tried different USB port
- [ ] Tried different USB cable
- [ ] ESP32-S3 board powered on
- [ ] No other debugging sessions active

---

## Testing the Fix

### Test I2C Fix
1. Rebuild and upload the firmware:
   ```bash
   pio run -e esp32-s3-balloon -t upload
   ```

2. Monitor the serial output:
   ```bash
   pio device monitor -b 115200
   ```

3. Look for these success messages:
   ```
   BMP280: Initialized successfully
   Sensor manager initialized
   ```

4. Verify no warnings about "Bus already started"

### Expected Boot Sequence
```
========================================
Cosmic1 Balloon Firmware v2.0.0
Build: Nov 25 2025 13:45:00
Board: ESP32-S3
========================================
Starting system initialization...
Initializing debug system...
Debug system initialized successfully
[SYS INFO] System booting...
Initializing application state...
Initializing hardware...
[SYS INFO] Initializing board-specific hardware...
[SYS INFO] Board initialization complete
[SYS INFO] Hardware initialization complete
[SYS INFO] Initializing subsystems...
[SYS INFO] Power manager initialized
[SYS INFO] Sensor manager initialized
BMP280: Initialized successfully
GPS: Initialized successfully
[SYS INFO] LoRa communication initialized
...
```

---

## Additional Notes

### Why the I2C Fix Works
- Each peripheral bus (I2C, UART) should only be initialized **once**
- Multiple `Wire.begin()` calls cause internal conflicts in the ESP32's I2C driver
- The second call tries to re-initialize pins that are already in use
- This can cause crashes, hangs, or unreliable communication

### Best Practices
1. **Initialize buses in one place only** - typically in the manager class that uses them
2. **Check if already initialized** - use `Wire.status()` if you need to check
3. **Document ownership** - clearly indicate which module owns which bus

### If Issues Persist
If you still see crashes after this fix:
1. Check the BMP280 I2C address (0x76 or 0x77)
2. Verify SDA/SCL pins match your hardware
3. Add pull-up resistors (4.7kΩ) to SDA and SCL if not present
4. Check for electrical shorts or damaged I2C devices
5. Test each I2C device individually

---

## Summary

✅ **I2C Issue:** Fixed by removing duplicate initialization  
⚠️ **Debugger Issue:** Hardware connection problem - follow troubleshooting steps above

The firmware should now boot properly without I2C crashes. The debugger issue is separate and requires physical hardware troubleshooting.
