#pragma once

#include <array>
#include <cstdint>

#ifdef MACHINIST_MUSIC_AUDIO_ENABLED

#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <pulse/simple.h>
#include <pulse/error.h>

class MachinistAudioCapture
{
public:
    MachinistAudioCapture();
    ~MachinistAudioCapture();

    bool Initialize();
    void Stop();
    std::array<uint8_t, 4> GetFFTBins() const;

private:
    // pa_simple_read() blocks indefinitely if the monitor source is idle/suspended.
    // State is heap-allocated and shared with the detached capture thread so Stop()
    // never has to join a thread that might be stuck in that blocking call.
    struct SharedState
    {
        std::atomic<bool>              running{false};
        pa_simple*                     pa_handle = nullptr;
        std::array<std::atomic<uint8_t>, 4> fft_bins{};
    };

    static void CaptureThreadFunc(std::shared_ptr<SharedState> state);

    std::shared_ptr<SharedState> state;
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
