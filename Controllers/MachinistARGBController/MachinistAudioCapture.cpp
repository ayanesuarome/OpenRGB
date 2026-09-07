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

    // Single-pole IIR low-pass filter states, persistent across reads, used to
    // split the signal into bass/mid/treble bands (a real time-domain split
    // by sample index would just be 3 near-identical chunks of the same
    // waveform, not different frequencies).
    const float sample_rate = 44100.0f;
    auto lpf_alpha = [sample_rate](float cutoff_hz)
    {
        constexpr float pi = 3.14159265358979323846f;
        float rc = 1.0f / (2.0f * pi * cutoff_hz);
        float dt = 1.0f / sample_rate;
        return dt / (rc + dt);
    };
    const float alpha_bass = lpf_alpha(200.0f);   // below ~200Hz
    const float alpha_mid  = lpf_alpha(2000.0f);  // below ~2000Hz
    float lpf_bass_state = 0.0f;
    float lpf_mid_state  = 0.0f;

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

        std::array<float, 3> band_energy = {0.0f, 0.0f, 0.0f};

        for (int i = 0; i < 512; i++)
        {
            float sample = sample_buffer[i];

            lpf_bass_state += alpha_bass * (sample - lpf_bass_state);
            lpf_mid_state  += alpha_mid  * (sample - lpf_mid_state);

            float bass_signal   = lpf_bass_state;
            float mid_signal    = lpf_mid_state - lpf_bass_state;
            float treble_signal = sample - lpf_mid_state;

            band_energy[0] += bass_signal   * bass_signal;
            band_energy[1] += mid_signal    * mid_signal;
            band_energy[2] += treble_signal * treble_signal;
        }

        // Normalize and convert to 0-255 range
        for (int i = 0; i < 3; i++)
        {
            band_energy[i] = std::sqrt(band_energy[i] / 512);

            if (band_energy[i] < silence_gate)
            {
                band_energy[i] = 0.0f;
            }

            // Scale to 0-255 with some sensitivity adjustment
            uint8_t value = static_cast<uint8_t>(
                std::min(255.0f, band_energy[i] * 500.0f)
            );

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
