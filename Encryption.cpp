/*
 * Encryption.cpp - Encryption/Decryption Implementation
 */

#include "data/Encryption.h"
#include <mbedtls/aes.h>

// ============== Encryption Settings ==============
uint8_t encryption_mode = DEFAULT_ENCRYPTION_MODE;
uint8_t encrypt_key[16] = DEFAULT_ENCRYPT_KEY;
uint8_t encrypt_key_len = ENCRYPT_KEY_LEN;

// ============== XOR Encryption ==============
void xorBuffer(uint8_t* data, size_t len) {
    if (encrypt_key_len == 0) return;
    for (size_t i = 0; i < len; i++) {
        data[i] ^= encrypt_key[i % encrypt_key_len];
    }
}

// ============== AES Decryption ==============
void aesDecrypt(uint8_t* data, size_t len) {
    if (encrypt_key_len == 0 || len == 0) return;

    // Prepare 16-byte key (pad or truncate)
    uint8_t aes_key[16] = {0};
    memcpy(aes_key, encrypt_key, min((int)encrypt_key_len, 16));

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, aes_key, 128);

    // Decrypt in 16-byte blocks
    size_t blocks = len / 16;
    for (size_t i = 0; i < blocks; i++) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, data + (i * 16), data + (i * 16));
    }

    mbedtls_aes_free(&aes);
}

// ============== Main Decryption Function ==============
void decryptBuffer(uint8_t* data, size_t len) {
    switch (encryption_mode) {
        case ENCRYPT_XOR:
            xorBuffer(data, len);
            break;
        case ENCRYPT_AES:
            aesDecrypt(data, len);
            break;
        case ENCRYPT_NONE:
        default:
            // No decryption
            break;
    }
}

// ============== Helper Functions ==============
const char* getEncryptionModeName(uint8_t mode) {
    switch (mode) {
        case ENCRYPT_XOR: return "XOR";
        case ENCRYPT_AES: return "AES (ESP-NOW)";
        default: return "None";
    }
}
