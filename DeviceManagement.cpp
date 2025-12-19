/*
 * DeviceManagement.cpp - Device Management Implementation
 */

#include "homekit/DeviceManagement.h"
#include <HomeSpan.h>
#include "homekit/HomeKitServices.h"
#include "data/Settings.h"
#include "hardware/LoRaModule.h"
#include "hardware/Display.h"
#include "core/Config.h"
#include "network/WebServerModule.h"
#include "network/MQTTModule.h"

// ============== Mode Flags ==============
bool homekit_started = false;

// External variables
extern volatile bool power_led_enabled;

// ============== HomeKit Setup ==============
void setupHomeKit() {
    displayProgress("HomeKit", "Initializing...", 0);
    homeSpan.setPortNum(51827);
    Serial.println("[HOMEKIT] Configuring...");
    homeSpan.setLogLevel(1);

    // Only enable status LED if power LED is enabled
    if (power_led_enabled) {
        homeSpan.setStatusPin(LED_PIN);
    }

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

// ============== Device Management Functions ==============
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

    // Publish Home Assistant auto-discovery if MQTT enabled
    if (mqtt_enabled) {
        publishHomeAssistantDiscovery(dev, id);
    }

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

            // Remove from MQTT (Home Assistant)
            if (mqtt_enabled) {
                removeDeviceFromMQTT(id);
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

    // Log activity for web UI - serialize JSON document to string
    String jsonStr;
    serializeJson(doc, jsonStr);
    logActivity(dev->name, jsonStr.c_str());

    // Publish to MQTT if enabled
    if (mqtt_enabled) {
        publishDeviceData(dev, doc, rssi);
    }
}
