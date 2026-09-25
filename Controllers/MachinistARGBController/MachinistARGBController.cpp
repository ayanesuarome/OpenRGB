/*---------------------------------------------------------*\
| MachinistARGBController.cpp                               |
|                                                           |
|   Driver for MACHINIST F-X9D ARGB Controller              |
|                                                           |
|   Ayane Suarez                                04 Sep 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <chrono>
#include <cstring>
#include "MachinistARGBController.h"
#include "LogManager.h"
#include "StringUtils.h"

MachinistARGBController::MachinistARGBController(hidapi_wrapper hid_wrapper, hid_device* dev_handle, const std::string& path)
{
    wrapper             = hid_wrapper;
    dev                 = dev_handle;
    location            = path;
    audio_capture       = nullptr;
    music_mode_active   = false;

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
    StopMusicMode();

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
    | 0x11, RGB order, sent once per channel (1 and 2)      |
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
    std::lock_guard<std::mutex> lock(music_mode_mutex);

    if(music_mode_active)
    {
        return;
    }

    if(audio_capture == nullptr)
    {
        audio_capture = new MachinistAudioCapture();

        if(!audio_capture->Initialize())
        {
            LOG_DEBUG("[MachinistARGB] Audio capture initialization failed, using static Music mode");
            delete audio_capture;
            audio_capture = nullptr;
            SendMusic(red, green, blue, brightness);
            return;
        }

        LOG_DEBUG("[MachinistARGB] Audio capture initialized successfully");
    }

    /*-----------------------------------------------------*\
    | The firmware ignores the 0xC0 audio stream unless the |
    | Music effect (0x16) has been selected first           |
    \*-----------------------------------------------------*/
    SendMusic(red, green, blue, brightness);

    if(music_update_thread.joinable())
    {
        music_update_thread.join();
    }

    music_mode_active   = true;
    music_update_thread = std::thread(&MachinistARGBController::MusicUpdateThreadFunction, this);

    LOG_DEBUG("[MachinistARGB] Music mode thread started");
}

void MachinistARGBController::MusicUpdateThreadFunction()
{
    unsigned char channel = 0x01;
    unsigned char fft_bins[3];

    while(music_mode_active && audio_capture != nullptr)
    {
        audio_capture->GetFFTBins(fft_bins);

        SendMusicWithAudio(channel, fft_bins);

        channel = (channel == 0x01) ? 0x02 : 0x01;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_DEBUG("[MachinistARGB] Music mode thread exiting");
}

void MachinistARGBController::StopMusicMode()
{
    std::lock_guard<std::mutex> lock(music_mode_mutex);

    if(!music_mode_active)
    {
        return;
    }

    LOG_DEBUG("[MachinistARGB] Stopping music mode");

    music_mode_active = false;

    if(music_update_thread.joinable())
    {
        music_update_thread.join();
    }

    if(audio_capture != nullptr)
    {
        audio_capture->Stop();
        delete audio_capture;
        audio_capture = nullptr;
    }
}

void MachinistARGBController::SendMusicWithAudio(unsigned char channel, unsigned char* fft_bins)
{
    if(dev == nullptr)
    {
        return;
    }

    /*-----------------------------------------------------*\
    | Music mode audio packet (8 bytes, verified from USB   |
    | capture), channel alternates between 0x01 and 0x02    |
    |   [0]     0x01 Report ID                              |
    |   [1]     0xC0 Audio command                          |
    |   [2]     Channel                                     |
    |   [3-5]   Audio level bytes (0x00-0xFF)               |
    |   [6]     0x01                                        |
    |   [7]     0x00                                        |
    \*-----------------------------------------------------*/
    unsigned char buf[8];
    memset(buf, 0, sizeof(buf));

    buf[0] = 0x01;
    buf[1] = 0xC0;
    buf[2] = channel;
    buf[3] = fft_bins[0];
    buf[4] = fft_bins[1];
    buf[5] = fft_bins[2];
    buf[6] = 0x01;
    buf[7] = 0x00;

    int res = wrapper.hid_write(dev, buf, sizeof(buf));
    if(res < 0)
    {
        LOG_DEBUG("[MachinistARGB] Failed to write music audio packet");
    }
}
