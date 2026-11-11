#include <cstdint>
#include <cstdio>
#include <cstring>

#include <btstack.h>

#include "gatt_server.hpp"

#ifndef PICO_ASHA_FW_VERS_MAJOR
#define PICO_ASHA_FW_VERS_MAJOR 0
#endif
#ifndef PICO_ASHA_FW_VERS_MINOR
#define PICO_ASHA_FW_VERS_MINOR 0
#endif
#ifndef PICO_ASHA_FW_VERS_PATCH
#define PICO_ASHA_FW_VERS_PATCH 0
#endif

// PnP ID announced to the peers (Device Information service).
//   Vendor ID source: 1 = Bluetooth SIG company ID, 2 = USB-IF vendor ID.
// By default these are the USB identifiers of the adapter (see
// usb_descriptors.cpp). They can be overridden at compile time, for example
// with -DPICO_ASHA_PNP_VENDOR_SOURCE=1 -DPICO_ASHA_PNP_VENDOR_ID=0x00E0.
#ifndef PICO_ASHA_PNP_VENDOR_SOURCE
#define PICO_ASHA_PNP_VENDOR_SOURCE 0x02
#endif
#ifndef PICO_ASHA_PNP_VENDOR_ID
#define PICO_ASHA_PNP_VENDOR_ID 0xCAFE
#endif
#ifndef PICO_ASHA_PNP_PRODUCT_ID
#define PICO_ASHA_PNP_PRODUCT_ID 0x4011
#endif

namespace asha
{

namespace
{

// 16-bit UUIDs assigned by the Bluetooth SIG
constexpr uint16_t uuid_generic_access      = 0x1800;
constexpr uint16_t uuid_generic_attribute   = 0x1801;
constexpr uint16_t uuid_device_information  = 0x180A;
constexpr uint16_t uuid_device_name         = 0x2A00;
constexpr uint16_t uuid_appearance          = 0x2A01;
constexpr uint16_t uuid_service_changed     = 0x2A05;
constexpr uint16_t uuid_model_number        = 0x2A24;
constexpr uint16_t uuid_firmware_revision   = 0x2A26;
constexpr uint16_t uuid_manufacturer_name   = 0x2A29;
constexpr uint16_t uuid_pnp_id              = 0x2A50;

// No security requirement: these attributes must be readable at any time,
// including before the link is encrypted (ATT_SECURITY_NONE).
constexpr uint8_t security_none = 0;

// Static values. att_db_util copies them into the ATT database.
uint8_t device_name[]       = "Pico-ASHA";
uint8_t appearance[2]       = {0x00, 0x00}; // Unknown
uint8_t manufacturer_name[] = "Pico-ASHA";
uint8_t model_number[]      = "USB audio to ASHA adapter";
uint8_t firmware_revision[16] = {};
uint8_t pnp_id[7]           = {};

} // namespace

void gatt_server_init()
{
    // Values built at run time
    std::snprintf(reinterpret_cast<char*>(firmware_revision), sizeof firmware_revision, "%d.%d.%d",
                  (int)PICO_ASHA_FW_VERS_MAJOR, (int)PICO_ASHA_FW_VERS_MINOR, (int)PICO_ASHA_FW_VERS_PATCH);

    // PnP ID: vendor ID source (1), vendor ID (2), product ID (2), product version (2)
    uint16_t const pnp_version = (uint16_t)(((PICO_ASHA_FW_VERS_MAJOR & 0xFF) << 8)
                                          | ((PICO_ASHA_FW_VERS_MINOR & 0x0F) << 4)
                                          |  (PICO_ASHA_FW_VERS_PATCH & 0x0F));
    pnp_id[0] = (uint8_t)PICO_ASHA_PNP_VENDOR_SOURCE;
    little_endian_store_16(pnp_id, 1, (uint16_t)PICO_ASHA_PNP_VENDOR_ID);
    little_endian_store_16(pnp_id, 3, (uint16_t)PICO_ASHA_PNP_PRODUCT_ID);
    little_endian_store_16(pnp_id, 5, pnp_version);

    // Build the ATT database
    att_db_util_init();

    // GAP
    att_db_util_add_service_uuid16(uuid_generic_access);
    att_db_util_add_characteristic_uuid16(uuid_device_name, ATT_PROPERTY_READ, security_none, security_none,
                                          device_name, (uint16_t)std::strlen(reinterpret_cast<char*>(device_name)));
    att_db_util_add_characteristic_uuid16(uuid_appearance, ATT_PROPERTY_READ, security_none, security_none,
                                          appearance, sizeof appearance);

    // GATT: Service Changed (indication). The client characteristic
    // configuration descriptor is added automatically.
    att_db_util_add_service_uuid16(uuid_generic_attribute);
    att_db_util_add_characteristic_uuid16(uuid_service_changed, ATT_PROPERTY_INDICATE, security_none, security_none,
                                          nullptr, 0);

    // Device Information
    att_db_util_add_service_uuid16(uuid_device_information);
    att_db_util_add_characteristic_uuid16(uuid_manufacturer_name, ATT_PROPERTY_READ, security_none, security_none,
                                          manufacturer_name, (uint16_t)std::strlen(reinterpret_cast<char*>(manufacturer_name)));
    att_db_util_add_characteristic_uuid16(uuid_model_number, ATT_PROPERTY_READ, security_none, security_none,
                                          model_number, (uint16_t)std::strlen(reinterpret_cast<char*>(model_number)));
    att_db_util_add_characteristic_uuid16(uuid_firmware_revision, ATT_PROPERTY_READ, security_none, security_none,
                                          firmware_revision, (uint16_t)std::strlen(reinterpret_cast<char*>(firmware_revision)));
    att_db_util_add_characteristic_uuid16(uuid_pnp_id, ATT_PROPERTY_READ, security_none, security_none,
                                          pnp_id, sizeof pnp_id);

    // All values are static: no read/write callbacks are required. Writes to
    // the Service Changed client characteristic configuration are handled by
    // the ATT server itself.
    att_server_init(att_db_util_get_address(), nullptr, nullptr);
}

} // namespace asha
