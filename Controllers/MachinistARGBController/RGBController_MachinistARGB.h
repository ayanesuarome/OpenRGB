/*---------------------------------------------------------*\
| RGBController_MachinistARGB.h                             |
|                                                           |
|   RGBController for MACHINIST F-X9D ARGB Controller       |
|                                                           |
|   OpenRGB Team                                            |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#ifndef RGBCONTROLLER_MACHINISTARGB_H
#define RGBCONTROLLER_MACHINISTARGB_H

#include "RGBController.h"
#include "MachinistARGBController.h"

class RGBController_MachinistARGB : public RGBController
{
public:
    RGBController_MachinistARGB(MachinistARGBController* controller_ptr);
    ~RGBController_MachinistARGB();

    void        SetupZones();

    void        DeviceUpdateLEDs() override;
    void        DeviceUpdateZoneLEDs(int zone) override;
    void        DeviceUpdateSingleLED(int led) override;

    void        DeviceUpdateMode() override;

private:
    MachinistARGBController* controller;
    int previous_mode = -1;  // Track previous mode to detect Music mode changes
};

#endif // RGBCONTROLLER_MACHINISTARGB_H
