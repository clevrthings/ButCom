# ButCom
## A tiny 1-wire communication protocol for microcontrollers

ButCom is a simple, robust, 1-wire communication protocol designed for microcontrollers such as the ATtiny85 and ESP32-C3.  
It allows **two MCUs** (MCU 1 and MCU 2) to exchange messages over a single data wire + GND using a half-duplex UART-style protocol.

---

## ⚡ Features

- Single-wire communication (open-drain style)
- Configurable bit timing for short or long cables
- HELLO handshake for device discovery and reboot detection
- CRC-8 for reliability
- Automatic ACK + retry system
- Duplicate filtering for DATA frames
- Pure communication layer (no application logic)
- Supports up to 16-byte payloads (configurable)
- One device per bus (simplifies the protocol)

---

## 🔌 Wiring

```text
 MCU 1  ---- DATA ----  MCU 2
   GND  --------------  GND
```

- Use a pull-up resistor (typically 4.7 kΩ) from **DATA** to **3.3 V**.
- Recommended supply voltage: **3.3 V**.

---

## 📘 Basic Usage

### 1. Create a ButCom instance

```cpp
#include "ButCom.h"

ButCom bus(DATA_PIN, useInternalPullup, deviceId);
```

- `DATA_PIN` → GPIO used for the 1-wire bus  
- `useInternalPullup` → `true` if MCU provides a usable internal pull‑up (e.g. ESP32‑C3)  
- `deviceId` → numeric ID (0–255) for this device  

---

### 2. Initialize

```cpp
bus.setSpeedQuality(2);       // 1 = fast, 4 = very robust
bus.setCallback(onMessage);   // callback for incoming frames
bus.begin(true);              // send HELLO on startup
```

---

### 3. Send a message

```cpp
uint8_t payload[3] = {10, 20, 30};
bus.send(payload, 3, true);
```

Parameters:

- `payload` → pointer to the data buffer you want to send  
- `3`       → number of bytes in the payload buffer  
- `true`    → if `true`, the receiver will send an ACK and ButCom will automatically retry if the ACK is not received in time  

---

### 4. Receive messages

```cpp
void onMessage(uint8_t msgId,
               uint8_t type,
               const uint8_t* data,
               uint8_t len)
{
    if (type == BUTCOM_MSG_DATA) {
        // data[0..len-1] contains application payload
    }
    else if (type == BUTCOM_MSG_HELLO && len >= 1) {
        // data[0] = ID of the other MCU
    }
    // BUTCOM_MSG_ACK is already handled internally (for retries)
}
```

---

### 5. Main loop

```cpp
void loop() {
    bus.loop();   // must be called frequently
}
```

Call `bus.loop()` as often as possible (in your `loop()` function or a fast task).

---

## 🎮 Example: ESP32-C3 (Xiao) as MCU 1

> Note: Seeed Xiao ESP32-C3 has **no onboard LED**.  
> This example uses an **external LED on GPIO10**.

```cpp
#include <Arduino.h>
#include "ButCom.h"

#define DATA_PIN 4
#define LED_PIN  10   // external LED on GPIO10

ButCom bus(DATA_PIN, true, 0x20); // internal pull-up, device ID 0x20

void onMessage(uint8_t msgId, uint8_t type,
               const uint8_t* data, uint8_t len)
{
    if (type == BUTCOM_MSG_DATA && len >= 1) {
        Serial.println(data[0] ? "BUTTON PRESSED" : "BUTTON RELEASED");
    }
    else if (type == BUTCOM_MSG_HELLO && len >= 1) {
        Serial.print("HELLO from device ID 0x");
        Serial.println(data[0], HEX);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    bus.setCallback(onMessage);
    bus.setSpeedQuality(2);   // default
    bus.begin(true);          // send HELLO on startup
}

void loop() {
    bus.loop();

    // Example: toggle the remote LED every 10 seconds
    static uint32_t lastToggle = 0;
    static bool ledOn = false;
    uint32_t now = millis();

    if (now - lastToggle > 10000) {
        lastToggle = now;
        ledOn = !ledOn;

        uint8_t payload[1] = { ledOn ? 1 : 0 };
        bus.send(payload, 1, true);         // send LED command with ACK
        digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
    }
}
```

