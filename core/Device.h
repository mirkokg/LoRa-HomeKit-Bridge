/*
 * Device.h - Device Structure and Management
 * Device data structure, arrays, and helper functions
 */

#ifndef DEVICE_H
#define DEVICE_H

#include <HomeSpan.h>
#include "Config.h"

// ============== Device Structure ==============
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

// ============== Global Device Array ==============
extern Device devices[MAX_DEVICES];
extern int device_count;

// ============== Helper Functions ==============
// Helper function to count only active devices
int getActiveDeviceCount();

// Helper to get contact sensor type name
const char* getContactTypeName(uint8_t type);

// Helper to get motion sensor type name
const char* getMotionTypeName(uint8_t type);

// Find device by ID
Device* findDevice(const char* id);

#endif // DEVICE_H
