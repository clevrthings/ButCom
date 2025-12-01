#include "ButCom.h"

/* ============================================================
   Physical Layer (ButComPhy)
   ============================================================ */

ButComPhy::ButComPhy(uint8_t pin, bool useInternalPullup)
    : _pin(pin),
      _usePullup(useInternalPullup),
      _bitUs(500),             // default 0.5ms per bit
      _halfBitUs(250),
      _idleMinUs(1500)         // 3 bit times
{}

void ButComPhy::setBitTimeUs(uint16_t us) {
    // clamp values for safety
    if (us < 300)  us = 300;
    if (us > 2000) us = 2000;

    _bitUs     = us;
    _halfBitUs = us / 2;
    _idleMinUs = 3 * (uint32_t)us;
}

void ButComPhy::begin() {
    if (_usePullup)
        pinMode(_pin, INPUT_PULLUP);
    else
        pinMode(_pin, INPUT);
}

void ButComPhy::driveLow() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
}

void ButComPhy::releaseLine() {
    if (_usePullup)
        pinMode(_pin, INPUT_PULLUP);
    else
        pinMode(_pin, INPUT);
}

void ButComPhy::waitIdle() {
    uint32_t highStart = micros();

    while (true) {
        if (digitalRead(_pin) == HIGH) {
            if ((uint32_t)(micros() - highStart) >= _idleMinUs)
                return;
        } else {
            highStart = micros(); // reset timer
        }
    }
}

void ButComPhy::sendByte(uint8_t value) {
    waitIdle();

    // Start bit
    driveLow();
    delayMicroseconds(_bitUs);

    // 8 data bits (LSB first)
    for (uint8_t i = 0; i < 8; i++) {
        bool bit = (value >> i) & 1;
        if (bit) releaseLine();
        else     driveLow();
        delayMicroseconds(_bitUs);
    }

    // Stop bit
    releaseLine();
    delayMicroseconds(_bitUs);
}

bool ButComPhy::receiveByte(uint8_t& out, uint32_t timeoutMs) {
    uint32_t startMs = millis();

    // Wait until line is HIGH
    while (digitalRead(_pin) == LOW) {
        if (millis() - startMs > timeoutMs) return false;
    }

    // Wait for falling edge (start bit)
    while (true) {
        if (millis() - startMs > timeoutMs) return false;

        if (digitalRead(_pin) == LOW) {
            uint32_t edgeTime = micros();

            // Glitch filter
            delayMicroseconds(_halfBitUs / 2);
            if (digitalRead(_pin) == LOW) {
                // Real start bit detected
                uint32_t sampleTime = edgeTime + _bitUs + _halfBitUs;
                uint8_t value = 0;

                for (uint8_t i = 0; i < 8; i++) {
                    while ((int32_t)(micros() - sampleTime) < 0) {
                        if (millis() - startMs > timeoutMs) return false;
                    }
                    if (digitalRead(_pin) == HIGH)
                        value |= (1 << i);
                    sampleTime += _bitUs;
                }

                out = value;
                return true;
            } else {
                // False start bit – wait until HIGH again
                while (digitalRead(_pin) == LOW) {
                    if (millis() - startMs > timeoutMs) return false;
                }
            }
        }
    }
}

/* ============================================================
   CRC-8 (ATM polynomial 0x07)
   ============================================================ */
uint8_t ButCom::crc8_update(uint8_t crc, uint8_t data) {
    crc ^= data;
    for (uint8_t i = 0; i < 8; i++) {
        crc = (crc & 0x80)
            ? (uint8_t)((crc << 1) ^ 0x07)
            : (uint8_t)(crc << 1);
    }
    return crc;
}

/* ============================================================
   Logical Layer (ButCom)
   ============================================================ */

ButCom::ButCom(uint8_t pin, bool internalPullup, uint8_t deviceId)
    : _phy(pin, internalPullup),
      _id(deviceId),
      _remoteId(0),
      _hasRemoteId(false),
      _callback(nullptr),
      _rxState(RX_WAIT_START),
      _rxExpectedLength(0),
      _rxIndex(0),
      _lastDataMsgId(0xFF),
      _ackTimeoutMs(40),
      _maxRetries(2),
      _lastHelloMs(0),
      _helloIntervalMs(5000),    // send HELLO every 5s
      _nextMsgId(1)
{
    _pending.active      = false;
    _pending.requiresAck = false;
    _pending.retries     = 0;
    _pending.length      = 0;
}

void ButCom::setSpeedQuality(uint8_t level) {
    if (level < 1) level = 1;
    if (level > 4) level = 4;

    uint16_t us =
        (level == 1) ? 300 :
        (level == 2) ? 500 :
        (level == 3) ? 800 :
                       1200;

    _phy.setBitTimeUs(us);

    // Adjust ack timeout proportionally
    _ackTimeoutMs =
        (us <= 500) ? 40 :
        (us <= 800) ? 60 :
                      80;
}

void ButCom::begin(bool sendHelloOnStart) {
    _phy.begin();
    _lastHelloMs = millis();
    if (sendHelloOnStart) sendHello();
}

void ButCom::sendHello() {
    uint8_t payload[1] = { _id };
    uint8_t msgId = _nextMsgId++;
    sendRawFrame(BUTCOM_MSG_HELLO, msgId, payload, 1);
    _lastHelloMs = millis();
}

