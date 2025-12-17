# WebServerModule.cpp - Implementation Note

The `WebServerModule.cpp` file should contain all web server handler functions from the original `LoRa_HomeKit_Bridge.ino` file (approximately lines 1426-2209).

## Contents to Extract:

1. **CSS_STYLES** constant (lines ~1429-1461) - All the CSS styling for the web interface
2. **handleRoot()** function (lines ~1463-1900+) - Main web UI handler with full HTML
3. **handleSave()** function - Settings save handler
4. **handleReset()** function - Factory reset handler
5. **handleScan()** function - WiFi scan handler
6. **handleTestDevice()** function - Device test handler
7. **handleUnpair()** function - HomeKit unpair handler
8. **handleRenameDevice()** function - Device rename handler
9. **handleRemoveDevice()** function - Device removal handler
10. **handleRestart()** function - ESP32 restart handler
11. **handleSetSensorType()** function - Sensor type change handler
12. **handleHardwareSettings()** function - Hardware settings handler
13. **handleNotFound()** function - 404/captive portal redirect handler

## Implementation Structure:

```cpp
#include "WebServerModule.h"
#include <WiFi.h>
#include <HomeSpan.h>
#include <ArduinoJson.h>
#include "Settings.h"
#include "Device.h"
#include "DeviceManagement.h"
#include "Display.h"
#include "WiFiModule.h"
#include "LoRaModule.h"
#include "Encryption.h"

// Global object
WebServer webServer(8080);

// CSS styles constant
const char CSS_STYLES[] PROGMEM = R"rawliteral(
... [full CSS from original file]
)rawliteral";

// All handler functions...
void handleRoot() { ... }
void handleSave() { ... }
// etc...

void setupWebServer() {
    webServer.on("/", handleRoot);
    webServer.on("/save", HTTP_POST, handleSave);
    webServer.on("/reset", HTTP_POST, handleReset);
    webServer.on("/api/scan", handleScan);
    webServer.on("/api/test", handleTestDevice);
    webServer.on("/api/unpair", handleUnpair);
    webServer.on("/api/rename", handleRenameDevice);
    webServer.on("/api/remove", handleRemoveDevice);
    webServer.on("/api/restart", handleRestart);
    webServer.on("/api/settype", handleSetSensorType);
    webServer.on("/api/hardware", handleHardwareSettings);
    webServer.onNotFound(handleNotFound);
    webServer.begin();
}
```

Extract the full content from the original file to create the complete implementation.
