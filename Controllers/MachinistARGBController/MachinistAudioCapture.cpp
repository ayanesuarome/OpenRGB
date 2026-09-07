#include "MachinistAudioCapture.h"

#ifdef MACHINIST_MUSIC_AUDIO_ENABLED

#include <cstring>
#include <algorithm>
#include <cmath>

MachinistAudioCapture::MachinistAudioCapture()
    : pa_handle(nullptr), running(false)
{
    fft_bins.fill(0);
    audio_buffer.fill(0.0f);
}

MachinistAudioCapture::~MachinistAudioCapture()
{
    Stop();
}

bool MachinistAudioCapture::Initialize()
{
    // PulseAudio configuration for capturing audio
    pa_sample_spec ss;
    ss.format = PA_SAMPLE_FLOAT32;
    ss.channels = 1;           // Mono capture
    ss.rate = 44100;           // 44.1kHz sample rate

    int error;

    // Connect to PulseAudio monitoring stream (loopback)
    // This captures audio being played through speakers
    pa_handle = pa_simple_new(
        nullptr,                                    // Server
        nullptr,                                    // Device
        PA_STREAM_RECORD,                          // Direction (record/monitor)
        "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor",  // Monitor device
        "OpenRGB Music Visualizer",                 // Application name
        &ss,                                        // Sample specification
        nullptr,                                    // Channel map
        nullptr,                                    // Attributes
        &error
    );

    if (!pa_handle)
    {
        // Fallback: try default device if monitor device not found
        pa_handle = pa_simple_new(
            nullptr, nullptr, PA_STREAM_RECORD,
            nullptr, "OpenRGB Music Visualizer",
            &ss, nullptr, nullptr, &error
        );
    }

    if (!pa_handle)
    {
        return false;
    }

    running = true;
    capture_thread = std::thread(&MachinistAudioCapture::CaptureThreadFunc, this);
    return true;
}

void MachinistAudioCapture::Stop()
{
    running = false;
    if (capture_thread.joinable())
    {
        capture_thread.join();
    }

    if (pa_handle)
    {
        pa_simple_free(pa_handle);
        pa_handle = nullptr;
    }
}

void MachinistAudioCapture::CaptureThreadFunc()
{
    float sample_buffer[512];

    while (running && pa_handle)
    {
        int error;

        // Read audio samples from PulseAudio
        if (pa_simple_read(pa_handle, sample_buffer, sizeof(sample_buffer), &error) < 0)
        {
            continue;  // Skip on error, keep running
        }

        // Simple frequency analysis (not true FFT, but good enough for visualization)
        // Divide audio into 4 frequency bands

        std::array<float, 4> band_energy = {0.0f, 0.0f, 0.0f, 0.0f};
        int samples_per_band = 512 / 4;

        for (int i = 0; i < 512; i++)
        {
            int band = i / samples_per_band;
            if (band >= 4) band = 3;

            // Calculate energy (RMS) for each frequency band
            band_energy[band] += sample_buffer[i] * sample_buffer[i];
        }

        // Normalize and convert to 0-255 range
        for (int i = 0; i < 4; i++)
        {
            band_energy[i] = std::sqrt(band_energy[i] / samples_per_band);

            // Scale to 0-255 with some sensitivity adjustment
            uint8_t value = static_cast<uint8_t>(
                std::min(255.0f, band_energy[i] * 500.0f)
            );

            fft_bins[i] = value;
        }

        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

std::array<uint8_t, 4> MachinistAudioCapture::GetFFTBins() const
{
    return fft_bins;
}

#endif  // MACHINIST_MUSIC_AUDIO_ENABLED
