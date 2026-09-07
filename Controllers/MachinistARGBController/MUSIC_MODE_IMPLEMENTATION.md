# Machinist F-X9D ARGB - Audio-Reactive Music Mode Implementation Guide

## Overview

The Machinist F-X9D ARGB Controller supports true audio-reactive animations via the **0xC0 protocol command**. This document outlines the complete implementation needed for full music synchronization support.

## Current State

- ✅ Basic Music mode (0x16) works as static animation
- ✅ Color and brightness controls available
- ❌ Audio capture not implemented
- ❌ FFT processing not implemented
- ❌ Real-time audio synchronization disabled

## Audio Protocol (0xC0)

### Packet Structure

```
[0]   = 0x01          Report ID (different from normal 0x03)
[1]   = 0xC0          Audio-reactive command ID
[2]   = 0x01 or 0x02  Channel (alternate each packet)
[3]   = FFT_BIN_0     Audio spectrum byte 0 (bass frequencies) - 0x00-0xFF
[4]   = FFT_BIN_1     Audio spectrum byte 1 (mid frequencies) - 0x00-0xFF
[5]   = FFT_BIN_2     Audio spectrum byte 2 (treble frequencies) - 0x00-0xFF
[6]   = FFT_BIN_3     Audio spectrum byte 3 (high frequencies) - 0x00-0xFF
[7]   = 0x01          Fixed parameter
[8]   = 0x00          Fixed parameter
[9-63] = 0x00         Padding
```

### Transmission Pattern

- **Frequency**: ~100-200ms intervals (5-10 Hz update rate)
- **Alternation**: Channel 0x01, then 0x02, alternating
- **Persistence**: Must continue while music is playing; stops when music stops
- **Payload**: 4 bytes of audio/FFT data (bytes 3-6)

### Captured Examples

```
01 c0 01 00 00 00 00 01 00  (Silence/baseline)
01 c0 02 00 00 00 00 01 00
01 c0 01 7f 49 a9 6a 01 00  (Active music - varying bass/mid/treble)
01 c0 02 bf 4b 54 6e 01 00
```

## FFT Mapping Hypothesis

Based on typical audio visualization patterns:

| Byte | Frequency Range | Purpose |
|------|-----------------|---------|
| [3]  | 0-500 Hz       | Bass/Low frequencies |
| [4]  | 500-2000 Hz    | Midrange frequencies |
| [5]  | 2000-8000 Hz   | Treble/High mid frequencies |
| [6]  | 8000+ Hz       | Ultra-high frequencies |

Each byte is normalized to 0x00-0xFF representing the amplitude of that frequency band.

## Implementation Steps

### Phase 1: Audio Capture (Linux: PulseAudio/ALSA)

```cpp
#include <pulse/pulseaudio.h>

class MusicModeAudioCapture {
private:
    pa_simple* audio_stream;
    std::vector<float> audio_buffer;
    std::thread capture_thread;
    
public:
    bool Initialize();
    void CaptureAudioThread();
    std::array<uint8_t, 4> GetFFTBins();
};
```

### Phase 2: FFT Processing

```cpp
#include <fftw3.h>  // or kiss_fft for lightweight

class FFTProcessor {
private:
    fftw_plan plan;
    std::array<double, FFT_SIZE> input;
    std::array<fftw_complex, FFT_SIZE/2+1> output;
    
public:
    std::array<uint8_t, 4> ComputeSpectrumBins(const std::vector<float>& audio_samples);
};
```

### Phase 3: Protocol Integration

Modify `MachinistARGBController.cpp`:

```cpp
void MachinistARGBController::SendMusicWithAudio(
    unsigned char red, unsigned char green, unsigned char blue,
    unsigned char brightness, const std::array<uint8_t, 4>& fft_bins)
{
    if(dev == nullptr) return;
    
    for(unsigned char channel = 0x01; channel <= 0x02; channel++)
    {
        unsigned char buf[64];
        memset(buf, 0, sizeof(buf));
        
        buf[0] = 0x01;              // Report ID (0x01 for audio)
        buf[1] = 0xC0;              // Audio command
        buf[2] = channel;           // Channel
        buf[3] = fft_bins[0];       // Bass
        buf[4] = fft_bins[1];       // Mid
        buf[5] = fft_bins[2];       // Treble
        buf[6] = fft_bins[3];       // High
        buf[7] = 0x01;              // Fixed
        buf[8] = 0x00;              // Fixed
        
        wrapper.hid_write(dev, buf, 64);
    }
}
```

### Phase 4: Threading & Update Loop

```cpp
// In RGBController_MachinistARGB.cpp
void MusicUpdateThread() {
    while(music_mode_active) {
        auto fft = audio_capture->GetFFTBins();
        controller->SendMusicWithAudio(red, green, blue, brightness, fft);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
}
```

## Cross-Platform Audio Capture

### Linux
- **Recommended**: PulseAudio via `pa_simple` API (simpler)
- **Alternative**: ALSA direct (lower-level, more complex)
- **Fallback**: JACK (professional audio)

### Windows
- **Recommended**: WASAPI (Windows Audio Session API)
- **Alternative**: DirectSound (legacy)

### macOS
- **Recommended**: Core Audio HAL (Hardware Abstraction Layer)

## Dependencies to Add

```cmake
# For audio capture:
find_package(PulseAudio REQUIRED)  # Linux
find_package(PortAudio)             # Cross-platform (portable option)

# For FFT:
find_package(FFTW3 REQUIRED)        # Or kiss_fft as header-only alternative
```

## Testing Strategy

1. **Unit Test**: Verify FFT bin extraction with known frequencies
2. **Integration Test**: Capture audio from speaker output loop
3. **Hardware Test**: Run with actual YouTube Music, verify LED sync
4. **Edge Cases**:
   - Silence (all bins ~0x00)
   - Pure tone (single bin high)
   - Complex music (multiple bins active)
   - Rapid tempo changes

## Known Challenges

1. **Audio Capture Permissions**: May require PulseAudio socket access on Linux
2. **Loopback Device**: Need to monitor speaker output, not just microphone
3. **FFT Latency**: Must be < 50ms to stay in sync with visual
4. **Cross-Platform**: Three different audio APIs required
5. **Resource Usage**: FFT @ 44.1kHz continuously will use ~5-10% CPU

## Performance Considerations

- FFT size: 512-2048 samples (balance between resolution and latency)
- Update frequency: ~7 Hz (send every 140ms, based on capture analysis)
- Buffer management: Ring buffer to prevent blocking

## Security/Permissions

- **Linux**: May need user in `audio` group or PulseAudio access
- **Windows**: Application needs audio capture permission (UAC)
- **macOS**: Microphone permission dialog (can use loopback instead)

## Fallback Strategy

If audio capture fails:
- Keep current static animation behavior
- Log warning to console
- Allow user to select static Music mode as fallback

## Timeline Estimate

- Phase 1 (Audio Capture): 4-6 hours
- Phase 2 (FFT Processing): 2-3 hours
- Phase 3 (Protocol Integration): 1-2 hours
- Phase 4 (Cross-platform Testing): 3-4 hours
- **Total: ~12-15 hours development time**

## References

- FFTW Documentation: http://www.fftw.org/
- PulseAudio Simple API: https://www.freedesktop.org/wiki/Software/PulseAudio/
- USB Capture Analysis: See `machinist_music_options.txt` (3142 packets with music playing)
