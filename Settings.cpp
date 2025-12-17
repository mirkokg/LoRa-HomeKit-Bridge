/*
 * Settings.cpp - Settings Management Implementation
 */

#include "data/Settings.h"
#include "hardware/Display.h"
#include "data/Encryption.h"
#include <esp_random.h>
#include <mbedtls/sha256.h>

// ============== Global Objects ==============
Preferences prefs;

// ============== Settings Variables ==============
char wifi_ssid[64] = DEFAULT_WIFI_SSID;
char wifi_password[64] = DEFAULT_WIFI_PASSWORD;
float lora_frequency = DEFAULT_LORA_FREQUENCY;
char gateway_key[32] = DEFAULT_GATEWAY_KEY;

uint8_t lora_sf = DEFAULT_LORA_SF;
uint32_t lora_bw = DEFAULT_LORA_BW;
uint8_t lora_cr = DEFAULT_LORA_CR;
uint16_t lora_preamble = DEFAULT_LORA_PREAMBLE;
uint8_t lora_syncword = DEFAULT_LORA_SYNCWORD;

volatile bool power_led_enabled = true;
volatile bool activity_led_enabled = true;

char homekit_code[9] = "";
char homekit_code_display[10] = "";
char homekit_qr_uri[25] = "";
bool wifi_configured = false;

bool auth_enabled = false;
char auth_username[AUTH_USERNAME_MAX_LEN] = "";
uint8_t auth_password_hash[AUTH_PASSWORD_HASH_LEN] = {0};

// ============== Helper Functions ==============
void toBase36(uint64_t num, char* out, int len) {
    const char* chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = len - 1; i >= 0; i--) {
        out[i] = chars[num % 36];
        num /= 36;
    }
    out[len] = '\0';
}

void generatePairingCode() {
    // Generate random 8-digit pairing code
    // HomeKit requires digits only, no leading zeros in first position
    randomSeed(esp_random());  // Use hardware RNG

    uint32_t code = random(10000000, 99999999);  // 8-digit number
    sprintf(homekit_code, "%08lu", code);
    sprintf(homekit_code_display, "%.4s-%.4s", homekit_code, homekit_code + 4);

    // Generate HomeKit QR URI
    // Format: X-HM://[base36 payload][setup_id]
    // Payload = (flags << 31) | (category << 27) | setup_code
    // flags=2 (IP), category=2 (Bridge)
    uint64_t payload = ((uint64_t)2 << 31) | ((uint64_t)2 << 27) | code;
    char base36[10];
    toBase36(payload, base36, 9);
    sprintf(homekit_qr_uri, "X-HM://%s%s", base36, HOMEKIT_SETUP_ID);

    Serial.printf("[HOMEKIT] Generated pairing code: %s, QR: %s\n", homekit_code_display, homekit_qr_uri);
}

void hashPassword(const char* password, uint8_t* hash) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
    mbedtls_sha256_update(&ctx, (const unsigned char*)password, strlen(password));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
}

bool verifyPassword(const char* password, const uint8_t* hash) {
    uint8_t computed_hash[AUTH_PASSWORD_HASH_LEN];
    hashPassword(password, computed_hash);
    return memcmp(computed_hash, hash, AUTH_PASSWORD_HASH_LEN) == 0;
}

