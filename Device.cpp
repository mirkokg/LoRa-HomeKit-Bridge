/*
 * Device.cpp - Device Management Implementation
 */

#include "core/Device.h"
#include <string.h>

// ============== Global Device Array ==============
Device devices[MAX_DEVICES];
int device_count = 0;

// ============== Helper Functions ==============
int getActiveDeviceCount() {
    int count = 0;
    for (int i = 0; i < device_count; i++) {
        if (devices[i].active) count++;
    }
    return count;
}

const char* getContactTypeName(uint8_t type) {
    switch (type) {
        case CONTACT_TYPE_LEAK: return "Leak";
        case CONTACT_TYPE_SMOKE: return "Smoke";
        case CONTACT_TYPE_CO: return "CO";
        case CONTACT_TYPE_OCCUPANCY: return "Occupancy";
        default: return "Contact";
    }
}

const char* getMotionTypeName(uint8_t type) {
    switch (type) {
        case MOTION_TYPE_OCCUPANCY: return "Occupancy";
        case MOTION_TYPE_LEAK: return "Leak";
        case MOTION_TYPE_SMOKE: return "Smoke";
        case MOTION_TYPE_CO: return "CO";
        default: return "Motion";
    }
}

Device* findDevice(const char* id) {
    for (int i = 0; i < device_count; i++) {
        if (devices[i].active && strcmp(devices[i].id, id) == 0) {
            return &devices[i];
        }
    }
    return nullptr;
}
