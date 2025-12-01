#pragma once
#include <Arduino.h>

/* ============================================================
   ButCom - Lightweight 1-wire communication protocol
   ------------------------------------------------------------
   Supports:
   - Start/Stop bit UART-like framing
   - HELLO handshake (device discovery)
   - CRC-8 validation
   - Automatic ACK & retry logic
   - Duplicate filter for DATA messages
   - Configurable line speed (1..4 quality)
   - One-device-per-bus architecture
   ============================================================ */

// ----------- Message Types -----------
#define BUTCOM_MSG_HELLO 0
#define BUTCOM_MSG_DATA  1
#define BUTCOM_MSG_ACK   2

// Maximum bytes per frame payload
#define BUTCOM_MAX_PAYLOAD 16

// User callback type
typedef void (*ButComCallback)(
    uint8_t msgId,
    uint8_t type,
    const uint8_t* payload,
    uint8_t length
);

/* ============================================================
   ButComPhy  (Physical Layer)
   ------------------------------------------------------------
   Implements single-wire bit-banged half-duplex protocol.
   Timing is adjustable via setBitTimeUs() for long/short cables.
   ============================================================ */
class ButComPhy {
public:
    ButComPhy(uint8_t pin, bool useInternalPullup = false);

    void begin();
    void setBitTimeUs(uint16_t bitUs);

    void sendByte(uint8_t value);                        // transmit one byte
    bool receiveByte(uint8_t& out, uint32_t timeoutMs);  // receive one byte

private:
    uint8_t _pin;
    bool    _usePullup;

    uint16_t _bitUs;
    uint16_t _halfBitUs;
    uint32_t _idleMinUs;

    void driveLow();
    void releaseLine();
    void waitIdle();
};

/* ============================================================
   ButCom (Logical Layer)
   ------------------------------------------------------------
   Handles:
   - HELLO handshake
   - CRC-8
   - Frame assembly & parsing
   - ACK & retry mechanism
   - Duplicate DATA filtering
   - Periodic HELLO resync
   ============================================================ */
class ButCom {
public:
    ButCom(uint8_t pin, bool internalPullup, uint8_t deviceId);

    void begin(bool sendHelloOnStart = true);
    void loop();

    // Send payload. Returns message ID used.
    // If requestAck=true → ButCom handles retries automatically.
    uint8_t send(const uint8_t* payload, uint8_t length, bool requestAck);

    // Optional configuration
    void setCallback(ButComCallback cb) { _callback = cb; }
    void setAckTimeout(uint16_t ms)     { _ackTimeoutMs = ms; }
    void setMaxRetries(uint8_t r)       { _maxRetries = r; }
    void setHelloInterval(uint32_t ms)  { _helloIntervalMs = ms; }

    // Speed Quality: 1=fast, 4=slow/robust
    void setSpeedQuality(uint8_t quality);

    // Device identity
    uint8_t id() const          { return _id; }
    bool    hasRemoteId() const { return _hasRemoteId; }
    uint8_t remoteId() const    { return _remoteId; }

private:
    // ----------- Frame Parsing State -----------
    enum RxState {
        RX_WAIT_START,
        RX_WAIT_LENGTH,
        RX_READ_BODY
    };

    struct PendingTx {
        bool active;
        bool requiresAck;
        uint8_t type;
        uint8_t msgId;
        uint8_t payload[BUTCOM_MAX_PAYLOAD];
        uint8_t length;
        uint8_t retries;
        uint32_t lastSendMs;
    };

    // Internal helpers
    void sendHello();
    void handleReceivedByte(uint8_t b);
    void processFrame(uint8_t bodyLength);

    void sendRawFrame(uint8_t type,
                      uint8_t msgId,
                      const uint8_t* payload,
                      uint8_t length);

    static uint8_t crc8_update(uint8_t crc, uint8_t data);

    // ----------- Members -----------
    ButComPhy _phy;
    uint8_t   _id;
    uint8_t   _remoteId;
    bool      _hasRemoteId;

    ButComCallback _callback;

    // RX state machine
    RxState  _rxState;
    uint8_t  _rxExpectedLength;
    uint8_t  _rxBuffer[2 + BUTCOM_MAX_PAYLOAD + 1];
    uint8_t  _rxIndex;

    uint8_t  _lastDataMsgId;

    // TX retry
    PendingTx _pending;
    uint16_t  _ackTimeoutMs;
    uint8_t   _maxRetries;

    // HELLO interval
    uint32_t _lastHelloMs;
    uint32_t _helloIntervalMs;

    uint8_t  _nextMsgId;
};