void loadSettings() {
    prefs.begin(NVS_NAMESPACE, true);

    wifi_configured = prefs.isKey("wifi_ssid") && prefs.getString("wifi_ssid", "").length() > 0;

    if (wifi_configured) {
        prefs.getString("wifi_ssid", wifi_ssid, sizeof(wifi_ssid));
        prefs.getString("wifi_pass", wifi_password, sizeof(wifi_password));
    }

    lora_frequency = prefs.getFloat("lora_freq", DEFAULT_LORA_FREQUENCY);
    prefs.getString("gw_key", gateway_key, sizeof(gateway_key));
    encryption_mode = prefs.getUChar("enc_mode", DEFAULT_ENCRYPTION_MODE);
    encrypt_key_len = prefs.getUChar("enc_len", ENCRYPT_KEY_LEN);
    if (encrypt_key_len > 0) {
        prefs.getBytes("enc_key", encrypt_key, encrypt_key_len);
    }

    // LoRa radio settings
    lora_sf = prefs.getUChar("lora_sf", DEFAULT_LORA_SF);
    lora_bw = prefs.getUInt("lora_bw", DEFAULT_LORA_BW);
    lora_cr = prefs.getUChar("lora_cr", DEFAULT_LORA_CR);
    lora_preamble = prefs.getUShort("lora_pre", DEFAULT_LORA_PREAMBLE);
    lora_syncword = prefs.getUChar("lora_sync", DEFAULT_LORA_SYNCWORD);

    // Hardware settings
    power_led_enabled = prefs.getBool("pwr_led", true);
    activity_led_enabled = prefs.getBool("act_led", true);
    oled_enabled = prefs.getBool("oled_en", true);
    oled_brightness = prefs.getUChar("oled_br", 255);
    oled_timeout = prefs.getUShort("oled_to", 60);

    // HomeKit pairing code - generate if not exists
    if (prefs.isKey("hk_code")) {
        prefs.getString("hk_code", homekit_code, sizeof(homekit_code));
        sprintf(homekit_code_display, "%.4s-%.4s", homekit_code, homekit_code + 4);

        // Generate QR URI from loaded code
        uint32_t code = atol(homekit_code);
        uint64_t payload = ((uint64_t)2 << 31) | ((uint64_t)2 << 27) | code;
        char base36[10];
        toBase36(payload, base36, 9);
        sprintf(homekit_qr_uri, "X-HM://%s%s", base36, HOMEKIT_SETUP_ID);
    }

    // HTTP Authentication settings
    auth_enabled = prefs.getBool("auth_en", false);
    if (auth_enabled) {
        prefs.getString("auth_user", auth_username, sizeof(auth_username));
        prefs.getBytes("auth_hash", auth_password_hash, AUTH_PASSWORD_HASH_LEN);
    }

    prefs.end();

    // Generate pairing code on first boot
    if (strlen(homekit_code) != 8) {
        generatePairingCode();
        // Save immediately
        prefs.begin(NVS_NAMESPACE, false);
        prefs.putString("hk_code", homekit_code);
        prefs.end();
    }

    Serial.printf("[SETTINGS] Loaded - WiFi: %s, Freq: %.1f MHz, SF%d, BW:%dkHz, Code: %s\n",
                  wifi_configured ? "YES" : "NO", lora_frequency, lora_sf, lora_bw/1000, homekit_code_display);
}

void saveSettings() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("wifi_ssid", wifi_ssid);
    prefs.putString("wifi_pass", wifi_password);
    prefs.putFloat("lora_freq", lora_frequency);
    prefs.putString("gw_key", gateway_key);
    prefs.putUChar("enc_mode", encryption_mode);
    prefs.putUChar("enc_len", encrypt_key_len);
    prefs.putBytes("enc_key", encrypt_key, encrypt_key_len);
    // LoRa radio settings
    prefs.putUChar("lora_sf", lora_sf);
    prefs.putUInt("lora_bw", lora_bw);
    prefs.putUChar("lora_cr", lora_cr);
    prefs.putUShort("lora_pre", lora_preamble);
    prefs.putUChar("lora_sync", lora_syncword);
    // Hardware settings
    prefs.putBool("pwr_led", power_led_enabled);
    prefs.putBool("act_led", activity_led_enabled);
    prefs.putBool("oled_en", oled_enabled);
    prefs.putUChar("oled_br", oled_brightness);
    prefs.putUShort("oled_to", oled_timeout);
    // HTTP Authentication
    prefs.putBool("auth_en", auth_enabled);
    if (auth_enabled) {
        prefs.putString("auth_user", auth_username);
        prefs.putBytes("auth_hash", auth_password_hash, AUTH_PASSWORD_HASH_LEN);
    } else {
        // Clear credentials when disabled
        prefs.remove("auth_user");
        prefs.remove("auth_hash");
        auth_username[0] = '\0';
        memset(auth_password_hash, 0, AUTH_PASSWORD_HASH_LEN);
    }
    // Pairing code
    prefs.putString("hk_code", homekit_code);
    prefs.end();
    Serial.println("[SETTINGS] Saved to NVS");
}