void ButCom::loop() {

    // ---- Receive one byte per iteration ----
    uint8_t b;
    if (_phy.receiveByte(b, 10)) {
        handleReceivedByte(b);
    }

    uint32_t now = millis();

    // ---- Automatic retry if waiting for ACK ----
    if (_pending.active && _pending.requiresAck) {
        if ((now - _pending.lastSendMs) > _ackTimeoutMs) {

            if (_pending.retries < _maxRetries) {
                _pending.retries++;
                _pending.lastSendMs = now;

                sendRawFrame(_pending.type,
                             _pending.msgId,
                             _pending.payload,
                             _pending.length);
            } else {
                // Give up after max retries
                _pending.active = false;
            }
        }
    }

    // ---- Periodic HELLO for resync ----
    if (_helloIntervalMs &&
        (now - _lastHelloMs) > _helloIntervalMs) {
        sendHello();
    }
}

uint8_t ButCom::send(const uint8_t* payload,
                     uint8_t length,
                     bool requestAck)
{
    if (length > BUTCOM_MAX_PAYLOAD)
        length = BUTCOM_MAX_PAYLOAD;

    uint8_t msgId = _nextMsgId++;

    sendRawFrame(BUTCOM_MSG_DATA, msgId, payload, length);

    // Start pending retry if no other TX is pending
    if (requestAck && !_pending.active) {
        _pending.active      = true;
        _pending.requiresAck = true;
        _pending.type        = BUTCOM_MSG_DATA;
        _pending.msgId       = msgId;
        _pending.length      = length;
        _pending.retries     = 0;
        _pending.lastSendMs  = millis();

        for (uint8_t i = 0; i < length; i++)
            _pending.payload[i] = payload[i];
    }

    return msgId;
}

void ButCom::sendRawFrame(uint8_t type,
                          uint8_t msgId,
                          const uint8_t* payload,
                          uint8_t length)
{
    uint8_t bodyLen = 2 + length + 1; // type + msgId + payload + crc

    uint8_t crc = 0;
    crc = crc8_update(crc, bodyLen);
    crc = crc8_update(crc, type);
    crc = crc8_update(crc, msgId);

    for (uint8_t i = 0; i < length; i++)
        crc = crc8_update(crc, payload ? payload[i] : 0);

    _phy.sendByte(0xA5);       // START
    _phy.sendByte(bodyLen);
    _phy.sendByte(type);
    _phy.sendByte(msgId);

    for (uint8_t i = 0; i < length; i++)
        _phy.sendByte(payload ? payload[i] : 0);

    _phy.sendByte(crc);
}

/* ============================================================
   RX State Machine
   ============================================================ */

void ButCom::handleReceivedByte(uint8_t b) {
    switch (_rxState) {
        case RX_WAIT_START:
            if (b == 0xA5)
                _rxState = RX_WAIT_LENGTH;
            break;

        case RX_WAIT_LENGTH:
            _rxExpectedLength = b;

            if (_rxExpectedLength < 3 ||
                _rxExpectedLength > (2 + BUTCOM_MAX_PAYLOAD + 1))
            {
                _rxState = RX_WAIT_START;
            } else {
                _rxIndex = 0;
                _rxState = RX_READ_BODY;
            }
            break;

        case RX_READ_BODY:
            _rxBuffer[_rxIndex++] = b;
            if (_rxIndex >= _rxExpectedLength) {
                processFrame(_rxExpectedLength);
                _rxState = RX_WAIT_START;
            }
            break;
    }
}

void ButCom::processFrame(uint8_t length) {

    uint8_t type   = _rxBuffer[0];
    uint8_t msgId  = _rxBuffer[1];
    uint8_t payLen = length - 3;
    uint8_t crcRx  = _rxBuffer[length - 1];

    // ---- Compute CRC ----
    uint8_t crc = 0;
    crc = crc8_update(crc, length);
    crc = crc8_update(crc, type);
    crc = crc8_update(crc, msgId);

    for (uint8_t i = 0; i < payLen; i++)
        crc = crc8_update(crc, _rxBuffer[2 + i]);

    if (crc != crcRx)
        return; // discard bad frame

    // ---- HELLO ----
    if (type == BUTCOM_MSG_HELLO && payLen >= 1) {
        _remoteId    = _rxBuffer[2];
        _hasRemoteId = true;
    }

    // ---- ACK ----
    if (type == BUTCOM_MSG_ACK) {
        if (_pending.active &&
            _pending.requiresAck &&
            _pending.msgId == msgId)
        {
            _pending.active = false;
        }
    }

    // ---- Duplicate check for DATA ----
    bool isDuplicate = false;
    if (type == BUTCOM_MSG_DATA) {
        if (msgId == _lastDataMsgId)
            isDuplicate = true;
        else
            _lastDataMsgId = msgId;
    }

    // ---- Auto-ACK (not for ACK frames!) ----
    if (type != BUTCOM_MSG_ACK)
        sendRawFrame(BUTCOM_MSG_ACK, msgId, nullptr, 0);

    if (isDuplicate)
        return;

    if (_callback) {
        const uint8_t* payloadPtr =
            (payLen > 0) ? &_rxBuffer[2] : nullptr;
        _callback(msgId, type, payloadPtr, payLen);
    }
}
