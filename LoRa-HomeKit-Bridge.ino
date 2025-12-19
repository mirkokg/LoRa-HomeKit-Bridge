/*
 * LoRa HomeKit Bridge - Arduino Version (Refactored)
 *
 * Uses HomeSpan library for Arduino-compatible HomeKit
 *
 * Hardware: TTGO LoRa32 V2.1_1.6
 *
 * Install these libraries via Arduino Library Manager:
 *   - HomeSpan by Gregg Silverstein
 *   - LoRa by Sandeep Mistry
 *   - ArduinoJson by Benoit Blanchon
 *   - PubSubClient by Nick O'Leary
 *   - ESP8266 and ESP32 OLED driver for SSD1306 displays by ThingPulse
 *   - QRCode by Richard Moore
 *
 * IMPORTANT: For ESP32, create empty file: Arduino/libraries/QRCode/src/QRCode_Library.h
 * This fixes a naming conflict with ESP32 SDK's built-in qrcode.h
 */

// ============== Include Modules ==============
#include "core/Config.h"
#include "core/Device.h"
#include "hardware/Display.h"
#include "data/Encryption.h"
#include "data/Settings.h"
#include "homekit/HomeKitServices.h"
#include "hardware/LoRaModule.h"
#include "network/WiFiModule.h"
#include "homekit/DeviceManagement.h"
#include "network/WebServerModule.h"
#include "network/MQTTModule.h"

// ============== Global Variables ==============
// Boot time tracking (only variable defined in main sketch)
unsigned long boot_time = 0;

// All other global variables are defined in their respective module .cpp files
// and declared as extern in their .h files

// ============== Setup ==============
void setup() {
    Serial.begin(115200);
    delay(100);

    boot_time = millis();

    Serial.println();
    Serial.println("========================================");
    Serial.println("  LoRa HomeKit Bridge (Arduino/HomeSpan)");
    Serial.println("  TTGO LoRa32 V2.1_1.6");
    Serial.println("========================================");
    Serial.println();

    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // LOW = OFF (LED is active-high on this board)

    // Initialize Display
    Serial.println("[BOOT] Init display...");
    displayInit();

    displayMessage("LoRa HomeKit", "Bridge v2.0", "", "Starting...");
    delay(2000);

    // Load settings from NVS
    Serial.println("[BOOT] Loading settings...");
    displayProgress("Settings", "Loading...", 50);
    loadSettings();
    displayProgress("Settings", "Loaded!", 100);

    // Apply hardware settings
    if (!activity_led_enabled) {
        digitalWrite(LED_PIN, LOW);  // Turn off LED
    }
    if (display_available) {
        display.setBrightness(oled_brightness);
        if (!oled_enabled) {
            display.displayOff();
        }
    }

    delay(500);

    // Initialize LoRa
    Serial.println("[BOOT] Initializing LoRa...");
    if (!initLoRa()) {
        Serial.println("[BOOT] LoRa failed - halting!");
        while(1) { delay(1000); }
    }

    // Try to connect to WiFi if configured
    bool wifi_ok = false;
    if (wifi_configured) {
        Serial.println("[BOOT] WiFi configured, connecting...");
        wifi_ok = connectWiFi();
    } else {
        Serial.println("[BOOT] No WiFi configured");
    }

    // Start AP mode if WiFi not connected
    if (!wifi_ok) {
        Serial.println("[BOOT] Starting AP mode for setup...");
        startAPMode();
    }

    // Setup HomeKit only if WiFi is connected
    if (wifi_ok) {
        Serial.println("[BOOT] Setting up HomeKit...");
        setupHomeKit();
    }

    // Start Web Server on port 80 (HomeSpan uses port 80 for HAP)
    Serial.println("[BOOT] Starting web server on port 80...");
    displayProgress("Web Server", "Starting...", 0);
    setupWebServer();
    displayProgress("Web Server", "Ready!", 100);

    // Initialize MQTT if enabled
    if (mqtt_enabled && wifi_ok) {
        Serial.println("[BOOT] Initializing MQTT...");
        initMQTT();
        connectMQTT();
    }

    IPAddress ip = ap_mode ? WiFi.softAPIP() : WiFi.localIP();
    Serial.printf("[BOOT] Web UI: http://%s/\n", ip.toString().c_str());

    delay(500);

    // Show final status
    displayStatus();

    Serial.println();
    Serial.println("========================================");
    if (ap_mode) {
        Serial.println("  SETUP MODE");
        Serial.print("  WiFi: "); Serial.println(AP_SSID);
        Serial.print("  Pass: "); Serial.println(AP_PASSWORD);
        Serial.print("  URL:  http://"); Serial.println(WiFi.softAPIP().toString());
    } else {
        Serial.println("  READY - Waiting for LoRa packets");
        Serial.print("  IP:   "); Serial.println(WiFi.localIP().toString());
        Serial.println("  Code: 111-22-333");
    }
    Serial.println("========================================");
    Serial.println();
}

// ============== Main Loop ==============
void loop() {
    // Handle DNS for captive portal in AP mode
    if (ap_mode) {
        dnsServer.processNextRequest();

        // Try to reconnect to WiFi if configured
        if (wifi_configured && attemptWiFiReconnect()) {
            // WiFi reconnected successfully!
            Serial.println("[WIFI] WiFi reconnected, initializing HomeKit and MQTT...");

            // Setup HomeKit
            if (!homekit_started) {
                Serial.println("[BOOT] Setting up HomeKit...");
                setupHomeKit();
            }

            // Initialize MQTT if enabled
            if (mqtt_enabled) {
                Serial.println("[BOOT] Initializing MQTT...");
                initMQTT();
                connectMQTT();
            }

            Serial.printf("[WIFI] Ready! IP: %s\n", WiFi.localIP().toString().c_str());
        }
    }

    // Process LoRa packets
    processLoRaPacket();

    // Process HomeSpan (only if started)
    if (homekit_started) {
        homeSpan.poll();
    }

    // Handle web requests
    webServer.handleClient();

    // Handle MQTT (reconnect if needed)
    if (mqtt_enabled && !ap_mode) {
        loopMQTT();
    }

    // Check OLED timeout
    checkOledTimeout();

    // Update display periodically
    static unsigned long lastDisplayUpdate = 0;
    if (millis() - lastDisplayUpdate > 2000) {
        if (!oled_is_off) {
            displayStatus();
        }
        lastDisplayUpdate = millis();
    }

    // Publish MQTT diagnostics periodically (every 5 minutes)
    static unsigned long lastDiagnosticsPublish = 0;
    if (mqtt_enabled && !ap_mode && isMQTTConnected()) {
        if (millis() - lastDiagnosticsPublish > 300000) {
            publishBridgeDiagnostics();
            lastDiagnosticsPublish = millis();
        }
    }

    // Enforce LED off state when activity LED is disabled
    // Only enforce when activity LED is off - power LED just controls HomeSpan status
    static unsigned long lastDebug = 0;
    if (!activity_led_enabled) {
        digitalWrite(LED_PIN, LOW);  // Keep LED off

        // Debug output every 5 seconds
        if (millis() - lastDebug > 5000) {
            Serial.printf("[LOOP] LED enforcement active: power=%d, activity=%d\n",
                          power_led_enabled, activity_led_enabled);
            lastDebug = millis();
        }
    }

    // Small delay to prevent watchdog issues
    delay(1);
}
