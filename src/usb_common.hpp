#pragma once

#include <cstdint>

#include "asha_audio.h"

namespace asha
{
    struct USBSettings {
        uint16_t uac_version = 2U;
        int16_t min_vol = ASHA_USB_VOL_MIN;
        int16_t max_vol = ASHA_USB_VOL_MAX;
        // Digital attenuation threshold: levels below it are not sent to the
        // hearing aids as a volume command (their volume stays at this value);
        // the PCM signal is attenuated digitally instead, like a software
        // volume control. The default is the hearing aids' own floor.
        int16_t dig_thr = ASHA_USB_VOL_MIN;

        explicit operator bool() const;
        bool operator==(const USBSettings&) const = default;
    };

    // Smartphone (Bluetooth audio input) settings. Steps are 0.375 dB.
    struct PhoneSettings {
        // Attenuation of the USB audio while the phone plays (-240 = muted)
        int16_t duck = -240;
        // Level of the phone audio, on the same scale as the USB volume
        int16_t level = -100;
        // Bluetooth quality offered to the phone, applied at the next start:
        // 0 standard, 1 economic, 2 minimal (mono), 3 16 kHz mono (experimental)
        uint8_t profile = 0;

        bool valid() const { return duck >= -240 && duck <= 0 && level >= -240 && level <= 0 && profile <= 3; }
        bool operator==(const PhoneSettings&) const = default;
    };

    extern USBSettings usb_settings;
}