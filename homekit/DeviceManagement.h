/*
 * DeviceManagement.h - Device Management Functions
 * Device registration, removal, renaming, and HomeKit accessory creation
 */

#ifndef DEVICE_MANAGEMENT_H
#define DEVICE_MANAGEMENT_H

#include <ArduinoJson.h>
#include "../core/Device.h"

// ============== Mode Flags ==============
extern bool homekit_started;

// ============== HomeKit Functions ==============
void setupHomeKit();

// ============== Device Management Functions ==============
void createHomekitAccessory(Device* dev);
Device* registerDevice(const char* id, JsonDocument& doc);
bool removeDevice(const char* id);
bool renameDevice(const char* id, const char* newName);
void updateDevice(Device* dev, JsonDocument& doc, int rssi);

#endif // DEVICE_MANAGEMENT_H
