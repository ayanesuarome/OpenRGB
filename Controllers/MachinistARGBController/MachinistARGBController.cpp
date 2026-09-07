/*---------------------------------------------------------*\
| MachinistARGBController.cpp                               |
|                                                           |
|   Driver for MACHINIST F-X9D ARGB Controller              |
|                                                           |
|   OpenRGB Team                                            |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "MachinistARGBController.h"
#include <cstring>
#include <vector>
#include "LogManager.h"
#include "StringUtils.h"

MachinistARGBController::MachinistARGBController(hidapi_wrapper hid_wrapper, hid_device* dev_handle, const std::string& path)
    : music_mode_active(false)
{
    wrapper     = hid_wrapper;
    dev         = dev_handle;
    location    = path;

    wchar_t serial_string[256];
    int ret = wrapper.hid_get_serial_number_string(dev, serial_string, 256);
    if(ret == 0)
    {
        serial = StringUtils::wstring_to_string(serial_string);
    }
    else
    {
        serial = "Unknown";
    }
}

MachinistARGBController::~MachinistARGBController()
{
    // Stop music mode if it's running
    if (music_mode_active)
    {
        StopMusicMode();
    }

    if(dev)
    {
        wrapper.hid_close(dev);
    }
}

std::string MachinistARGBController::GetDeviceLocation()
{
    return("HID: " + location);
}

std::string MachinistARGBController::GetSerialString()
{
    return(serial);
}

void MachinistARGBController::SendAnimatedEffect(unsigned char effect_id, unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
{
    if(dev == nullptr)
    {
        LOG_DEBUG("[MachinistARGB] SendAnimatedEffect called with no open device handle");
        return;
    }

    unsigned char inverted_speed = 0xFF - speed;
    for(unsigned char channel = 0x01; channel <= 0x02; channel++)
    {
        unsigned char buf[64];
        memset(buf, 0, sizeof(buf));

        buf[0] = 0x03;
        buf[1] = channel;
        buf[2] = effect_id;
        buf[3] = red;
        buf[4] = green;
        buf[5] = blue;
        buf[6] = brightness;
        buf[7] = inverted_speed;

        int res = wrapper.hid_write(dev, buf, 64);
        if(res < 0)
        {
            LOG_DEBUG("[MachinistARGB] Failed to write animated effect packet");
        }
    }
}

void MachinistARGBController::SendColor(unsigned char red, unsigned char green, unsigned char blue, unsigned char brightness)
{
    if(dev == nullptr)
    {
        LOG_DEBUG("[MachinistARGB] SendColor called with no open device handle");
        return;
    }

    /*-----------------------------------------------------*\
    | Verified from USB capture: Report 0x03, subcommand    |
    | 0x11, RGB order, sent once per channel (1 and 2)       |
    \*-----------------------------------------------------*/
    for(unsigned char channel = 0x01; channel <= 0x02; channel++)
    {
        unsigned char buf[64];
        memset(buf, 0, sizeof(buf));

        buf[0] = 0x03;
        buf[1] = channel;
        buf[2] = 0x11;
        buf[3] = red;
        buf[4] = green;
        buf[5] = blue;
        buf[6] = brightness;
        buf[7] = 0xFF;

        int res = wrapper.hid_write(dev, buf, 64);
        if(res < 0)
        {
            LOG_DEBUG("[MachinistARGB] Failed to write color packet");
        }
    }
}

