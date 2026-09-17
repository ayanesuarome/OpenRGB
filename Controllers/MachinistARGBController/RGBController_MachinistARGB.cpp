/*---------------------------------------------------------*\
| RGBController_MachinistARGB.cpp                           |
|                                                           |
|   RGBController for MACHINIST F-X9D ARGB Controller       |
|                                                           |
|   OpenRGB Team                                            |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "RGBController_MachinistARGB.h"

/**------------------------------------------------------------------*\
    @name Machinist F-X9D ARGB
    @category Motherboard
    @type  USB
    @save  :x:
    @direct :white_check_mark:
    @effects :white_check_mark:
    @detectors DetectMachinistARGBControllers
    @comment Winbond LED Dongle ARGB Controller for Machinist F-X9D
\*--------------------------------------------------------------------*/

RGBController_MachinistARGB::RGBController_MachinistARGB(MachinistARGBController* controller_ptr)
{
    controller                  = controller_ptr;

    name                        = "Machinist F-X9D ARGB Controller";
    vendor                      = "Machinist";
    type                        = DEVICE_TYPE_MOTHERBOARD;
    description                 = "Machinist F-X9D ARGB Controller";
    location                    = controller->GetDeviceLocation();
    serial                      = controller->GetSerialString();

    mode Direct;
    Direct.name                 = "Direct";
    Direct.value                = 0;
    Direct.flags                = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
    Direct.color_mode           = MODE_COLORS_PER_LED;
    Direct.brightness_min       = 0x00;
    Direct.brightness_max       = 0xFF;
    Direct.brightness           = 0xFF;
    modes.push_back(Direct);

    mode Static;
    Static.name                 = "Static";
    Static.value                = 1;
    Static.flags                = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_BRIGHTNESS;
    Static.color_mode           = MODE_COLORS_PER_LED;
    Static.brightness_min       = 0x00;
    Static.brightness_max       = 0xFF;
    Static.brightness           = 0xFF;
    modes.push_back(Static);

    mode Breathing;
    Breathing.name              = "Breathing";
    Breathing.value             = 2;
    Breathing.flags             = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS;
    Breathing.color_mode        = MODE_COLORS_PER_LED;
    Breathing.speed_min         = 0x00;
    Breathing.speed_max         = 0xFF;
    Breathing.speed             = 0x7F;
    Breathing.brightness_min    = 0x00;
    Breathing.brightness_max    = 0xFF;
    Breathing.brightness        = 0xFF;
    modes.push_back(Breathing);

    mode RainbowWave;
    RainbowWave.name            = "Rainbow Wave"; // Staggered moving rainbow pattern.
    RainbowWave.value           = 3;
    RainbowWave.flags           = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS;
    RainbowWave.color_mode      = MODE_COLORS_PER_LED;
    RainbowWave.speed_min       = 0x00;
    RainbowWave.speed_max       = 0xFF;
    RainbowWave.speed           = 0x7F;
    RainbowWave.brightness_min  = 0x00;
    RainbowWave.brightness_max  = 0xFF;
    RainbowWave.brightness      = 0xFF;
    modes.push_back(RainbowWave);

    mode SpectrumCycle;
    SpectrumCycle.name          = "Spectrum Cycle"; // Full-spectrum color cycle.
    SpectrumCycle.value         = 4;
    SpectrumCycle.flags         = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS;
    SpectrumCycle.color_mode    = MODE_COLORS_PER_LED;
    SpectrumCycle.speed_min     = 0x00;
    SpectrumCycle.speed_max     = 0xFF;
    SpectrumCycle.speed         = 0x7F;
    SpectrumCycle.brightness_min= 0x00;
    SpectrumCycle.brightness_max= 0xFF;
    SpectrumCycle.brightness    = 0xFF;
    modes.push_back(SpectrumCycle);

    mode Rainbow;
    Rainbow.name                = "Rainbow"; // Distinct from Rainbow Wave and Spectrum Cycle on this device.
    Rainbow.value               = 5;
    Rainbow.flags               = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS;
    Rainbow.color_mode          = MODE_COLORS_PER_LED;
    Rainbow.speed_min           = 0x00;
    Rainbow.speed_max           = 0xFF;
    Rainbow.speed               = 0x7F;
    Rainbow.brightness_min      = 0x00;
    Rainbow.brightness_max      = 0xFF;
    Rainbow.brightness          = 0xFF;
    modes.push_back(Rainbow);

    mode Spring;
    Spring.name                 = "Spring"; // Device-specific spring-like pulse animation.
    Spring.value                = 6;
    Spring.flags                = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS;
    Spring.color_mode           = MODE_COLORS_PER_LED;
    Spring.speed_min            = 0x00;
    Spring.speed_max            = 0xFF;
    Spring.speed                = 0x7F;
    Spring.brightness_min       = 0x00;
    Spring.brightness_max       = 0xFF;
    Spring.brightness           = 0xFF;
    modes.push_back(Spring);

    mode Water;
    Water.name                  = "Water"; // Device-specific flowing/ripple animation.
    Water.value                 = 7;
    Water.flags                 = MODE_FLAG_HAS_PER_LED_COLOR | MODE_FLAG_HAS_SPEED | MODE_FLAG_HAS_BRIGHTNESS;
    Water.color_mode            = MODE_COLORS_PER_LED;
    Water.speed_min             = 0x00;
    Water.speed_max             = 0xFF;
    Water.speed                 = 0x7F;
    Water.brightness_min        = 0x00;
    Water.brightness_max        = 0xFF;
    Water.brightness            = 0xFF;
    modes.push_back(Water);

    mode Music;
    Music.name                  = "Music"; // Audio-reactive device mode.
    Music.value                 = 8;
    Music.flags                 = MODE_FLAG_HAS_PER_LED_COLOR;
    Music.color_mode            = MODE_COLORS_PER_LED;
    modes.push_back(Music);

    SetupZones();
}

