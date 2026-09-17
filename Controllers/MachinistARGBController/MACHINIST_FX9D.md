# Machinist F-X9D ARGB Controller

## Applicability

This document applies to the **MACHINIST F-X9D X99 motherboard** and its
onboard Winbond LED dongle. The implementation was validated with:

- Hardware vendor: MACHINIST
- Hardware model: F-X9D
- Firmware version: 5.11
- Firmware date: 2026-06-02
- USB VID: `0x0416`
- USB PID: `0x0125`
- HID usage page: `0xFF00` on Linux
- HID usage: `0x0001` on Linux

Other MACHINIST boards, firmware revisions, or controllers using the same VID/PID
are not confirmed to be compatible.

## Transport and Detection

The working lighting interface is a USB HID Winbond LED dongle, not the SMBus
controller initially observed at address `0x44`.

- Linux uses the libusb HID backend and `REGISTER_HID_WRAPPED_DETECTOR_PU`.
- Windows uses the wrapped HID detector with the Windows-reported interface and
  usage values.
- The wrapped detector is required so enumeration and subsequent open/write/close
  operations use the same HID backend.
- The device is write-only from OpenRGB's perspective; no state polling is used.

Normal effect reports are 64 bytes. The first 8 bytes are:

```text
03 CHANNEL EFFECT RED GREEN BLUE BRIGHTNESS SPEED
```

The remaining bytes are zero padding. Commands are sent to channels `0x01` and
`0x02`. The firmware interprets speed inversely; OpenRGB converts the UI speed
before transmission.

## Modes Exposed by OpenRGB

The controller exposes nine modes:

| Mode | Command | Controls |
|---|---:|---|
| Direct | `0x11` | Color, brightness |
| Static | `0x11` | Color, brightness |
| Breathing | `0x12` | Color, speed, brightness |
| Rainbow Wave | `0x17` | Color, speed, brightness |
| Spectrum Cycle | `0x14` | Color, speed, brightness |
| Rainbow | `0x1A` | Color, speed, brightness |
| Spring | `0x18` | Color, speed, brightness |
| Water | `0x19` | Color, speed, brightness |
| Music | `0x16` plus `0xC0` | Color, fixed maximum brightness |

The vendor's `0x15` Random command is intentionally not exposed. On the tested
F-X9D firmware 5.11 it was visually indistinguishable from Spectrum Cycle
(`0x14`). Strobe is also omitted because it maps to the same firmware command as
Breathing (`0x12`).

## Music Mode

Music mode is armed once with the normal `0x16` effect report. OpenRGB then sends
an 8-byte audio report every 100 ms, alternating the channel:

```text
01 C0 CHANNEL LEVEL LEVEL LEVEL 01 00
```

The three level bytes contain the same RMS loudness value. The controller performs
the visual color animation internally; OpenRGB does not calculate a true FFT.

- Linux: PulseAudio/PipeWire monitor capture through `libpulse-simple`.
- Windows: WASAPI shared-mode loopback of the default console render endpoint.
- Linux capture reads 512 float samples at 44.1 kHz and requests two such
  fragments to reduce silence-response latency.
- A failed Linux audio read clears all three levels before retrying.
- If capture initialization fails, Music remains available as the device's static
  built-in effect without starting the audio update thread.
- Switching away from Music stops the HID update thread and signals audio capture.
  A short residual display interval can remain because the last HID packet was
  sent at most one update period earlier and audio-server buffering varies.

## Build Requirements

### Linux

`libpulse-simple` is optional. When available to qmake, it enables Music audio
capture. A working PulseAudio/PipeWire compatibility layer and the `pactl`
command are required for system-output monitor selection.

### Windows

No third-party audio package is required. WASAPI support uses the Windows audio
session APIs and links the system `ole32` library. The normal OpenRGB MSVC and Qt
build environment is required.

## Validation

Validated on:

- Pop!_OS 24.04 LTS, x86-64
- Linux kernel `7.1.5-76070105-generic`
- Windows 11 LTSC 2024, x64
- MACHINIST F-X9D firmware 5.11

Static effects, mode transitions, Music mode, and system-audio reaction were
validated on Linux. Music mode was also validated on Windows with WASAPI loopback.
Other hardware revisions remain untested.

## Source Location

The implementation is in:

- `Controllers/MachinistARGBController/`
- `OpenRGB.pro` for platform audio dependencies and definitions
