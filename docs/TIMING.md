# ButCom Timing Notes

This document provides additional timing-related details and recommendations for ButCom.

## Bit Timing

The fundamental timing unit is the **bit time**, `bitUs`, in microseconds.  
It is configured either directly via:

```cpp
bus.setSpeedQuality(level);  // 1..4
```

or internally mapped to bit times as:

| Quality | bitUs (approx) |
|---------|----------------|
| 1       | 300 µs         |
| 2       | 500 µs         |
| 3       | 800 µs         |
| 4       | 1200 µs        |

A longer bit time:

- Reduces the required CPU timing accuracy
- Increases tolerance for cable capacitance and noise
- Decreases maximum throughput

---

## Idle Detection

Before sending a byte, ButCom:

1. Ensures the line is HIGH
2. Measures how long it stays HIGH
3. Waits until the line has been HIGH for at least `3 * bitUs`

This avoids collisions between frames and ensures that `START` is clearly distinguishable from line noise.

---

## Start Bit Detection

The receiver:

1. Waits for the line to be HIGH
2. Watches for a falling edge (HIGH → LOW)
3. Applies a short delay (half-bit glitch filter)
4. Confirms the line is still LOW → valid start bit

Then it schedules the first sample in the **middle** of the first data bit:

```text
sampleTime = startEdgeTime + bitUs + (bitUs / 2);
```

Each subsequent bit is sampled every `bitUs` from there.

---

## Recommended Cable Lengths

The actual maximum cable length depends heavily on:

- Cable type (twisted pair vs. random wire)
- Environment (EMI, ground loops)
- Pull-up strength
- Supply voltage and MCU input thresholds

As a rough guideline for 3.3 V systems with 4.7 kΩ pull-up:

- Quality 1 (300 µs): ~0.5–1 m (very clean, short)
- Quality 2 (500 µs): ~1–2 m
- Quality 3 (800 µs): ~2–5 m
- Quality 4 (1200 µs): ~3–10 m

These are conservative estimates and should be validated in your real setup.

---

## Throughput Estimation

Each byte on the wire:

- 1 start bit
- 8 data bits
- 1 stop bit

→ **10 bit times per byte**

Each ButCom frame:

- 1 START byte
- 1 LEN byte
- 1 TYPE byte
- 1 MSGID byte
- N PAYLOAD bytes
- 1 CRC byte

→ **(5 + N)** bytes

Total bit time for one frame ≈ `(5 + N) * 10 * bitUs`.

Example:  
`N = 4`, `bitUs = 500 µs` → `(5 + 4) * 10 * 500 µs = 9 * 10 * 500 µs = 45 ms` per frame.

---

## ACK and Retries

When `requestAck=true` in `send()`:

- Sender transmits a DATA frame.
- Receiver responds with an ACK frame using the same MSGID.
- If no ACK is seen within `_ackTimeoutMs`, the sender retries up to `_maxRetries` times.

The timeout is automatically scaled according to `bitUs`, but can be overridden:

```cpp
bus.setAckTimeout(80);   // milliseconds
bus.setMaxRetries(3);
```

---

## Recommendations

- Always call `bus.loop()` frequently (e.g. every few milliseconds).
- Use quality 2 for short cables and normal conditions.
- Move to quality 3 or 4 if you see CRC errors or missing ACKs on longer cables.
- Keep payloads small when using long cables or noisy environments.
