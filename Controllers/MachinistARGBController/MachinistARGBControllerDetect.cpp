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
| This dongle is probed through two separate HID backends on       |
| Linux (plain hidraw and libusb). The plain hidraw enumeration is  |
| unreliable for this device (it intermittently fails to enumerate |
| it at all), so detection must rely on the libusb backend, which  |
| consistently reports usage_page=0x0000 usage=0x0000 interface=0  |
| for this device. On Windows the (only) backend reports           |
| usage_page=0x0001 usage=0x0000 interface=0.                      |
\*-----------------------------------------------------------------*/
#ifdef _WIN32
REGISTER_HID_WRAPPED_DETECTOR_IPU("Machinist F-X9D ARGB", DetectMachinistARGBControllers, MACHINIST_VID, MACHINIST_PID, 0, 0x0001, 0x0000);
#else
REGISTER_HID_WRAPPED_DETECTOR_IPU("Machinist F-X9D ARGB", DetectMachinistARGBControllers, MACHINIST_VID, MACHINIST_PID, 0, 0x0000, 0x0000);
#endif
