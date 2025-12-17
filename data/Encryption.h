/*
 * Encryption.h - Encryption/Decryption Functions
 * XOR and AES encryption support for LoRa packets
 */

#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <Arduino.h>
#include "../core/Config.h"

// ============== Global Settings ==============
extern uint8_t encryption_mode;
extern uint8_t encrypt_key[16];
extern uint8_t encrypt_key_len;

// ============== Encryption Functions ==============
void xorBuffer(uint8_t* data, size_t len);
void aesDecrypt(uint8_t* data, size_t len);
void decryptBuffer(uint8_t* data, size_t len);
const char* getEncryptionModeName(uint8_t mode);

#endif // ENCRYPTION_H
