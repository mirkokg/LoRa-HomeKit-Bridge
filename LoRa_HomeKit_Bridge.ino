/*
 * LoRa HomeKit Bridge - Arduino Version
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

 #include <WiFi.h>
 #include <SPI.h>
 #include <LoRa.h>
 #include <Wire.h>
 #include <WebServer.h>
 #include <DNSServer.h>
 #include <Preferences.h>
 #include <ArduinoJson.h>
 #include <SSD1306Wire.h>
// QRCode library by Richard Moore - workaround for ESP32 SDK naming conflict
// Create empty file: Arduino/libraries/QRCode/src/QRCode_Library.h
 #include <QRCode_Library.h>
 #include <qrcode.h>
 #include <HomeSpan.h>
 #include <mbedtls/aes.h>  // For ESP-NOW style AES encryption
 
 // ============== Board Configuration (TTGO LoRa32 V2.1_1.6) ==============
 // LoRa SPI pins
 #define LORA_SCK     5
 #define LORA_MISO    19
 #define LORA_MOSI    27
 #define LORA_CS      18
 #define LORA_RST     23
 #define LORA_DIO0    26
 
 // OLED pins - V1.6/V2.1 has NO reset pin (use -1)
 #define OLED_SDA     21
 #define OLED_SCL     22
 #define OLED_RST     -1    // No reset pin on V1.6/V2.1!
 #define OLED_ADDR    0x3C
 
 #define LED_PIN      25
 #define BUTTON_PIN   0
 
 // ============== Configuration ==============
 #define MAX_DEVICES         20
 #define NVS_NAMESPACE       "lora_hk"
 #define DEVICE_TIMEOUT_MS   (60 * 60 * 1000)
 
 #define AP_SSID             "LoRa-Bridge-Setup"
 #define AP_PASSWORD         "12345678"
 #define DNS_PORT            53
 
 // Default settings
 #define DEFAULT_WIFI_SSID       ""
 #define DEFAULT_WIFI_PASSWORD   ""
 #define DEFAULT_LORA_FREQUENCY  868.0
 #define DEFAULT_GATEWAY_KEY     "xy"
 #define DEFAULT_ENCRYPT_KEY     { 0x4B, 0xA3, 0x3F, 0x9C }
 #define ENCRYPT_KEY_LEN         4
 
 // Default LoRa radio settings (must match your sensors!)
 #define DEFAULT_LORA_SF         8       // Spreading Factor 6-12
 #define DEFAULT_LORA_BW         125000  // Bandwidth in Hz (125000, 250000, 500000)
 #define DEFAULT_LORA_CR         5       // Coding Rate 5-8 (4/5 to 4/8)
 #define DEFAULT_LORA_PREAMBLE   6       // Preamble length 6-65535
 #define DEFAULT_LORA_SYNCWORD   0x12    // Sync word
 
 // Encryption modes
 enum EncryptionMode : uint8_t {
     ENCRYPT_NONE = 0,
     ENCRYPT_XOR = 1,
     ENCRYPT_AES = 2   // ESP-NOW style AES
 };
 #define DEFAULT_ENCRYPTION_MODE ENCRYPT_XOR
 
 // HomeKit pairing settings
 #define HOMEKIT_SETUP_ID        "LRHK"          // 4 character setup ID
 
 // ============== Global Objects ==============
 SSD1306Wire display(OLED_ADDR, OLED_SDA, OLED_SCL);
 WebServer webServer(8080);  // Use port 8080 - HomeSpan uses port 80 for HAP!
 DNSServer dnsServer;
 Preferences prefs;
 
 // Settings
 char wifi_ssid[64] = DEFAULT_WIFI_SSID;
 char wifi_password[64] = DEFAULT_WIFI_PASSWORD;
 float lora_frequency = DEFAULT_LORA_FREQUENCY;
 char gateway_key[32] = DEFAULT_GATEWAY_KEY;
 uint8_t encryption_mode = DEFAULT_ENCRYPTION_MODE;
 uint8_t encrypt_key[16] = DEFAULT_ENCRYPT_KEY;
 uint8_t encrypt_key_len = ENCRYPT_KEY_LEN;
 
 // LoRa radio settings
 uint8_t lora_sf = DEFAULT_LORA_SF;
 uint32_t lora_bw = DEFAULT_LORA_BW;
 uint8_t lora_cr = DEFAULT_LORA_CR;
 uint16_t lora_preamble = DEFAULT_LORA_PREAMBLE;
 uint8_t lora_syncword = DEFAULT_LORA_SYNCWORD;
 
 // Hardware settings
 bool power_led_enabled = true;
 bool activity_led_enabled = true;
 bool oled_enabled = true;
 uint8_t oled_brightness = 255;
 uint16_t oled_timeout = 60;  // seconds, 0 = always on
 unsigned long oled_last_activity = 0;
 bool oled_is_off = false;
 
 // HomeKit pairing code (generated on first boot)
 char homekit_code[9] = "";        // 8 digits + null
 char homekit_code_display[10] = ""; // XXXX-XXXX + null
 char homekit_qr_uri[25] = "";     // X-HM://XXXXXXXXXYYYY + null
 
 // Mode flags
 bool ap_mode = false;
 bool wifi_configured = false;
 bool homekit_started = false;
 
 // Statistics
 unsigned long boot_time = 0;
 uint32_t packets_received = 0;
 unsigned long last_packet_time = 0;
 String last_event = "";
 
 // ============== Device Structure ==============
 // Sensor type definitions
 enum ContactType : uint8_t {
     CONTACT_TYPE_CONTACT = 0,    // Default contact sensor
     CONTACT_TYPE_LEAK = 1,       // Water leak sensor (critical alerts!)
     CONTACT_TYPE_SMOKE = 2,      // Smoke sensor (critical alerts!)
     CONTACT_TYPE_CO = 3,         // Carbon monoxide sensor (critical alerts!)
     CONTACT_TYPE_OCCUPANCY = 4   // Occupancy sensor
 };
 
 enum MotionType : uint8_t {
     MOTION_TYPE_MOTION = 0,      // Default motion sensor
     MOTION_TYPE_OCCUPANCY = 1,   // Occupancy sensor
     MOTION_TYPE_LEAK = 2,        // Water leak sensor (critical alerts!)
     MOTION_TYPE_SMOKE = 3,       // Smoke sensor (critical alerts!)
     MOTION_TYPE_CO = 4           // Carbon monoxide sensor (critical alerts!)
 };
 
 struct Device {
     char id[32];           // Original device ID from LoRa
     char name[32];         // Custom display name (can be renamed)
     bool active;
     int rssi;
     unsigned long last_seen;
     
     bool has_temp;
     bool has_hum;
     bool has_batt;
     bool has_light;
     bool has_motion;
     bool has_contact;
     
     // Sensor type overrides (for HomeKit display)
     uint8_t contact_type;  // ContactType enum
     uint8_t motion_type;   // MotionType enum
     
     float temperature;
     float humidity;
     int battery;
     int lux;
     bool motion;
     bool contact;
     
     // HomeSpan service pointers (not persisted)
     uint32_t aid;          // HomeKit Accessory ID for dynamic removal
     SpanCharacteristic* tempChar;
     SpanCharacteristic* humChar;
     SpanCharacteristic* battChar;
     SpanCharacteristic* lightChar;
     SpanCharacteristic* motionChar;
     SpanCharacteristic* contactChar;
     SpanCharacteristic* nameChar;  // For updating name in HomeKit
 };
 
 Device devices[MAX_DEVICES];
 int device_count = 0;
 
 // Helper function to count only active devices
 int getActiveDeviceCount() {
     int count = 0;
     for (int i = 0; i < device_count; i++) {
         if (devices[i].active) count++;
     }
     return count;
 }
 
 // Helper to get contact sensor type name
 const char* getContactTypeName(uint8_t type) {
     switch (type) {
         case CONTACT_TYPE_LEAK: return "Leak";
         case CONTACT_TYPE_SMOKE: return "Smoke";
         case CONTACT_TYPE_CO: return "CO";
         case CONTACT_TYPE_OCCUPANCY: return "Occupancy";
         default: return "Contact";
     }
 }
 
 // Helper to get motion sensor type name
 const char* getMotionTypeName(uint8_t type) {
     switch (type) {
         case MOTION_TYPE_OCCUPANCY: return "Occupancy";
         case MOTION_TYPE_LEAK: return "Leak";
         case MOTION_TYPE_SMOKE: return "Smoke";
         case MOTION_TYPE_CO: return "CO";
         default: return "Motion";
     }
 }
 
 // Forward declarations
 void saveDevices();
 void loadDevices();
 void wakeOled();
 
 // ============== Display Functions ==============
 bool display_available = false;
 
 void feedWatchdog() {
     // Use vTaskDelay which properly yields to FreeRTOS and feeds watchdog
     vTaskDelay(1);
 }
 
 void displayInit() {
     Serial.println("[DISPLAY] Init OLED...");
     Serial.flush();
     
     // Simply init the display - the library handles I2C internally
     // V1.6/V2.1 has no reset pin, so no manual reset needed
     display.init();
     display.flipScreenVertically();
     display.setFont(ArialMT_Plain_10);
     display.setTextAlignment(TEXT_ALIGN_LEFT);
     display.clear();
     display.display();
     display_available = true;
     
     Serial.println("[DISPLAY] OLED ready!");
     Serial.flush();
 }
 
 void displayMessage(const char* line1, const char* line2 = "", const char* line3 = "", const char* line4 = "") {
     // Always log to serial
     Serial.printf("[MSG] %s | %s | %s | %s\n", line1, line2, line3, line4);
     
     if (!display_available || !oled_enabled) return;
     wakeOled();
     
     display.clear();
     display.setFont(ArialMT_Plain_10);
     
     if (strlen(line1) > 0) display.drawString(0, 0, line1);
     if (strlen(line2) > 0) display.drawString(0, 16, line2);
     if (strlen(line3) > 0) display.drawString(0, 32, line3);
     if (strlen(line4) > 0) display.drawString(0, 48, line4);
     
     display.display();
 }
 
 void displayProgress(const char* title, const char* status, int percent = -1) {
     Serial.printf("[PROGRESS] %s: %s (%d%%)\n", title, status, percent);
     
     if (!display_available || !oled_enabled) return;
     wakeOled();
     
     display.clear();
     
     // Title
     display.setFont(ArialMT_Plain_16);
     display.drawString(0, 0, title);
     
     // Status text
     display.setFont(ArialMT_Plain_10);
     display.drawString(0, 24, status);
     
     // Progress bar if percent given
     if (percent >= 0) {
         display.drawRect(0, 50, 128, 10);
         display.fillRect(2, 52, (124 * percent) / 100, 6);
     }
     
     display.display();
 }
 
// Display HomeKit QR code on OLED when not paired
void displayPairingScreen() {
    if (!display_available || !oled_enabled) return;
    wakeOled();
    
    display.clear();
    
    // Generate QR code from HomeKit URI
    // Version 2 = 25x25 modules, fits nicely on 64px height with scale 2
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(2)];
    qrcode_initText(&qrcode, qrcodeData, 2, ECC_LOW, homekit_qr_uri);
    
    // QR code size: Version 2 = 25 modules
    int qrSize = qrcode.size;
    int scale = 2;  // 2x2 pixels per module = 50x50 pixels
    int qrPixelSize = qrSize * scale;
    
    // Position QR code on right side of display, vertically centered
    int qrX = 128 - qrPixelSize - 4;  // Right side with margin
    int qrY = (64 - qrPixelSize) / 2;  // Vertically centered
    
    // Draw white background for QR code (improves scanning)
    display.setColor(WHITE);
    display.fillRect(qrX - 2, qrY - 2, qrPixelSize + 4, qrPixelSize + 4);
    
    // Draw QR code modules
    display.setColor(BLACK);
    for (uint8_t y = 0; y < qrSize; y++) {
        for (uint8_t x = 0; x < qrSize; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                display.fillRect(qrX + x * scale, qrY + y * scale, scale, scale);
            }
        }
    }
    display.setColor(WHITE);
    
    // Draw text on left side
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "Scan to pair");
    
    // Draw pairing code (split into two lines)
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 14, String(homekit_code_display).substring(0, 4));
    display.drawString(0, 32, String(homekit_code_display).substring(5, 9));
    
    // Small hint at bottom
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 54, "or Home app");
    
    display.display();
}

void wakeOled() {
    if (!display_available) return;
    oled_last_activity = millis();
    if (oled_is_off && oled_enabled) {
        display.displayOn();
        oled_is_off = false;
    }
}

void checkOledTimeout() {
    if (!display_available || !oled_enabled || oled_timeout == 0) return;
    
    if (!oled_is_off && (millis() - oled_last_activity > oled_timeout * 1000)) {
        display.displayOff();
        oled_is_off = true;
    }
}

void displayStatus() {
    if (!display_available || !oled_enabled) return;
    
    // Check if we should show pairing screen (WiFi connected but not paired)
    if (!ap_mode && homekit_started && WiFi.status() == WL_CONNECTED) {
        bool isPaired = homeSpan.controllerListBegin() != homeSpan.controllerListEnd();
        if (!isPaired) {
            // Show pairing screen with code
            displayPairingScreen();
            return;
        }
    }
    
    display.clear();
    display.setFont(ArialMT_Plain_10);
    
    // Header with mode indicator
    String header = ap_mode ? ">>> SETUP MODE <<<" : "LoRa HomeKit Bridge";
    display.drawString(0, 0, header);
    display.drawLine(0, 12, 128, 12);
    
    if (ap_mode) {
        // AP Mode display
        display.drawString(0, 16, "WiFi: " + String(AP_SSID));
        display.drawString(0, 28, "Pass: " + String(AP_PASSWORD));
        display.drawString(0, 40, "IP: " + WiFi.softAPIP().toString() + ":8080");
        display.drawString(0, 52, "Open browser to setup");
    } else {
        // Normal operation display
        if (WiFi.status() == WL_CONNECTED) {
            display.drawString(0, 16, WiFi.localIP().toString() + ":8080");
        } else {
            display.drawString(0, 16, "WiFi: Reconnecting...");
        }
        
        display.drawString(0, 28, "LoRa: " + String(lora_frequency, 1) + " MHz");
        display.drawString(0, 40, "Dev:" + String(device_count) + " Pkt:" + String(packets_received));
        
        // Show last event or pairing code
        if (last_event.length() > 0 && millis() - last_packet_time < 5000) {
            display.drawString(0, 52, last_event.substring(0, 21));
        } else {
            display.drawString(0, 52, "HK: " + String(homekit_code_display));
        }
    }
    
    display.display();
}
 
 // ============== HomeSpan Accessory Definitions ==============
 
 // Temperature Sensor Service
 struct TempSensor : Service::TemperatureSensor {
     SpanCharacteristic* temp;
     Device* dev;
     
     TempSensor(Device* d) : Service::TemperatureSensor() {
         dev = d;
         temp = new Characteristic::CurrentTemperature(dev->temperature);
         temp->setRange(-40, 125);
         dev->tempChar = temp;
         // Add ConfiguredName for dynamic renaming (if not already set on another service)
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_temp && temp->timeVal() > 5000) {
             temp->setVal(dev->temperature);
         }
     }
 };
 
 // Humidity Sensor Service
 struct HumSensor : Service::HumiditySensor {
     SpanCharacteristic* hum;
     Device* dev;
     
     HumSensor(Device* d) : Service::HumiditySensor() {
         dev = d;
         hum = new Characteristic::CurrentRelativeHumidity(dev->humidity);
         dev->humChar = hum;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_hum && hum->timeVal() > 5000) {
             hum->setVal(dev->humidity);
         }
     }
 };
 
 // Battery Service
 struct BatteryService : Service::BatteryService {
     SpanCharacteristic* level;
     SpanCharacteristic* status;
     Device* dev;
     
     BatteryService(Device* d) : Service::BatteryService() {
         dev = d;
         level = new Characteristic::BatteryLevel(dev->battery);
         status = new Characteristic::StatusLowBattery(dev->battery < 20 ? 1 : 0);
         dev->battChar = level;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_batt && level->timeVal() > 5000) {
             level->setVal(dev->battery);
             status->setVal(dev->battery < 20 ? 1 : 0);
         }
     }
 };
 
 // Light Sensor Service
 struct LightSensor : Service::LightSensor {
     SpanCharacteristic* lux;
     Device* dev;
     
     LightSensor(Device* d) : Service::LightSensor() {
         dev = d;
         lux = new Characteristic::CurrentAmbientLightLevel(max(0.0001f, (float)dev->lux));
         dev->lightChar = lux;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_light && lux->timeVal() > 5000) {
             lux->setVal(max(0.0001f, (float)dev->lux));
         }
     }
 };
 
 // Motion Sensor Service (supports Motion or Occupancy type)
 struct MotionSensorService : Service::MotionSensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     MotionSensorService(Device* d) : Service::MotionSensor() {
         dev = d;
         sensor = new Characteristic::MotionDetected(dev->motion);
         dev->motionChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_motion && sensor->timeVal() > 1000) {
             sensor->setVal(dev->motion);
         }
     }
 };
 
 // Occupancy Sensor Service (for motion->occupancy type)
 struct OccupancySensorMotion : Service::OccupancySensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     OccupancySensorMotion(Device* d) : Service::OccupancySensor() {
         dev = d;
         sensor = new Characteristic::OccupancyDetected(dev->motion ? 1 : 0);
         dev->motionChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_motion && sensor->timeVal() > 1000) {
             sensor->setVal(dev->motion ? 1 : 0);
         }
     }
 };
 
 // Leak Sensor Service for motion (critical alerts!)
 struct LeakSensorMotion : Service::LeakSensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     LeakSensorMotion(Device* d) : Service::LeakSensor() {
         dev = d;
         sensor = new Characteristic::LeakDetected(dev->motion ? 1 : 0);
         dev->motionChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_motion && sensor->timeVal() > 1000) {
             sensor->setVal(dev->motion ? 1 : 0);
         }
     }
 };
 
 // Smoke Sensor Service for motion (critical alerts!)
 struct SmokeSensorMotion : Service::SmokeSensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     SmokeSensorMotion(Device* d) : Service::SmokeSensor() {
         dev = d;
         sensor = new Characteristic::SmokeDetected(dev->motion ? 1 : 0);
         dev->motionChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_motion && sensor->timeVal() > 1000) {
             sensor->setVal(dev->motion ? 1 : 0);
         }
     }
 };
 
 // Carbon Monoxide Sensor Service for motion (critical alerts!)
 struct COSensorMotion : Service::CarbonMonoxideSensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     COSensorMotion(Device* d) : Service::CarbonMonoxideSensor() {
         dev = d;
         sensor = new Characteristic::CarbonMonoxideDetected(dev->motion ? 1 : 0);
         dev->motionChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_motion && sensor->timeVal() > 1000) {
             sensor->setVal(dev->motion ? 1 : 0);
         }
     }
 };
 
 // Contact Sensor Service (default)
 struct ContactSensorService : Service::ContactSensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     ContactSensorService(Device* d) : Service::ContactSensor() {
         dev = d;
         sensor = new Characteristic::ContactSensorState(dev->contact ? 0 : 1);
         dev->contactChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_contact && sensor->timeVal() > 1000) {
             sensor->setVal(dev->contact ? 0 : 1);
         }
     }
 };
 
 // Leak Sensor Service (critical alerts!)
 struct LeakSensorService : Service::LeakSensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     LeakSensorService(Device* d) : Service::LeakSensor() {
         dev = d;
         // Leak: contact=true (closed/wet) -> DETECTED(1), contact=false (open/dry) -> NOT_DETECTED(0)
         sensor = new Characteristic::LeakDetected(dev->contact ? 1 : 0);
         dev->contactChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_contact && sensor->timeVal() > 1000) {
             sensor->setVal(dev->contact ? 1 : 0);
         }
     }
 };
 
 // Smoke Sensor Service (critical alerts!)
 struct SmokeSensorService : Service::SmokeSensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     SmokeSensorService(Device* d) : Service::SmokeSensor() {
         dev = d;
         sensor = new Characteristic::SmokeDetected(dev->contact ? 1 : 0);
         dev->contactChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_contact && sensor->timeVal() > 1000) {
             sensor->setVal(dev->contact ? 1 : 0);
         }
     }
 };
 
 // Carbon Monoxide Sensor Service (critical alerts!)
 struct COSensorService : Service::CarbonMonoxideSensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     COSensorService(Device* d) : Service::CarbonMonoxideSensor() {
         dev = d;
         sensor = new Characteristic::CarbonMonoxideDetected(dev->contact ? 1 : 0);
         dev->contactChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_contact && sensor->timeVal() > 1000) {
             sensor->setVal(dev->contact ? 1 : 0);
         }
     }
 };
 
 // Occupancy Sensor Service (for contact->occupancy type)
 struct OccupancySensorContact : Service::OccupancySensor {
     SpanCharacteristic* sensor;
     Device* dev;
     
     OccupancySensorContact(Device* d) : Service::OccupancySensor() {
         dev = d;
         sensor = new Characteristic::OccupancyDetected(dev->contact ? 1 : 0);
         dev->contactChar = sensor;
         if (!dev->nameChar) {
             dev->nameChar = new Characteristic::ConfiguredName(dev->name);
         }
     }
     
     void loop() {
         if (dev->has_contact && sensor->timeVal() > 1000) {
             sensor->setVal(dev->contact ? 1 : 0);
         }
     }
 };
 
 // ============== Encryption Functions ==============
 
 // XOR encryption/decryption (symmetric)
 void xorBuffer(uint8_t* data, size_t len) {
     if (encrypt_key_len == 0) return;
     for (size_t i = 0; i < len; i++) {
         data[i] ^= encrypt_key[i % encrypt_key_len];
     }
 }
 
 // AES decryption (ESP-NOW style - ECB mode for simplicity)
 void aesDecrypt(uint8_t* data, size_t len) {
     if (encrypt_key_len == 0 || len == 0) return;
     
     // Prepare 16-byte key (pad or truncate)
     uint8_t aes_key[16] = {0};
     memcpy(aes_key, encrypt_key, min((int)encrypt_key_len, 16));
     
     mbedtls_aes_context aes;
     mbedtls_aes_init(&aes);
     mbedtls_aes_setkey_dec(&aes, aes_key, 128);
     
     // Decrypt in 16-byte blocks
     size_t blocks = len / 16;
     for (size_t i = 0; i < blocks; i++) {
         mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, data + (i * 16), data + (i * 16));
     }
     
     mbedtls_aes_free(&aes);
 }
 
 // Main decryption function - applies the selected encryption mode
 void decryptBuffer(uint8_t* data, size_t len) {
     switch (encryption_mode) {
         case ENCRYPT_XOR:
             xorBuffer(data, len);
             break;
         case ENCRYPT_AES:
             aesDecrypt(data, len);
             break;
         case ENCRYPT_NONE:
         default:
             // No decryption
             break;
     }
 }
 
 // Helper to get encryption mode name
 const char* getEncryptionModeName(uint8_t mode) {
     switch (mode) {
         case ENCRYPT_XOR: return "XOR";
         case ENCRYPT_AES: return "AES (ESP-NOW)";
         default: return "None";
     }
 }
 
 // ============== Settings Management ==============
 // Convert number to base36 string (padded to specified length)
 void toBase36(uint64_t num, char* out, int len) {
     const char* chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
     for (int i = len - 1; i >= 0; i--) {
         out[i] = chars[num % 36];
         num /= 36;
     }
     out[len] = '\0';
 }
 
 void generatePairingCode() {
     // Generate random 8-digit pairing code
     // HomeKit requires digits only, no leading zeros in first position
     randomSeed(esp_random());  // Use hardware RNG
     
     uint32_t code = random(10000000, 99999999);  // 8-digit number
     sprintf(homekit_code, "%08lu", code);
     sprintf(homekit_code_display, "%.4s-%.4s", homekit_code, homekit_code + 4);
     
     // Generate HomeKit QR URI
     // Format: X-HM://[base36 payload][setup_id]
     // Payload = (flags << 31) | (category << 27) | setup_code
     // flags=2 (IP), category=2 (Bridge)
     uint64_t payload = ((uint64_t)2 << 31) | ((uint64_t)2 << 27) | code;
     char base36[10];
     toBase36(payload, base36, 9);
     sprintf(homekit_qr_uri, "X-HM://%s%s", base36, HOMEKIT_SETUP_ID);
     
     Serial.printf("[HOMEKIT] Generated pairing code: %s, QR: %s\n", homekit_code_display, homekit_qr_uri);
 }
 
 void loadSettings() {
     prefs.begin(NVS_NAMESPACE, true);
     
     wifi_configured = prefs.isKey("wifi_ssid") && prefs.getString("wifi_ssid", "").length() > 0;
     
     if (wifi_configured) {
         prefs.getString("wifi_ssid", wifi_ssid, sizeof(wifi_ssid));
         prefs.getString("wifi_pass", wifi_password, sizeof(wifi_password));
     }
     
     lora_frequency = prefs.getFloat("lora_freq", DEFAULT_LORA_FREQUENCY);
     prefs.getString("gw_key", gateway_key, sizeof(gateway_key));
     encryption_mode = prefs.getUChar("enc_mode", DEFAULT_ENCRYPTION_MODE);
     encrypt_key_len = prefs.getUChar("enc_len", ENCRYPT_KEY_LEN);
     if (encrypt_key_len > 0) {
         prefs.getBytes("enc_key", encrypt_key, encrypt_key_len);
     }
     
     // LoRa radio settings
     lora_sf = prefs.getUChar("lora_sf", DEFAULT_LORA_SF);
     lora_bw = prefs.getUInt("lora_bw", DEFAULT_LORA_BW);
     lora_cr = prefs.getUChar("lora_cr", DEFAULT_LORA_CR);
     lora_preamble = prefs.getUShort("lora_pre", DEFAULT_LORA_PREAMBLE);
     lora_syncword = prefs.getUChar("lora_sync", DEFAULT_LORA_SYNCWORD);
     
     // Hardware settings
     power_led_enabled = prefs.getBool("pwr_led", true);
     activity_led_enabled = prefs.getBool("act_led", true);
     oled_enabled = prefs.getBool("oled_en", true);
     oled_brightness = prefs.getUChar("oled_br", 255);
     oled_timeout = prefs.getUShort("oled_to", 60);
     
     // HomeKit pairing code - generate if not exists
     if (prefs.isKey("hk_code")) {
         prefs.getString("hk_code", homekit_code, sizeof(homekit_code));
         sprintf(homekit_code_display, "%.4s-%.4s", homekit_code, homekit_code + 4);
         
         // Generate QR URI from loaded code
         uint32_t code = atol(homekit_code);
         uint64_t payload = ((uint64_t)2 << 31) | ((uint64_t)2 << 27) | code;
         char base36[10];
         toBase36(payload, base36, 9);
         sprintf(homekit_qr_uri, "X-HM://%s%s", base36, HOMEKIT_SETUP_ID);
     }
     
     prefs.end();
     
     // Generate pairing code on first boot
     if (strlen(homekit_code) != 8) {
         generatePairingCode();
         // Save immediately
         prefs.begin(NVS_NAMESPACE, false);
         prefs.putString("hk_code", homekit_code);
         prefs.end();
     }
     
     Serial.printf("[SETTINGS] Loaded - WiFi: %s, Freq: %.1f MHz, SF%d, BW:%dkHz, Code: %s\n", 
                   wifi_configured ? "YES" : "NO", lora_frequency, lora_sf, lora_bw/1000, homekit_code_display);
 }
 
 void saveSettings() {
     prefs.begin(NVS_NAMESPACE, false);
     prefs.putString("wifi_ssid", wifi_ssid);
     prefs.putString("wifi_pass", wifi_password);
     prefs.putFloat("lora_freq", lora_frequency);
     prefs.putString("gw_key", gateway_key);
     prefs.putUChar("enc_mode", encryption_mode);
     prefs.putUChar("enc_len", encrypt_key_len);
     prefs.putBytes("enc_key", encrypt_key, encrypt_key_len);
     // LoRa radio settings
     prefs.putUChar("lora_sf", lora_sf);
     prefs.putUInt("lora_bw", lora_bw);
     prefs.putUChar("lora_cr", lora_cr);
     prefs.putUShort("lora_pre", lora_preamble);
     prefs.putUChar("lora_sync", lora_syncword);
     // Hardware settings
     prefs.putBool("pwr_led", power_led_enabled);
     prefs.putBool("act_led", activity_led_enabled);
     prefs.putBool("oled_en", oled_enabled);
     prefs.putUChar("oled_br", oled_brightness);
     prefs.putUShort("oled_to", oled_timeout);
     // Pairing code
     prefs.putString("hk_code", homekit_code);
     prefs.end();
     Serial.println("[SETTINGS] Saved to NVS");
 }
 
 void clearSettings() {
     prefs.begin(NVS_NAMESPACE, false);
     prefs.clear();
     prefs.end();
     Serial.println("[SETTINGS] Cleared all settings");
 }
 
 // ============== Device Persistence ==============
 void saveDevices() {
     prefs.begin(NVS_NAMESPACE, false);
     
     // Clear old device data first to prevent stale entries from reappearing
     int oldCount = prefs.getInt("dev_count", 0);
     for (int i = 0; i < oldCount; i++) {
         String prefix = "dev" + String(i) + "_";
         prefs.remove((prefix + "id").c_str());
         prefs.remove((prefix + "name").c_str());
         prefs.remove((prefix + "temp").c_str());
         prefs.remove((prefix + "hum").c_str());
         prefs.remove((prefix + "batt").c_str());
         prefs.remove((prefix + "light").c_str());
         prefs.remove((prefix + "motion").c_str());
         prefs.remove((prefix + "contact").c_str());
         prefs.remove((prefix + "ctype").c_str());
         prefs.remove((prefix + "mtype").c_str());
     }
     
     // Save each active device with sequential indices
     int saveIndex = 0;
     for (int i = 0; i < device_count; i++) {
         if (!devices[i].active) continue;
         
         String prefix = "dev" + String(saveIndex) + "_";
         prefs.putString((prefix + "id").c_str(), devices[i].id);
         prefs.putString((prefix + "name").c_str(), devices[i].name);
         prefs.putBool((prefix + "temp").c_str(), devices[i].has_temp);
         prefs.putBool((prefix + "hum").c_str(), devices[i].has_hum);
         prefs.putBool((prefix + "batt").c_str(), devices[i].has_batt);
         prefs.putBool((prefix + "light").c_str(), devices[i].has_light);
         prefs.putBool((prefix + "motion").c_str(), devices[i].has_motion);
         prefs.putBool((prefix + "contact").c_str(), devices[i].has_contact);
         prefs.putUChar((prefix + "ctype").c_str(), devices[i].contact_type);
         prefs.putUChar((prefix + "mtype").c_str(), devices[i].motion_type);
         saveIndex++;
     }
     
     // Save count of active devices only
     prefs.putInt("dev_count", saveIndex);
     
     prefs.end();
     Serial.printf("[DEVICES] Saved %d devices to NVS\n", saveIndex);
 }
 
 void loadDevices() {
     prefs.begin(NVS_NAMESPACE, true);
     
     int saved_count = prefs.getInt("dev_count", 0);
     Serial.printf("[DEVICES] Loading %d devices from NVS\n", saved_count);
     
     for (int i = 0; i < saved_count && i < MAX_DEVICES; i++) {
         String prefix = "dev" + String(i) + "_";
         
         String id = prefs.getString((prefix + "id").c_str(), "");
         if (id.length() == 0) continue;
         
         Device* dev = &devices[device_count++];
         memset(dev, 0, sizeof(Device));
         
         strncpy(dev->id, id.c_str(), sizeof(dev->id) - 1);
         String name = prefs.getString((prefix + "name").c_str(), id);
         strncpy(dev->name, name.c_str(), sizeof(dev->name) - 1);
         
         dev->active = true;
         dev->has_temp = prefs.getBool((prefix + "temp").c_str(), false);
         dev->has_hum = prefs.getBool((prefix + "hum").c_str(), false);
         dev->has_batt = prefs.getBool((prefix + "batt").c_str(), false);
         dev->has_light = prefs.getBool((prefix + "light").c_str(), false);
         dev->has_motion = prefs.getBool((prefix + "motion").c_str(), false);
         dev->has_contact = prefs.getBool((prefix + "contact").c_str(), false);
         dev->contact_type = prefs.getUChar((prefix + "ctype").c_str(), 0);
         dev->motion_type = prefs.getUChar((prefix + "mtype").c_str(), 0);
         
         Serial.printf("[DEVICES] Loaded: %s (%s) ctype:%d mtype:%d\n", 
                       dev->id, dev->name, dev->contact_type, dev->motion_type);
     }
     
     prefs.end();
 }
 
 void createHomekitAccessory(Device* dev) {
     if (!homekit_started) return;
     
     Serial.printf("[HOMEKIT] Creating accessory for LoRa:%s as HomeKit:%s\n", dev->id, dev->name);
     
     SpanAccessory* acc = new SpanAccessory();
     dev->aid = acc->getAID();  // Store AID for later deletion
     dev->nameChar = nullptr;   // Will be set by first sensor service with ConfiguredName
     Serial.printf("[HOMEKIT] Assigned AID: %d\n", dev->aid);
     
     new Service::AccessoryInformation();
     new Characteristic::Identify();
     new Characteristic::Name(dev->name);           // HomeKit display name (changeable)
     new Characteristic::Manufacturer("LoRa Sensor");
     new Characteristic::Model("LoRa-v1");
     new Characteristic::SerialNumber(dev->name);   // HomeKit identifier (changeable)
     new Characteristic::FirmwareRevision("1.0");
     
     // Each sensor service will add ConfiguredName if dev->nameChar is null
     if (dev->has_temp) new TempSensor(dev);
     if (dev->has_hum) new HumSensor(dev);
     if (dev->has_batt) new BatteryService(dev);
     if (dev->has_light) new LightSensor(dev);
     
     // Motion sensor with type selection (Leak/Smoke/CO have critical alerts!)
     if (dev->has_motion) {
         Serial.printf("[HOMEKIT] Creating motion sensor type: %d (%s)\n", dev->motion_type, getMotionTypeName(dev->motion_type));
         switch (dev->motion_type) {
             case MOTION_TYPE_OCCUPANCY:
                 Serial.println("[HOMEKIT] -> OccupancySensor");
                 new OccupancySensorMotion(dev);
                 break;
             case MOTION_TYPE_LEAK:
                 Serial.println("[HOMEKIT] -> LeakSensor (critical!)");
                 new LeakSensorMotion(dev);
                 break;
             case MOTION_TYPE_SMOKE:
                 Serial.println("[HOMEKIT] -> SmokeSensor (critical!)");
                 new SmokeSensorMotion(dev);
                 break;
             case MOTION_TYPE_CO:
                 Serial.println("[HOMEKIT] -> COSensor (critical!)");
                 new COSensorMotion(dev);
                 break;
             default:
                 Serial.println("[HOMEKIT] -> MotionSensor");
                 new MotionSensorService(dev);
                 break;
         }
     }
     
     // Contact sensor with type selection (Leak/Smoke/CO have critical alerts!)
     if (dev->has_contact) {
         switch (dev->contact_type) {
             case CONTACT_TYPE_LEAK:
                 new LeakSensorService(dev);
                 break;
             case CONTACT_TYPE_SMOKE:
                 new SmokeSensorService(dev);
                 break;
             case CONTACT_TYPE_CO:
                 new COSensorService(dev);
                 break;
             case CONTACT_TYPE_OCCUPANCY:
                 new OccupancySensorContact(dev);
                 break;
             default:
                 new ContactSensorService(dev);
                 break;
         }
     }
     
     // Notify HomeKit that accessory database has changed
     homeSpan.updateDatabase();
     Serial.println("[HOMEKIT] Database updated");
 }
 
 // ============== Device Management ==============
 Device* findDevice(const char* id) {
     for (int i = 0; i < device_count; i++) {
         if (devices[i].active && strcmp(devices[i].id, id) == 0) {
             return &devices[i];
         }
     }
     return nullptr;
 }
 
 Device* registerDevice(const char* id, JsonDocument& doc) {
     if (device_count >= MAX_DEVICES) {
         Serial.println("[DEVICE] Max devices reached!");
         last_event = "ERR: Max devices!";
         return nullptr;
     }
     
     Device* dev = &devices[device_count++];
     memset(dev, 0, sizeof(Device));
     strncpy(dev->id, id, sizeof(dev->id) - 1);
     strncpy(dev->name, id, sizeof(dev->name) - 1);  // Default name = ID
     dev->active = true;
     
     // Detect capabilities from first message
     dev->has_temp = doc.containsKey("t");
     dev->has_hum = doc.containsKey("hu");
     dev->has_batt = doc.containsKey("b");
     dev->has_light = doc.containsKey("l");
     dev->has_motion = doc.containsKey("m");
     dev->has_contact = doc.containsKey("c");
     
     Serial.printf("[DEVICE] New: %s (temp:%d hum:%d batt:%d light:%d motion:%d contact:%d)\n",
                   id, dev->has_temp, dev->has_hum, dev->has_batt, 
                   dev->has_light, dev->has_motion, dev->has_contact);
     
     last_event = "New: " + String(id);
     
     // Create HomeKit accessory
     createHomekitAccessory(dev);
     
     // Save to flash
     saveDevices();
     
     return dev;
 }
 
 bool removeDevice(const char* id) {
     for (int i = 0; i < device_count; i++) {
         if (devices[i].active && strcmp(devices[i].id, id) == 0) {
             Serial.printf("[DEVICE] Removing: %s (AID: %d)\n", id, devices[i].aid);
             
             // Delete from HomeKit dynamically
             if (devices[i].aid > 0 && homekit_started) {
                 if (homeSpan.deleteAccessory(devices[i].aid)) {
                     Serial.printf("[HOMEKIT] Deleted accessory AID: %d\n", devices[i].aid);
                     homeSpan.updateDatabase();
                     Serial.println("[HOMEKIT] Database updated");
                 } else {
                     Serial.printf("[HOMEKIT] Failed to delete AID: %d\n", devices[i].aid);
                 }
             }
             
             // Clear device pointers
             devices[i].active = false;
             devices[i].aid = 0;
             devices[i].tempChar = nullptr;
             devices[i].humChar = nullptr;
             devices[i].battChar = nullptr;
             devices[i].lightChar = nullptr;
             devices[i].motionChar = nullptr;
             devices[i].contactChar = nullptr;
             devices[i].nameChar = nullptr;
             
             saveDevices();
             return true;
         }
     }
     return false;
 }
 
 bool renameDevice(const char* id, const char* newName) {
     Device* dev = findDevice(id);
     if (!dev) return false;
     
     Serial.printf("[DEVICE] Renaming %s (LoRa ID: %s) to: %s\n", dev->name, dev->id, newName);
     
     // Delete old HomeKit accessory and recreate with new AID
     uint32_t spacerAid = 0;
     if (dev->aid > 0 && homekit_started) {
         Serial.printf("[HOMEKIT] Deleting accessory AID: %d for rename\n", dev->aid);
         homeSpan.deleteAccessory(dev->aid);
         
         // Create spacer to consume old AID
         SpanAccessory* spacer = new SpanAccessory();
         spacerAid = spacer->getAID();
         
         homeSpan.updateDatabase();
     }
     
     // Clear HomeKit pointers
     dev->aid = 0;
     dev->nameChar = nullptr;
     dev->tempChar = nullptr;
     dev->humChar = nullptr;
     dev->battChar = nullptr;
     dev->lightChar = nullptr;
     dev->motionChar = nullptr;
     dev->contactChar = nullptr;
     
     // Update display name only (keep LoRa ID for packet matching)
     strncpy(dev->name, newName, sizeof(dev->name) - 1);
     
     // Recreate HomeKit accessory with new name (gets new AID)
     if (homekit_started) {
         delay(100);
         createHomekitAccessory(dev);
         
         // Delete spacer
         if (spacerAid > 0) {
             homeSpan.deleteAccessory(spacerAid);
             homeSpan.updateDatabase();
         }
         Serial.printf("[HOMEKIT] Recreated accessory with new name, AID=%d\n", dev->aid);
     }
     
     saveDevices();
     return true;
 }
 
 void updateDevice(Device* dev, JsonDocument& doc, int rssi) {
     dev->rssi = rssi;
     dev->last_seen = millis();
     
     String eventStr = String(dev->id) + " ";
     
     if (doc.containsKey("t")) {
         dev->temperature = doc["t"].as<float>();
         if (dev->tempChar) dev->tempChar->setVal(dev->temperature);
         eventStr += String(dev->temperature, 1) + "C ";
     }
     if (doc.containsKey("hu")) {
         dev->humidity = doc["hu"].as<float>();
         if (dev->humChar) dev->humChar->setVal(dev->humidity);
         eventStr += String((int)dev->humidity) + "% ";
     }
     if (doc.containsKey("b")) {
         dev->battery = doc["b"].as<int>();
         if (dev->battChar) dev->battChar->setVal(dev->battery);
     }
     if (doc.containsKey("l")) {
         dev->lux = doc["l"].as<int>();
         if (dev->lightChar) dev->lightChar->setVal(max(0.0001f, (float)dev->lux));
     }
     if (doc.containsKey("m")) {
         // Handle both string ("on"/"off") and boolean (true/false) values
         if (doc["m"].is<bool>()) {
             dev->motion = doc["m"].as<bool>();
         } else {
             String mVal = doc["m"].as<String>();
             dev->motion = (mVal == "on" || mVal == "1" || mVal == "true");
         }
         if (dev->motionChar) dev->motionChar->setVal(dev->motion);
         eventStr += dev->motion ? "MOT " : "";
     }
     if (doc.containsKey("c")) {
         // Handle both string ("on"/"off") and boolean (true/false) values
         if (doc["c"].is<bool>()) {
             dev->contact = doc["c"].as<bool>();
         } else {
             String cVal = doc["c"].as<String>();
             dev->contact = (cVal == "on" || cVal == "1" || cVal == "true");
         }
         if (dev->contactChar) dev->contactChar->setVal(dev->contact ? 0 : 1);
     }
     
     last_event = eventStr;
 }
 
 // ============== LoRa Functions ==============
 bool initLoRa() {
     displayProgress("LoRa", "Initializing...", 0);
     
     Serial.println("[LORA] Starting SPI...");
     SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
     LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
     
     displayProgress("LoRa", "Starting radio...", 30);
     
     Serial.printf("[LORA] Trying frequency: %.2f MHz\n", lora_frequency);
     if (!LoRa.begin((long)(lora_frequency * 1E6))) {
         displayMessage("ERROR!", "LoRa init failed!", "Check hardware", "");
         Serial.println("[LORA] ERROR: Init failed!");
         return false;
     }
     
     displayProgress("LoRa", "Configuring...", 60);
     
     // LoRa settings must match your sensors!
     LoRa.setSignalBandwidth(lora_bw);
     LoRa.setSpreadingFactor(lora_sf);
     LoRa.setCodingRate4(lora_cr);
     LoRa.setSyncWord(lora_syncword);
     LoRa.setPreambleLength(lora_preamble);
     LoRa.disableCrc();
     
     displayProgress("LoRa", "Ready!", 100);
     Serial.printf("[LORA] Initialized: %.2f MHz, SF%d, BW:%dkHz, CR:4/%d, Preamble:%d, Sync:0x%02X\n", 
                   lora_frequency, lora_sf, lora_bw/1000, lora_cr, lora_preamble, lora_syncword);
     
     delay(500);
     return true;
 }
 
 void processLoRaPacket() {
     int packetSize = LoRa.parsePacket();
     if (packetSize == 0) return;
     
     // Blink LED if enabled
     if (activity_led_enabled) {
         digitalWrite(LED_PIN, LOW);
     }
     
     // Wake OLED on activity
     wakeOled();
     
     uint8_t buffer[256];
     int len = 0;
     while (LoRa.available() && len < 255) {
         buffer[len++] = LoRa.read();
     }
     buffer[len] = 0;
     
     int rssi = LoRa.packetRssi();
     
     // Debug: show raw data before decryption
     Serial.printf("[LORA] Received %d bytes, RSSI: %d\n", len, rssi);
     Serial.print("[LORA] Raw hex: ");
     int showLen = (len < 64) ? len : 64;
     for (int i = 0; i < showLen; i++) {
         Serial.printf("%02X ", buffer[i]);
     }
     if (len > 64) Serial.print("...");
     Serial.println();
     
     // Decrypt if enabled
     decryptBuffer(buffer, len);
     
     // Debug: show data after decryption
     Serial.printf("[LORA] Decrypted (%s): %s\n", getEncryptionModeName(encryption_mode), (char*)buffer);
     
     // Parse JSON
     StaticJsonDocument<512> doc;
     DeserializationError error = deserializeJson(doc, (char*)buffer);
     
     if (error) {
         Serial.printf("[LORA] JSON parse error: %s\n", error.c_str());
         Serial.printf("[LORA] Check: encryption mode=%s, key length=%d\n", 
                       getEncryptionModeName(encryption_mode), encrypt_key_len);
         last_event = "ERR: Bad JSON";
         digitalWrite(LED_PIN, HIGH);
         return;
     }
     
     // Check gateway key
     if (!doc.containsKey("k") || strcmp(doc["k"], gateway_key) != 0) {
         Serial.println("[LORA] Gateway key mismatch");
         last_event = "ERR: Wrong key";
         digitalWrite(LED_PIN, HIGH);
         return;
     }
     
     // Check device ID
     if (!doc.containsKey("id")) {
         Serial.println("[LORA] Missing device ID");
         last_event = "ERR: No device ID";
         digitalWrite(LED_PIN, HIGH);
         return;
     }
     
     const char* id = doc["id"];
     packets_received++;
     last_packet_time = millis();
     
     // Find or register device
     Device* dev = findDevice(id);
     if (!dev) {
         dev = registerDevice(id, doc);
     }
     
     if (dev) {
         updateDevice(dev, doc, rssi);
         Serial.printf("[LORA] %s RSSI:%d", id, rssi);
         if (doc.containsKey("t")) Serial.printf(" T:%.1f°C", doc["t"].as<float>());
         if (doc.containsKey("hu")) Serial.printf(" H:%.0f%%", doc["hu"].as<float>());
         if (doc.containsKey("b")) Serial.printf(" B:%d%%", doc["b"].as<int>());
         Serial.println();
     }
     
     if (activity_led_enabled) {
         digitalWrite(LED_PIN, HIGH);
     }
 }
 
 // ============== WiFi Functions ==============
 bool connectWiFi() {
     if (strlen(wifi_ssid) == 0) {
         Serial.println("[WIFI] No SSID configured");
         return false;
     }
     
     displayProgress("WiFi", ("Connecting to " + String(wifi_ssid)).c_str(), 0);
     
     Serial.printf("[WIFI] Connecting to: %s\n", wifi_ssid);
     WiFi.mode(WIFI_STA);
     WiFi.begin(wifi_ssid, wifi_password);
     
     int timeout = 30; // 15 seconds
     int progress = 0;
     
     while (WiFi.status() != WL_CONNECTED && timeout-- > 0) {
         delay(500);
         progress = (30 - timeout) * 100 / 30;
         displayProgress("WiFi", ("Connecting to " + String(wifi_ssid)).c_str(), progress);
         Serial.print(".");
     }
     Serial.println();
     
     if (WiFi.status() == WL_CONNECTED) {
         displayProgress("WiFi", ("Connected: " + WiFi.localIP().toString()).c_str(), 100);
         Serial.printf("[WIFI] Connected: %s\n", WiFi.localIP().toString().c_str());
         delay(1000);
         return true;
     }
     
     displayMessage("WiFi Failed!", "Could not connect to:", wifi_ssid, "Starting setup mode...");
     Serial.println("[WIFI] Connection failed!");
     delay(2000);
     return false;
 }
 
 void startAPMode() {
     displayProgress("Setup Mode", "Starting AP...", 0);
     
     Serial.println("[AP] Starting Access Point...");
     WiFi.mode(WIFI_AP);
     delay(100);
     
     WiFi.softAP(AP_SSID, AP_PASSWORD);
     delay(100);
     
     displayProgress("Setup Mode", "Starting DNS...", 50);
     
     // Start DNS server for captive portal
     dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
     
     ap_mode = true;
     
     displayProgress("Setup Mode", "Ready!", 100);
     Serial.printf("[AP] Started: %s / %s\n", AP_SSID, AP_PASSWORD);
     Serial.printf("[AP] IP: %s\n", WiFi.softAPIP().toString().c_str());
     
     delay(500);
 }
 

// ============== Web Server Handlers ==============
// The web UI is served as a multi-page application with client-side navigation

const char CSS_STYLES[] PROGMEM = R"rawliteral(
:root{--bg-primary:#0a0e14;--bg-secondary:#111821;--bg-tertiary:#1a232f;--bg-card:#151d28;--bg-card-hover:#1a2636;--border-primary:#2a3744;--border-accent:#3d4f5f;--text-primary:#e6edf3;--text-secondary:#8b949e;--text-muted:#6e7681;--accent-primary:#f0883e;--accent-secondary:#db6d28;--accent-glow:rgba(240,136,62,0.3);--success:#3fb950;--success-glow:rgba(63,185,80,0.3);--warning:#d29922;--warning-glow:rgba(210,153,34,0.3);--danger:#f85149;--danger-glow:rgba(248,81,73,0.3);--shadow-md:0 4px 12px rgba(0,0,0,0.5)}
[data-theme="light"]{--bg-primary:#f6f8fa;--bg-secondary:#ffffff;--bg-tertiary:#ebeef1;--bg-card:#ffffff;--bg-card-hover:#f3f6f9;--border-primary:#d0d7de;--border-accent:#a8b3bd;--text-primary:#1f2328;--text-secondary:#656d76;--text-muted:#8b949e;--accent-primary:#d35400;--accent-secondary:#b84700;--accent-glow:rgba(211,84,0,0.15);--success:#1a7f37;--success-glow:rgba(26,127,55,0.15);--warning:#9a6700;--warning-glow:rgba(154,103,0,0.15);--danger:#cf222e;--danger-glow:rgba(207,34,46,0.15);--shadow-md:0 4px 12px rgba(0,0,0,0.1)}
*{margin:0;padding:0;box-sizing:border-box}body{font-family:-apple-system,system-ui,sans-serif;background:var(--bg-primary);color:var(--text-primary);min-height:100vh;line-height:1.5;transition:background .3s,color .3s}
.app{display:flex;min-height:100vh;position:relative}.sidebar{width:240px;background:var(--bg-secondary);border-right:1px solid var(--border-primary);display:flex;flex-direction:column;position:fixed;height:100vh;transition:transform .3s;z-index:100}
.sidebar-header{padding:16px;border-bottom:1px solid var(--border-primary)}.logo{display:flex;align-items:center;gap:10px}.logo-icon{width:36px;height:36px;background:linear-gradient(135deg,var(--accent-primary),var(--accent-secondary));border-radius:8px;display:flex;align-items:center;justify-content:center}
.logo-icon svg{width:20px;height:20px;fill:#fff}.logo-text{display:flex;flex-direction:column}.logo-title{font-size:14px;font-weight:700}.logo-subtitle{font-size:9px;color:var(--text-secondary);text-transform:uppercase;letter-spacing:1px}
.conn-status{display:flex;align-items:center;gap:8px;padding:8px 12px;margin:12px;background:var(--bg-tertiary);border-radius:6px;border:1px solid var(--border-primary)}.status-led{width:6px;height:6px;border-radius:50%;background:var(--success);animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.5}}.status-text{font-size:11px;color:var(--text-secondary)}.nav-section{padding:4px 12px}.nav-label{font-size:9px;font-weight:600;color:var(--text-muted);text-transform:uppercase;letter-spacing:1px;padding:8px 6px 4px}
.nav-item{display:flex;align-items:center;gap:8px;padding:8px 12px;border-radius:6px;color:var(--text-secondary);font-size:12px;font-weight:500;cursor:pointer;border:1px solid transparent;margin-bottom:2px;transition:all .2s;text-decoration:none}
.nav-item:hover{background:var(--bg-tertiary);color:var(--text-primary)}.nav-item.active{background:var(--accent-glow);color:var(--accent-primary);border-color:var(--accent-primary)}.nav-item svg{width:16px;height:16px;flex-shrink:0}
.sidebar-footer{margin-top:auto;padding:12px;border-top:1px solid var(--border-primary)}.theme-toggle{display:flex;align-items:center;justify-content:space-between;padding:8px 12px;background:var(--bg-tertiary);border-radius:6px;border:1px solid var(--border-primary)}
.theme-label{font-size:11px;color:var(--text-secondary);display:flex;align-items:center;gap:6px}.theme-label svg{width:14px;height:14px}.toggle-sw{width:40px;height:22px;background:var(--bg-card);border-radius:11px;cursor:pointer;position:relative;border:2px solid var(--border-primary);transition:all .3s}
.toggle-sw::after{content:'';position:absolute;width:14px;height:14px;background:var(--accent-primary);border-radius:50%;top:2px;left:2px;transition:transform .3s}[data-theme="dark"] .toggle-sw::after{transform:translateX(18px)}
.main{flex:1;margin-left:240px;padding:20px;min-height:100vh}.page{display:none;animation:fadeIn .3s}.page.active{display:block}@keyframes fadeIn{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:translateY(0)}}
.page-header{margin-bottom:20px}.page-title{font-size:20px;font-weight:700;margin-bottom:4px}.page-desc{color:var(--text-secondary);font-size:13px}
.card{background:var(--bg-card);border:1px solid var(--border-primary);border-radius:10px;padding:16px;margin-bottom:16px;transition:all .3s}.card:hover{border-color:var(--border-accent)}
.card-header{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;padding-bottom:12px;border-bottom:1px solid var(--border-primary)}.card-title{font-size:14px;font-weight:600;display:flex;align-items:center;gap:6px}.card-title svg{width:16px;height:16px;color:var(--accent-primary)}
.grid-2{display:grid;grid-template-columns:repeat(2,1fr);gap:16px}.status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px}
.status-item{background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:6px;padding:12px;display:flex;flex-direction:column;gap:4px}.status-label{font-size:9px;font-weight:600;color:var(--text-muted);text-transform:uppercase;letter-spacing:1px}.status-value{font-family:monospace;font-size:13px;font-weight:600;color:var(--text-primary)}.status-value.hl{color:var(--accent-primary)}
.badge{display:inline-flex;align-items:center;gap:4px;padding:2px 8px;border-radius:12px;font-size:10px;font-weight:600}.badge.success{background:var(--success-glow);color:var(--success)}.badge.warning{background:var(--warning-glow);color:var(--warning)}.badge.danger{background:var(--danger-glow);color:var(--danger)}.badge::before{content:'';width:4px;height:4px;border-radius:50%;background:currentColor}
.form-group{margin-bottom:14px}.form-label{display:block;font-size:11px;font-weight:600;color:var(--text-secondary);margin-bottom:4px}.form-input,.form-select{width:100%;padding:8px 12px;background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:6px;color:var(--text-primary);font-family:monospace;font-size:12px;transition:all .2s}
.form-input:focus,.form-select:focus{outline:none;border-color:var(--accent-primary)}.form-hint{font-size:10px;color:var(--text-muted);margin-top:3px}.form-hint.warning{color:var(--warning);display:flex;align-items:center;gap:4px;padding:8px;background:var(--warning-glow);border-radius:6px;margin-bottom:14px}
.toggle-group{display:flex;align-items:center;justify-content:space-between;padding:12px;background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:6px;margin-bottom:8px}.toggle-info{display:flex;flex-direction:column;gap:2px}.toggle-title{font-size:12px;font-weight:600;color:var(--text-primary)}.toggle-desc{font-size:10px;color:var(--text-muted)}
.toggle-btn{width:44px;height:24px;background:var(--bg-primary);border-radius:12px;cursor:pointer;position:relative;border:2px solid var(--border-primary);transition:all .3s;flex-shrink:0}.toggle-btn::after{content:'';position:absolute;width:16px;height:16px;background:var(--text-muted);border-radius:50%;top:2px;left:2px;transition:all .3s}.toggle-btn.active{background:var(--accent-glow);border-color:var(--accent-primary)}.toggle-btn.active::after{background:var(--accent-primary);transform:translateX(20px)}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:8px 16px;border-radius:6px;font-size:12px;font-weight:600;cursor:pointer;transition:all .2s;border:none}.btn svg{width:14px;height:14px}.btn-primary{background:linear-gradient(135deg,var(--accent-primary),var(--accent-secondary));color:#fff}.btn-primary:hover{transform:translateY(-1px)}.btn-secondary{background:var(--bg-tertiary);color:var(--text-primary);border:1px solid var(--border-primary)}.btn-secondary:hover{background:var(--bg-card-hover)}.btn-danger{background:var(--danger);color:#fff}.btn-danger:hover{transform:translateY(-1px)}.btn-warning{background:var(--warning);color:#fff}.btn-group{display:flex;gap:8px;flex-wrap:wrap}
.qr-container{display:flex;flex-direction:column;align-items:center;padding:20px;background:var(--bg-tertiary);border-radius:10px;border:1px solid var(--border-primary)}.qr-code{width:160px;height:160px;background:#fff;border-radius:10px;padding:10px;margin-bottom:14px}.qr-code img{width:100%;height:100%;image-rendering:pixelated}.hk-code{font-family:monospace;font-size:22px;font-weight:700;letter-spacing:2px;color:var(--text-primary);margin-bottom:4px}.hk-code-label{font-size:10px;color:var(--text-muted);text-transform:uppercase;letter-spacing:1px}
.device-card{background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:8px;padding:12px;display:flex;align-items:center;gap:12px;margin-bottom:8px;transition:all .2s}.device-card:hover{border-color:var(--accent-primary)}.device-icon{width:40px;height:40px;background:var(--bg-card);border-radius:8px;display:flex;align-items:center;justify-content:center;border:1px solid var(--border-primary)}.device-icon svg{width:20px;height:20px;color:var(--accent-primary)}.device-info{flex:1}.device-name{font-weight:600;font-size:13px;margin-bottom:2px}.device-meta{font-size:10px;color:var(--text-muted);font-family:monospace}.device-actions{display:flex;gap:4px}.device-btn{padding:4px 8px;font-size:10px;border-radius:4px;cursor:pointer;background:var(--bg-card);border:1px solid var(--border-primary);color:var(--text-secondary);transition:all .2s}.device-btn:hover{border-color:var(--accent-primary);color:var(--accent-primary)}.device-btn.danger:hover{border-color:var(--danger);color:var(--danger)}
.test-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(120px,1fr));gap:8px}.test-btn{padding:14px;background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:8px;display:flex;flex-direction:column;align-items:center;gap:8px;cursor:pointer;transition:all .2s;color:var(--text-primary)}.test-btn:hover{border-color:var(--accent-primary);background:var(--bg-card-hover);transform:translateY(-1px)}.test-btn svg{width:20px;height:20px;color:var(--accent-primary)}.test-btn span{font-size:11px;font-weight:600}
.action-card{background:var(--bg-tertiary);border:1px solid var(--border-primary);border-radius:8px;padding:14px;display:flex;align-items:center;justify-content:space-between;margin-bottom:8px}.action-info{display:flex;align-items:center;gap:12px}.action-icon{width:36px;height:36px;background:var(--bg-card);border-radius:8px;display:flex;align-items:center;justify-content:center;border:1px solid var(--border-primary)}.action-icon svg{width:18px;height:18px}.action-icon.warning svg{color:var(--warning)}.action-icon.danger svg{color:var(--danger)}.action-text h4{font-size:13px;font-weight:600;margin-bottom:2px}.action-text p{font-size:11px;color:var(--text-muted)}
.mobile-menu{display:none;position:fixed;top:12px;left:12px;z-index:200;width:36px;height:36px;background:var(--bg-secondary);border:1px solid var(--border-primary);border-radius:8px;cursor:pointer;align-items:center;justify-content:center}.mobile-menu svg{width:20px;height:20px;color:var(--text-primary)}.sidebar-overlay{display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.5);z-index:99}
@media(max-width:900px){.grid-2{grid-template-columns:1fr}}@media(max-width:768px){.sidebar{transform:translateX(-100%)}.sidebar.open{transform:translateX(0)}.sidebar-overlay.active{display:block}.mobile-menu{display:flex}.main{margin-left:0;padding:60px 12px 16px}.page-title{font-size:18px}.status-grid{grid-template-columns:1fr}.qr-code{width:140px;height:140px}.hk-code{font-size:18px}}
)rawliteral";

void handleRoot() {
    String html;
    html.reserve(24000);
    
    bool isPaired = homekit_started && (homeSpan.controllerListBegin() != homeSpan.controllerListEnd());
    int activeDevices = getActiveDeviceCount();
    unsigned long uptime = (millis() - boot_time) / 1000;
    String uptimeStr = (uptime >= 3600) ? String(uptime/3600)+"h "+String((uptime%3600)/60)+"m" : String(uptime/60)+"m "+String(uptime%60)+"s";
    
    String encKeyHex = "";
    for (int i = 0; i < encrypt_key_len; i++) { char hex[3]; sprintf(hex, "%02X", encrypt_key[i]); encKeyHex += hex; }
    char syncHex[5]; sprintf(syncHex, "%02X", lora_syncword);
    
    html += F("<!DOCTYPE html><html lang=\"en\" data-theme=\"light\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>LoRa HomeKit Bridge</title><style>");
    html += FPSTR(CSS_STYLES);
    html += F("</style></head><body>");
    
    // Mobile menu and overlay
    html += F("<div class=\"sidebar-overlay\" onclick=\"toggleSidebar()\"></div>");
    html += F("<button class=\"mobile-menu\" onclick=\"toggleSidebar()\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M3 12h18M3 6h18M3 18h18\"/></svg></button>");
    
    // App container
    html += F("<div class=\"app\">");
    
    // Sidebar
    html += F("<aside class=\"sidebar\" id=\"sidebar\"><div class=\"sidebar-header\"><div class=\"logo\"><div class=\"logo-icon\"><svg viewBox=\"0 0 24 24\" fill=\"currentColor\"><path d=\"M12 2L2 7v10l10 5 10-5V7L12 2z\"/></svg></div><div class=\"logo-text\"><span class=\"logo-title\">LoRa HomeKit</span><span class=\"logo-subtitle\">Control Panel</span></div></div></div>");
    html += F("<div class=\"conn-status\"><div class=\"status-led\"></div><span class=\"status-text\">");
    html += ap_mode ? "Setup Mode" : "Connected";
    html += F("</span></div>");
    
    // Navigation
    html += F("<nav class=\"nav-section\"><div class=\"nav-label\">Main</div>");
    html += F("<a class=\"nav-item active\" data-page=\"status\" onclick=\"showPage('status')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><path d=\"M3 9h18M9 21V9\"/></svg>Status</a>");
    html += F("<a class=\"nav-item\" data-page=\"homekit\" onclick=\"showPage('homekit')\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"m3 9 9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z\"/><path d=\"M9 22V12h6v10\"/></svg>HomeKit</a>");
    html += F("<a class=\"nav-item\" data-page=\"devices\" onclick=\"showPage('devices')\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><rect width=\"20\" height=\"14\" x=\"2\" y=\"3\" rx=\"2\"/><path d=\"M8 21h8m-4-4v4\"/></svg>Devices</a>");
    html += F("<a class=\"nav-item\" data-page=\"test\" onclick=\"showPage('test')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M14.7 6.3a1 1 0 000 1.4l1.6 1.6a1 1 0 001.4 0l3.77-3.77a6 6 0 01-7.94 7.94l-6.91 6.91a2.12 2.12 0 01-3-3l6.91-6.91a6 6 0 017.94-7.94l-3.76 3.76z\"/></svg>Test</a>");
    html += F("</nav><nav class=\"nav-section\"><div class=\"nav-label\">Settings</div>");
    html += F("<a class=\"nav-item\" data-page=\"wifi\" onclick=\"showPage('wifi')\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M5 12.55a11 11 0 0 1 14.08 0M1.42 9a16 16 0 0 1 21.16 0M8.53 16.11a6 6 0 0 1 6.95 0\"/><circle cx=\"12\" cy=\"20\" r=\"1\"/></svg>WiFi</a>");
    html += F("<a class=\"nav-item\" data-page=\"lora\" onclick=\"showPage('lora')\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M4.9 19.1C1 15.2 1 8.8 4.9 4.9m2.9 11.3c-2.3-2.3-2.3-6.1 0-8.5\"/><circle cx=\"12\" cy=\"12\" r=\"2\"/><path d=\"M16.2 7.8c2.3 2.3 2.3 6.1 0 8.5m2.9-11.4C23 8.8 23 15.1 19.1 19\"/></svg>LoRa</a>");
    html += F("<a class=\"nav-item\" data-page=\"encryption\" onclick=\"showPage('encryption')\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><rect width=\"18\" height=\"11\" x=\"3\" y=\"11\" rx=\"2\"/><path d=\"M7 11V7a5 5 0 0 1 10 0v4\"/><circle cx=\"12\" cy=\"16\" r=\"1\"/></svg>Encryption</a>");
    html += F("<a class=\"nav-item\" data-page=\"hardware\" onclick=\"showPage('hardware')\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><rect width=\"16\" height=\"16\" x=\"4\" y=\"4\" rx=\"2\"/><path d=\"M9 9h6v6H9zm0-7v2m6-2v2M9 20v2m6-2v2M2 9h2m-2 6h2m16-6h2m-2 6h2\"/></svg>Hardware</a>");
    html += F("</nav><nav class=\"nav-section\"><div class=\"nav-label\">Actions</div>");
    html += F("<a class=\"nav-item\" data-page=\"actions\" onclick=\"showPage('actions')\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M23 4v6h-6M1 20v-6h6\"/><path d=\"M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15\"/></svg>System</a>");
    html += F("</nav><div class=\"sidebar-footer\"><div class=\"theme-toggle\"><span class=\"theme-label\"><svg viewBox=\"0 0 24 24\"><path fill=\"currentColor\" d=\"M12 22c5.523 0 10-4.477 10-10S17.523 2 12 2 2 6.477 2 12s4.477 10 10 10Zm0-1.5v-17a8.5 8.5 0 1 1 0 17Z\"/></svg>Dark Mode</span><div class=\"toggle-sw\" onclick=\"toggleTheme()\"></div></div></div></aside>");
    
    // Main content
    html += F("<main class=\"main\">");
    
    // Status Page
    html += F("<div class=\"page active\" id=\"page-status\"><div class=\"page-header\"><h1 class=\"page-title\">System Status</h1><p class=\"page-desc\">Overview of your LoRa HomeKit Bridge</p></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M22 11.08V12a10 10 0 1 1-5.93-9.14\"/><path d=\"M22 4 12 14.01l-3-3\"/></svg>Connection</h3>");
    html += ap_mode ? F("<span class=\"badge warning\">Setup Mode</span>") : F("<span class=\"badge success\">Online</span>");
    html += F("</div><div class=\"status-grid\">");
    if (ap_mode) {
        html += F("<div class=\"status-item\"><span class=\"status-label\">AP Name</span><span class=\"status-value\">"); html += AP_SSID; html += F("</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">Password</span><span class=\"status-value\">"); html += AP_PASSWORD; html += F("</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">IP Address</span><span class=\"status-value\">"); html += WiFi.softAPIP().toString(); html += F("</span></div>");
    } else {
        html += F("<div class=\"status-item\"><span class=\"status-label\">WiFi</span><span class=\"status-value hl\">Connected</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">IP Address</span><span class=\"status-value\">"); html += WiFi.localIP().toString(); html += F("</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">Signal</span><span class=\"status-value\">"); html += String(WiFi.RSSI()); html += F(" dBm</span></div>");
        html += F("<div class=\"status-item\"><span class=\"status-label\">Network</span><span class=\"status-value\">"); html += wifi_ssid; html += F("</span></div>");
    }
    html += F("</div></div><div class=\"grid-2\"><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M4.9 19.1C1 15.2 1 8.8 4.9 4.9m2.9 11.3c-2.3-2.3-2.3-6.1 0-8.5\"/><circle cx=\"12\" cy=\"12\" r=\"2\"/><path d=\"M16.2 7.8c2.3 2.3 2.3 6.1 0 8.5m2.9-11.4C23 8.8 23 15.1 19.1 19\"/></svg>LoRa Radio</h3></div><div class=\"status-grid\">");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Frequency</span><span class=\"status-value\">"); html += String(lora_frequency,1); html += F(" MHz</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">SF</span><span class=\"status-value\">SF"); html += String(lora_sf); html += F("</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Bandwidth</span><span class=\"status-value\">"); html += String(lora_bw/1000); html += F(" kHz</span></div>");
    html += F("</div></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M3 3v18h18m-3-4V9m-5 8V5M8 17v-3\"/></svg>Statistics</h3></div><div class=\"status-grid\">");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Devices</span><span class=\"status-value hl\">"); html += String(activeDevices); html += F("</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Packets</span><span class=\"status-value\">"); html += String(packets_received); html += F("</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Uptime</span><span class=\"status-value\">"); html += uptimeStr; html += F("</span></div>");
    html += F("</div></div></div></div>");
    
    // HomeKit Page
    html += F("<div class=\"page\" id=\"page-homekit\"><div class=\"page-header\"><h1 class=\"page-title\">HomeKit Pairing</h1><p class=\"page-desc\">Pair with Apple HomeKit</p></div><div class=\"grid-2\"><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">QR Code</h3></div><div class=\"qr-container\"><div class=\"qr-code\" id=\"qrcode\"></div><div class=\"hk-code\">");
    html += homekit_code_display;
    html += F("</div><div class=\"hk-code-label\">Setup Code</div></div></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Pairing Status</h3>");
    html += isPaired ? F("<span class=\"badge success\">Paired</span>") : F("<span class=\"badge warning\">Not Paired</span>");
    html += F("</div><div class=\"status-grid\"><div class=\"status-item\"><span class=\"status-label\">Status</span><span class=\"status-value hl\">"); 
    html += isPaired ? "Paired" : "Waiting"; 
    html += F("</span></div><div class=\"status-item\"><span class=\"status-label\">Accessories</span><span class=\"status-value\">"); html += String(activeDevices); html += F("</span></div></div>");
    if (isPaired) { html += F("<div style=\"margin-top:14px\"><button class=\"btn btn-danger\" onclick=\"unpairHomeKit()\">Unpair HomeKit</button></div>"); }
    html += F("</div></div></div>");
    
    // Devices Page
    html += F("<div class=\"page\" id=\"page-devices\"><div class=\"page-header\"><h1 class=\"page-title\">Devices</h1><p class=\"page-desc\">Manage connected devices</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Connected ("); html += String(activeDevices); html += F(")</h3><button class=\"btn btn-secondary\" onclick=\"location.reload()\">Refresh</button></div>");
    if (activeDevices == 0) {
        html += F("<p style=\"color:var(--text-muted);font-size:12px\">No devices yet. Add test devices or wait for LoRa sensors.</p>");
    } else {
        for (int i = 0; i < device_count; i++) {
            if (!devices[i].active) continue;
            html += F("<div class=\"device-card\"><div class=\"device-icon\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><rect width=\"16\" height=\"16\" x=\"4\" y=\"4\" rx=\"2\"/><circle cx=\"12\" cy=\"12\" r=\"3\"/></svg></div><div class=\"device-info\"><div class=\"device-name\">"); html += devices[i].name;
            html += F("</div><div class=\"device-meta\">");
            if (devices[i].has_temp) { html += "T:" + String(devices[i].temperature,1) + "C "; }
            if (devices[i].has_hum) { html += "H:" + String((int)devices[i].humidity) + "% "; }
            html += "RSSI:" + String(devices[i].rssi);
            html += F("</div></div><div class=\"device-actions\"><button class=\"device-btn\" onclick=\"renameDevice('"); html += devices[i].id; html += F("','"); html += devices[i].name; html += F("')\">Rename</button><button class=\"device-btn danger\" onclick=\"removeDevice('"); html += devices[i].id; html += F("')\">Remove</button></div></div>");
        }
    }
    html += F("</div></div>");
    
    // Test Page
    html += F("<div class=\"page\" id=\"page-test\"><div class=\"page-header\"><h1 class=\"page-title\">Test Sensors</h1><p class=\"page-desc\">Add simulated sensors</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Simulated Sensors</h3></div><div class=\"test-grid\">");
    html += F("<button class=\"test-btn\" onclick=\"addTest('temp')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M14 14.76V3.5a2.5 2.5 0 00-5 0v11.26a4.5 4.5 0 105 0z\"/></svg><span>Temperature</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('humidity')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M12 2.69l5.66 5.66a8 8 0 11-11.31 0z\"/></svg><span>Humidity</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('temp_hum')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M14 14.76V3.5a2.5 2.5 0 00-5 0v11.26\"/></svg><span>Temp+Hum</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('motion')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><circle cx=\"12\" cy=\"12\" r=\"3\"/></svg><span>Motion</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('contact')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/></svg><span>Contact</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('light')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><circle cx=\"12\" cy=\"12\" r=\"5\"/></svg><span>Light</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('full')\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><rect width=\"16\" height=\"16\" x=\"4\" y=\"4\" rx=\"2\"/><path d=\"M9 9h6v6H9z\"/></svg><span>Full Sensor</span></button>");
    html += F("<button class=\"test-btn\" onclick=\"addTest('update')\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M23 4v6h-6\"/></svg><span>Update Values</span></button>");
    html += F("</div><p id=\"test-status\" style=\"color:var(--accent-primary);font-size:11px;margin-top:10px;text-align:center\"></p></div></div>");
    
    // WiFi Page
    html += F("<div class=\"page\" id=\"page-wifi\"><div class=\"page-header\"><h1 class=\"page-title\">WiFi Settings</h1><p class=\"page-desc\">Configure network</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Network</h3></div>");
    html += F("<form id=\"wifiForm\" onsubmit=\"return saveSettings(event)\"><div class=\"form-group\"><label class=\"form-label\">WiFi Network</label><div style=\"display:flex;gap:8px\"><select class=\"form-select\" id=\"wifiSelect\" style=\"flex:1\" onchange=\"document.getElementById('ssid').value=this.value\"><option value=\"\">-- Scan --</option></select><button type=\"button\" class=\"btn btn-secondary\" onclick=\"scanWifi()\">Scan</button></div></div>");
    html += F("<div class=\"form-group\"><label class=\"form-label\">SSID</label><input type=\"text\" class=\"form-input\" id=\"ssid\" name=\"ssid\" value=\""); html += wifi_ssid;
    html += F("\"></div><div class=\"form-group\"><label class=\"form-label\">Password</label><input type=\"password\" class=\"form-input\" name=\"password\" placeholder=\"Leave empty to keep current\"></div>");
    html += F("<button type=\"submit\" class=\"btn btn-primary\">Save & Restart</button></form></div></div>");
    
    // LoRa Page
    html += F("<div class=\"page\" id=\"page-lora\"><div class=\"page-header\"><h1 class=\"page-title\">LoRa Settings</h1><p class=\"page-desc\">Configure radio</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Radio Configuration</h3></div>");
    html += F("<p class=\"form-hint warning\"><svg width=\"12\" height=\"12\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z\"/></svg>Must match your sensors!</p>");
    html += F("<form id=\"loraForm\" onsubmit=\"return saveSettings(event)\"><div class=\"grid-2\"><div class=\"form-group\"><label class=\"form-label\">Frequency</label><select class=\"form-select\" name=\"freq\"><option value=\"433.0\"");
    if (lora_frequency < 500) html += " selected";
    html += F(">433 MHz</option><option value=\"868.0\"");
    if (lora_frequency > 800 && lora_frequency < 900) html += " selected";
    html += F(">868 MHz</option><option value=\"915.0\"");
    if (lora_frequency > 900) html += " selected";
    html += F(">915 MHz</option></select></div><div class=\"form-group\"><label class=\"form-label\">Spreading Factor</label><select class=\"form-select\" name=\"lora_sf\">");
    for (int sf = 6; sf <= 12; sf++) { html += "<option value=\"" + String(sf) + "\""; if (lora_sf == sf) html += " selected"; html += ">SF" + String(sf) + "</option>"; }
    html += F("</select></div><div class=\"form-group\"><label class=\"form-label\">Bandwidth</label><select class=\"form-select\" name=\"lora_bw\"><option value=\"125000\"");
    if (lora_bw == 125000) html += " selected";
    html += F(">125 kHz</option><option value=\"250000\"");
    if (lora_bw == 250000) html += " selected";
    html += F(">250 kHz</option><option value=\"500000\"");
    if (lora_bw == 500000) html += " selected";
    html += F(">500 kHz</option></select></div><div class=\"form-group\"><label class=\"form-label\">Coding Rate</label><select class=\"form-select\" name=\"lora_cr\"><option value=\"5\"");
    if (lora_cr == 5) html += " selected";
    html += F(">4/5</option><option value=\"6\"");
    if (lora_cr == 6) html += " selected";
    html += F(">4/6</option><option value=\"7\"");
    if (lora_cr == 7) html += " selected";
    html += F(">4/7</option><option value=\"8\"");
    if (lora_cr == 8) html += " selected";
    html += F(">4/8</option></select></div><div class=\"form-group\"><label class=\"form-label\">Preamble</label><input type=\"number\" class=\"form-input\" name=\"lora_pre\" value=\""); html += String(lora_preamble);
    html += F("\" min=\"6\" max=\"65535\"></div><div class=\"form-group\"><label class=\"form-label\">Sync Word</label><input type=\"text\" class=\"form-input\" name=\"lora_sync\" value=\""); html += syncHex;
    html += F("\" maxlength=\"2\"></div></div><button type=\"submit\" class=\"btn btn-primary\">Save & Restart</button></form></div></div>");
    
    // Encryption Page
    html += F("<div class=\"page\" id=\"page-encryption\"><div class=\"page-header\"><h1 class=\"page-title\">Encryption</h1><p class=\"page-desc\">Configure data encryption</p></div><div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Encryption</h3></div>");
    html += F("<p class=\"form-hint warning\"><svg width=\"12\" height=\"12\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\"><path d=\"M10.29 3.86L1.82 18a2 2 0 001.71 3h16.94a2 2 0 001.71-3L13.71 3.86a2 2 0 00-3.42 0z\"/></svg>Must match your sensors!</p>");
    html += F("<form id=\"encForm\" onsubmit=\"return saveSettings(event)\"><div class=\"form-group\"><label class=\"form-label\">Gateway Key</label><input type=\"text\" class=\"form-input\" name=\"gw_key\" value=\""); html += gateway_key;
    html += F("\"><p class=\"form-hint\">Sensors with different keys ignored</p></div><div class=\"form-group\"><label class=\"form-label\">Mode</label><select class=\"form-select\" name=\"enc_mode\"><option value=\"0\"");
    if (encryption_mode == 0) html += " selected";
    html += F(">None</option><option value=\"1\"");
    if (encryption_mode == 1) html += " selected";
    html += F(">XOR</option><option value=\"2\"");
    if (encryption_mode == 2) html += " selected";
    html += F(">AES-128</option></select></div><div class=\"form-group\"><label class=\"form-label\">Key (hex)</label><input type=\"text\" class=\"form-input\" name=\"enc_key\" value=\""); html += encKeyHex;
    html += F("\"><p class=\"form-hint\">XOR: 2-32 chars | AES: 32 chars</p></div><button type=\"submit\" class=\"btn btn-primary\">Save & Restart</button></form></div></div>");
    
    // Hardware Page  
    html += F("<div class=\"page\" id=\"page-hardware\"><div class=\"page-header\"><h1 class=\"page-title\">Hardware</h1><p class=\"page-desc\">Configure LEDs and display</p></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">LED Indicators</h3></div>");
    html += F("<div class=\"toggle-group\"><div class=\"toggle-info\"><span class=\"toggle-title\">Power LED</span><span class=\"toggle-desc\">Shows when powered</span></div><div class=\"toggle-btn"); if (power_led_enabled) html += " active"; html += F("\" id=\"pwrLed\" onclick=\"toggleHw('pwr_led')\"></div></div>");
    html += F("<div class=\"toggle-group\"><div class=\"toggle-info\"><span class=\"toggle-title\">Activity LED</span><span class=\"toggle-desc\">Blinks on packets</span></div><div class=\"toggle-btn"); if (activity_led_enabled) html += " active"; html += F("\" id=\"actLed\" onclick=\"toggleHw('act_led')\"></div></div></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Display</h3></div>");
    html += F("<div class=\"toggle-group\"><div class=\"toggle-info\"><span class=\"toggle-title\">OLED Screen</span><span class=\"toggle-desc\">Enable display</span></div><div class=\"toggle-btn"); if (oled_enabled) html += " active"; html += F("\" id=\"oledEn\" onclick=\"toggleHw('oled_en')\"></div></div>");
    html += F("<div class=\"form-group\" style=\"margin-top:12px\"><label class=\"form-label\">Screen Timeout</label><select class=\"form-select\" id=\"oledTimeout\" onchange=\"setHwVal('oled_to',this.value)\"><option value=\"0\""); if (oled_timeout == 0) html += " selected"; html += F(">Always On</option><option value=\"30\""); if (oled_timeout == 30) html += " selected"; html += F(">30s</option><option value=\"60\""); if (oled_timeout == 60) html += " selected"; html += F(">1 min</option><option value=\"300\""); if (oled_timeout == 300) html += " selected"; html += F(">5 min</option></select></div>");
    html += F("<div class=\"form-group\"><label class=\"form-label\">Brightness</label><input type=\"range\" id=\"oledBr\" min=\"1\" max=\"255\" value=\""); html += String(oled_brightness); html += F("\" style=\"width:100%;accent-color:var(--accent-primary)\" onchange=\"setHwVal('oled_br',this.value)\"></div></div></div>");
    
    // Actions Page
    html += F("<div class=\"page\" id=\"page-actions\"><div class=\"page-header\"><h1 class=\"page-title\">System</h1><p class=\"page-desc\">Device management</p></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Maintenance</h3></div>");
    html += F("<div class=\"action-card\"><div class=\"action-info\"><div class=\"action-icon warning\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M23 4v6h-6M1 20v-6h6\"/><path d=\"M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15\"/></svg></div><div class=\"action-text\"><h4>Restart</h4><p>Reboot device</p></div></div><button class=\"btn btn-warning\" onclick=\"restartDevice()\">Restart</button></div>");
    html += F("<div class=\"action-card\"><div class=\"action-info\"><div class=\"action-icon danger\"><svg fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" viewBox=\"0 0 24 24\"><path d=\"M3 6h18m-2 0v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2m-6 5v6m4-6v6\"/></svg></div><div class=\"action-text\"><h4>Factory Reset</h4><p>Erase all settings</p></div></div><button class=\"btn btn-danger\" onclick=\"factoryReset()\">Reset</button></div></div>");
    html += F("<div class=\"card\"><div class=\"card-header\"><h3 class=\"card-title\">Info</h3></div><div class=\"status-grid\">");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Firmware</span><span class=\"status-value\">v2.0</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">Hardware</span><span class=\"status-value\">TTGO LoRa32</span></div>");
    html += F("<div class=\"status-item\"><span class=\"status-label\">MAC</span><span class=\"status-value\">"); html += WiFi.macAddress(); html += F("</span></div>");
    html += F("</div></div></div>");
    
    html += F("</main></div>");
    
    // JavaScript
    html += F("<script src=\"https://cdn.jsdelivr.net/npm/qrcode-generator@1.4.4/qrcode.min.js\"></script><script>");
    html += F("function showPage(p){document.querySelectorAll('.page').forEach(e=>e.classList.remove('active'));document.getElementById('page-'+p).classList.add('active');document.querySelectorAll('.nav-item').forEach(e=>e.classList.remove('active'));document.querySelector('[data-page=\"'+p+'\"]').classList.add('active');document.getElementById('sidebar').classList.remove('open');document.querySelector('.sidebar-overlay').classList.remove('active');}");
    html += F("function toggleTheme(){var t=document.documentElement.getAttribute('data-theme')==='dark'?'light':'dark';document.documentElement.setAttribute('data-theme',t);localStorage.setItem('theme',t);}var st=localStorage.getItem('theme');if(st)document.documentElement.setAttribute('data-theme',st);");
    html += F("function toggleSidebar(){document.getElementById('sidebar').classList.toggle('open');document.querySelector('.sidebar-overlay').classList.toggle('active');}");
    html += F("window.onload=function(){var qd=document.getElementById('qrcode');if(qd&&typeof qrcode!=='undefined'){try{var qr=qrcode(0,'M');qr.addData('"); html += homekit_qr_uri; html += F("');qr.make();qd.innerHTML=qr.createImgTag(4,0);}catch(e){}}};");
    html += F("function scanWifi(){var s=document.getElementById('wifiSelect');s.innerHTML='<option>Scanning...</option>';fetch('/api/scan').then(r=>r.json()).then(d=>{s.innerHTML='<option value=\"\">-- Select --</option>';d.networks.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{s.innerHTML+='<option value=\"'+n.ssid+'\">'+n.ssid+' ('+n.rssi+')</option>';});}).catch(()=>{s.innerHTML='<option>Failed</option>';});}");
    html += F("function addTest(t){var s=document.getElementById('test-status');if(s)s.innerHTML='Adding...';fetch('/api/test?type='+t).then(r=>r.json()).then(d=>{if(s)s.innerHTML=d.message;setTimeout(()=>location.reload(),2000);});}");
    html += F("function renameDevice(id,name){var n=prompt('New name:',name);if(n&&n!==name){fetch('/api/rename?id='+encodeURIComponent(id)+'&name='+encodeURIComponent(n)).then(r=>r.json()).then(d=>{alert(d.message);location.reload();});}}");
    html += F("function removeDevice(id){if(confirm('Remove '+id+'?')){fetch('/api/remove?id='+encodeURIComponent(id)).then(r=>r.json()).then(d=>{alert(d.message);location.reload();});}}");
    html += F("function unpairHomeKit(){if(confirm('Unpair?')){fetch('/api/unpair').then(()=>{alert('Unpairing...');setTimeout(()=>location.reload(),3000);});}}");
    html += F("function restartDevice(){if(confirm('Restart?')){fetch('/api/restart').then(()=>{alert('Restarting...');setTimeout(()=>location.reload(),5000);});}}");
    html += F("function factoryReset(){if(confirm('Reset ALL settings?')){fetch('/reset',{method:'POST'}).then(()=>{alert('Resetting...');});}}");
    html += F("function saveSettings(e){e.preventDefault();var f=new FormData(e.target);fetch('/save',{method:'POST',body:new URLSearchParams(f)}).then(()=>{alert('Saved! Restarting...');setTimeout(()=>location.reload(),5000);});return false;}");
    html += F("function toggleHw(k){fetch('/api/hardware?'+k+'=toggle').then(r=>r.json()).then(d=>{if(k==='pwr_led')document.getElementById('pwrLed').classList.toggle('active',d.pwr_led);if(k==='act_led')document.getElementById('actLed').classList.toggle('active',d.act_led);if(k==='oled_en')document.getElementById('oledEn').classList.toggle('active',d.oled_en);});}");
    html += F("function setHwVal(k,v){fetch('/api/hardware?'+k+'='+v);}");
    html += F("</script></body></html>");
    
    webServer.send(200, "text/html", html);
}

 void handleSave() {
     bool needsRestart = false;
     
     if (webServer.hasArg("ssid") && webServer.arg("ssid").length() > 0) {
         String newSsid = webServer.arg("ssid");
         if (strcmp(wifi_ssid, newSsid.c_str()) != 0) {
             strncpy(wifi_ssid, newSsid.c_str(), 63);
             wifi_ssid[63] = 0;
             needsRestart = true;
         }
     }
     
     if (webServer.hasArg("password") && webServer.arg("password").length() > 0) {
         strncpy(wifi_password, webServer.arg("password").c_str(), 63);
         wifi_password[63] = 0;
         needsRestart = true;
     }
     
     if (webServer.hasArg("freq")) {
         float newFreq = webServer.arg("freq").toFloat();
         if (newFreq != lora_frequency) {
             lora_frequency = newFreq;
             needsRestart = true;
         }
     }
     
     // LoRa radio settings
     if (webServer.hasArg("lora_sf")) {
         uint8_t newSf = webServer.arg("lora_sf").toInt();
         if (newSf != lora_sf) {
             lora_sf = newSf;
             needsRestart = true;
         }
     }
     if (webServer.hasArg("lora_bw")) {
         uint32_t newBw = webServer.arg("lora_bw").toInt();
         if (newBw != lora_bw) {
             lora_bw = newBw;
             needsRestart = true;
         }
     }
     if (webServer.hasArg("lora_cr")) {
         uint8_t newCr = webServer.arg("lora_cr").toInt();
         if (newCr != lora_cr) {
             lora_cr = newCr;
             needsRestart = true;
         }
     }
     if (webServer.hasArg("lora_pre")) {
         uint16_t newPre = webServer.arg("lora_pre").toInt();
         if (newPre != lora_preamble) {
             lora_preamble = newPre;
             needsRestart = true;
         }
     }
     if (webServer.hasArg("lora_sync") && webServer.arg("lora_sync").length() > 0) {
         uint8_t newSync = strtol(webServer.arg("lora_sync").c_str(), NULL, 16);
         if (newSync != lora_syncword) {
             lora_syncword = newSync;
             needsRestart = true;
         }
     }
     
     if (webServer.hasArg("gw_key")) {
         strncpy(gateway_key, webServer.arg("gw_key").c_str(), 31);
         gateway_key[31] = 0;
     }
     
     if (webServer.hasArg("enc_mode")) {
         encryption_mode = webServer.arg("enc_mode").toInt();
     }
     
     if (webServer.hasArg("enc_key") && webServer.arg("enc_key").length() >= 2) {
         String keyHex = webServer.arg("enc_key");
         encrypt_key_len = min((int)(keyHex.length() / 2), 16);
         for (int i = 0; i < encrypt_key_len; i++) {
             char hex[3] = { keyHex[i*2], keyHex[i*2+1], 0 };
             encrypt_key[i] = strtol(hex, NULL, 16);
         }
     }
     
     saveSettings();
     
     String html = "<!DOCTYPE html><html><head>";
     html += "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
     html += "<style>";
     html += "body{font-family:-apple-system,system-ui,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}";
     html += ".box{text-align:center;padding:40px}";
     html += "h1{color:#4ecdc4;font-size:3em;margin:0}";
     html += "p{color:#aaa;margin-top:16px}";
     html += ".spinner{width:40px;height:40px;border:3px solid rgba(255,255,255,0.1);border-top:3px solid #4ecdc4;border-radius:50%;animation:spin 1s linear infinite;margin:24px auto}";
     html += "@keyframes spin{to{transform:rotate(360deg)}}";
     html += "</style></head><body><div class=\"box\">";
     html += "<h1>OK</h1>";
     html += "<p>Settings saved!</p>";
     html += "<div class=\"spinner\"></div>";
     html += "<p>Restarting...</p>";
     html += "</div>";
     
     if (needsRestart || ap_mode) {
         html += "<script>setTimeout(function(){";
         if (ap_mode && strlen(wifi_ssid) > 0) {
             // If we were in AP mode and WiFi is now configured, 
             // we can't redirect since the device will be on a different network
             html += "document.body.innerHTML='<div class=\"box\"><h1>OK</h1><p>Settings saved!</p><p style=\"color:#4ecdc4\">Connect to your WiFi network and find the bridge at its new IP.</p></div>';";
         } else {
             html += "location.href='/';";
         }
         html += "},5000);</script>";
     } else {
         html += "<script>setTimeout(function(){location.href='/';},2000);</script>";
     }
     
     html += "</body></html>";
     
     webServer.send(200, "text/html", html);
     
     delay(1000);
     
     if (needsRestart || ap_mode) {
         ESP.restart();
     }
 }
 
 void handleReset() {
     clearSettings();
     
     String html = "<!DOCTYPE html><html><head>";
     html += "<meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
     html += "<style>";
     html += "body{font-family:-apple-system,system-ui,sans-serif;background:linear-gradient(135deg,#1a1a2e,#16213e);color:#fff;display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}";
     html += ".box{text-align:center;padding:40px}";
     html += "h1{color:#e74c3c;font-size:3em;margin:0}";
     html += "p{color:#aaa;margin-top:16px}";
     html += ".spinner{width:40px;height:40px;border:3px solid rgba(255,255,255,0.1);border-top:3px solid #e74c3c;border-radius:50%;animation:spin 1s linear infinite;margin:24px auto}";
     html += "@keyframes spin{to{transform:rotate(360deg)}}";
     html += "</style></head><body><div class=\"box\">";
     html += "<h1>RESET</h1>";
     html += "<p>Factory reset complete!</p>";
     html += "<div class=\"spinner\"></div>";
     html += "<p>Restarting into setup mode...</p>";
     html += "<p style=\"color:#666;font-size:0.9em\">Connect to: " + String(AP_SSID) + "</p>";
     html += "</div>";
     html += "<script>setTimeout(function(){},5000);</script>";
     html += "</body></html>";
     
     webServer.send(200, "text/html", html);
     
     delay(1000);
     ESP.restart();
 }
 
 void handleScan() {
     Serial.println("[WIFI] Scanning networks...");
     
     int n = WiFi.scanNetworks();
     
     StaticJsonDocument<1024> doc;
     JsonArray networks = doc.createNestedArray("networks");
     
     for (int i = 0; i < n && i < 15; i++) {
         JsonObject net = networks.createNestedObject();
         net["ssid"] = WiFi.SSID(i);
         net["rssi"] = WiFi.RSSI(i);
         net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
     }
     
     WiFi.scanDelete();
     
     String output;
     serializeJson(doc, output);
     webServer.send(200, "application/json", output);
     
     Serial.printf("[WIFI] Found %d networks\n", n);
 }
 
 // Test device handler - creates simulated sensors for testing
 void handleTestDevice() {
     String type = webServer.arg("type");
     String response;
     StaticJsonDocument<256> responseDoc;
     
     Serial.printf("[TEST] Creating test device type: %s\n", type.c_str());
     
     // Generate a unique test device ID
     static int testCounter = 0;
     testCounter++;
     String deviceId = "Test_" + type + "_" + String(testCounter);
     
     // Create a fake JSON document simulating a LoRa packet
     StaticJsonDocument<256> doc;
     doc["k"] = gateway_key;  // Use configured gateway key
     doc["id"] = deviceId;
     
     if (type == "temp") {
         doc["t"] = 22.5 + (random(0, 100) / 10.0);  // Random temp 22.5-32.5
         doc["b"] = 85;
     } else if (type == "humidity") {
         doc["hu"] = 45 + random(0, 30);  // Random humidity 45-75%
         doc["b"] = 90;
     } else if (type == "temp_hum") {
         doc["t"] = 21.0 + (random(0, 80) / 10.0);
         doc["hu"] = 40 + random(0, 40);
         doc["b"] = 75;
     } else if (type == "motion") {
         doc["m"] = true;
         doc["b"] = 100;
     } else if (type == "contact") {
         doc["c"] = false;  // false = closed/normal
         doc["b"] = 95;
     } else if (type == "light") {
         doc["l"] = 100 + random(0, 900);  // Random lux 100-1000
         doc["b"] = 80;
     } else if (type == "full") {
         doc["t"] = 23.5;
         doc["hu"] = 55;
         doc["l"] = 500;
         doc["b"] = 70;
     } else if (type == "update") {
         // Update existing test devices with new random values
         for (int i = 0; i < device_count; i++) {
             if (strncmp(devices[i].id, "Test_", 5) == 0) {
                 if (devices[i].has_temp) {
                     devices[i].temperature = 20.0 + (random(0, 100) / 10.0);
                     if (devices[i].tempChar) devices[i].tempChar->setVal(devices[i].temperature);
                 }
                 if (devices[i].has_hum) {
                     devices[i].humidity = 40 + random(0, 40);
                     if (devices[i].humChar) devices[i].humChar->setVal(devices[i].humidity);
                 }
                 if (devices[i].has_light) {
                     devices[i].lux = 100 + random(0, 900);
                     if (devices[i].lightChar) devices[i].lightChar->setVal(max(0.0001f, (float)devices[i].lux));
                 }
                 if (devices[i].has_motion) {
                     devices[i].motion = !devices[i].motion;
                     if (devices[i].motionChar) devices[i].motionChar->setVal(devices[i].motion);
                 }
                 if (devices[i].has_contact) {
                     devices[i].contact = !devices[i].contact;
                     if (devices[i].contactChar) devices[i].contactChar->setVal(devices[i].contact ? 0 : 1);
                 }
                 Serial.printf("[TEST] Updated device: %s\n", devices[i].id);
             }
         }
         responseDoc["success"] = true;
         responseDoc["message"] = "Updated all test devices!";
         serializeJson(responseDoc, response);
         webServer.send(200, "application/json", response);
         return;
     } else {
         responseDoc["success"] = false;
         responseDoc["message"] = "Unknown test type: " + type;
         serializeJson(responseDoc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     // Find or register the device (simulating LoRa packet processing)
     Device* dev = findDevice(deviceId.c_str());
     if (!dev) {
         dev = registerDevice(deviceId.c_str(), doc);
     }
     
     if (dev) {
         updateDevice(dev, doc, -50);  // Fake RSSI of -50
         packets_received++;
         last_packet_time = millis();
         last_event = "Test: " + deviceId;
         
         responseDoc["success"] = true;
         responseDoc["message"] = "Created " + deviceId + " - check Home app!";
         responseDoc["device_id"] = deviceId;
     } else {
         responseDoc["success"] = false;
         responseDoc["message"] = "Failed to create device (max reached?)";
     }
     
     serializeJson(responseDoc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Unpair HomeKit handler - removes all paired controllers
 void handleUnpair() {
     Serial.println("[HOMEKIT] Unpairing all controllers...");
     
     // HomeSpan's unpair command
     homeSpan.processSerialCommand("U");
     
     webServer.send(200, "application/json", "{\"success\":true,\"message\":\"Unpaired. Restarting...\"}");
     
     delay(1000);
     ESP.restart();
 }
 
 // Rename device handler
 void handleRenameDevice() {
     String id = webServer.arg("id");
     String newName = webServer.arg("name");
     
     StaticJsonDocument<256> doc;
     
     if (id.length() == 0 || newName.length() == 0) {
         doc["success"] = false;
         doc["message"] = "Missing id or name parameter";
         String response;
         serializeJson(doc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     if (renameDevice(id.c_str(), newName.c_str())) {
         doc["success"] = true;
         doc["message"] = "Renamed to: " + newName;
     } else {
         doc["success"] = false;
         doc["message"] = "Device not found: " + id;
     }
     
     String response;
     serializeJson(doc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Remove device handler
 void handleRemoveDevice() {
     String id = webServer.arg("id");
     
     StaticJsonDocument<256> doc;
     
     if (id.length() == 0) {
         doc["success"] = false;
         doc["message"] = "Missing id parameter";
         String response;
         serializeJson(doc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     if (removeDevice(id.c_str())) {
         doc["success"] = true;
         doc["message"] = "Device removed from HomeKit.";
     } else {
         doc["success"] = false;
         doc["message"] = "Device not found: " + id;
     }
     
     String response;
     serializeJson(doc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Restart device handler
 void handleRestart() {
     webServer.send(200, "application/json", "{\"success\":true,\"message\":\"Restarting...\"}");
     delay(500);
     ESP.restart();
 }
 
 // Set sensor type handler
 void handleSetSensorType() {
     String id = webServer.arg("id");
     String sensor = webServer.arg("sensor");
     String typeStr = webServer.arg("type");
     
     StaticJsonDocument<256> doc;
     
     if (id.length() == 0 || sensor.length() == 0) {
         doc["success"] = false;
         doc["message"] = "Missing parameters";
         String response;
         serializeJson(doc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     Device* dev = findDevice(id.c_str());
     if (!dev) {
         doc["success"] = false;
         doc["message"] = "Device not found: " + id;
         String response;
         serializeJson(doc, response);
         webServer.send(404, "application/json", response);
         return;
     }
     
     uint8_t newType = typeStr.toInt();
     bool changed = false;
     String typeName;
     
     if (sensor == "contact" && dev->has_contact) {
         if (dev->contact_type != newType) {
             dev->contact_type = newType;
             changed = true;
             typeName = getContactTypeName(newType);
         }
     } else if (sensor == "motion" && dev->has_motion) {
         if (dev->motion_type != newType) {
             dev->motion_type = newType;
             changed = true;
             typeName = getMotionTypeName(newType);
         }
     } else {
         doc["success"] = false;
         doc["message"] = "Invalid sensor type";
         String response;
         serializeJson(doc, response);
         webServer.send(400, "application/json", response);
         return;
     }
     
     if (changed) {
         saveDevices();
         
         // Delete old accessory and recreate with new type
         if (dev->aid > 0 && homekit_started) {
             uint32_t oldAid = dev->aid;
             Serial.printf("[HOMEKIT] Changing sensor type for %s to %s (type=%d, old AID=%d)\n", 
                           id.c_str(), typeName.c_str(), newType, oldAid);
             
             homeSpan.deleteAccessory(dev->aid);
             
             // Create a temporary spacer accessory to consume the old AID
             // This forces the real accessory to get a new AID
             SpanAccessory* spacer = new SpanAccessory();
             uint32_t spacerAid = spacer->getAID();
             Serial.printf("[HOMEKIT] Created spacer with AID=%d\n", spacerAid);
             
             // Update database after deletion
             homeSpan.updateDatabase();
             Serial.println("[HOMEKIT] Database updated after deletion");
             
             // Clear pointers
             dev->aid = 0;
             dev->nameChar = nullptr;
             dev->tempChar = nullptr;
             dev->humChar = nullptr;
             dev->battChar = nullptr;
             dev->lightChar = nullptr;
             dev->motionChar = nullptr;
             dev->contactChar = nullptr;
             
             // Small delay to let HomeKit process the deletion
             delay(100);
             
             // Recreate with new type (will get a new AID since spacer took the old one)
             Serial.printf("[HOMEKIT] Recreating accessory with motion_type=%d\n", dev->motion_type);
             createHomekitAccessory(dev);
             
             // Now delete the spacer
             homeSpan.deleteAccessory(spacerAid);
             homeSpan.updateDatabase();
             Serial.printf("[HOMEKIT] Deleted spacer, new accessory AID=%d\n", dev->aid);
         }
         
         doc["success"] = true;
         doc["message"] = "Changed to " + typeName + " sensor";
     } else {
         doc["success"] = true;
         doc["message"] = "No change needed";
     }
     
     String response;
     serializeJson(doc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Hardware settings handler
 void handleHardwareSettings() {
     // Handle toggle actions
     if (webServer.hasArg("pwr_led")) {
         if (webServer.arg("pwr_led") == "toggle") {
             power_led_enabled = !power_led_enabled;
         } else {
             power_led_enabled = webServer.arg("pwr_led") == "1";
         }
         if (!power_led_enabled) {
             digitalWrite(LED_PIN, HIGH);
         }
         saveSettings();
     }
     
     if (webServer.hasArg("act_led")) {
         if (webServer.arg("act_led") == "toggle") {
             activity_led_enabled = !activity_led_enabled;
         } else {
             activity_led_enabled = webServer.arg("act_led") == "1";
         }
         saveSettings();
     }
     
     if (webServer.hasArg("oled_en")) {
         if (webServer.arg("oled_en") == "toggle") {
             oled_enabled = !oled_enabled;
         } else {
             oled_enabled = webServer.arg("oled_en") == "1";
         }
         if (!oled_enabled && display_available) {
             display.displayOff();
         } else if (oled_enabled && display_available) {
             display.displayOn();
             oled_is_off = false;
         }
         saveSettings();
     }
     
     if (webServer.hasArg("oled_br")) {
         oled_brightness = webServer.arg("oled_br").toInt();
         if (display_available) {
             display.setBrightness(oled_brightness);
         }
         saveSettings();
     }
     
     if (webServer.hasArg("oled_to")) {
         oled_timeout = webServer.arg("oled_to").toInt();
         saveSettings();
     }
     
     StaticJsonDocument<256> doc;
     doc["pwr_led"] = power_led_enabled;
     doc["act_led"] = activity_led_enabled;
     doc["oled_en"] = oled_enabled;
     doc["oled_br"] = oled_brightness;
     doc["oled_to"] = oled_timeout;
     
     String response;
     serializeJson(doc, response);
     webServer.send(200, "application/json", response);
 }
 
 // Captive portal handler - redirect all requests to root
 void handleNotFound() {
     if (ap_mode) {
         webServer.sendHeader("Location", "http://" + WiFi.softAPIP().toString(), true);
         webServer.send(302, "text/plain", "");
     } else {
         webServer.send(404, "text/plain", "Not Found");
     }
 }
 
 // ============== HomeKit Setup ==============
 void setupHomeKit() {
     displayProgress("HomeKit", "Initializing...", 0);
     
     Serial.println("[HOMEKIT] Configuring...");
     homeSpan.setLogLevel(1);
     homeSpan.setStatusPin(LED_PIN);
     homeSpan.setControlPin(BUTTON_PIN);
     homeSpan.setPairingCode(homekit_code);  // Use generated pairing code
     homeSpan.setQRID(HOMEKIT_SETUP_ID);
     homeSpan.enableOTA();
     Serial.printf("[HOMEKIT] Pairing code: %s\n", homekit_code_display);
     
     displayProgress("HomeKit", "Creating bridge...", 50);
     
     Serial.println("[HOMEKIT] Starting bridge...");
     homeSpan.begin(Category::Bridges, "LoRa Bridge", "LORA", "LoRa-HK");
     
     // Create bridge accessory
     Serial.println("[HOMEKIT] Creating bridge accessory...");
     new SpanAccessory();
     new Service::AccessoryInformation();
     new Characteristic::Identify();
     new Characteristic::Name("LoRa Bridge");
     new Characteristic::Manufacturer("ESP32");
     new Characteristic::Model("TTGO-LoRa32");
     new Characteristic::SerialNumber("LORA-001");
     new Characteristic::FirmwareRevision("2.0");
     
     homekit_started = true;
     
     // Load saved devices and create HomeKit accessories
     loadDevices();
     if (device_count > 0) {
         Serial.printf("[HOMEKIT] Creating accessories for %d saved devices...\n", device_count);
         for (int i = 0; i < device_count; i++) {
             if (devices[i].active) {
                 createHomekitAccessory(&devices[i]);
             }
         }
     }
     
     displayProgress("HomeKit", "Ready!", 100);
     Serial.printf("[HOMEKIT] Initialized with %d devices\n", device_count);
     
     delay(500);
 }
 
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
     digitalWrite(LED_PIN, HIGH);  // HIGH = OFF on this board
     
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
         digitalWrite(LED_PIN, HIGH);
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
     
     // Start Web Server on port 8080 (HomeSpan uses 80 for HAP)
     Serial.println("[BOOT] Starting web server on port 8080...");
     displayProgress("Web Server", "Starting...", 0);
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
     displayProgress("Web Server", "Ready!", 100);
     
     IPAddress ip = ap_mode ? WiFi.softAPIP() : WiFi.localIP();
     Serial.printf("[BOOT] Web UI: http://%s:8080/\n", ip.toString().c_str());
     
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
     
     // Heartbeat log every 30 seconds to confirm device is running
     static unsigned long lastHeartbeat = 0;
     if (millis() - lastHeartbeat > 30000) {
         Serial.printf("[HEARTBEAT] Running - LoRa: %.1f MHz, Devices: %d, Packets: %d\n", 
                       lora_frequency, getActiveDeviceCount(), packets_received);
         lastHeartbeat = millis();
     }
     
     // Check for button press (long press = reset to AP mode)
     static unsigned long buttonPressStart = 0;
     if (digitalRead(BUTTON_PIN) == LOW) {
         if (buttonPressStart == 0) {
             buttonPressStart = millis();
             wakeOled();  // Wake display on button press
         } else if (millis() - buttonPressStart > 5000) {
             // 5 second hold = factory reset
             displayMessage("FACTORY RESET", "", "Releasing button", "will reset...");
             while (digitalRead(BUTTON_PIN) == LOW) { 
                 delay(100); 
             }
             clearSettings();
             ESP.restart();
         }
     } else {
         buttonPressStart = 0;
     }
 }
 