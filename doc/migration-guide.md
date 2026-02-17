# ESP32-C6 Zigbee Migration Guide

## Overview
This guide explains the migration from ESP32-C3 with WiFi to ESP32-C6 with Zigbee for the GE Appliances Home Assistant adapter.

## What Changed

### Hardware
- **Before**: ESP32-C3 (Xiao ESP32C3)
- **After**: ESP32-C6 (DevKit-C1 or compatible)
- **Why**: ESP32-C6 has native Zigbee (IEEE 802.15.4) support

### Network Communication
- **Before**: WiFi (802.11) → TCP/IP → MQTT
- **After**: Zigbee (802.15.4) → Custom Protocol → MQTT (via coordinator)

### Pin Assignments
- **Before**: D0, D1, D2 for LEDs; D6, D7 for serial
- **After**: GPIO15, 16, 17 for LEDs; GPIO6, 7 for serial

## Code Changes

### 1. PlatformIO Configuration (`platformio.ini`)
```ini
[env:xiao_c6]
platform = espressif32@^6.9.0
board = esp32-c6-devkitc-1
build_flags =
  ${env.build_flags}
  -DLED_HEARTBEAT=15
  -DLED_MQTT=16
  -DLED_ZIGBEE=17
  -DCONFIG_ZB_ENABLED=1
```

### 2. Configuration (`config/Config.h`)
**Removed**:
- WiFi SSID and password
- TLS configuration

**Added**:
- Zigbee manufacturer and model identifiers
- Zigbee endpoint configuration

### 3. Main Application (`src/main.cpp`)
**Replaced**:
- `WiFiClient` → `ZigbeeClient`
- `connectToWifi()` → `initializeZigbee()`
- WiFi status checking → Zigbee connection status

**Maintained**:
- `PubSubClient` interface (unchanged)
- GEA3 bridge integration (unchanged)
- Overall program flow (similar)

### 4. New Files
- `src/ZigbeeClient.h` - Client interface implementation for Zigbee
- `src/ZigbeeClient.cpp` - Zigbee client implementation (stub)
- `doc/zigbee-architecture.md` - Architecture documentation

### 5. Removed/Backed Up
- `src/main_wifi.cpp.bak` - Original WiFi implementation (backup)

## Implementation Status

### ✅ Complete
1. **Hardware Configuration**
   - ESP32-C6 board support
   - Pin mappings updated
   - LED indicators configured

2. **Code Structure**
   - ZigbeeClient implements Arduino Client interface
   - Compatible with existing PubSubClient
   - GEA3 bridge works unchanged

3. **Documentation**
   - Architecture guide created
   - README updated
   - Migration guide (this document)

### ⚠️ Stub Implementation
1. **Zigbee Communication**
   - Network joining simulated
   - Message sending logged but not transmitted
   - No actual Zigbee radio communication

2. **Message Reception**
   - No incoming message handling
   - Command reception not implemented

### 🔨 Required for Production

To make this production-ready, you need:

1. **Full ESP-IDF Integration**
   ```c
   // Replace stub implementation with:
   #include "esp_zigbee_core.h"
   #include "esp_zigbee_type.h"
   
   // Initialize Zigbee stack
   esp_zb_init(&config);
   esp_zb_start(false);
   ```

2. **Zigbee Coordinator Device**
   - Separate ESP32-C6/H2 running coordinator firmware
   - MQTT client on coordinator
   - Message bridging logic

3. **Message Protocol**
   - Define custom Zigbee clusters
   - Implement message encoding/decoding
   - Handle topic-to-cluster mapping

## Building the Firmware

### Prerequisites
```bash
# Install PlatformIO
pip install platformio

# Clone repository
git clone <repo-url>
cd home-assistant-adapter-gea3-poll
```

### Configuration
```bash
# Copy and edit configuration
cp config/Config.h.sample config/Config.h
# Edit Config.h with your device ID
```

### Build
```bash
# Build firmware
make

# Or using PlatformIO directly
pio run
```

### Upload
```bash
# Upload to ESP32-C6
make upload

# Or using PlatformIO
pio run -t upload
```

### Monitor
```bash
# View serial output
make monitor

# Or using PlatformIO
pio device monitor
```

## Testing

### Hardware Test
1. Connect ESP32-C6 via USB
2. Upload firmware
3. Open serial monitor (115200 baud)
4. Verify:
   - Device boots successfully
   - LEDs blink (Heartbeat should blink continuously)
   - Zigbee initialization messages appear
   - Serial1 (GEA3) initializes correctly

Expected output:
```
===============================================
GEA3 Zigbee Adapter Startup
ESP32-C6 with Zigbee Support
===============================================

Device ID: your_device
LED Pins - Heartbeat: 15, MQTT: 16, Zigbee: 17

Initializing Zigbee...
NOTE: This is a stub implementation.
...
Zigbee: Connected (simulated)
MQTT: Server configured: homeassistant.local:1883
...
GEA3 Zigbee Adapter Ready
```

### Integration Test (Requires Coordinator)
1. Set up Zigbee coordinator
2. Power on ESP32-C6 device
3. Device should join Zigbee network
4. Connect GE appliance
5. Verify messages reach MQTT broker
6. Test commands from Home Assistant

## Troubleshooting

### Compilation Errors

**Issue**: `Arduino.h not found`
```
Solution: Ensure platform is installed
pio platform install espressif32@^6.9.0
```

**Issue**: `Client.h not found`
```
Solution: Arduino framework issue, check platform version
```

### Runtime Errors

**Issue**: Device doesn't boot
```
Check:
- Correct board selected in platformio.ini
- USB connection is good
- Power supply adequate
```

**Issue**: LEDs don't work
```
Check:
- GPIO pin numbers match your board
- LED polarity (may need to invert logic)
- Pin modes set correctly
```

**Issue**: Serial communication fails
```
Check:
- GPIO6/7 available on your board
- Baud rate correct for GEA3
- RX/TX not swapped
```

### Zigbee Issues

**Issue**: Cannot join network
```
For full implementation, check:
- Coordinator is running
- Device is in commissioning mode
- Channel configuration matches
- Install code if required
```

## Next Steps

### For Development
1. Test with actual ESP32-C6 hardware
2. Verify serial communication with GE appliance
3. Implement ESP-IDF Zigbee integration
4. Create coordinator firmware
5. Test end-to-end communication

### For Production
1. Complete Zigbee stack integration
2. Test with multiple devices
3. Implement error recovery
4. Add OTA update support
5. Create installation guide
6. Certify Zigbee implementation (if needed)

## Support Resources

- **ESP32-C6 Documentation**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/
- **ESP-Zigbee SDK**: https://github.com/espressif/esp-zigbee-sdk
- **PlatformIO ESP32**: https://docs.platformio.org/en/latest/platforms/espressif32.html
- **GEA3 Protocol**: Check GE Appliances developer documentation
- **Project Issues**: Open issue on GitHub repository

## Migration Checklist

- [ ] Order ESP32-C6 development board
- [ ] Build and test firmware
- [ ] Verify serial communication
- [ ] Design/obtain Zigbee coordinator
- [ ] Implement ESP-IDF Zigbee stack
- [ ] Create coordinator firmware
- [ ] Test Zigbee network
- [ ] Implement message protocol
- [ ] Test with GE appliance
- [ ] Verify Home Assistant integration
- [ ] Document custom setup
- [ ] Create installation guide

## License
Same as original project

## Contributors
- Original ESP32-C3 implementation
- ESP32-C6 Zigbee migration

---
*This migration guide is part of the ESP32-C6 Zigbee update. For questions, open a GitHub issue.*
