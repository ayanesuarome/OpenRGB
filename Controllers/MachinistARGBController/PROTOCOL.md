# Machinist F-X9D ARGB Controller - Protocol Documentation

## Hardware Overview

**Device**: Machinist F-X9D X99 Motherboard ARGB Controller (Winbond LED Dongle)  
**VID**: 0x0416 (Winbond Electronics)  
**PID**: 0x0125  
**Interface**: USB HID  
**Usage Page**: 0xFF00  
**Usage**: 0x0001  

## USB Protocol

### Packet Format

All commands use an 8-byte HID interrupt transfer format:

```
[0]   = 0x03          Report ID (fixed)
[1]   = 0x01 or 0x02  Channel (must be sent to both channels)
[2]   = Effect ID     Subcommand identifying the effect/mode
[3]   = Red           Color value (0x00-0xFF)
[4]   = Green         Color value (0x00-0xFF)
[5]   = Blue          Color value (0x00-0xFF)
[6]   = Brightness    Brightness/intensity (0x00-0xFF)
[7]   = Speed Param   Effect-specific parameter, speed control
[8-63] = 0x00         Padding (total buffer: 64 bytes)
```

**Key Notes:**
- Each command must be sent **twice** (once per channel: 0x01, then 0x02)
- Report ID is always 0x03
- Channels are sent sequentially, not simultaneously
- Brightness applies to all modes (effects and static)
- Speed parameter is **inverted** on firmware: 0xFF = slowest, 0x00 = fastest
  - OpenRGB inverts before sending: `packet_speed = 0xFF - ui_speed`

### Supported Effects (Subcommands)

| Effect ID | Mode Name | Type | Parameters | Availability |
|-----------|-----------|------|-----------|---|
| 0x11 | Static | Direct Color | Brightness only | ✅ Implemented |
| 0x12 | Breathing | Animation | Speed, Brightness | ✅ Implemented |
| 0x17 | Wave | Animation | Speed, Brightness | ✅ Implemented (matches capture order) |
| 0x14 | Cycling | Animation | Speed, Brightness | ✅ Implemented (matches capture order) |
| 0x1A | Rainbow | Animation | Speed, Brightness | ✅ Implemented (matches capture order) |
| 0x15 | Random | Animation | Speed, Brightness | ✅ Implemented (matches capture order) |
| 0x18 | Spring | Animation | Speed, Brightness | ✅ Implemented (matches capture order) |
| 0x19 | Water | Animation | Speed, Brightness | ✅ Implemented (matches capture order) |
| 0x16 | Music | Reactive | Brightness (speed unclear) | ✅ Implemented (matches capture order) |

**Mapping methodology:** `machinist_all_options.txt` contains a single sample per mode
(except Breathing, which has 37 samples sweeping speed). Modes were mapped by matching
the capture's packet order to the tested mode order from the capture notes:
Static, Breathing, Strobe, Wave, Cycling, Rainbow, Random, Spring, Water, Music.

**Strobe removed:** The vendor's own saved profile format (`WAVE.orp`) independently
stores both `Breathing` and `Strobe` under mode ID `0x12`, matching the USB capture.
Since the vendor app confirmed identical behavior on hardware, `Strobe` was removed
as a redundant duplicate of `Breathing` rather than kept as an alias.

### Example Packets

**Static Red (max brightness)**
```
03 01 11 FF 00 00 FF 00
03 02 11 FF 00 00 FF 00
```

**Breathing White (medium speed)**
```
03 01 12 FF FF FF FF 7F
03 02 12 FF FF FF FF 7F
```

**Wave Green (fast, half brightness)**
```
03 01 14 00 FF 00 80 10
03 02 14 00 FF 00 80 10
```

## Implementation

### Files

- **MachinistARGBController.h/cpp** - Low-level USB HID communication
- **RGBController_MachinistARGB.h/cpp** - OpenRGB RGB controller abstraction
- **MachinistARGBControllerDetect.cpp** - Device detection and registration

### Color Mode

Single color ARGB header (1 LED logical representation):
- All colors fade/pulse/animate together
- Speed and brightness are independent controls
- Color selection applies to all LEDs on the header

### Supported OpenRGB Modes

```
Mode 0: Direct
  - Per-LED color control
  - Brightness slider (0x00-0xFF)
  - Static display (no animation)

Mode 1: Static
  - Per-LED color control
  - Brightness slider (0x00-0xFF)
  - Static display (no animation)

Mode 2: Breathing
  - Per-LED color control
  - Speed slider (0x00-0xFF, inverted on device)
  - Brightness slider (0x00-0xFF)
  - Pulsing fade in/out animation

Mode 3: Wave
  - Per-LED color control
  - Speed slider (0x00-0xFF, inverted on device)
  - Brightness slider (0x00-0xFF)
  - Wave sweep animation

Mode 4: Cycling
  - Per-LED color control
  - Speed slider (0x00-0xFF, inverted on device)
  - Brightness slider (0x00-0xFF)
  - Color cycling animation

Mode 5: Rainbow
  - Per-LED color control
  - Speed slider (0x00-0xFF, inverted on device)
  - Brightness slider (0x00-0xFF)

Mode 6: Random
  - Per-LED color control
  - Speed slider (0x00-0xFF, inverted on device)
  - Brightness slider (0x00-0xFF)

Mode 7: Spring
  - Per-LED color control
  - Speed slider (0x00-0xFF, inverted on device)
  - Brightness slider (0x00-0xFF)

Mode 8: Water
  - Per-LED color control
  - Speed slider (0x00-0xFF, inverted on device)
  - Brightness slider (0x00-0xFF)

Mode 9: Music
  - Per-LED color control
  - Brightness slider (0x00-0xFF)
  - Device behavior may appear static depending on firmware/audio-reactive state
```

## Device Detection

The Machinist controller **only enumerates via the libusb HID backend** on Linux. 
It is **not visible** to the static hidraw backend.

**Solution**: Uses `REGISTER_HID_WRAPPED_DETECTOR_PU` with libusb wrapper, ensuring consistent backend usage for all HID I/O operations (open/write/close).

## USB Traffic Analysis

Protocol verified against captured USB traffic (`machinist_all_options.txt`):
- 37 samples of Breathing mode (0x12) with varying speed parameters
- 1 sample each of subcommands 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A (mapping refined with hardware validation)
- Consistent packet structure across all effects
- Speed parameter range: 0x00-0xFF (with firmware inversion)

Mode naming and ID mapping were cross-checked against the saved profile file `WAVE.orp` (contains mode labels and mode IDs).

## Limitations

1. **Single color**: Device is monochrome ARGB header, not addressable LEDs
2. **No profiles**: Device doesn't support saving profiles to internal memory
3. **No polling**: Device doesn't report current state; all control is write-only
4. **Channel parity**: Both channels must receive identical commands
5. **Speed inversion**: Firmware interprets speed backwards (0=fast, 0xFF=slow)

## Future Work

- Validate whether CPU/AP modes from vendor UI require non-HID or additional commands
- Test on Windows platform compatibility
- Explore any additional parameters in extended packet formats
- Profile manager integration (if supported by firmware)
