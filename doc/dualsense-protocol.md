# DualSense Controller Protocol Reference

Technical reference extracted from [daidr/dualsense-tester](https://github.com/daidr/dualsense-tester). Covers DualSense, DualSense Edge, and DualShock 4 (v1/v2).

## 1. Device Identification

| Device | Vendor ID | Product ID |
|--------|-----------|------------|
| All Sony controllers | `0x054C` | — |
| DualShock 4 v1 | `0x054C` | `0x05C4` |
| DualShock 4 v2 | `0x054C` | `0x09CC` |
| DualSense | `0x054C` | `0x0CE6` |
| DualSense Edge | `0x054C` | `0x0DF2` |

HID usage page: `0x0001` (Generic Desktop), usage: `0x0005` (Game Pad).

### Connection Type Detection

DualSense uses HID descriptor inspection to distinguish USB from Bluetooth:

- **USB**: max input report byte count = **504 bits** (63 bytes)
- **Bluetooth**: max input report byte count = **616 bits** (77 bytes)

DualShock 4 uses the same 504-bit threshold for USB; anything else defaults to Bluetooth.

---

## 2. Report IDs

### DualSense

| Report ID | Direction | Type | Description |
|-----------|-----------|------|-------------|
| `0x02` | Output (USB) | Output | Main output report (rumble, LED, triggers, haptics) |
| `0x31` | Output (BT) | Output | Bluetooth output (same payload, wrapped with seq + CRC32) |
| `0x36` | Output (BT) | Output | Bluetooth audio + haptic stream |
| `0x20` | Feature | Feature | Firmware info read |
| `0x22` | Feature | Feature | BT patch info read |
| `0x80` | Feature | Feature | Send factory/test command |
| `0x81` | Feature | Feature | Receive factory/test result |
| `0x84` | Feature | Feature | Individual data verify start |
| `0x85` | Feature | Feature | Individual data verify result |

### DualShock 4

| Report ID | Direction | Type | Description |
|-----------|-----------|------|-------------|
| `0x05` | Output (USB) | Output | Main output report |
| `0x11` | Output (BT) | Output | Bluetooth output (with CRC32) |

### Bluetooth Microphone

| Report ID | Direction | Type | Description |
|-----------|-----------|------|-------------|
| `0x31` | Input (BT) | Input | Mic audio/control (shared with main input report) |
| `0x32` | Output (BT) | Output | Mic stream open/close control |

---

## 3. Input Report Structure (DualSense)

The input report is the main data stream from the controller. Offsets differ by one byte between USB and Bluetooth because Bluetooth prepends a report ID byte.

### Byte Layout

| Offset (USB) | Offset (BT) | Size | Field | Description |
|-------------|------------|------|-------|-------------|
| 0 | 1 | 1 | `analogStickLX` | Left stick X axis (0-255, center=128) |
| 1 | 2 | 1 | `analogStickLY` | Left stick Y axis (0-255, center=128) |
| 2 | 3 | 1 | `analogStickRX` | Right stick X axis (0-255, center=128) |
| 3 | 4 | 1 | `analogStickRY` | Right stick Y axis (0-255, center=128) |
| 4 | 5 | 1 | `analogTriggerL` | Left trigger analog (0-255) |
| 5 | 6 | 1 | `analogTriggerR` | Right trigger analog (0-255) |
| 6 | 7 | 1 | `sequenceNum` | Sequence number (0-255, wraps) |
| 7 | 8 | 3 | `digitalKeys` | Hat switch + buttons (see below) |
| 10 | 11 | 1 | — | Reserved |
| 11 | 12 | 4 | `incrementalNumber` | Incremental counter |
| 15 | 16 | 2 | `gyroPitch` | Gyroscope pitch (int16 LE, signed) |
| 17 | 18 | 2 | `gyroYaw` | Gyroscope yaw (int16 LE, signed) |
| 19 | 20 | 2 | `gyroRoll` | Gyroscope roll (int16 LE, signed) |
| 21 | 22 | 2 | `accelX` | Accelerometer X (int16 LE, signed) |
| 23 | 24 | 2 | `accelY` | Accelerometer Y (int16 LE, signed) |
| 25 | 26 | 2 | `accelZ` | Accelerometer Z (int16 LE, signed) |
| 27 | 28 | 4 | `motionTimeStamp` | Motion timestamp (uint32 LE) |
| 31 | 32 | 1 | `motionTemperature` | Motion sensor temperature |
| 32 | 33 | 9 | `touchData` | Touchpad data (see below) |
| 41 | 42 | 1 | `atStatus0` | Adaptive trigger status 0 |
| 42 | 43 | 1 | `atStatus1` | Adaptive trigger status 1 |
| 43 | 44 | 4 | `hostTimestamp` | Host timestamp (uint32 LE) |
| 47 | 48 | 1 | `atStatus2` | Adaptive trigger status 2 |
| 48 | 49 | 1 | `activeProfile` | Active profile (Edge only) |
| 49 | 50 | 1 | `triggerLevel` | Trigger level (Edge only) |
| 52 | 53 | 1 | `status0` | Battery/charge status |
| 53 | 54 | 1 | `status1` | Headphone/mic detect |
| 54 | 55 | 1 | `status2` | Status byte 2 |
| 55 | 56 | 16 | `aesCmac` | AES-CMAC authentication tag |
| — | 73 | 4 | `crc32` | CRC32 checksum (BT only) |

### DualSense Edge Differences

- Byte 48 (USB) / 49 (BT): `activeProfile` — low 2 bits = 0 indicates normal mode vs profile config mode
- Byte 49 (USB) / 50 (BT): `triggerLevel`
- Bluetooth `sequenceNum` is at offset 0 (not offset 1)

---

## 4. Digital Keys Bit Mapping

The `digitalKeys` field spans 3 bytes starting at offset 7 (USB) / 8 (BT).

### Byte 0 (keys)

| Bit | Mask | Button |
|-----|------|--------|
| 0-3 | `0x0F` | Hat switch / D-Pad (see D-Pad table) |
| 4 | `0x10` | Square |
| 5 | `0x20` | Cross |
| 6 | `0x40` | Circle |
| 7 | `0x80` | Triangle |

### Byte 1 (keys+1)

| Bit | Mask | Button |
|-----|------|--------|
| 0 | `0x01` | L1 |
| 1 | `0x02` | R1 |
| 2 | `0x04` | — |
| 3 | `0x08` | — |
| 4 | `0x10` | Create |
| 5 | `0x20` | Options |
| 6 | `0x40` | L3 (left stick press) |
| 7 | `0x80` | R3 (right stick press) |

### Byte 2 (keys+2)

| Bit | Mask | Button |
|-----|------|--------|
| 0 | `0x01` | PS |
| 1 | `0x02` | Touchpad press |
| 2 | `0x04` | Mic |

### D-Pad Hat Switch Values

| Value | Direction |
|-------|-----------|
| 0 | Up |
| 1 | Up-Right |
| 2 | Right |
| 3 | Down-Right |
| 4 | Down |
| 5 | Down-Left |
| 6 | Left |
| 7 | Up-Left |
| 8+ | Centered (no direction) |

---

## 5. Analog Stick Normalization

Raw values are 0-255 unsigned. Normalized to `[-1.0, +1.0]`:

```
normalized = (2 * raw) / 255 - 1.0
```

Center (128) maps to approximately `0.0`.

---

## 6. Touchpad Data

Touchpad data occupies 9 bytes starting at offset 32 (USB) / 33 (BT). Supports two touch points with a resolution of **1920 x 1080**.

### Touch Point 1 (bytes 0-3 of touchData)

| Byte | Bits | Field |
|------|------|-------|
| 0 | 7-0 | Touch ID (bit 7 = contact flag; ID = value & 0x7F) |
| 1 | 7-0 | X low 8 bits |
| 2 | 3-0 | X high 4 bits |
| 2 | 7-4 | Y low 4 bits |
| 3 | 7-0 | Y high 8 bits |

**Reconstruction:**
```
id   = touchData[0] & 0x7F
x    = ((touchData[2] & 0x0F) << 8) | touchData[1]
y    = (touchData[3] << 4) | (touchData[2] >> 4)
```

X range: 0-1920, Y range: 0-1080.

A touch ID < 128 indicates an active touch; the touch ID is used for tracking individual fingers across frames.

### Touch Point 2 (bytes 4-7 of touchData)

Same encoding as Point 1, starting at byte 4:
```
id   = touchData[4] & 0x7F
x    = ((touchData[6] & 0x0F) << 8) | touchData[5]
y    = (touchData[7] << 4) | (touchData[6] >> 4)
```

### Physical Dimensions

- Touchpad real width: 430 px
- Touchpad real height: 235 px
- Touchpad area on controller drawing: offset (340, 160)

---

## 7. Gyroscope and Accelerometer

Both gyroscope and accelerometer values are **signed 16-bit little-endian** integers read via `getInt16(offset, true)`.

| Field | Offset (USB) | Offset (BT) | Type |
|-------|-------------|------------|------|
| `gyroPitch` | 15 | 16 | int16 LE |
| `gyroYaw` | 17 | 18 | int16 LE |
| `gyroRoll` | 19 | 20 | int16 LE |
| `accelX` | 21 | 22 | int16 LE |
| `accelY` | 23 | 24 | int16 LE |
| `accelZ` | 25 | 26 | int16 LE |
| `motionTimeStamp` | 27 | 28 | uint32 LE |
| `motionTemperature` | 31 | 32 | uint8 |

The motion timestamp provides a timing reference for integrating angular velocity. The temperature field indicates the IMU sensor temperature.

---

## 8. Status Fields

### status0 (Battery & Charge)

| Bits | Field |
|------|-------|
| 3-0 | Battery level (0-10, see table) |
| 7-4 | Charge status (0-15, see table) |

**Battery Levels:**

| Value | Range |
|-------|-------|
| 0 | 0-9% |
| 1 | 10-19% |
| 2 | 20-29% |
| 3 | 30-39% |
| 4 | 40-49% |
| 5 | 50-59% |
| 6 | 60-69% |
| 7 | 70-79% |
| 8 | 80-89% |
| 9 | 90-99% |
| 10 | 100% |

**Charge Status:**

| Value | Status |
|-------|--------|
| 0 | Discharging |
| 1 | Charging |
| 2 | Charging complete |
| 10 | Abnormal voltage |
| 11 | Abnormal temperature |
| 15 | Charging error |

When charge status = 2 (complete), battery level is forced to 10 (100%).

### status1 (Headphone & Mic)

| Bit | Field |
|-----|-------|
| 0 | Headphone connected |
| 1 | Microphone connected |

---

## 9. Output Report Structure (DualSense)

The output report is sent to the controller to control rumble, LED, adaptive triggers, and haptics. 46-byte payload (report ID `0x02` for USB, `0x31` for BT).

### Byte Layout

| Offset | Field | Default | Description |
|--------|-------|---------|-------------|
| 0 | `validFlag0` | `0x00` | Bitmask: which fields to apply (see below) |
| 1 | `validFlag1` | `0xF7` | Bitmask for second set of fields |
| 2 | `bcVibrationRight` | `0x00` | Right motor vibration intensity (0-255) |
| 3 | `bcVibrationLeft` | `0x00` | Left motor vibration intensity (0-255) |
| 4 | `headphoneVolume` | `0x00` | Headphone volume |
| 5 | `speakerVolume` | `0x00` | Speaker volume |
| 6 | `micVolume` | `0x00` | Microphone volume |
| 7 | `audioControl` | `0x00` | Audio control flags |
| 8 | `muteLedControl` | `0x00` | Mute LED control |
| 9 | `powerSaveMuteControl` | `0x00` | Power save / mute control |
| 10 | `adaptiveTriggerRightMode` | `0x00` | Right trigger effect mode |
| 11-20 | `adaptiveTriggerRightParam0-9` | `0x00` | Right trigger effect params |
| 21 | `adaptiveTriggerLeftMode` | `0x00` | Left trigger effect mode |
| 22-31 | `adaptiveTriggerLeftParam0-9` | `0x00` | Left trigger effect params |
| 32-35 | Reserved | — | Reserved bytes |
| 36 | `hapticVolume` | `0x00` | Haptic feedback volume |
| 37 | `audioControl2` | `0x00` | Additional audio control |
| 38 | `validFlag2` | `0x00` | Third valid flag byte |
| 39 | Reserved | — | |
| 40 | `lightbarSetup` | `0x00` | Lightbar setup |
| 41 | `ledBrightness` | `0x00` | LED brightness |
| 42 | `playerIndicator` | `0x00` | Player LED bitmask |
| 43 | `ledCRed` | `0x00` | Lightbar red (0-255) |
| 44 | `ledCGreen` | `0xFF` | Lightbar green (0-255) |
| 45 | `ledCBlue` | `0x00` | Lightbar blue (0-255) |

### validFlag0 Bitmask

The valid flags control which sections of the output report the controller actually applies:

| Bit | Field |
|-----|-------|
| 5 | Speaker volume |
| 7 | Audio control / headphone volume |

### validFlag1 Bitmask

| Bit | Field |
|-----|-------|
| 1 | Mute LED |
| 3 | Player indicator |
| 4 | Lightbar color |
| 5 | Power save |

The default value `0xF7` enables most fields except vibration and trigger effects.

### Bluetooth Output Report Wrapping

For Bluetooth, the output report payload is wrapped:

```
Byte 0: (seq << 4) | 0x00     // sequence number in high nibble
Byte 1: 0x10                    // constant flag byte
Byte 2-77: original payload     // 46-byte output struct + padding
Byte 74-77: CRC32               // over [0xA2, 0x31] + bytes 0-73
```

Total: 78 bytes. CRC32 is computed over prefix `[0xA2, 0x31]` concatenated with bytes 0 through 73 of the report data.

---

## 10. Adaptive Trigger Effects

Each trigger (left/right) has a mode byte + 10 parameter bytes in the output report.

### Effect Modes

| Mode | Name | Parameters | Description |
|------|------|------------|-------------|
| `0x00` | Off | (none) | No effect |
| `0x01` | Resistance | `start_pos`, `force` | Constant resistance starting at position |
| `0x02` | Soft Trigger | `start_pos`, `end_pos`, `force` | Vibration zone between two positions |
| `0x06` | Auto Trigger | `start_pos`, `force`, `frequency` | Automatic vibration at position and frequency |

### Parameter Details

**Mode 0x01 — Resistance:**
- `Param0` (byte 0): `start_pos` (0-255, maps to physical trigger position)
- `Param1` (byte 1): `force` (0-255, resistance strength)

**Mode 0x02 — Soft Trigger:**
- `Param0` (byte 0): `start_pos` (0-255)
- `Param1` (byte 1): `end_pos` (0-255)
- `Param2` (byte 2): `force` (0-255)

**Mode 0x06 — Auto Trigger:**
- `Param0` (byte 0): `frequency` (0-15)
- `Param1` (byte 1): `force` (0-255)
- `Param2` (byte 2): `start_pos` (0-255)

Remaining parameter bytes (3-9) are reserved/zero.

---

## 11. Player LED / Lightbar

### Player LED Bitmask (byte 42 of output report)

| Value | Pattern |
|-------|---------|
| `0x00` | All off |
| `0x04` | Player 1 |
| `0x0A` | Player 2 |
| `0x15` | Player 3 |
| `0x1B` | Player 4 |
| `0x1F` | All players |

### LED Brightness (byte 41)

| Value | Level |
|-------|-------|
| 0 | High |
| 1 | Medium |
| 2 | Low |

### Lightbar RGB (bytes 43-45)

Three bytes for red, green, blue (0-255 each). Default: `(0, 255, 0)` = bright green.

### Mute Button LED Control (byte 8)

| Value | Mode |
|-------|------|
| 0 | Mic through (LED on when mic active) |
| 1 | Mic muted (LED on when muted) |
| 2 | All muted |

---

## 12. Bluetooth Audio Stream (Report 0x36)

Bluetooth audio uses report ID `0x36` with a 397-byte payload + 4-byte CRC32. The report contains three sub-packets: control, audio (Opus), and haptic (PCM).

### Report Layout (397 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | Sequence (high nibble) + flags (low nibble, always 0) |
| 1-65 | 65 | Control sub-packet |
| 66-276 | 211 | Audio sub-packet (Opus) |
| 277-392 | 116 | Haptic sub-packet (PCM) |
| 393-396 | 4 | CRC32 (prefix `[0xA2, 0x36]`) |

### Control Sub-Packet (65 bytes, offset 1)

| Offset | Field |
|--------|-------|
| 0 | `0x90` marker |
| 1 | Valid flags |
| 6 | Volume / audio target |
| 10 | `audioControl` (0x09) |

### Audio Sub-Packet (211 bytes, offset 66)

| Offset | Field |
|--------|-------|
| 0 | `0x91` marker |
| 1 | `0x07` |
| 2-7 | Delay/mixing parameters |
| 8 | Frame counter (incrementing) |
| 9 | Output route tag: `0x93` = speaker, `0x96` = headphone |
| 10 | `0xC8` (= 200, Opus frame length) |
| 11-210 | 200 bytes Opus encoded audio |

### Haptic Sub-Packet (116 bytes, offset 277)

| Offset | Field |
|--------|-------|
| 0 | `0x92` marker |
| 1 | `0x40` (= 64, PCM block length) |
| 2-65 | 64 bytes: 32 samples x 2 channels (L/R interleaved, int8) |

### Opus Encoding Parameters

| Parameter | Value |
|-----------|-------|
| Sample rate | 48000 Hz (nominal; effective ~45000 Hz) |
| Channels | 2 (stereo) |
| Bitrate | 160 kbps CBR |
| Frame duration | 10 ms |
| Application | `lowdelay` (pure CELT) |
| Frame size | 200 bytes (fixed) |

### Haptic Audio Extraction

Audio is resampled to **3000 Hz** for haptic processing, then converted to signed 8-bit integers:

```
int8_value = clamp(round(sample * 127), -128, 127) & 0xFF
```

Each haptic frame = 32 samples per channel, L/R interleaved: `[L0, R0, L1, R1, ..., L31, R31]` = 64 bytes.

---

## 13. Bluetooth Microphone Protocol

### Input Report (Report ID 0x31)

The first byte's low nibble determines the payload type:

| Payload Type | Value | Description |
|-------------|-------|-------------|
| Control | `0x01` | Microphone state and control |
| Audio | `0x02` | Opus-encoded microphone audio |

**Control payload:**
- Byte 10, bit 2: Mute button pressed
- Byte 54, bit 1: Headset microphone plugged in

**Audio payload:**
- Bytes 2-72: 71-byte Opus frame

### Output Reports

**Mic State Report (report ID `0x31`, 77 bytes):**
Controls mic volume, mute LED, and audio routing.

| Offset | Field |
|--------|-------|
| 0 | `(seq << 4)` |
| 1 | `0x10` |
| 2 | `0xC0` — mic volume + audio control |
| 3 | `0x83` — mute LED + power save + audio control 2 |
| 8 | `0x08` if active and unmuted, else `0x00` |
| 9 | `0x08` if headset mic plugged, else `0x09` |
| 10 | `0x01` if muted, else `0x00` |
| 11 | `0x0F` if active and unmuted, else `0x1F` |
| 39 | `0x01` |
| 73-76 | CRC32 |

**Mic Control Report (report ID `0x32`, 141 bytes):**
Opens or closes the Opus microphone stream.

| Offset | Field |
|--------|-------|
| 0 | `(seq << 4)` |
| 1 | `0x91` |
| 2 | `0x07` |
| 3 | `0xFF` (active) / `0xFE` (inactive) |
| 4-8 | `0x40` padding |
| 9 | Sequence low nibble |
| 10 | `0x92` |
| 11 | `0x40` |
| 137-140 | CRC32 |

---

## 14. CRC32 Checksums

### Polynomial

Standard CRC32 with polynomial `0xEDB88320` (reflected).

### Computation

Initial value: `0xFFFFFFFF`. Final XOR: `0xFFFFFFFF` (i.e., `crc ^ -1`).

### Output Report Checksum (Bluetooth)

Prefix bytes: `[0xA2, report_id]`. CRC is computed over the prefix + all report data bytes except the last 4 (which hold the CRC itself).

### Feature Report Checksum (Bluetooth)

Prefix bytes: `[0x53, report_id]`. Same algorithm.

### DualShock 4 Feature Report Checksum

Prefix bytes: `[0xA3, report_id]`.

### DualSense Edge Profile Checksum

CRC is computed over 170 bytes assembled from 3 profile report buffers (58 bytes each, with headers stripped). Written to the last 4 bytes of the third buffer.

---

## 15. Feature Report Commands (Factory/Test)

Factory commands are sent via feature report `0x80` and results read via `0x81`.

### Command Format

**Send (feature report `0x80`):**
| Byte | Field |
|------|-------|
| 0 | Device ID (`DualSenseTestDeviceId`) |
| 1 | Action ID (`DualSenseTestActionId`) |
| 2+ | Optional parameters |

**Receive (feature report `0x81`):**
| Byte | Field |
|------|-------|
| 0 | Report ID (`0x81`) |
| 1 | Device ID (echo) |
| 2 | Action ID (echo) |
| 3 | Status (0=idle, 1=running, 2=complete, 3=complete2, 255=timeout) |
| 4-59 | Result data (56 bytes per page) |

### Device IDs

| ID | Name |
|----|------|
| 1 | SYSTEM |
| 2 | POWER |
| 3 | MEMORY |
| 4 | ANALOG_DATA |
| 5 | TOUCH |
| 6 | AUDIO |
| 7 | ADAPTIVE_TRIGGER |
| 8 | BULLET |
| 9 | BLUETOOTH |
| 10 | MOTION |
| 11 | TRIGGER |
| 12 | STICK |
| 13 | LED |
| 14 | BT_PATCH |
| 15 | DSP_FW |
| 16 | SPIDER_DSP_FW |
| 17 | FINGER |
| 19 | POSITION_TRACKING |
| 20 | BUILTIN_MIC_CALIB_DATA |
| `0x70` | TELEMETRY |

### Common Commands

| Device | Action | Name | Result Size |
|--------|--------|------|-------------|
| SYSTEM (1) | READ_PCBAID (4) | Read PCBA ID | 6 bytes |
| SYSTEM (1) | READ_PCBAID_FULL (16) | Read full PCBA ID | 24 bytes |
| SYSTEM (1) | READ_SERIAL_NUMBER (19) | Read serial number | 32 bytes |
| SYSTEM (1) | READ_ASSEMBLE_PARTS_INFO (21) | Assembly parts info | 32 bytes |
| SYSTEM (1) | READ_BATTERY_BARCODE (24) | Battery barcode | 32 bytes |
| SYSTEM (1) | READ_VCM_LEFT_BARCODE (26) | Left VCM barcode | 32 bytes |
| SYSTEM (1) | READ_VCM_RIGHT_BARCODE (28) | Right VCM barcode | 32 bytes |
| SYSTEM (1) | GET_MCU_UNIQUE_ID (9) | MCU unique ID | 9 bytes |
| BLUETOOTH (9) | READ_BDADR (2) | BD MAC address | 6 bytes |
| ANALOG_DATA (4) | BATTERY (3) | Battery voltage | 4 bytes (uint16 LE mV) |
| TOUCH (5) | SOLOMON_UID (2) | Touchpad UID | 8 bytes |
| TOUCH (5) | SOLOMON_VERSION (4) | Touchpad FW version | 8 bytes |
| AUDIO (6) | WAVEOUT_CTRL (2) | Audio output control | — |
| TELEMETRY (0x70) | GET_INFO (1) | Telemetry block | 56*N bytes (paged) |

### Firmware Info (Feature Report 0x20)

Read via `receiveFeatureReport(0x20)`. Contains:
- Build date/time
- FW type, SW series, HW info
- Main FW version, SBL FW version, DSP FW version, Spider DSP FW version
- Update version
- PCBA ID (6 bytes)
- Unique ID (8 bytes)
- BD MAC address (6 bytes)
- Serial number (32 bytes)
- Assembly parts info (32 bytes)
- Battery barcode, VCM left/right barcodes (32 bytes each)
- Individual data verify status

---

## 16. DualSense Edge Profile Protocol

### Profile Button Mapping

| Button | ID |
|--------|-----|
| Square | `0x60` |
| Cross | `0x61` |
| Circle | `0x62` |
| Triangle | `0x63` |

### Button Remap Enum

UP, LEFT, DOWN, RIGHT, CIRCLE, CROSS, SQUARE, TRIANGLE, R1, R2, R3, L1, L2, L3, PADDLE_LEFT, PADDLE_RIGHT, Options, Touchpad.

### Profile Data

Profiles are read/written via 3 x 64-byte feature report buffers. Profile data includes:

- **Joystick sensitivity curves**: 7 presets (DEFAULT, QUICK, PRECISE, STEADY, DIGITAL, DYNAMIC, CUSTOM) with point arrays and deadzone support
- **Trigger deadzone**: Per-trigger min/max (0-255 mapped to 0-100%)
- **Vibration intensity**: Off, Weak, Medium, Strong
- **Trigger effect intensity**: Off, Weak, Medium, Strong
- **Disabled buttons bitmap**: 32-bit field at buffer2 offset 26-29; factory default disables both back paddles (`0x00C00000`)

### Profile Serialization

Profiles can be exported/imported as JSON with Base64-encoded raw DataView objects.

---

## 17. DualShock 4 Differences

### Input Report Offsets

DualShock 4 uses different base offsets:
- USB: base offset +0
- Bluetooth: base offset +2

| Offset (USB) | Offset (BT) | Field |
|-------------|------------|-------|
| 0 | 2 | `analogStickLX` |
| 4 | 6 | `digitalKeys` (3 bytes) |
| 6 | 8 | `sequenceNum` |
| 7 | 9 | `analogTriggerL` |
| 9 | 11 | `motionTimeStamp` |
| 12 | 14 | `gyroPitch` (int16 LE) |
| 18 | 20 | `accelX` (int16 LE) |
| 29 | 31 | `status` |
| 34 | 36 | `touchData` |
| — | 73 | `crc32` (BT only) |

### Output Report (Report ID `0x05` USB / `0x11` BT)

73-byte payload:

| Offset | Field | Default |
|--------|-------|---------|
| 0 | `hwControl` | `0xC4` |
| 1 | `audioControl` | — |
| 2-3 | `validFlag0`, `validFlag1` | — |
| 4 | Reserved | — |
| 5 | `motorRight` | Rumble right (0-255) |
| 6 | `motorLeft` | Rumble left (0-255) |
| 7 | `ledRed` | LED red |
| 8 | `ledGreen` | LED green |
| 9 | `ledBlue` | LED blue |
| 10-11 | `ledBlinkOn`, `ledBlinkOff` | LED blink on/off duration |

---

## 18. Telemetry Data

Telemetry is read via device `0x70` (TELEMETRY), action `0x01` (GET_INFO), as paged 56-byte blocks.

### Common Fields (Page 0)

- Serial number
- Total record count
- Active time, charge time
- Haptic device time, trigger device time
- Battery charge count
- USB detect count
- Bluetooth connect count
- Auth count

### DualSense Standard (4 Pages)

Additional: stick round-trip counts, button press counts for all 20 buttons.

### DualSense Edge (6 Pages)

Additional: trigger stroke counts (short/middle/full for L2/R2), back paddle counts, per-stick-module serial numbers, separate round-trip/button/function-button counts.

---

## 19. Audio Output Control

### Factory Wave Out

Audio output can be controlled via factory test commands:

- **Headphone**: Enable with params `[1, 0, 0, 0, 4, 0, 6, ...]`
- **Speaker**: Enable with params `[1, 0, 0, 0, 0, 0, 8, ...]`

### USB Audio Routing

USB audio uses the controller's built-in soundcard:
- Channels 0-1: Speaker output
- Channels 2-3: Haptic motor output (left/right)

---

## 20. Reference Sources

- [daidr/dualsense-tester](https://github.com/daidr/dualsense-tester) — browser-based DualSense testing tool
- [Ohjurot/DualSense-Windows](https://github.com/Ohjurot/DualSense-Windows) — Windows DualSense driver
- [flok/pydualsense](https://github.com/flok/pydualsense/) — Python DualSense library
- [nondebug/dualsense](https://github.com/nondebug/dualsense) — DualSense HID implementation
