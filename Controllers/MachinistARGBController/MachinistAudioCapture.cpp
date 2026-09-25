/*---------------------------------------------------------*\
| MachinistAudioCapture.cpp                                 |
|                                                           |
|   Audio capture helper for MACHINIST Music mode           |
|                                                           |
|   Ayane Suarez                                04 Sep 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "MachinistAudioCapture.h"

#ifdef MACHINIST_MUSIC_AUDIO_ENABLED

#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include "LogManager.h"

#ifdef _WIN32
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#endif

/*---------------------------------------------------------*\
| Levels below the silence gate are treated as silence so   |
| the noise floor does not make the LEDs react while idle   |
\*---------------------------------------------------------*/
#define MACHINIST_AUDIO_SILENCE_GATE    0.01f
#define MACHINIST_AUDIO_LEVEL_GAIN      500.0f
#define MACHINIST_AUDIO_SAMPLE_COUNT    512

MachinistAudioCapture::MachinistAudioCapture()
{
    state = std::make_shared<SharedState>();

    state->running                      = false;
#ifdef _WIN32
    state->initialization_complete      = false;
    state->initialization_successful    = false;
#else
    state->pa_handle                    = nullptr;
#endif

    for(int i = 0; i < 3; i++)
    {
        state->fft_bins[i] = 0;
    }
}

MachinistAudioCapture::~MachinistAudioCapture()
{
    Stop();
}

#ifndef _WIN32
/*---------------------------------------------------------*\
| Returns the monitor (loopback) source of the current      |
| default PulseAudio/PipeWire sink                          |
\*---------------------------------------------------------*/
static std::string GetDefaultMonitorSource()
{
    FILE* pipe = popen("pactl get-default-sink 2>/dev/null", "r");
    if(!pipe)
    {
        return("");
    }

    char buf[256] = {0};
    std::string sink_name;
    if(fgets(buf, sizeof(buf), pipe))
    {
        sink_name = buf;

        while(!sink_name.empty() && (sink_name.back() == '\n' || sink_name.back() == '\r'))
        {
            sink_name.pop_back();
        }
    }
    pclose(pipe);

    if(sink_name.empty())
    {
        return("");
    }

    return(sink_name + ".monitor");
}
#endif

bool MachinistAudioCapture::Initialize()
{
#ifdef _WIN32
    state->running = true;
    std::thread(&MachinistAudioCapture::CaptureThreadFunc, state).detach();

    std::unique_lock<std::mutex> lock(state->initialization_mutex);
    while(!state->initialization_complete)
    {
        state->initialization_condition.wait(lock);
    }

    return(state->initialization_successful);
#else
    pa_sample_spec ss;
    ss.format                   = PA_SAMPLE_FLOAT32;
    ss.channels                 = 1;
    ss.rate                     = 44100;

    int error = 0;
    pa_buffer_attr buffer_attr;
    memset(&buffer_attr, 0, sizeof(buffer_attr));
    buffer_attr.fragsize        = sizeof(float) * MACHINIST_AUDIO_SAMPLE_COUNT;
    buffer_attr.maxlength       = buffer_attr.fragsize * 2;

    /*-----------------------------------------------------*\
    | Monitor sources capture playback (loopback), not the  |
    | microphone. Try the default sink monitor first, then  |
    | fall back to the default source (NULL)                |
    \*-----------------------------------------------------*/
    std::string default_monitor = GetDefaultMonitorSource();

    std::vector<const char*> monitor_devices;
    if(!default_monitor.empty())
    {
        monitor_devices.push_back(default_monitor.c_str());
    }
    monitor_devices.push_back(nullptr);

    for(unsigned int i = 0; i < monitor_devices.size(); i++)
    {
        pa_simple* handle = pa_simple_new(
            nullptr,                                // Server (local)
            "OpenRGB Music Visualizer",             // Application name
            PA_STREAM_RECORD,                       // Record direction
            monitor_devices[i],                     // Source name, or NULL for default
            "Music mode capture",                   // Stream description
            &ss,                                    // Sample specification
            nullptr,                                // Channel map (default)
            &buffer_attr,                           // Attributes
            &error
        );

        if(handle != nullptr)
        {
            LOG_DEBUG("[MachinistARGB] Audio capture opened on device: %s",
                      monitor_devices[i] ? monitor_devices[i] : "(default)");
            state->pa_handle    = handle;
            state->running      = true;
            std::thread(&MachinistAudioCapture::CaptureThreadFunc, state).detach();
            return(true);
        }

        LOG_DEBUG("[MachinistARGB] Failed to open device %s: %s",
                  monitor_devices[i] ? monitor_devices[i] : "(default)", pa_strerror(error));
    }

    return(false);
#endif
}

