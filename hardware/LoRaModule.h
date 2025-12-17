/*
 * LoRaModule.h - LoRa Communication Functions
 * LoRa radio initialization and packet processing
 */

#ifndef LORA_MODULE_H
#define LORA_MODULE_H

#include <Arduino.h>
#include "../core/Config.h"

// ============== Statistics ==============
extern uint32_t packets_received;
extern unsigned long last_packet_time;
extern String last_event;

// ============== LoRa Functions ==============
bool initLoRa();
void processLoRaPacket();

#endif // LORA_MODULE_H
