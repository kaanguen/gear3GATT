# BLE wire protocol, version 1

All custom UUIDs are fixed and little-endian fields are used unless stated otherwise.

| Purpose | UUID | Properties |
|---|---|---|
| Sensor Bridge service | `7b3a0001-6d2b-4a4e-9f12-8f4d2c7a1000` | Primary |
| Motion frame | `7b3a0002-6d2b-4a4e-9f12-8f4d2c7a1000` | Notify |
| Heart-rate metadata | `7b3a0003-6d2b-4a4e-9f12-8f4d2c7a1000` | Notify |
| Control command | `7b3a0004-6d2b-4a4e-9f12-8f4d2c7a1000` | Write |
| Status | `7b3a0005-6d2b-4a4e-9f12-8f4d2c7a1000` | Read, Notify |
| Standard Heart Rate service | `0x180D` | Primary |
| Standard Heart Rate Measurement | `0x2A37` | Notify |

## Motion frame (20 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Protocol version, `0x01` |
| 1 | 1 | Validity flags: bit 0 accelerometer valid, bit 1 gyroscope valid; other bits zero |
| 2 | 2 | Motion sequence, unsigned 16-bit |
| 4 | 4 | Callback receipt timestamp, unsigned 32-bit milliseconds on the watch's monotonic clock |
| 8 | 6 | Acceleration X/Y/Z, signed 16-bit each, milli-g |
| 14 | 6 | Angular rate X/Y/Z, signed 16-bit each, milli-degrees/second |

Values are signed little-endian two's-complement. A field whose validity bit is clear MUST be ignored; its encoded value is not a measurement. The motion sequence increments for each successfully queued notification and wraps modulo 65536. The timestamp uses the oldest still-valid contributing sensor callback receipt time (or packet time if neither vector is valid); it is monotonic, not wall-clock time, and wraps modulo 2^32 milliseconds. Consumers should compare adjacent values using unsigned modular arithmetic.

The frame is exactly 20 bytes and therefore fits the ATT value limit when MTU is 23. This version never sends a larger value and does not require MTU negotiation.

## Heart rate

The standard `0x180D/0x2A37` Heart Rate Measurement is preferred for broad client compatibility. Version 1 uses the standard 8-bit BPM form: flags byte `0x00`, followed by BPM byte. It is sent only after a valid new heart-rate sample.

The standard characteristic has no sequence or timestamp fields. The companion custom Heart-rate metadata notification preserves them:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Protocol version, `0x01` |
| 1 | 1 | Validity flags: bit 0 heart rate valid |
| 2 | 2 | Heart-rate sequence, unsigned 16-bit |
| 4 | 4 | Callback receipt timestamp, unsigned 32-bit monotonic milliseconds |
| 8 | 2 | BPM, unsigned 16-bit |

The custom metadata notification is sent for a new HR event, including an invalid event with bit 0 clear and BPM zero. The standard measurement is suppressed for invalid events. The implemented valid BPM range is 1–255. Its sequence increments for each successfully queued metadata notification and wraps modulo 65536. A clear validity bit means the BPM field must not be interpreted as a measurement.

## Commands (Write, 4 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Protocol version, `0x01` |
| 1 | 1 | Opcode: `0x01` start, `0x02` stop, `0x03` set motion rate |
| 2 | 2 | Rate in Hz, unsigned 16-bit little-endian; used only by opcode `0x03` |

Start and stop commands require a zero rate. Rate commands accept 1–20 Hz; other values are rejected and reported as an invalid-command error. A valid rate change affects transmission cadence, not the sensor's native callback cadence. The initial transmit rate is 20 Hz.

## Status (8 bytes)

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Protocol version, `0x01` |
| 1 | 1 | State bitfield: bit 0 measuring, bit 1 BLE connected, bit 2 transmitting, bit 3 accelerometer available, bit 4 gyroscope available, bit 5 heart-rate sensor available |
| 2 | 1 | Error code: `0` none, `1` Bluetooth unavailable, `2` permission denied, `3` sensor unavailable, `4` invalid command, `5` notification failure, `6` advertising/peripheral failure, `7` application runtime resource failure, `8` BLE/GATT server unsupported |
| 3 | 1 | Reserved, zero |
| 4 | 2 | Current motion transmit rate, unsigned 16-bit little-endian |
| 6 | 2 | Most recently transmitted motion sequence, unsigned 16-bit little-endian |

The transmitting flag means a sensor notification completed successfully in the local Bluetooth stack in the last 2000 ms while subscribed and connected; it does not acknowledge receipt by the client.

Status can be read while the foreground GATT server is active. When a client has enabled notifications, it is also notified on state/error changes.

## Units and freshness

Acceleration is converted to milli-g and angular rate to milli-degrees/second using the Tizen sensor values' documented units. Values are rounded and saturated to signed 16-bit range. Invalid, unavailable, or stale readings are never marked valid. Timestamps originate at the sensor callback and use the device monotonic timebase; no clock synchronization with the client is implied.


## Receiver and lifecycle rules

Subscribe to the required Notify characteristics (CCCD) after every connection.
Use acknowledged Write requests for commands; malformed versions, lengths, or
values are rejected and status error 4 is set. A successful ATT Write means the
command was syntactically accepted. Read Status to determine whether sensor
startup succeeded. Never treat Write success as proof that a sensor is running.
All packets remain at most 20 bytes regardless of a larger negotiated MTU.

A sequence describes a locally queued packet, not a confirmed receiver delivery.
Sequence counters persist across local stop/start within an app process and reset
to zero on process restart. Reconnect alone does not establish a new time origin.
Time is CLOCK_MONOTONIC at callback reception, modulo 2^32 ms (about 49.71 days),
not UTC and not the hardware timestamp from sensor_event_s. The client must start
a new stream interpretation after reconnect because process restart cannot be
identified unambiguously with this v1 frame. No clock synchronization is provided.

Motion vectors use SENSOR_AXIS_DEVICE_ORIENTED; no Unity coordinate conversion
is performed. The exact physical axis orientation must be checked on the R760.
Acceleration and angular velocity callbacks are independent and not synchronized.
A missing/NaN/infinite/accuracy-BAD event invalidates the corresponding vector.
Unknown accuracy is accepted as uncalibrated/unspecified quality, not certified
accuracy. Freshness limits are 250 ms for motion and 2000 ms for heart rate.

The v1 angular-rate field spans only -32.768 to +32.767 degrees/second because
it uses int16 milli-degrees/second. Values exceeding that range are encoded with
the vector validity bit clear; they must not be used as real angular rates.
This v1 precision/range tradeoff may require a separately versioned protocol
revision before use for fast hand movements. Acceleration spans approximately
-32.768 to +32.767 g, subject to the sensor's narrower actual range.

Only latest values are retained. If acquisition is faster than transmission,
intermediate callbacks can be coalesced; this is not a lossless recording format.
The motion timer defaults to 20 Hz, while HR uses its sensor's actual callback
cadence. Each characteristic allows one outstanding notification; busy cycles
are skipped rather than accumulated without bound. No notification ACK or replay
is implemented. The application is intended for one central peer.

App pause releases the GATT server and listeners. Resume advertises again but
requires an explicit new Start command/button. There is no background service,
phone bridge, cloud, or automatic alternative transport.
