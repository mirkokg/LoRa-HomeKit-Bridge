/*
 * Settings.h - Settings Management
 * NVS storage and retrieval of all configuration
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>
#include "../core/Config.h"
#include "../core/Device.h"

// ============== Global Objects ==============
extern Preferences prefs;

// ============== Settings Variables ==============
extern char wifi_ssid[64];
extern char wifi_password[64];
extern float lora_frequency;
extern char gateway_key[32];

// LoRa radio settings
extern uint8_t lora_sf;
extern uint32_t lora_bw;
extern uint8_t lora_cr;
extern uint16_t lora_preamble;
extern uint8_t lora_syncword;

// Hardware settings (defined in Display.h but used here)
extern bool power_led_enabled;
extern bool activity_led_enabled;

// HomeKit pairing code
extern char homekit_code[9];

// Mode flags
extern bool wifi_configured;

// ============== Settings Functions ==============
void toBase36(uint64_t num, char* out, int len);
void generatePairingCode();
void loadSettings();
void saveSettings();
void clearSettings();

// Device persistence
void saveDevices();
void loadDevices();

#endif // SETTINGS_H
