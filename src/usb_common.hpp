#pragma once

#include <cstdint>

#include "asha_audio.h"

namespace asha
{
    struct USBSettings {
        uint16_t uac_version = 2U;
        int16_t min_vol = ASHA_USB_VOL_MIN;
        int16_t max_vol = ASHA_USB_VOL_MAX;
        // Volume curve exponent in hundredths (100 = 1.00 = unchanged).
        // Applied to the normalised position in the min..max range.
        uint16_t curve = ASHA_VOL_CURVE_DEFAULT;
        // Range of levels the OS actually sends for its volume slider (what
        // it gives at the bottom and at 100%). Calibrated by the user from the
        // "Current Volume" readings, because the OS may not follow the range
        // announced by the device.
        int16_t win_min = ASHA_WIN_RANGE_MIN;
        int16_t win_max = ASHA_WIN_RANGE_MAX;

        explicit operator bool() const;
        bool operator==(const USBSettings&) const = default;
    };

    extern USBSettings usb_settings;
}