#pragma once

#include "usb_descriptors.h" // ASHA_USB_NET

// Optional USB network interface (RNDIS / CDC-ECM) with a small web server, so
// the hearing aid settings can be changed from any browser (PC or phone)
// connected to the Pico by USB. Enabled with -DPICO_ASHA_USB_NET=ON.

// MAC address of the Pico side of the link (locally administered). Shared by
// the USB descriptors and the TinyUSB network driver.
#define ASHA_NET_MAC_INIT {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00}

namespace asha::usbnet
{
#if ASHA_USB_NET
    // Call once before the USB main loop
    void init();
    // Call regularly from the USB main loop (core 0)
    void task();
#else
    inline void init() {}
    inline void task() {}
#endif
}
