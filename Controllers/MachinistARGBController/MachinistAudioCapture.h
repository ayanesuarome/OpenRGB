/*---------------------------------------------------------*\
| MachinistAudioCapture.h                                   |
|                                                           |
|   Audio capture helper for MACHINIST Music mode           |
|                                                           |
|   OpenRGB Team                                            |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#ifndef MACHINISTAUDIOCAPTURE_H
#define MACHINISTAUDIOCAPTURE_H

#include <array>
#include <cstdint>

#ifdef MACHINIST_MUSIC_AUDIO_ENABLED

#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pulse/simple.h>
#include <pulse/error.h>
#endif

class MachinistAudioCapture
{
public:
    MachinistAudioCapture();
    ~MachinistAudioCapture();

    bool Initialize();
    void Stop();
    std::array<uint8_t, 3> GetFFTBins() const;

private:
    // pa_simple_read() blocks indefinitely if the monitor source is idle/suspended.
    // State is heap-allocated and shared with the detached capture thread so Stop()
    // never has to join a thread that might be stuck in that blocking call.
    struct SharedState
    {
        std::atomic<bool>              running{false};
#ifndef _WIN32
        pa_simple*                     pa_handle = nullptr;
#endif
        std::array<std::atomic<uint8_t>, 3> fft_bins{};
    };

    static void CaptureThreadFunc(std::shared_ptr<SharedState> state);

    std::shared_ptr<SharedState> state;
};

#else

// Stub implementation when audio is not available
class MachinistAudioCapture
{
public:
    MachinistAudioCapture()
    {
    }

    ~MachinistAudioCapture()
    {
    }

    bool Initialize()
    {
        return false;
    }

    void Stop()
    {
    }

    std::array<uint8_t, 3> GetFFTBins() const
    {
        return {0, 0, 0};
    }
};

#endif  // MACHINIST_MUSIC_AUDIO_ENABLED

#endif // MACHINISTAUDIOCAPTURE_H
