/*
 * Config.h - Configuration and Constants
 * All hardware pin definitions, configuration constants, and enums
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============== Board Configuration (TTGO LoRa32 V2.1_1.6) ==============
// LoRa SPI pins
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23
#define LORA_DIO0 26

// OLED pins - V1.6/V2.1 has NO reset pin (use -1)
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST -1 // No reset pin on V1.6/V2.1!
#define OLED_ADDR 0x3C

#define LED_PIN 25
#define BUTTON_PIN 0

// ============== Configuration ==============
#define MAX_DEVICES 20
#define NVS_NAMESPACE "lora_hk"
#define DEVICE_TIMEOUT_MS (60 * 60 * 1000)

#define AP_SSID "LoRa-Bridge-Setup"
#define AP_PASSWORD "12345678"
#define DNS_PORT 53

// Default settings
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""
#define DEFAULT_LORA_FREQUENCY 868.0
#define DEFAULT_GATEWAY_KEY "xy"
#define DEFAULT_ENCRYPT_KEY {0x4B, 0xA3, 0x3F, 0x9C}
#define ENCRYPT_KEY_LEN 4

// Default LoRa radio settings (must match your sensors!)
#define DEFAULT_LORA_SF 8          // Spreading Factor 6-12
#define DEFAULT_LORA_BW 125000     // Bandwidth in Hz (125000, 250000, 500000)
#define DEFAULT_LORA_CR 5          // Coding Rate 5-8 (4/5 to 4/8)
#define DEFAULT_LORA_PREAMBLE 6    // Preamble length 6-65535
#define DEFAULT_LORA_SYNCWORD 0x12 // Sync word

// Encryption modes
enum EncryptionMode : uint8_t {
  ENCRYPT_NONE = 0,
  ENCRYPT_XOR = 1,
  ENCRYPT_AES = 2 // ESP-NOW style AES
};
#define DEFAULT_ENCRYPTION_MODE ENCRYPT_XOR

// HomeKit pairing settings
#define HOMEKIT_SETUP_ID "LRHK" // 4 character setup ID

// HTTP Authentication defaults
#define AUTH_USERNAME_MAX_LEN 32
#define AUTH_PASSWORD_HASH_LEN 32 // SHA-256 = 32 bytes

// ============== Sensor Type Definitions ==============
enum ContactType : uint8_t {
  CONTACT_TYPE_CONTACT = 0,  // Default contact sensor
  CONTACT_TYPE_LEAK = 1,     // Water leak sensor (critical alerts!)
  CONTACT_TYPE_SMOKE = 2,    // Smoke sensor (critical alerts!)
  CONTACT_TYPE_CO = 3,       // Carbon monoxide sensor (critical alerts!)
  CONTACT_TYPE_OCCUPANCY = 4 // Occupancy sensor
};

enum MotionType : uint8_t {
  MOTION_TYPE_MOTION = 0,    // Default motion sensor
  MOTION_TYPE_OCCUPANCY = 1, // Occupancy sensor
  MOTION_TYPE_LEAK = 2,      // Water leak sensor (critical alerts!)
  MOTION_TYPE_SMOKE = 3,     // Smoke sensor (critical alerts!)
  MOTION_TYPE_CO = 4         // Carbon monoxide sensor (critical alerts!)
};

#endif // CONFIG_H
