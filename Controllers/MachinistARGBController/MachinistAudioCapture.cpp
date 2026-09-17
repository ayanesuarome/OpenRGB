/*---------------------------------------------------------*\
| MachinistAudioCapture.cpp                                 |
|                                                           |
|   Audio capture helper for MACHINIST Music mode           |
|                                                           |
|   OpenRGB Team                                            |
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
#include <vector>
#include <string>
#include "LogManager.h"

#ifdef _WIN32
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#endif

MachinistAudioCapture::MachinistAudioCapture()
    : state(std::make_shared<SharedState>())
{
}

MachinistAudioCapture::~MachinistAudioCapture()
{
    Stop();
}

#ifndef _WIN32
static std::string GetDefaultMonitorSource()
{
    // Ask PulseAudio/PipeWire for the current default sink, then use its monitor source.
    // This works regardless of which audio device is active on the machine.
    FILE* pipe = popen("pactl get-default-sink 2>/dev/null", "r");
    if (!pipe)
    {
        return "";
    }

    char buf[256] = {0};
    std::string sink_name;
    if (fgets(buf, sizeof(buf), pipe))
    {
        sink_name = buf;
        // Strip trailing newline
        while (!sink_name.empty() && (sink_name.back() == '\n' || sink_name.back() == '\r'))
        {
            sink_name.pop_back();
        }
    }
    pclose(pipe);

    if (sink_name.empty())
    {
        return "";
    }

    return sink_name + ".monitor";
}
#endif

bool MachinistAudioCapture::Initialize()
{
#ifdef _WIN32
    state->running = true;
    try
    {
        std::thread(&MachinistAudioCapture::CaptureThreadFunc, state).detach();
    }
    catch (const std::exception& exception)
    {
        LOG_DEBUG("[MachinistARGB] Failed to start WASAPI capture thread: %s", exception.what());
        state->running = false;
        return false;
    }

    std::unique_lock<std::mutex> lock(state->initialization_mutex);
    state->initialization_condition.wait(lock, [this]() {
        return state->initialization_complete;
    });
    return state->initialization_successful;
#else
    // PulseAudio configuration for capturing audio
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32;
    ss.channels = 1;           // Mono capture
    ss.rate = 44100;           // 44.1kHz sample rate

    int error = 0;
    pa_buffer_attr buffer_attr = {};
    buffer_attr.fragsize = sizeof(float) * 512;
    buffer_attr.maxlength = buffer_attr.fragsize * 2;

    // Monitor sources capture what's being played (loopback), not the mic.
    // Try the machine's actual default sink monitor first, then a few
    // common fallback names, then nullptr as a last resort (default source).
    std::string default_monitor = GetDefaultMonitorSource();

    std::vector<const char*> monitor_devices;
    if (!default_monitor.empty())
    {
        monitor_devices.push_back(default_monitor.c_str());
    }
    monitor_devices.push_back("alsa_output.usb-0c76_USB_PnP_Audio_Device-00.analog-stereo.monitor");
    monitor_devices.push_back("alsa_output.pci-0000_03_00.1.hdmi-stereo.monitor");
    monitor_devices.push_back("alsa_output.pci-0000_00_1f.3.analog-stereo.monitor");
    monitor_devices.push_back(nullptr);

    // Try each device in order
    for (size_t i = 0; i < monitor_devices.size(); i++)
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

        if (handle != nullptr)
        {
            LOG_DEBUG("[MachinistARGB] Audio capture opened on device: %s",
                      monitor_devices[i] ? monitor_devices[i] : "(default)");
            state->pa_handle = handle;
            state->running = true;
            std::thread(&MachinistAudioCapture::CaptureThreadFunc, state).detach();
            return true;
        }

        LOG_DEBUG("[MachinistARGB] Failed to open device %s: %s",
                  monitor_devices[i] ? monitor_devices[i] : "(default)", pa_strerror(error));
    }

    // All attempts failed
    return false;
#endif
}

void MachinistAudioCapture::Stop()
{
    // Just flag the thread to stop and let it clean up on its own. pa_simple_read()
    // can block indefinitely when the monitor source is idle/suspended, so we must
    // never join() it here - doing so would hang the whole shutdown sequence.
    state->running = false;
}

void MachinistAudioCapture::CaptureThreadFunc(std::shared_ptr<SharedState> state)
{
#ifdef _WIN32
    auto finish_initialization = [&state](bool successful) {
        {
            std::lock_guard<std::mutex> lock(state->initialization_mutex);
            state->initialization_successful = successful;
            state->initialization_complete = true;
        }
        state->initialization_condition.notify_one();
    };

    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_initialized = SUCCEEDED(result);
    if (!com_initialized && result != RPC_E_CHANGED_MODE)
    {
        LOG_DEBUG("[MachinistARGB] WASAPI COM initialization failed: 0x%08lx", result);
        state->running = false;
        finish_initialization(false);
        return;
    }

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    Microsoft::WRL::ComPtr<IMMDevice> device;
    Microsoft::WRL::ComPtr<IAudioClient> audio_client;
    Microsoft::WRL::ComPtr<IAudioCaptureClient> capture_client;

    result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), &enumerator);
    if (SUCCEEDED(result))
    {
        result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    }
    if (SUCCEEDED(result))
    {
        result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  &audio_client);
    }

    WAVEFORMATEX* mix_format = nullptr;
    UINT16 channels = 0;
    WORD bits_per_sample = 0;
    bool float_format = false;
    bool supported_format = false;
    if (SUCCEEDED(result))
    {
        result = audio_client->GetMixFormat(&mix_format);
        if (SUCCEEDED(result))
        {
            channels = mix_format->nChannels;
            bits_per_sample = mix_format->wBitsPerSample;
            if (mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
            {
                float_format = true;
                supported_format = bits_per_sample == 32;
            }
            else if (mix_format->wFormatTag == WAVE_FORMAT_PCM)
            {
                supported_format = bits_per_sample == 16;
            }
            else if (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                     mix_format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            {
                const auto* extensible_format = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix_format);
                float_format = IsEqualGUID(extensible_format->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
                supported_format = (float_format && bits_per_sample == 32) ||
                                   (IsEqualGUID(extensible_format->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) &&
                                    bits_per_sample == 16);
            }

            if (!supported_format)
            {
                result = AUDCLNT_E_UNSUPPORTED_FORMAT;
            }
        }
    }
    if (SUCCEEDED(result))
    {
        result = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                          AUDCLNT_STREAMFLAGS_LOOPBACK,
                                          0, 0, mix_format, nullptr);
    }
    if (SUCCEEDED(result))
    {
        result = audio_client->GetService(__uuidof(IAudioCaptureClient),
                                          &capture_client);
    }
    if (mix_format)
    {
        CoTaskMemFree(mix_format);
    }

    if (FAILED(result) || channels == 0)
    {
        LOG_DEBUG("[MachinistARGB] WASAPI loopback initialization failed: 0x%08lx", result);
        state->running = false;
        finish_initialization(false);
        if (com_initialized)
        {
            CoUninitialize();
        }
        return;
    }

    result = audio_client->Start();
    if (FAILED(result))
    {
        LOG_DEBUG("[MachinistARGB] WASAPI loopback start failed: 0x%08lx", result);
        state->running = false;
        finish_initialization(false);
        if (com_initialized)
        {
            CoUninitialize();
        }
        return;
    }

    LOG_DEBUG("[MachinistARGB] WASAPI loopback capture started");
    finish_initialization(true);
    while (state->running)
    {
        UINT32 packet_length = 0;
        if (FAILED(capture_client->GetNextPacketSize(&packet_length)))
        {
            break;
        }

        while (packet_length > 0 && state->running)
        {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture_client->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
            {
                break;
            }

            if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !data)
            {
                for (int i = 0; i < 3; ++i)
                {
                    state->fft_bins[i] = 0;
                }
            }
            else if (frames > 0)
            {
                double energy = 0.0;
                const UINT32 sample_count = frames * channels;
                if (float_format)
                {
                    const auto* samples = reinterpret_cast<const float*>(data);
                    for (UINT32 i = 0; i < sample_count; ++i)
                    {
                        energy += static_cast<double>(samples[i]) * samples[i];
                    }
                }
                else if (bits_per_sample == 16)
                {
                    const auto* samples = reinterpret_cast<const int16_t*>(data);
                    for (UINT32 i = 0; i < sample_count; ++i)
                    {
                        const double sample = samples[i] / 32768.0;
                        energy += sample * sample;
                    }
                }
                float level = static_cast<float>(std::sqrt(energy / sample_count));
                if (level < 0.01f)
                {
                    level = 0.0f;
                }
                const uint8_t value = static_cast<uint8_t>(std::min(255.0f, level * 500.0f));
                for (int i = 0; i < 3; ++i)
                {
                    state->fft_bins[i] = value;
                }
            }

            capture_client->ReleaseBuffer(frames);
            if (FAILED(capture_client->GetNextPacketSize(&packet_length)))
            {
                packet_length = 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    audio_client->Stop();
    if (com_initialized)
    {
        CoUninitialize();
    }
#else
    float sample_buffer[512];

    // Below this level, treat the signal as silence to avoid noise-floor
    // jitter making the LEDs appear to "react" while paused/idle.
    const float silence_gate = 0.01f;

    while (state->running && state->pa_handle)
    {
        int error;

        // Read audio samples from PulseAudio
        if (pa_simple_read(state->pa_handle, sample_buffer, sizeof(sample_buffer), &error) < 0)
        {
            for (int i = 0; i < 3; i++)
            {
                state->fft_bins[i] = 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // Real USB capture analysis (machinist_music_option.pcapng, 574 samples)
        // shows the 3 bytes are always nearly identical (max delta 32/255,
        // >98% of samples within 20 of each other) - the device does its own
        // internal color cycling and only needs an overall loudness/energy
        // level here, not independent per-band RGB values.
        float energy = 0.0f;
        for (int i = 0; i < 512; i++)
        {
            energy += sample_buffer[i] * sample_buffer[i];
        }
        energy = std::sqrt(energy / 512);

        if (energy < silence_gate)
        {
            energy = 0.0f;
        }

        uint8_t value = static_cast<uint8_t>(std::min(255.0f, energy * 500.0f));

        for (int i = 0; i < 3; i++)
        {
            state->fft_bins[i] = value;
        }

        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (state->pa_handle)
    {
        pa_simple_free(state->pa_handle);
        state->pa_handle = nullptr;
    }
#endif
}

std::array<uint8_t, 3> MachinistAudioCapture::GetFFTBins() const
{
    return { state->fft_bins[0].load(), state->fft_bins[1].load(),
             state->fft_bins[2].load() };
}

#endif  // MACHINIST_MUSIC_AUDIO_ENABLED
