#pragma once

// ---------------------------------------------------------------------------
// Firmware variants (Raspberry Pi Pico 2 W)
//
// Exactly one variant is active per build. CMake defines
// PICO_ASHA_VARIANT_ASHA, PICO_ASHA_VARIANT_MFI or PICO_ASHA_VARIANT_CLASSIC
// (targets pico_asha, pico_asha_mfi and pico_asha_classic).
//
//  ASHA    : ASHA only (service 0xFDF0, L2CAP CoC, G.722). The only variant
//            which streams audio. No MFi step is run (no discovery, no read,
//            no battery notification).
//  MFI     : MFi only (Apple service, battery). Diagnostic probe: connect,
//            pair, read the battery level and enable its notifications.
//            No ASHA step. No audio: Apple's MFi audio protocol is not
//            public.
//  CLASSIC : Bluetooth Classic only (inquiry + SDP). Diagnostic probe: no
//            BLE scan, no ASHA or MFi connection,
//            no audio.
// ---------------------------------------------------------------------------

#if !defined(PICO_ASHA_VARIANT_ASHA) && !defined(PICO_ASHA_VARIANT_MFI) && !defined(PICO_ASHA_VARIANT_CLASSIC)
#define PICO_ASHA_VARIANT_ASHA 1
#endif

#if (defined(PICO_ASHA_VARIANT_ASHA) + defined(PICO_ASHA_VARIANT_MFI) + defined(PICO_ASHA_VARIANT_CLASSIC)) != 1
#error "Define exactly one of PICO_ASHA_VARIANT_ASHA, PICO_ASHA_VARIANT_MFI, PICO_ASHA_VARIANT_CLASSIC"
#endif

namespace asha::variant
{

#if defined(PICO_ASHA_VARIANT_MFI)
inline constexpr bool uses_asha    = false;
inline constexpr bool uses_mfi     = true;
inline constexpr bool uses_classic = false;
inline constexpr const char* name  = "MFI";
#elif defined(PICO_ASHA_VARIANT_CLASSIC)
inline constexpr bool uses_asha    = false;
inline constexpr bool uses_mfi     = false;
inline constexpr bool uses_classic = true;
inline constexpr const char* name  = "Classic";
#else
inline constexpr bool uses_asha    = true;
inline constexpr bool uses_mfi     = false;
inline constexpr bool uses_classic = false;
inline constexpr const char* name  = "ASHA";
#endif

// Variants which use Bluetooth Low Energy (scan, connection, GATT)
inline constexpr bool uses_ble = !uses_classic;

} // namespace asha::variant
