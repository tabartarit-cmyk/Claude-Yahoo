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

        explicit operator bool() const;
        bool operator==(const USBSettings&) const = default;
    };

    extern USBSettings usb_settings;
}