void clearSettings() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    Serial.println("[SETTINGS] Cleared all settings");
}

// ============== Device Persistence ==============
void saveDevices() {
    prefs.begin(NVS_NAMESPACE, false);

    // Clear old device data first to prevent stale entries from reappearing
    int oldCount = prefs.getInt("dev_count", 0);
    for (int i = 0; i < oldCount; i++) {
        String prefix = "dev" + String(i) + "_";
        prefs.remove((prefix + "id").c_str());
        prefs.remove((prefix + "name").c_str());
        prefs.remove((prefix + "temp").c_str());
        prefs.remove((prefix + "hum").c_str());
        prefs.remove((prefix + "batt").c_str());
        prefs.remove((prefix + "light").c_str());
        prefs.remove((prefix + "motion").c_str());
        prefs.remove((prefix + "contact").c_str());
        prefs.remove((prefix + "ctype").c_str());
        prefs.remove((prefix + "mtype").c_str());
    }

    // Save each active device with sequential indices
    int saveIndex = 0;
    for (int i = 0; i < device_count; i++) {
        if (!devices[i].active) continue;

        String prefix = "dev" + String(saveIndex) + "_";
        prefs.putString((prefix + "id").c_str(), devices[i].id);
        prefs.putString((prefix + "name").c_str(), devices[i].name);
        prefs.putBool((prefix + "temp").c_str(), devices[i].has_temp);
        prefs.putBool((prefix + "hum").c_str(), devices[i].has_hum);
        prefs.putBool((prefix + "batt").c_str(), devices[i].has_batt);
        prefs.putBool((prefix + "light").c_str(), devices[i].has_light);
        prefs.putBool((prefix + "motion").c_str(), devices[i].has_motion);
        prefs.putBool((prefix + "contact").c_str(), devices[i].has_contact);
        prefs.putUChar((prefix + "ctype").c_str(), devices[i].contact_type);
        prefs.putUChar((prefix + "mtype").c_str(), devices[i].motion_type);
        saveIndex++;
    }

    // Save count of active devices only
    prefs.putInt("dev_count", saveIndex);

    prefs.end();
    Serial.printf("[DEVICES] Saved %d devices to NVS\n", saveIndex);
}

void loadDevices() {
    prefs.begin(NVS_NAMESPACE, true);

    int saved_count = prefs.getInt("dev_count", 0);
    Serial.printf("[DEVICES] Loading %d devices from NVS\n", saved_count);

    for (int i = 0; i < saved_count && i < MAX_DEVICES; i++) {
        String prefix = "dev" + String(i) + "_";

        String id = prefs.getString((prefix + "id").c_str(), "");
        if (id.length() == 0) continue;

        Device* dev = &devices[device_count++];
        memset(dev, 0, sizeof(Device));

        strncpy(dev->id, id.c_str(), sizeof(dev->id) - 1);
        String name = prefs.getString((prefix + "name").c_str(), id);
        strncpy(dev->name, name.c_str(), sizeof(dev->name) - 1);

        dev->active = true;
        dev->has_temp = prefs.getBool((prefix + "temp").c_str(), false);
        dev->has_hum = prefs.getBool((prefix + "hum").c_str(), false);
        dev->has_batt = prefs.getBool((prefix + "batt").c_str(), false);
        dev->has_light = prefs.getBool((prefix + "light").c_str(), false);
        dev->has_motion = prefs.getBool((prefix + "motion").c_str(), false);
        dev->has_contact = prefs.getBool((prefix + "contact").c_str(), false);
        dev->contact_type = prefs.getUChar((prefix + "ctype").c_str(), 0);
        dev->motion_type = prefs.getUChar((prefix + "mtype").c_str(), 0);

        Serial.printf("[DEVICES] Loaded: %s (%s) ctype:%d mtype:%d\n",
                      dev->id, dev->name, dev->contact_type, dev->motion_type);
    }

    prefs.end();
}
