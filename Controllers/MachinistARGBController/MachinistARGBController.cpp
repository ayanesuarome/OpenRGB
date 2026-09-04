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

void MachinistARGBController::SendWave(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
{
    SendAnimatedEffect(0x17, red, green, blue, speed, brightness);
}

void MachinistARGBController::SendCycling(unsigned char red, unsigned char green, unsigned char blue, unsigned char speed, unsigned char brightness)
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

