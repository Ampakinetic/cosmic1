# WiFi Connection Troubleshooting Guide

## Problem: "_onStaArduinoEvent(): Reason: 201 - NO_AP_FOUND"


## 1. Verify Network Availability

### Check WiFi Network Name
- Confirm "wynoffice" is the exact SSID (case-sensitive)
- Check if the network is broadcasting
- Try connecting another device to verify the network exists

### Use Network Scanner
Run the updated code - it will automatically scan for available networks after 10 seconds of connection attempts. Look for:
- Your network name in the scan results
- Signal strength (RSSI) - should be > -80dBm for reliable connection

## 2. Check WiFi Credentials

### Common Issues
- **Special Characters**: Avoid spaces or special characters in SSID/password
- **Network Security**: Ensure you're using WPA2/WPA3 if required

### Test with Different Network
Try temporarily changing to a known working network in `include/wifi_config.h`:

```cpp
const char *WIFI_SSID = "your_phone_hotspot";
const char *WIFI_PASSWORD = "your_password";
```

## 3. Hardware and Environment Issues

### ESP32-S3 Specific
- **USB Connection**: Ensure USB-C cable supports data transfer (not just charging)
- **Power Supply**: ESP32-S3 needs stable 3.3V power during WiFi scan
- **Antenna**: Check if antenna is properly connected (if external antenna used)
- **Range**: ESP32-S3 has similar range to ESP32, ensure you're within coverage

### Router Settings
- **2.4GHz Band**: ESP32-S3 only supports 2.4GHz WiFi
- **Channel**: Try channels 1, 6, or 11 (less interference)
- **Broadcast**: Ensure SSID broadcast is enabled on router
- **Hidden Network**: ESP32-S3 may have issues with hidden networks

## 4. Advanced Debugging

### Enable Debug Output
The updated code provides detailed debugging:
1. **Network Scan**: Shows all available networks with signal strength
2. **Connection Status**: Real-time WiFi status updates
3. **Fallback Mode**: Creates AP if connection fails

### Monitor Serial Output
```bash
pio device monitor
```

Expected output:
```
=== ESP32-S3 Camera Starting ===
Camera Model: ESP32S3_EYE
WiFi SSID: wynoffice
Board: ESP32-S3 DevKitC-1
=================================
=== Starting WiFi Connection ===
Attempting to connect to: wynoffice
WiFi connecting.....
WiFi Status: 201
Scanning for available networks...
Found 3 networks:
1: wynoffice (RSSI: -65)
2: neighbor_wifi (RSSI: -72)
3: guest_network (RSSI: -78)
WiFi connecting..........
WiFi connected successfully!
IP address: 192.168.1.100
Signal strength (RSSI): -65 dBm
Camera Ready! Use 'http://192.168.1.100' to connect
```

## 5. Fallback Options

### Access Point Mode
If WiFi connection fails, ESP32-S3 automatically creates an AP:
- **Network**: "ESP32-Camera-Setup"
- **Password**: "12345678"
- **IP**: 192.168.4.1

Connect to this AP to:
1. Check if your original network appears in scan
2. Test different credentials
3. Verify hardware functionality

## 6. Quick Solutions

### Try These First
1. **Restart Router**: Sometimes fixes broadcast issues
2. **Move Closer**: Test within 1-2 meters of router
3. **Remove Security**: Temporarily disable WPA2 for testing
4. **Different Channel**: Change router to channel 6

### ESP32-S3 Reset
If all else fails:
1. Hold BOOT button
2. Press RESET button
3. Release BOOT button
4. Wait for factory reset

## 7. Next Steps

### When Connected
Once WiFi connects successfully:
1. **Test Camera**: Access via browser using IP address
2. **Check Stream**: Verify video streaming works
3. **Save Settings**: Working credentials are saved in flash

### If Still Failing
1. **Try Different ESP32-S3 Board**: Hardware issue possible
2. **Test with ESP32**: Compare performance
3. **Check Environment**: Interference from other devices

## Support

### Common Solutions
- 70% of WiFi issues are incorrect SSID/password
- 20% are network range/interference problems
- 10% are hardware/power issues

### Get Help
- Check router logs for connection attempts
- Use WiFi analyzer app to verify network
- Test with known working WiFi network first

Remember: The ESP32-S3 debugging will show you exactly what networks are available and help identify the issue!
