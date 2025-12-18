# LoRa HomeKit Bridge Firmware

<p align="center">
  <img src="https://img.shields.io/badge/Platform-ESP32-blue" alt="Platform ESP32">
  <img src="https://img.shields.io/badge/Protocol-LoRa-orange" alt="LoRa">
  <img src="https://img.shields.io/badge/HomeKit-Compatible-green" alt="HomeKit Compatible">
  <img src="https://img.shields.io/badge/Arduino-IDE-teal" alt="Arduino IDE">
</p>

A powerful bridge that receives data from [PricelessToolkit](https://www.pricelesstoolkit.com/en/) **LoRa sensors** and exposes them to **Apple HomeKit**. Transform your long-range LoRa sensors into smart home devices that work natively with Siri and the Apple Home app.

Device will act as a HomeKit bridge and all of the detected PricelessToolkit LoRa devicess will be created dynamically on first message reception.

---

## ✨ Features

- **🏠 Native HomeKit Integration** — Sensors appear directly in the Apple Home app
- **📡 Long-Range LoRa Reception** — Receive data from sensors up to several kilometers away
- **🔐 Encryption Support** — None, XOR, or AES (ESP-NOW compatible) encryption
- **📺 OLED Display** — Real-time status on the built-in 128x64 screen
- **🌐 Web Configuration** — Beautiful web UI for setup and management
- **📱 QR Code Pairing** — Scan to add to HomeKit instantly
- **🔧 Dynamic Devices** — Sensors are automatically discovered and added
- **⚠️ Critical Alerts** — Map sensors to Leak/Smoke/CO for urgent HomeKit notifications

## ✨ Planned Features

- Support for OTA updates
- Support for other boards
- Automatic builds
- Suggestions are welcome!

### Supported Sensor Types

| Sensor | HomeKit Service | JSON Key |
|--------|-----------------|----------|
| Temperature | Temperature Sensor | `t` |
| Humidity | Humidity Sensor | `hu` |
| Battery Level | Battery Service | `b` |
| Light Level | Light Sensor | `l` |
| Motion | Motion/Occupancy/Leak/Smoke/CO | `m` |
| Contact | Contact/Occupancy/Leak/Smoke/CO | `c` |

Note: I only tested with [PIRBOX-LITE](https://www.pricelesstoolkit.com/en/projects/49-135-pirbox-lite-motion-sensor-lora-0741049314542.html). As this is the only PricelessToolkit device that I actually own at this point. I added support for the rest of device types based on code of the original open source gateway.

---

## 🛠 Hardware Requirements

### Required Board

**TTGO LoRa32 V2.1_1.6** (or compatible ESP32 + SX1276/SX1278 LoRa module)

| Component | Specification |
|-----------|---------------|
| MCU | ESP32 |
| LoRa Radio | SX1276/SX1278 |
| Display | 0.96" SSD1306 OLED (128x64) |
| Flash | 4MB minimum |

### Pinout (TTGO LoRa32 V2.1_1.6)

| Function | GPIO |
|----------|------|
| LoRa SCK | 5 |
| LoRa MISO | 19 |
| LoRa MOSI | 27 |
| LoRa CS | 18 |
| LoRa RST | 23 |
| LoRa DIO0 | 26 |
| OLED SDA | 21 |
| OLED SCL | 22 |
| LED | 25 |
| Button | 0 |

---

## 📦 Installation

### Step 1: Install Arduino IDE

Download from [arduino.cc](https://www.arduino.cc/en/software) (version 2.x recommended)

### Step 2: Add ESP32 Board Support

1. Open **File** → **Preferences**
2. Add this URL to "Additional Board Manager URLs":
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. Open **Tools** → **Board** → **Board Manager**
4. Search for `esp32` and install **"ESP32 by Espressif Systems"** (v3.1.0+)

### Step 3: Install Required Libraries

Open **Sketch** → **Include Library** → **Manage Libraries** and install:

| Library | Author | Version |
|---------|--------|---------|
| **HomeSpan** | Gregg Silverstein | ≥2.0.0 |
| **LoRa** | Sandeep Mistry | ≥0.8.0 |
| **ArduinoJson** | Benoit Blanchon | ≥6.21.0 |
| **ESP8266 and ESP32 OLED driver for SSD1306 displays** | ThingPulse | ≥4.4.0 |
| **QRCode** | Richard Moore | ≥0.0.1 |
| **PubSubClient3** | Nick O'Leary | ≥3.3.0 |
**Run this command:**

`echo '#include "qrcode.h"' > ~/Documents/Arduino/libraries/QRCode/src/QRCode_Library.h`

### Step 4: Configure Board Settings

1. **Tools** → **Board** → **ESP32 Arduino** → **ESP32 Dev Module**
2. Configure these settings:

| Setting | Value |
|---------|-------|
| Flash Size | 4MB (32Mb) |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| Flash Mode | QIO |
| Flash Frequency | 80MHz |
| Upload Speed | 921600 |

### Step 5: Flash the Firmware

1. Open `LoRa_HomeKit_Bridge_Arduino.ino` in Arduino IDE
2. Connect your TTGO LoRa32 via USB
3. Select the correct port in **Tools** → **Port**
4. Click **Upload** (→ arrow button)

---

## 🚀 Initial Setup

### First Boot — Setup Mode

On first boot (or after factory reset), the bridge starts in **Setup Mode**:

1. **Connect to WiFi Network:**
   - SSID: `LoRa-Bridge-Setup`
   - Password: `12345678`

2. **Open Configuration Page:**
   - Go to `http://<bridge-ip>` in your browser
   - Or wait for captive portal redirect

3. **Configure Settings:**
   - Select your home WiFi network (use "Scan" button)
   - Enter WiFi password
   - Set LoRa frequency to match your sensors (433/868/915 MHz)
   - Configure gateway key and encryption (must match your sensors!)

4. **Save and Restart:**
   - Click "Save and Restart"
   - The bridge will reboot and connect to your WiFi

### LED Indicators

| Pattern | Meaning |
|---------|---------|
| LED Off | Normal operation |
| Brief Flash | LoRa packet received |
| Slow Blink | HomeKit pairing mode |

---

## 📱 HomeKit Pairing

### Method 1: QR Code (Recommended)

1. Open the **Home** app on your iPhone/iPad
2. Tap **+** → **Add Accessory**
3. **Scan the QR code** shown on the device screen and web interface at `http://<bridge-ip>`

### Method 2: Manual Code Entry

1. Open the **Home** app
2. Tap **+** → **Add Accessory** → **More Options...**
3. Select "LoRa Bridge" from the list
4. Enter the **8-digit pairing code** shown on:
   - The OLED display
   - The web interface

> **Note:** The pairing code is randomly generated on first boot and saved. It appears as `XXXX-XXXX` format.

### Troubleshooting Pairing

- Ensure your iPhone and the bridge are on the **same WiFi network**
- The bridge must be **unbound** from any previous HomeKit home
- Try restarting the bridge and the Home app

---

## 🌐 Web Interface

Access the configuration web UI at `http://<bridge-ip>`

### Status Section
- View WiFi connection status and IP address
- Monitor LoRa radio configuration
- See connected devices and packet count
- Display HomeKit pairing code and QR

### HomeKit Section
- View pairing status
- Scan QR code for easy pairing
- Unpair from HomeKit

### Devices Section
- View all discovered LoRa sensors
- **Rename** devices for friendlier HomeKit names
- **Remove** devices from HomeKit
- **Change sensor type** (e.g., Contact → Leak Sensor for water detection)

### Test Devices Section
- Add simulated sensors to test HomeKit integration
- Useful for verifying setup before real sensors arrive

### Settings Section
- Configure WiFi network
- Set LoRa radio parameters (must match your sensors!)
- Configure gateway key and encryption
- Factory reset option

![screenshot](screenshot.png)

---

## 📡 LoRa Sensor Protocol

Your LoRa sensors must send JSON packets in this format:

### Basic Packet Structure

```json
{
  "k": "xy",
  "id": "sensor_1",
  "t": 22.5,
  "hu": 55,
  "b": 85
}
```

### Required Fields

| Field | Type | Description |
|-------|------|-------------|
| `k` | String | Gateway key (must match bridge config) |
| `id` | String | Unique sensor identifier |

### Optional Sensor Fields

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `t` | Float | -40 to 125 | Temperature in °C |
| `hu` | Float | 0 to 100 | Humidity in % |
| `b` | Integer | 0 to 100 | Battery level in % |
| `l` | Integer | 0+ | Light level in lux |
| `m` | Boolean/String | `true`/`false` or `"on"`/`"off"` | Motion detected |
| `c` | Boolean/String | `true`/`false` or `"on"`/`"off"` | Contact state (true=open) |

### Example Packets

**Temperature + Humidity Sensor:**
```json
{"k":"xy","id":"bedroom_th","t":21.5,"hu":48,"b":92}
```

**Motion Sensor:**
```json
{"k":"xy","id":"hallway_pir","m":true,"b":100}
```

**Door Contact Sensor:**
```json
{"k":"xy","id":"front_door","c":false,"b":87}
```

**Full Multi-Sensor:**
```json
{"k":"xy","id":"outdoor","t":15.2,"hu":72,"l":8500,"b":65}
```

---

## ⚙️ Configuration Reference

### LoRa Radio Settings

| Setting | Default | Options | Description |
|---------|---------|---------|-------------|
| Frequency | 868.0 MHz | 433.0, 868.0, 915.0 | Regional frequency band |
| Spreading Factor | SF8 | SF6-SF12 | Higher = longer range, slower |
| Bandwidth | 125 kHz | 125, 250, 500 kHz | Higher = faster, shorter range |
| Coding Rate | 4/5 | 4/5, 4/6, 4/7, 4/8 | Higher denominator = more redundancy |
| Preamble | 6 | 6-65535 | Detection assistance |
| Sync Word | 0x12 | 0x00-0xFF | Network identifier |

> ⚠️ **All LoRa settings must match between the bridge and your sensors!**

### Encryption Modes

| Mode | Description | Key Length |
|------|-------------|------------|
| **None** | No encryption | - |
| **XOR** | Simple XOR cipher | 1-16 bytes |
| **AES** | AES-128-ECB (ESP-NOW compatible) | 16 bytes |

### Regional Frequencies

| Region | Frequency |
|--------|-----------|
| Europe | 868.0 MHz |
| Americas | 915.0 MHz |
| Asia | 433.0 MHz |

---

## 🔄 Sensor Type Mapping

You can remap contact or motion sensors to different HomeKit service types via the web UI. This enables **critical alerts** for safety sensors:

### Contact Sensor Types

| Type | HomeKit Service | Critical Alert | Use Case |
|------|-----------------|----------------|----------|
| Contact | Contact Sensor | No | Doors, windows |
| Leak | Leak Sensor | **Yes** | Water detection |
| Smoke | Smoke Sensor | **Yes** | Smoke detection |
| CO | CO Sensor | **Yes** | Carbon monoxide |
| Occupancy | Occupancy Sensor | No | Presence detection |

### Motion Sensor Types

| Type | HomeKit Service | Critical Alert | Use Case |
|------|-----------------|----------------|----------|
| Motion | Motion Sensor | No | Standard PIR |
| Occupancy | Occupancy Sensor | No | Room presence |
| Leak | Leak Sensor | **Yes** | Water detection |
| Smoke | Smoke Sensor | **Yes** | Smoke detection |
| CO | CO Sensor | **Yes** | Carbon monoxide |

> **Critical Alerts** bypass Do Not Disturb and will alert you immediately!

---

## 🔧 Troubleshooting

### Bridge Won't Start

| Symptom | Solution |
|---------|----------|
| No display output | Check OLED wiring, correct I2C address (0x3C) |
| "LoRa init failed" | Verify SPI connections, check LoRa module is soldered properly |
| Stuck on boot | Try erasing flash: `esptool.py erase_flash` then re-upload |

### WiFi Issues

| Symptom | Solution |
|---------|----------|
| Won't connect | Device creates AP "LoRa-Bridge-Setup", connect to configure |
| Frequent disconnects | Check WiFi signal strength, try moving closer to router |
| Wrong IP | Check OLED display or router's DHCP client list |

### HomeKit Issues

| Symptom | Solution |
|---------|----------|
| Can't find bridge | Ensure same WiFi network, restart Home app |
| Pairing fails | Unpair via web UI, restart bridge, try again |
| Devices not updating | Check LoRa packets are being received (LED flashes) |
| Wrong sensor values | Verify encryption settings match your sensors |

### LoRa Issues

| Symptom | Solution |
|---------|----------|
| No packets received | Check frequency, SF, BW, sync word match sensors |
| JSON parse errors | Check encryption mode and key match sensors |
| "Gateway key mismatch" | Configure same `k` value on sensors |
| Intermittent reception | Try increasing SF or preamble length |

### Factory Reset

**Via Web UI:**
- Go to Settings → click "Factory Reset"

**Via Button:**
- Hold GPIO 0 button for **5+ seconds** until display shows "FACTORY RESET"
- Release to reset all settings

---

## 📋 Technical Specifications

| Specification | Value |
|---------------|-------|
| Max Devices | 20 |
| Web Server Port | 80 |
| HomeKit Port | 51827 (HAP) |
| Device Timeout | 1 hour |
| Display Update | Every 2 seconds |
| Heartbeat Log | Every 30 seconds |

### Memory Usage

- Flash: ~1.5MB firmware
- RAM: ~50KB per device (includes HomeKit services)

### Power Consumption

- Active: ~120mA (WiFi + LoRa receiving)
- Peak: ~300mA (WiFi transmit)

---

## 📚 Resources

- [HomeSpan Library](https://github.com/HomeSpan/HomeSpan) — The Arduino HomeKit library used
- [LoRa Library](https://github.com/sandeepmistry/arduino-LoRa) — LoRa radio driver
- [Apple HomeKit Spec](https://developer.apple.com/homekit/) — HomeKit documentation

---

## 📄 License

This project is open source. Feel free to use, modify, and distribute.

---

## 🙏 Credits

Built with:
- **HomeSpan** by Gregg Silverstein
- **LoRa** by Sandeep Mistry
- **ArduinoJson** by Benoit Blanchon
- **SSD1306 Driver** by ThingPulse
- **PubSubClient3** by Nick O'Leary

---

<p align="center">
  <sub>Made with ❤️ for the smart home community</sub>
</p>
