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
    if (!power_led_enabled) {
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
    }

    // Process LoRa packets
    processLoRaPacket();

    // Process HomeSpan (only if started)
    if (homekit_started) {
        homeSpan.poll();
    }

    // Handle web requests
    webServer.handleClient();

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

    // Enforce LED off state when LEDs are disabled
    // This overrides HomeSpan's status LED control (runs every loop to override HomeSpan)
    static unsigned long lastDebug = 0;
    if (!power_led_enabled || !activity_led_enabled) {
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
