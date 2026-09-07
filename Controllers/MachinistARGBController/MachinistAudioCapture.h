#pragma once

#include <array>
#include <cstdint>

#ifdef MACHINIST_MUSIC_AUDIO_ENABLED

#include <thread>
#include <atomic>
#include <pulse/pulseaudio.h>

class MachinistAudioCapture
{
public:
    MachinistAudioCapture();
    ~MachinistAudioCapture();

    bool Initialize();
    void Stop();
    std::array<uint8_t, 4> GetFFTBins() const;

private:
    void CaptureThreadFunc();

    pa_simple*                  pa_handle;
    std::thread                 capture_thread;
    std::atomic<bool>           running;
    std::array<uint8_t, 4>      fft_bins;
    std::array<float, 512>      audio_buffer;
};

#else

// Stub implementation when audio is not available
class MachinistAudioCapture
{
public:
    MachinistAudioCapture() {}
    ~MachinistAudioCapture() {}
    bool Initialize() { return false; }
    void Stop() {}
    std::array<uint8_t, 4> GetFFTBins() const { return {0, 0, 0, 0}; }
};

#endif  // MACHINIST_MUSIC_AUDIO_ENABLED