void MachinistARGBController::SendBreathing(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
{
    SendAnimatedEffect(0x12, red, green, blue, speed, brightness);
}

void MachinistARGBController::SendRainbowWave(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
{
    SendAnimatedEffect(0x17, red, green, blue, speed, brightness);
}

void MachinistARGBController::SendSpectrumCycle(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
{
    SendAnimatedEffect(0x14, red, green, blue, speed, brightness);
}

void MachinistARGBController::SendRainbow(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
{
    SendAnimatedEffect(0x1A, red, green, blue, speed, brightness);
}

void MachinistARGBController::SendRandom(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
{
    SendAnimatedEffect(0x15, red, green, blue, speed, brightness);
}

void MachinistARGBController::SendSpring(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
{
    SendAnimatedEffect(0x18, red, green, blue, speed, brightness);
}

void MachinistARGBController::SendWater(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
{
    SendAnimatedEffect(0x19, red, green, blue, speed, brightness);
}

void MachinistARGBController::SendMusic(unsigned char red, unsigned char green, unsigned char blue, unsigned char brightness)
{
    SendAnimatedEffect(0x16, red, green, blue, 0x7F, brightness);
}

void MachinistARGBController::StartMusicMode(unsigned char red, unsigned char green, unsigned char blue, unsigned char brightness)
{
    /*
     * Start audio-reactive music mode
     * 1. Initialize audio capture from system audio
     * 2. Start thread for periodic FFT data transmission via 0xC0 protocol
     * 3. LED color set to given parameters
     */

    if (music_mode_active)
    {
        // Already running
        return;
    }

    if (!audio_capture)
    {
        audio_capture = std::make_unique<MachinistAudioCapture>();
        if (!audio_capture->Initialize())
        {
            LOG_DEBUG("[MachinistARGB] Audio capture initialization failed, using static Music mode");
            // Fallback to static music mode if audio capture fails
            SendMusic(red, green, blue, brightness);
            audio_capture.reset();
            return;
        }
        LOG_DEBUG("[MachinistARGB] Audio capture initialized successfully");
    }

    music_mode_active = true;

    // Lambda for music update thread
    // Captures this pointer and parameters by value to avoid dangling references
    auto music_update_func = [this, red, green, blue, brightness]() {
        unsigned char channel = 0x01;

        while (music_mode_active && audio_capture)
        {
            try
            {
                // Get current FFT bins from audio capture
                auto fft_bins = audio_capture->GetFFTBins();

                // Send to device via 0xC0 protocol
                SendMusicWithAudio(channel, fft_bins);

                // Alternate channel for next transmission
                channel = (channel == 0x01) ? 0x02 : 0x01;
            }
            catch (const std::exception& e)
            {
                LOG_DEBUG("[MachinistARGB] Music thread exception: %s", e.what());
                break;
            }

            // Update frequency: ~100-150ms per channel pair
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        LOG_DEBUG("[MachinistARGB] Music mode thread exiting");
    };

    // Stop old thread if it exists
    if (music_update_thread.joinable())
    {
        try
        {
            music_update_thread.join();
        }
        catch (const std::exception& e)
        {
            LOG_DEBUG("[MachinistARGB] Error joining old music thread: %s", e.what());
        }
    }

    // Start the music update thread
    try
    {
        music_update_thread = std::thread(music_update_func);
        LOG_DEBUG("[MachinistARGB] Music mode thread started successfully");
    }
    catch (const std::exception& e)
    {
        LOG_DEBUG("[MachinistARGB] Failed to start music thread: %s", e.what());
        music_mode_active = false;
        if (audio_capture)
        {
            audio_capture->Stop();
            audio_capture.reset();
        }
    }
}

void MachinistARGBController::StopMusicMode()
{
    if (!music_mode_active)
    {
        return;
    }

    LOG_DEBUG("[MachinistARGB] Stopping music mode");

    music_mode_active = false;

    if (music_update_thread.joinable())
    {
        try
        {
            music_update_thread.join();
            LOG_DEBUG("[MachinistARGB] Music thread joined successfully");
        }
        catch (const std::exception& e)
        {
            LOG_DEBUG("[MachinistARGB] Error joining music thread: %s", e.what());
        }
    }

    if (audio_capture)
    {
        audio_capture->Stop();
        audio_capture.reset();
        LOG_DEBUG("[MachinistARGB] Audio capture stopped and cleaned up");
    }
}

void MachinistARGBController::UpdateMusicMode(unsigned char red, unsigned char green, unsigned char blue, unsigned char brightness)
{
    // Update music mode parameters (currently used for static settings if audio fails)
    SendMusic(red, green, blue, brightness);
}

void MachinistARGBController::SendMusicWithAudio(unsigned char channel, const std::array<uint8_t, 4>& fft_bins)
{
    /*
     * Send audio-reactive data via 0xC0 protocol
     *
     * Packet structure:
     * [0] = 0x01           Report ID (different from normal 0x03)
     * [1] = 0xC0           Audio-reactive command ID
     * [2] = 0x01 or 0x02   Channel (alternate each packet)
     * [3] = FFT_BIN_0      Bass frequencies (0x00-0xFF)
     * [4] = FFT_BIN_1      Mid frequencies (0x00-0xFF)
     * [5] = FFT_BIN_2      Treble frequencies (0x00-0xFF)
     * [6] = FFT_BIN_3      High frequencies (0x00-0xFF)
     * [7] = 0x01           Fixed parameter
     * [8] = 0x00           Fixed parameter
     * [9-63] = 0x00        Padding
     */

    if (dev == nullptr || !audio_capture)
        return;

    unsigned char buf[64];
    memset(buf, 0, sizeof(buf));

    buf[0] = 0x01;              // Report ID (0x01 for audio command)
    buf[1] = 0xC0;              // Audio-reactive command
    buf[2] = channel;           // Channel (0x01 or 0x02)
    buf[3] = fft_bins[0];       // Bass
    buf[4] = fft_bins[1];       // Midrange
    buf[5] = fft_bins[2];       // Treble
    buf[6] = fft_bins[3];       // High frequency
    buf[7] = 0x01;              // Fixed parameter
    buf[8] = 0x00;              // Fixed parameter

    wrapper.hid_write(dev, buf, 64);
}


