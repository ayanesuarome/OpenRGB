/*---------------------------------------------------------*\
| MachinistAudioCapture.h                                   |
|                                                           |
|   Audio capture helper for MACHINIST Music mode           |
|                                                           |
|   Ayane Suarez                                04 Sep 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#ifdef MACHINIST_MUSIC_AUDIO_ENABLED

#include <atomic>
#include <memory>
#ifdef _WIN32
#include <condition_variable>
#include <mutex>
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
    void GetFFTBins(unsigned char* bins) const;

private:
    /*-----------------------------------------------------*\
    | pa_simple_read() can block indefinitely on an idle    |
    | source, so the capture thread is detached and shares  |
    | ownership of this state instead of being joined       |
    \*-----------------------------------------------------*/
    struct SharedState
    {
        std::atomic<bool>           running;
        std::atomic<unsigned char>  fft_bins[3];
#ifdef _WIN32
        std::mutex                  initialization_mutex;
        std::condition_variable     initialization_condition;
        bool                        initialization_complete;
        bool                        initialization_successful;
#else
        pa_simple*                  pa_handle;
#endif
    };

    static void CaptureThreadFunc(std::shared_ptr<SharedState> state);
#ifdef _WIN32
    static void FinishInitialization(SharedState* state, bool successful);
#endif

    std::shared_ptr<SharedState> state;
};

#else

/*---------------------------------------------------------*\
| Stub used when audio capture support is not built in      |
\*---------------------------------------------------------*/
class MachinistAudioCapture
{
public:
    bool Initialize()
    {
        return(false);
    }

    void Stop()
    {
    }

    void GetFFTBins(unsigned char* bins) const
    {
        bins[0] = 0;
        bins[1] = 0;
        bins[2] = 0;
    }
};

#endif
