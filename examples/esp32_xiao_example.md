# ESP32-C3 (Xiao) Example

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
