/*---------------------------------------------------------*\
| MachinistARGBController.h                                 |
|                                                           |
|   Driver for MACHINIST F-X9D ARGB Controller              |
|                                                           |
|   Ayane Suarez                                04 Sep 2026 |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <hidapi.h>
#include "hidapi_wrapper.h"
#include "MachinistAudioCapture.h"

#define MACHINIST_VID 0x0416
#define MACHINIST_PID 0x0125

/*---------------------------------------------------------*\
| Low-level USB HID driver for Machinist ARGB controller    |
| protocol operations, including Music mode audio updates.  |
\*---------------------------------------------------------*/
class MachinistARGBController
{
public:
    MachinistARGBController(hidapi_wrapper hid_wrapper, hid_device* dev_handle, const std::string& path);
    ~MachinistARGBController();

    std::string GetDeviceLocation();
    std::string GetSerialString();

    void        SendColor(unsigned char red, unsigned char green, unsigned char blue, unsigned char brightness);
    void        SendBreathing(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness);
    void        SendRainbowWave(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness);
    void        SendSpectrumCycle(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness);
    void        SendRainbow(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness);
    void        SendSpring(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness);
    void        SendWater(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness);
    void        SendMusic(unsigned char red, unsigned char green, unsigned char blue, unsigned char brightness);
    void        StartMusicMode(unsigned char red, unsigned char green, unsigned char blue, unsigned char brightness);
    void        StopMusicMode();

private:
    void        SendAnimatedEffect(unsigned char effect_id, unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness);
    void        SendMusicWithAudio(unsigned char channel, unsigned char* fft_bins);
    void        MusicUpdateThreadFunction();

    hidapi_wrapper          wrapper;
    hid_device*             dev;
    std::string             location;
    std::string             serial;
    MachinistAudioCapture*  audio_capture;
    std::thread             music_update_thread;
    std::atomic<bool>       music_mode_active;

    /*-----------------------------------------------------*\
    | DeviceUpdateLEDs() may be called from multiple        |
    | threads, serialize Music mode start and stop          |
    \*-----------------------------------------------------*/
    std::mutex              music_mode_mutex;
};
