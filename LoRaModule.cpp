/*
 * LoRaModule.cpp - LoRa Communication Implementation
 */

#include "hardware/LoRaModule.h"
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include "hardware/Display.h"
#include "data/Settings.h"
#include "data/Encryption.h"
#include "core/Device.h"

// Forward declarations for device management (defined in DeviceManagement module)
extern Device* registerDevice(const char* id, JsonDocument& doc);
extern void updateDevice(Device* dev, JsonDocument& doc, int rssi);

// External variables
extern bool activity_led_enabled;

// ============== Statistics ==============
uint32_t packets_received = 0;
unsigned long last_packet_time = 0;
String last_event = "";

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

    // Blink LED if enabled, keep off if disabled
    if (activity_led_enabled) {
        digitalWrite(LED_PIN, LOW);
    } else {
        digitalWrite(LED_PIN, HIGH);
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

    // Turn LED off (HIGH) after activity
    digitalWrite(LED_PIN, HIGH);
}
