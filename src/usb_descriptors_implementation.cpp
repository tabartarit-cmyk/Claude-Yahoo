/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Ha Thach (tinyusb.org)
 * Copyright (c) 2022-2024 Arnold Schroeter
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "usb_descriptors.h"
#include "tusb.h"
#include "usb_common.hpp"

//--------------------------------------------------------------------+
#define USBD_DESC_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO10_SPEAKER_STEREO_FB_DESC_LEN(1) + CFG_TUD_CDC * TUD_CDC_DESC_LEN)

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    
    // Use Interface Association Descriptor (IAD) for CDC
    // As required by USB Audio Class v2.0 specs, class should be 0xEF, subclass 0x02, protocol 0x01
    .bDeviceClass = 0xEF,
    .bDeviceSubClass = 0x02,
    .bDeviceProtocol = 0x01,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = 0x2E8A, // Raspberry Pi
    .idProduct = 0x000A, // Dummy product ID
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum {
    ITF_NUM_AUDIO_CONTROL = 0,
    ITF_NUM_AUDIO_STREAMING,
    ITF_NUM_CDC,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

#if ASHA_USB_NET

#define UAC1_BODY \
    TUD_AUDIO10_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX, EPNUM_AUDIO, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_FS, EPNUM_AUDIO_FB, 16000), \
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64)

#define UAC2_BODY \
    TUD_AUDIO20_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX, EPNUM_AUDIO, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_HS, EPNUM_AUDIO_FB, 4), \
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64)

#define UAC1_BASE_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO10_SPEAKER_STEREO_FB_DESC_LEN(1) + TUD_CDC_DESC_LEN)
#define UAC2_BASE_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO20_SPEAKER_STEREO_FB_DESC_LEN + TUD_CDC_DESC_LEN)

#if CFG_TUD_NET
#define NET_BODY \
    TUD_RNDIS_DESCRIPTOR(ITF_NUM_NET, 6, EPNUM_NET_NOTIF, 8, EPNUM_NET_OUT, EPNUM_NET_IN, CFG_TUD_NET_ENDPOINT_SIZE)
#define NET_BODY_ECM \
    TUD_ECM_DESCRIPTOR(ITF_NUM_NET, 6, EPNUM_NET_NOTIF, 64, EPNUM_NET_OUT, EPNUM_NET_IN, CFG_TUD_NET_ENDPOINT_SIZE, 0)
#define NET_LEN TUD_RNDIS_DESC_LEN
#define NET_LEN_ECM TUD_ECM_DESC_LEN
#else
#define NET_BODY
#define NET_BODY_ECM
#define NET_LEN 0
#define NET_LEN_ECM 0
#endif

uint8_t const desc_uac1_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL + (CFG_TUD_NET ? 2 : 0), 0, UAC1_BASE_LEN + NET_LEN, 0x00, 100),
    UAC1_BODY,
    NET_BODY
};

uint8_t const desc_uac2_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL + (CFG_TUD_NET ? 2 : 0), 0, UAC2_BASE_LEN + NET_LEN, 0x00, 100),
    UAC2_BODY,
    NET_BODY
};

uint8_t const desc_uac1_configuration_ecm[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL + (CFG_TUD_NET ? 2 : 0), 0, UAC1_BASE_LEN + NET_LEN_ECM, 0x00, 100),
    UAC1_BODY,
    NET_BODY_ECM
};

uint8_t const desc_uac2_configuration_ecm[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL + (CFG_TUD_NET ? 2 : 0), 0, UAC2_BASE_LEN + NET_LEN_ECM, 0x00, 100),
    UAC2_BODY,
    NET_BODY_ECM
};

#else // !ASHA_USB_NET

#define CONFIG_UAC1_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO10_SPEAKER_STEREO_FB_DESC_LEN(1) + CFG_TUD_CDC * TUD_CDC_DESC_LEN)

uint8_t const desc_uac1_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_UAC1_TOTAL_LEN, 0x00, 100),
    TUD_AUDIO10_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX, EPNUM_AUDIO, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_FS, EPNUM_AUDIO_FB, 16000),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64)
};

TU_VERIFY_STATIC(sizeof(desc_uac1_configuration) == CONFIG_UAC1_TOTAL_LEN, "Incorrect size");

#define CONFIG_UAC2_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO20_SPEAKER_STEREO_FB_DESC_LEN + CFG_TUD_CDC * TUD_CDC_DESC_LEN)

uint8_t const desc_uac2_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_UAC2_TOTAL_LEN, 0x00, 100),
    TUD_AUDIO20_SPEAKER_STEREO_FB_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX, CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX, EPNUM_AUDIO, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_HS, EPNUM_AUDIO_FB, 4),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 5, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64)
};

TU_VERIFY_STATIC(sizeof(desc_uac2_configuration) == CONFIG_UAC2_TOTAL_LEN, "Incorrect size");

#endif // ASHA_USB_NET

// Invoked when received GET CONFIGURATION DESCRIPTOR
uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return (tud_audio_buffer_and_formatting_supported() ? desc_uac2_configuration : desc_uac1_configuration);
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// Array of pointer to string descriptors
char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "Pico-ASHA",                // 1: Manufacturer
    "Pico-ASHA Audio Gateway",  // 2: Product
    "123456",                   // 3: Serial, overridden dynamically from unique ID
    "Pico-ASHA Audio",          // 4: Audio Control
    "Pico-ASHA Serial",         // 5: CDC Control
#if ASHA_USB_NET
    "Pico-ASHA Network",        // 6: Network Interface
#endif
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    switch (index) {
        case 0:
            memcpy(&_desc_str[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;

        case 3: {
            // Get unique ID and convert to string
            pico_unique_board_id_t board_id;
            pico_get_unique_board_id(&board_id);
            for (chr_count = 0; chr_count < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; chr_count++) {
                sprintf((char*)&_desc_str[1 + chr_count * 2], "%02X", board_id.id[chr_count]);
            }
            chr_count *= 2;
            break;
        }

        default: {
            if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;

            const char* str = string_desc_arr[index];
            chr_count = strlen(str);
            size_t const max_len = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
            if (chr_count > max_len) chr_count = max_len;

            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = str[i];
            }
            break;
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}