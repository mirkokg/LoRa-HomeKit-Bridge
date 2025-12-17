/*
 * HomeKitServices.h - HomeSpan Service Definitions
 * All HomeKit accessory service structs
 */

#ifndef HOMEKIT_SERVICES_H
#define HOMEKIT_SERVICES_H

#include <HomeSpan.h>
#include "../core/Device.h"

// ============== Temperature Sensor Service ==============
struct TempSensor : Service::TemperatureSensor {
    SpanCharacteristic* temp;
    Device* dev;

    TempSensor(Device* d) : Service::TemperatureSensor() {
        dev = d;
        temp = new Characteristic::CurrentTemperature(dev->temperature);
        temp->setRange(-40, 125);
        dev->tempChar = temp;
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

// ============== Humidity Sensor Service ==============
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

// ============== Battery Service ==============
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

// ============== Light Sensor Service ==============
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

// ============== Motion Sensor Service ==============
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

// ============== Occupancy Sensor Service (for motion->occupancy type) ==============
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

// ============== Leak Sensor Service for motion ==============
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

// ============== Smoke Sensor Service for motion ==============
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

// ============== Carbon Monoxide Sensor Service for motion ==============
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

// ============== Contact Sensor Service ==============
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

// ============== Leak Sensor Service ==============
struct LeakSensorService : Service::LeakSensor {
    SpanCharacteristic* sensor;
    Device* dev;

    LeakSensorService(Device* d) : Service::LeakSensor() {
        dev = d;
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

// ============== Smoke Sensor Service ==============
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

// ============== Carbon Monoxide Sensor Service ==============
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

// ============== Occupancy Sensor Service (for contact->occupancy type) ==============
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

#endif // HOMEKIT_SERVICES_H
