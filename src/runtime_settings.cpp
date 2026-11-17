#include "runtime_settings.hpp"

#include <hardware/structs/watchdog.h>

#include "util.hpp"

namespace asha
{

// Scratch-register encoding for deferred TLV writes across a watchdog reboot.
// scratch[0] = command word; scratch[1] = packed data (both commands).
// HCI dump:     scratch[1] bit 0 = enabled flag.
// USB settings: scratch[1] bits  0-15 = (min_vol - ASHA_USB_VOL_MIN_EXT),
//               scratch[1] bits 16-31 = (max_vol - ASHA_USB_VOL_MIN_EXT),
//               scratch[3] bits  0-1  = uac_version,
//               scratch[3] bits  2-16 = (dig_thr - ASHA_USB_VOL_MIN_EXT).
// Scratch registers survive a watchdog reset but clear on power-on reset.
static constexpr uint32_t SCRATCH_CMD_HCI_DUMP     = 0x41534801u; // 'A','S','H',1
static constexpr uint32_t SCRATCH_CMD_USB_SETTINGS = 0x41534802u; // 'A','S','H',2

void RuntimeSettings::init()
{
    mutex_enter_blocking(&mtx);
    btstack_tlv_get_instance(&tlv_impl, &tlv_ctx);
    apply_pending_scratch();
    get_settings();
    mutex_exit(&mtx);
}

bool RuntimeSettings::get_hci_dump_enabled()
{
    mutex_enter_blocking(&mtx);
    bool enabled = hci_dump_enabled;
    mutex_exit(&mtx);
    return enabled;
}

bool RuntimeSettings::get_full_set_paired()
{
    mutex_enter_blocking(&mtx);
    bool paired = full_set_paired;
    mutex_exit(&mtx);
    return paired;
}

USBSettings RuntimeSettings::get_usb_settings()
{
    mutex_enter_blocking(&mtx);
    USBSettings settings = usb_settings;
    mutex_exit(&mtx);
    return settings;
}

PhoneSettings RuntimeSettings::get_phone_settings()
{
    mutex_enter_blocking(&mtx);
    PhoneSettings settings = phone_settings;
    mutex_exit(&mtx);
    return settings;
}

bool RuntimeSettings::set_phone_settings(PhoneSettings const& settings)
{
    if (!settings.valid()) return false;
    mutex_enter_blocking(&mtx);
    bool ok = store_tlv_tag(Tag::PhoneSetting, settings);
    if (ok) phone_settings = settings;
    mutex_exit(&mtx);
    return ok;
}

RuntimeSettings::operator bool()
{
    mutex_enter_blocking(&mtx);
    bool ok = (tlv_impl && tlv_ctx && got_settings);
    mutex_exit(&mtx);
    return ok;
}

/* Get settings from flash. Settings not found in flash
   will be set to their default setting. Default settings
   are not written to flash to both conserve flash and
   to make updating default settings easier */
void RuntimeSettings::get_settings()
{
    if (!get_tlv_tag(Tag::PhoneSetting, phone_settings) || !phone_settings.valid()) {
        phone_settings = PhoneSettings{};
    }
    if (!get_tlv_tag(Tag::HCIDump, hci_dump_enabled)) {
        hci_dump_enabled = false;
    }
    if (!get_tlv_tag(Tag::FullSetPaired, full_set_paired)) {
        full_set_paired = false;
    }
    if (!get_tlv_tag(Tag::USBSetting, usb_settings)) {
        if (!usb_settings) {
            usb_settings = USBSettings();
        }
    }
    got_settings = true;
}

bool RuntimeSettings::set_hci_dump_enabled(bool is_enabled)
{
    bool res = false;
    mutex_enter_blocking(&mtx);
    if (hci_dump_enabled != is_enabled) {
        hci_dump_enabled = is_enabled;
        res = store_tlv_tag(Tag::HCIDump, hci_dump_enabled);
    }
    mutex_exit(&mtx);
    return res;
}

bool RuntimeSettings::set_full_set_paired(bool have_full_set)
{
    bool res = false;
    mutex_enter_blocking(&mtx);
    if (full_set_paired != have_full_set) {
        full_set_paired = have_full_set;
        res = store_tlv_tag(Tag::FullSetPaired, have_full_set);
    }
    mutex_exit(&mtx);
    return res;
}

bool RuntimeSettings::set_usb_settings(USBSettings const &settings)
{
    bool res = false;
    mutex_enter_blocking(&mtx);
    if (settings && settings != usb_settings) {
        usb_settings = settings;
        res = store_tlv_tag(Tag::USBSetting, usb_settings);
    }
    mutex_exit(&mtx);
    return res;
}

void RuntimeSettings::defer_hci_dump(bool enabled)
{
    watchdog_hw->scratch[0] = SCRATCH_CMD_HCI_DUMP;
    watchdog_hw->scratch[1] = enabled ? 1u : 0u;
}

void RuntimeSettings::defer_usb_settings(USBSettings const& s)
{
    watchdog_hw->scratch[0] = SCRATCH_CMD_USB_SETTINGS;
    watchdog_hw->scratch[1] =
          (uint32_t)(s.min_vol - ASHA_USB_VOL_MIN_EXT)          // bits 0-15
        | ((uint32_t)(s.max_vol - ASHA_USB_VOL_MIN_EXT) << 16); // bits 16-31
    watchdog_hw->scratch[3] = (uint32_t)(s.uac_version & 3u)
        | ((uint32_t)(s.dig_thr - ASHA_USB_VOL_MIN_EXT) << 2); // bits 2-16
}

// Called from init() after btstack_tlv_get_instance(), before get_settings().
// Writes any deferred setting to TLV so get_settings() picks it up normally.
void RuntimeSettings::apply_pending_scratch()
{
    switch (watchdog_hw->scratch[0]) {
    case SCRATCH_CMD_HCI_DUMP: {
        bool enabled = watchdog_hw->scratch[1] != 0u;
        store_tlv_tag(Tag::HCIDump, enabled);
        break;
    }
    case SCRATCH_CMD_USB_SETTINGS: {
        uint32_t p = watchdog_hw->scratch[1];
        USBSettings s;
        s.min_vol     = (int16_t)(p & 0xFFFFu) + ASHA_USB_VOL_MIN_EXT;
        s.max_vol     = (int16_t)((p >> 16) & 0xFFFFu) + ASHA_USB_VOL_MIN_EXT;
        uint32_t q = watchdog_hw->scratch[3];
        s.uac_version = (uint16_t)(q & 3u);
        s.dig_thr     = (int16_t)((q >> 2) & 0x7FFFu) + ASHA_USB_VOL_MIN_EXT;
        store_tlv_tag(Tag::USBSetting, s);
        break;
    }
    default:
        break;
    }
    watchdog_hw->scratch[0] = 0u;
    watchdog_hw->scratch[1] = 0u;
    watchdog_hw->scratch[3] = 0u;
}

} // namespace asha