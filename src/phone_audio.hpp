#pragma once

#include "asha_audio.h"
#include "usb_common.hpp"

// Smartphone audio input over Bluetooth Classic (A2DP sink), built with
// -DPICO_ASHA_A2DP=ON. While the phone plays, its audio replaces the USB audio
// (or the USB audio is attenuated by a configurable amount); the USB audio
// comes back at its previous level when the phone stops.
//
// Everything here runs on the Bluetooth core (BTstack run loop).

namespace asha::phone
{
#if ASHA_A2DP
    // Current settings (read from the audio level code)
    extern PhoneSettings settings;

    // After l2cap_init() and runtime_settings.init(), before hci_power_control()
    void init();
    // Periodically from the BTstack timer: reports state changes to the host
    void service();
    // The serial host (re)connected: send the state again
    void on_serial_connected();
    // Make the Pico visible to phones for two minutes (or stop)
    void set_pairing(bool enabled);
    // Forget every paired phone
    void forget_all();
    // Apply (and store) new settings
    bool apply_settings(PhoneSettings const& new_settings);
#else
    inline void init() {}
    inline void service() {}
    inline void on_serial_connected() {}
    inline void set_pairing(bool) {}
    inline void forget_all() {}
    inline bool apply_settings(PhoneSettings const&) { return false; }
#endif
}