---

## 🎮 Example: ATtiny85 as MCU 2

```cpp
#include <Arduino.h>
#include "ButCom.h"

#define BUTTON_PIN PB1
#define LED_PIN    PB0
#define DATA_PIN   PB2

ButCom bus(DATA_PIN, false, 0x10); // external pull-up, device ID 0x10

void onMessage(uint8_t msgId, uint8_t type,
               const uint8_t* data, uint8_t len)
{
    if (type == BUTCOM_MSG_DATA && len >= 1) {
        // First byte controls LED state
        digitalWrite(LED_PIN, data[0] ? HIGH : LOW);
    }
}

void setup() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    bus.setCallback(onMessage);
    bus.setSpeedQuality(2);
    bus.begin(true);  // send HELLO on startup
}

void loop() {
    bus.loop();

    // Simple time-based debounce
    static uint8_t stable = 1;
    static uint8_t lastReading = 1;
    static uint32_t lastChangeMs = 0;
    const uint16_t DEBOUNCE_MS = 30;

    uint8_t reading = (digitalRead(BUTTON_PIN) == LOW) ? 0 : 1;
    uint32_t now = millis();

    if (reading != lastReading) {
        lastReading = reading;
        lastChangeMs = now;
    }

    if ((now - lastChangeMs) > DEBOUNCE_MS && reading != stable) {
        stable = reading;

        uint8_t payload[1];
        payload[0] = (stable == 0) ? 1 : 0;  // 1 = pressed, 0 = released
        bus.send(payload, 1, true);
    }
}
```

---

## ⚙️ Speed Quality

Use `setSpeedQuality()` to tune the protocol for cable length / noise:

```cpp
// 1 = fastest (short cables), 4 = slowest but most tolerant
bus.setSpeedQuality(3);
```

| Quality | Bit Time (approx.) | Use Case                     |
|---------|--------------------|------------------------------|
| 1       | ~300 µs            | Very short cable, clean env. |
| 2       | ~500 µs            | Default, up to ~2 m          |
| 3       | ~800 µs            | Longer / noisier cable       |
| 4       | ~1200 µs           | Very long / very noisy cable |

The ACK timeout is automatically scaled based on this setting.

---

## 🔬 Frame Format

Each frame on the wire has this structure:

```text
START  LEN  TYPE  MSGID  PAYLOAD...  CRC8
 0xA5  xx    xx    xx     xx...      xx
```

- `START`  → fixed value `0xA5`  
- `LEN`    → number of bytes following (TYPE + MSGID + PAYLOAD + CRC)  
- `TYPE`   → `0` = HELLO, `1` = DATA, `2` = ACK  
- `MSGID`  → message identifier (1..255)  
- `PAYLOAD`→ 0..BUTCOM_MAX_PAYLOAD bytes, defined by the user  
- `CRC8`   → CRC-8 (ATM, polynomial `0x07`) over `LEN`, `TYPE`, `MSGID`, `PAYLOAD`  

ACK frames reuse the same `MSGID` as the frame they acknowledge.

---

## 🔁 HELLO Handshake

Both sides send a HELLO periodically:

- on startup (if `begin(true)` is used)  
- every `helloIntervalMs` (default: 5000 ms)

HELLO payload:

```text
payload[0] = deviceId of sender
```

This allows each side to know which device is present on the other end of the bus, and to recover gracefully if one side is reset.

You can change the interval (or disable it):

```cpp
bus.setHelloInterval(0);       // disable periodic HELLO
bus.setHelloInterval(2000);    // HELLO every 2 seconds
```

---

## 🧪 Reliability Features

- Open-drain style line handling (drive LOW, release to HIGH)
- Idle-line detection before sending a byte
- Glitch filtering on the start bit
- CRC-8 validation on each frame
- Optional ACK + automatic retransmission
- Duplicate DATA frame filtering (based on `MSGID`)
- Periodic HELLO handshake for resync

---

## 📄 License

MIT License – free for personal and commercial use.

---

## 🤝 Contributing

Contributions are welcome:
- Additional examples (other MCUs)
- More frame types / higher-level conventions
- Tooling to visualize the protocol timing
- Better integration with Arduino Library Manager

