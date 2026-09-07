#include "MachinistAudioCapture.h"

#ifdef MACHINIST_MUSIC_AUDIO_ENABLED

#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include "LogManager.h"

MachinistAudioCapture::MachinistAudioCapture()
    : state(std::make_shared<SharedState>())
{
}

MachinistAudioCapture::~MachinistAudioCapture()
{
    Stop();
}

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

bool MachinistAudioCapture::Initialize()
{
    // PulseAudio configuration for capturing audio
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32;
    ss.channels = 1;           // Mono capture
    ss.rate = 44100;           // 44.1kHz sample rate

    int error = 0;

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
            nullptr,                                // Attributes
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
            continue;  // Skip on error, keep running
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
}

std::array<uint8_t, 3> MachinistAudioCapture::GetFFTBins() const
{
    return { state->fft_bins[0].load(), state->fft_bins[1].load(),
             state->fft_bins[2].load() };
}

#endif  // MACHINIST_MUSIC_AUDIO_ENABLED
