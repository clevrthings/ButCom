# ATtiny85 Example

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