void MachinistAudioCapture::Stop()
{
    /*-----------------------------------------------------*\
    | Only signal the detached thread, never join it, as it |
    | may be blocked in pa_simple_read()                    |
    \*-----------------------------------------------------*/
    state->running = false;
}

#ifdef _WIN32
void MachinistAudioCapture::FinishInitialization(SharedState* state, bool successful)
{
    {
        std::lock_guard<std::mutex> lock(state->initialization_mutex);
        state->initialization_successful    = successful;
        state->initialization_complete      = true;
    }
    state->initialization_condition.notify_one();
}
#endif

void MachinistAudioCapture::CaptureThreadFunc(std::shared_ptr<SharedState> state)
{
#ifdef _WIN32
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(result);
    if(!com_initialized && result != RPC_E_CHANGED_MODE)
    {
        LOG_DEBUG("[MachinistARGB] WASAPI COM initialization failed: 0x%08lx", result);
        state->running = false;
        FinishInitialization(state.get(), false);
        return;
    }

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    Microsoft::WRL::ComPtr<IMMDevice> device;
    Microsoft::WRL::ComPtr<IAudioClient> audio_client;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture_client;

    result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), &enumerator);
    if(SUCCEEDED(result))
    {
        result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    }
    if(SUCCEEDED(result))
    {
        result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  &audio_client);
    }

    WAVEFORMATEX*   mix_format          = nullptr;
    UINT16          channels            = 0;
    WORD            bits_per_sample     = 0;
    bool            float_format        = false;
    bool            supported_format    = false;
    if(SUCCEEDED(result))
    {
        result = audio_client->GetMixFormat(&mix_format);
        if(SUCCEEDED(result))
        {
            channels        = mix_format->nChannels;
            bits_per_sample = mix_format->wBitsPerSample;
            if(mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
            {
                float_format        = true;
                supported_format    = bits_per_sample == 32;
            }
            else if(mix_format->wFormatTag == WAVE_FORMAT_PCM)
            {
                supported_format    = bits_per_sample == 16;
            }
            else if(mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                    mix_format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            {
                const WAVEFORMATEXTENSIBLE* extensible_format = (const WAVEFORMATEXTENSIBLE*)mix_format;
                float_format        = IsEqualGUID(extensible_format->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
                supported_format    = (float_format && bits_per_sample == 32) ||
                                      (IsEqualGUID(extensible_format->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) &&
                                       bits_per_sample == 16);
            }

            if(!supported_format)
            {
                result = AUDCLNT_E_UNSUPPORTED_FORMAT;
            }
        }
    }
    if(SUCCEEDED(result))
    {
        result = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                          AUDCLNT_STREAMFLAGS_LOOPBACK,
                                          0, 0, mix_format, nullptr);
    }
    if(SUCCEEDED(result))
    {
        result = audio_client->GetService(__uuidof(IAudioCaptureClient),
                                          &capture_client);
    }
    if(mix_format)
    {
        CoTaskMemFree(mix_format);
    }

    if(FAILED(result) || channels == 0)
    {
        LOG_DEBUG("[MachinistARGB] WASAPI loopback initialization failed: 0x%08lx", result);
        state->running = false;
        FinishInitialization(state.get(), false);
        if(com_initialized)
        {
            CoUninitialize();
        }
        return;
    }

    result = audio_client->Start();
    if(FAILED(result))
    {
        LOG_DEBUG("[MachinistARGB] WASAPI loopback start failed: 0x%08lx", result);
        state->running = false;
        FinishInitialization(state.get(), false);
        if(com_initialized)
        {
            CoUninitialize();
        }
        return;
    }

    LOG_DEBUG("[MachinistARGB] WASAPI loopback capture started");
    FinishInitialization(state.get(), true);
    while(state->running)
    {
        UINT32 packet_length = 0;
        if(FAILED(capture_client->GetNextPacketSize(&packet_length)))
        {
            break;
        }

        while(packet_length > 0 && state->running)
        {
            BYTE*   data    = nullptr;
            UINT32  frames  = 0;
            DWORD   flags   = 0;
            if(FAILED(capture_client->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
            {
                break;
            }

            if((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data)
            {
                for(int i = 0; i < 3; i++)
                {
                    state->fft_bins[i] = 0;
                }
            }
            else if(frames > 0)
            {
                double energy = 0.0;
                const UINT32 sample_count = frames * channels;
                if(float_format)
                {
                    const float* samples = (const float*)data;
                    for(UINT32 i = 0; i < sample_count; i++)
                    {
                        energy += (double)samples[i] * samples[i];
                    }
                }
                else if(bits_per_sample == 16)
                {
                    const short* samples = (const short*)data;
                    for(UINT32 i = 0; i < sample_count; i++)
                    {
                        const double sample = samples[i] / 32768.0;
                        energy += sample * sample;
                    }
                }
                float level = (float)std::sqrt(energy / sample_count);
                if(level < MACHINIST_AUDIO_SILENCE_GATE)
                {
                    level = 0.0f;
                }
                unsigned char value = (unsigned char)std::min(255.0f, level * MACHINIST_AUDIO_LEVEL_GAIN);
                for(int i = 0; i < 3; i++)
                {
                    state->fft_bins[i] = value;
                }
            }

            capture_client->ReleaseBuffer(frames);
            if(FAILED(capture_client->GetNextPacketSize(&packet_length)))
            {
                packet_length = 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    audio_client->Stop();
    if(com_initialized)
    {
        CoUninitialize();
    }
#else
    float sample_buffer[MACHINIST_AUDIO_SAMPLE_COUNT];

    while(state->running && state->pa_handle)
    {
        int error;

        if(pa_simple_read(state->pa_handle, sample_buffer, sizeof(sample_buffer), &error) < 0)
        {
            for(int i = 0; i < 3; i++)
            {
                state->fft_bins[i] = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        /*-------------------------------------------------*\
        | USB captures show the three level bytes are       |
        | nearly identical, the device only needs overall   |
        | loudness and handles color cycling internally     |
        \*-------------------------------------------------*/
        float energy = 0.0f;
        for(int i = 0; i < MACHINIST_AUDIO_SAMPLE_COUNT; i++)
        {
            energy += sample_buffer[i] * sample_buffer[i];
        }
        energy = std::sqrt(energy / MACHINIST_AUDIO_SAMPLE_COUNT);

        if(energy < MACHINIST_AUDIO_SILENCE_GATE)
        {
            energy = 0.0f;
        }

        unsigned char value = (unsigned char)std::min(255.0f, energy * MACHINIST_AUDIO_LEVEL_GAIN);

        for(int i = 0; i < 3; i++)
        {
            state->fft_bins[i] = value;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if(state->pa_handle)
    {
        pa_simple_free(state->pa_handle);
        state->pa_handle = nullptr;
    }
#endif
}

void MachinistAudioCapture::GetFFTBins(unsigned char* bins) const
{
    for(int i = 0; i < 3; i++)
    {
        bins[i] = state->fft_bins[i];
    }
}

#endif
