# Machinist F-X9D ARGB - Audio-Reactive Music Mode

## Overview

The Machinist F-X9D ARGB controller supports an audio-reactive effect through the
`0xC0` HID command. OpenRGB activates the controller's Music effect (`0x16`) and
then sends the current system-output audio level periodically.

The controller performs the visual animation internally. OpenRGB supplies one
overall audio-energy value rather than calculating independent frequency bands.

## Current State

- Basic Music mode (`0x16`) works as a static device animation.
- Color settings remain available.
- Linux captures system output through PulseAudio/PipeWire.
- Windows captures system output through WASAPI loopback.
- Audio levels are sent in real time through the `0xC0` command.
- If audio capture cannot be initialized, OpenRGB falls back to static Music mode.
- The current implementation uses RMS audio energy, not a true FFT spectrum.

## HID Protocol

### Music Activation Packet

Music mode is first armed using the normal animated-effect packet:

```text
03 01 16 RR GG BB BR SS
```

The packet is sent for both controller channels. `RR`, `GG`, and `BB` are the
selected color, `BR` is brightness, and the speed field is fixed for Music mode.

### Audio Packet (`0xC0`)

The audio packet is 8 bytes long:

```text
[0] = 0x01          Audio report ID
[1] = 0xC0          Audio-reactive command
[2] = 0x01 or 0x02  Controller channel
[3] = LEVEL         Current audio level, 0x00-0xFF
[4] = LEVEL         Same level for the second value
[5] = LEVEL         Same level for the third value
[6] = 0x01          Fixed parameter
[7] = 0x00          Fixed parameter
```

The channel alternates between `0x01` and `0x02`, approximately once every
100 milliseconds. The packet format matches the captured vendor traffic, for
example:

```text
01 c0 01 00 00 00 01 00  (Silence/baseline)
01 c0 02 00 00 00 01 00
01 c0 01 4a 4a 4a 01 00  (Active audio)
01 c0 02 bf bf bf 01 00
```

## Audio-Level Processing

USB captures show that the three audio values are nearly identical. The device
generates the color animation itself, so sending one overall loudness value in
all three positions is more accurate than inventing separate bass, midrange, and
treble values.

For each captured buffer, OpenRGB:

1. Calculates RMS energy from the samples.
2. Applies a small noise gate so idle output does not cause LED activity.
3. Scales the result to the byte range `0x00`-`0xFF`.
4. Clamps values above `0xFF`.
5. Stores the same level in all three output values.

The implementation keeps the method name `GetFFTBins()` for compatibility with
the controller code, but its current return values are RMS levels, not FFT bins.

## Platform Implementations

### Linux: PulseAudio/PipeWire

When `libpulse-simple` is available, `MachinistAudioCapture` opens a monitor
source for the current default output sink. It attempts these sources in order:

1. The monitor corresponding to `pactl get-default-sink`.
2. Known monitor names used by common USB, HDMI, and analog devices.
3. The default PulseAudio source as a final fallback.

The capture thread reads 512 floating-point samples at 44.1 kHz and updates the
shared audio level.

### Windows: WASAPI Loopback

Windows uses the native Windows Audio Session API and does not require an extra
audio DLL. The capture thread:

1. Initializes COM with `CoInitializeEx`.
2. Selects the default render endpoint with `IMMDeviceEnumerator`.
3. Creates an `IAudioClient` in shared mode with
   `AUDCLNT_STREAMFLAGS_LOOPBACK`.
4. Reads rendered audio packets through `IAudioCaptureClient`.
5. Supports the normal shared-mode float format and 16-bit PCM fallback.
6. Calculates RMS energy and updates the three level values.

Loopback capture observes audio being played by Windows, not microphone input.
The Windows build defines `MACHINIST_MUSIC_AUDIO_ENABLED` and links `ole32` for
the COM APIs used by WASAPI.

## Controller and Threading Flow

When Music mode is selected, `StartMusicMode()`:

1. Creates and initializes `MachinistAudioCapture`.
2. Sends the `0x16` Music effect packet to arm the controller.
3. Starts a worker thread for periodic `0xC0` updates.
4. Reads the latest audio level and sends it on alternating channels.

When the mode changes, `StopMusicMode()` stops the update loop, signals the audio
capture helper, and releases its resources. Access to the audio helper and update
thread is protected by `music_mode_mutex`, since LED updates can be requested
from multiple OpenRGB threads.

The capture thread is detached because PulseAudio reads can block while a monitor
source is idle or suspended. `Stop()` signals the shared atomic state and lets the
capture thread release its platform-specific handle when it exits.

## Fallback Behavior

If audio capture initialization fails:

- OpenRGB logs the failure at debug level.
- The normal static Music packet is still sent.
- No audio worker thread is started.
- The controller remains usable in its built-in Music effect.

This allows Music mode to work even when PulseAudio is unavailable or when a
Windows audio endpoint cannot be opened.

## Build Requirements

### Linux

`libpulse-simple` is optional. When detected by qmake, it enables
`MACHINIST_MUSIC_AUDIO_ENABLED` and links the PulseAudio implementation.

### Windows

No third-party audio package is required. The Windows build enables
`MACHINIST_MUSIC_AUDIO_ENABLED` and links the system `ole32` library. The build
also requires the normal OpenRGB MSVC and Qt toolchain.

## Testing Checklist

1. Build OpenRGB on Linux with `libpulse-simple` installed.
2. Build OpenRGB on Windows with the MSVC Qt kit.
3. Select the Machinist controller's Music mode.
4. Play audio through the system's default output device.
5. Confirm debug logs show successful capture initialization.
6. Confirm the LEDs react to silence, normal playback, and changing volume.
7. Switch away from Music mode and confirm that the update thread stops cleanly.
8. Test with no usable audio endpoint and confirm static Music fallback.

## Known Limitations

- The current implementation does not calculate a frequency-domain FFT.
- All three audio payload values intentionally contain the same RMS level.
- Windows captures the default console render endpoint selected by WASAPI.
- Linux depends on a working PulseAudio/PipeWire compatibility layer.
- Hardware validation is still required to confirm behavior across different
  Machinist firmware revisions.

## Relevant Source Files

- `MachinistAudioCapture.h`: platform selection and shared capture interface.
- `MachinistAudioCapture.cpp`: PulseAudio and WASAPI capture implementations.
- `MachinistARGBController.cpp`: Music lifecycle and `0xC0` packet transmission.
- `RGBController_MachinistARGB.cpp`: OpenRGB mode selection and mode transitions.
- `OpenRGB.pro`: platform-specific audio define and Windows `ole32` linkage.
