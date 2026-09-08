/*---------------------------------------------------------*\
| MachinistARGBControllerDetect.cpp                         |
|                                                           |
|   Detector for MACHINIST F-X9D ARGB Controller            |
|                                                           |
|   OpenRGB Team                                            |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include "DetectionManager.h"
#include "MachinistARGBController.h"
#include "RGBController_MachinistARGB.h"
#include <hidapi.h>

DetectedControllers DetectMachinistARGBControllers(hidapi_wrapper wrapper, hid_device_info* info, const std::string& /*name*/)
{
    DetectedControllers detected_controllers;
    hid_device*         dev;

    dev = wrapper.hid_open_path(info->path);

    if(dev)
    {
        MachinistARGBController*    controller      = new MachinistARGBController(wrapper, dev, info->path);
        RGBController_MachinistARGB* rgb_controller = new RGBController_MachinistARGB(controller);

        detected_controllers.push_back(rgb_controller);
    }

    return(detected_controllers);
}

/*-----------------------------------------------------------------*\
| This device is only enumerated via the dynamically-loaded         |
| libusb HID backend on Linux (hidraw backend never lists it, and   |
| reports usage_page/usage as 0), so it must use the wrapped        |
| detector to open/write/close through the matching backend.        |
| Filtering on usage_page/usage also picks the single vendor         |
| interface and avoids duplicate registrations from other           |
| collections exposed by the same physical device.                  |
\*-----------------------------------------------------------------*/
REGISTER_HID_WRAPPED_DETECTOR_PU("Machinist F-X9D ARGB", DetectMachinistARGBControllers, MACHINIST_VID, MACHINIST_PID, MACHINIST_USAGE_PAGE, MACHINIST_USAGE);