RGBController_MachinistARGB::~RGBController_MachinistARGB()
{
    Shutdown();

    delete controller;
}

void RGBController_MachinistARGB::SetupZones()
{
    zones.clear();
    leds.clear();
    colors.clear();

    zone arbg_zone;
    arbg_zone.name              = "ARGB Header 1";
    arbg_zone.type              = ZONE_TYPE_LINEAR;
    arbg_zone.leds_min          = 1;
    arbg_zone.leds_max          = 1;
    arbg_zone.leds_count        = 1;
    zones.push_back(arbg_zone);

    for(unsigned int led_idx = 0; led_idx < zones[0].leds_count; led_idx++)
    {
        led new_led;
        new_led.name            = "LED " + std::to_string(led_idx + 1);
        leds.push_back(new_led);
    }

    SetupColors();
}

void RGBController_MachinistARGB::DeviceUpdateLEDs()
{
    if(colors.size() > 0)
    {
        unsigned char red       = RGBGetRValue(colors[0]);
        unsigned char green     = RGBGetGValue(colors[0]);
        unsigned char blue      = RGBGetBValue(colors[0]);

        switch(active_mode)
        {
            case 0:  // Direct
            case 1:  // Static
            {
                unsigned char brightness = static_cast<unsigned char>(modes[active_mode].brightness);
                controller->SendColor(red, green, blue, brightness);
                break;
            }
            case 2:  // Breathing (0x12)
            {
                unsigned char speed = static_cast<unsigned char>(modes[active_mode].speed);
                unsigned char brightness = static_cast<unsigned char>(modes[active_mode].brightness);
                controller->SendBreathing(red, green, blue, speed, brightness);
                break;
            }
            case 3:  // Rainbow Wave (0x17)
            {
                unsigned char speed = static_cast<unsigned char>(modes[active_mode].speed);
                unsigned char brightness = static_cast<unsigned char>(modes[active_mode].brightness);
                controller->SendRainbowWave(red, green, blue, speed, brightness);
                break;
            }
            case 4:  // Spectrum Cycle (0x14)
            {
                unsigned char speed = static_cast<unsigned char>(modes[active_mode].speed);
                unsigned char brightness = static_cast<unsigned char>(modes[active_mode].brightness);
                controller->SendSpectrumCycle(red, green, blue, speed, brightness);
                break;
            }
            case 5:  // Rainbow (0x1A)
            {
                unsigned char speed = static_cast<unsigned char>(modes[active_mode].speed);
                unsigned char brightness = static_cast<unsigned char>(modes[active_mode].brightness);
                controller->SendRainbow(red, green, blue, speed, brightness);
                break;
            }
            case 6:  // Spring (0x18)
            {
                unsigned char speed = static_cast<unsigned char>(modes[active_mode].speed);
                unsigned char brightness = static_cast<unsigned char>(modes[active_mode].brightness);
                controller->SendSpring(red, green, blue, speed, brightness);
                break;
            }
            case 7:  // Water (0x19)
            {
                unsigned char speed = static_cast<unsigned char>(modes[active_mode].speed);
                unsigned char brightness = static_cast<unsigned char>(modes[active_mode].brightness);
                controller->SendWater(red, green, blue, speed, brightness);
                break;
            }
            case 8: // Music (0x16)
            {
                // No brightness slider for this mode; always runs at max.
                controller->StartMusicMode(red, green, blue, 0xFF);
                break;
            }
            default:
                break;
        }
    }
}

void RGBController_MachinistARGB::DeviceUpdateZoneLEDs(int /*zone*/)
{
    DeviceUpdateLEDs();
}

void RGBController_MachinistARGB::DeviceUpdateSingleLED(int /*led*/)
{
    DeviceUpdateLEDs();
}

void RGBController_MachinistARGB::DeviceUpdateMode()
{
    /*
     * Handle mode transitions, especially for Music mode
     * which requires audio capture to be started/stopped
     */

    // If switching away from Music mode, stop audio capture
    if (previous_mode == 8 && active_mode != 8)
    {
        controller->StopMusicMode();
    }

    // If switching to Music mode, DeviceUpdateLEDs will handle the startup
    DeviceUpdateLEDs();

    // Update previous mode tracker
    previous_mode = active_mode;
}
