#include "phone_audio.hpp"

#if ASHA_A2DP

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "btstack.h"
#include "classic/btstack_link_key_db_tlv.h"

#include "asha_comms.hpp"
#include "runtime_settings.hpp"

namespace asha::phone
{

PhoneSettings settings = {};

static constexpr uint32_t pairing_duration_ms = 120000;
static constexpr uint32_t media_header_len = 12; // RTP header without CSRC
static constexpr uint32_t sbc_header_len = 1;

// SBC: 16 kHz, Stereo / Joint Stereo, 16 blocks, 8 subbands, Loudness, bitpool 2..58 (~128-129 kbps total, ~64 kbps/ch)
static uint8_t sbc_capabilities[] = {0x83, 0x15, 2, 58};
static uint8_t sbc_configuration[4];

static btstack_packet_callback_registration_t hci_callback;
static uint8_t sdp_sink_record[150];

static const btstack_sbc_decoder_t* sbc_decoder = nullptr;
static btstack_sbc_decoder_bluedroid_t sbc_decoder_context;

static bd_addr_t phone_addr = {};
static char phone_name[28] = {};
static bool phone_connected = false;
static bool pairing_mode = false;
static bool dirty = true;
static bool last_audible = false;
static btstack_timer_source_t pairing_timer = {};

// ----- Decoded audio -> audio core

static void handle_pcm_data(int16_t* data, int num_frames, int num_channels, int sample_rate, void* context)
{
    (void)context;
    if (num_frames <= 0) return;
    if (num_channels == 2) {
        asha_audio_phone_push(data, (uint32_t)num_frames, (uint32_t)sample_rate);
        return;
    }
    // Mono: duplicate to both channels
    int16_t stereo[2 * 128];
    int done = 0;
    while (done < num_frames) {
        int const n = std::min(num_frames - done, 128);
        for (int i = 0; i < n; ++i) {
            stereo[2 * i] = data[done + i];
            stereo[2 * i + 1] = data[done + i];
        }
        asha_audio_phone_push(stereo, (uint32_t)n, (uint32_t)sample_rate);
        done += n;
    }
}

static void handle_media_packet(uint8_t seid, uint8_t* packet, uint16_t size)
{
    (void)seid;
    if (!sbc_decoder) return;
    if (size <= media_header_len + sbc_header_len) return;
    uint8_t* const sbc = packet + media_header_len + sbc_header_len;
    uint16_t const length = (uint16_t)(size - media_header_len - sbc_header_len);
    sbc_decoder->decode_signed_16(&sbc_decoder_context, 0, sbc, length);
}

// ----- Events

static void start_decoder()
{
    sbc_decoder = btstack_sbc_decoder_bluedroid_init_instance(&sbc_decoder_context);
    sbc_decoder->configure(&sbc_decoder_context, SBC_MODE_STANDARD, handle_pcm_data, nullptr);
}

static void a2dp_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;

    switch (packet[2]) {
        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            if (a2dp_subevent_stream_established_get_status(packet) != ERROR_CODE_SUCCESS) break;
            a2dp_subevent_stream_established_get_bd_addr(packet, phone_addr);
            phone_connected = true;
            phone_name[0] = '\0';
            gap_remote_name_request(phone_addr, 0, 0);
            dirty = true;
            break;
        case A2DP_SUBEVENT_STREAM_STARTED:
            start_decoder();
            asha_audio_phone_set_stream(true);
            dirty = true;
            break;
        case A2DP_SUBEVENT_STREAM_SUSPENDED:
        case A2DP_SUBEVENT_STREAM_RELEASED:
            asha_audio_phone_set_stream(false);
            dirty = true;
            break;
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            asha_audio_phone_set_stream(false);
            phone_connected = false;
            dirty = true;
            break;
        default:
            break;
    }
}

static void hci_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size)
{
    (void)channel;
    (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_PIN_CODE_REQUEST: {
            // Phones without Secure Simple Pairing
            bd_addr_t address;
            hci_event_pin_code_request_get_bd_addr(packet, address);
            gap_pin_code_response(address, "0000");
            break;
        }
        case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE: {
            bd_addr_t address;
            hci_event_remote_name_request_complete_get_bd_addr(packet, address);
            if (hci_event_remote_name_request_complete_get_status(packet) == 0 && bd_addr_cmp(address, phone_addr) == 0) {
                std::snprintf(phone_name, sizeof(phone_name), "%s", hci_event_remote_name_request_complete_get_remote_name(packet));
                dirty = true;
            }
            break;
        }
        default:
            break;
    }
}

// ----- Pairing mode

static void pairing_timeout(btstack_timer_source_t* timer)
{
    (void)timer;
    set_pairing(false);
}

void set_pairing(bool enabled)
{
    btstack_run_loop_remove_timer(&pairing_timer);
    pairing_mode = enabled;
    gap_discoverable_control(enabled ? 1 : 0);
    gap_connectable_control(1);
    if (enabled) {
        pairing_timer.process = &pairing_timeout;
        btstack_run_loop_set_timer(&pairing_timer, pairing_duration_ms);
        btstack_run_loop_add_timer(&pairing_timer);
    }
    dirty = true;
}

void forget_all()
{
    gap_delete_all_link_keys();
    dirty = true;
}

bool apply_settings(PhoneSettings const& new_settings)
{
    if (!new_settings.valid()) return false;
    settings = new_settings;
    runtime_settings.set_phone_settings(new_settings);
    dirty = true;
    return true;
}

// ----- Initialisation and reporting

void init()
{
    settings = runtime_settings.get_phone_settings();

    // Keep the link keys of paired phones in flash, next to the other settings
    const btstack_tlv_t* tlv_impl = nullptr;
    void* tlv_ctx = nullptr;
    btstack_tlv_get_instance(&tlv_impl, &tlv_ctx);
    if (tlv_impl) {
        hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(tlv_impl, tlv_ctx));
    }

    sdp_init();

    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&a2dp_handler);
    a2dp_sink_register_media_handler(&handle_media_packet);
    avdtp_stream_endpoint_t* endpoint = a2dp_sink_create_stream_endpoint(
        AVDTP_AUDIO, AVDTP_CODEC_SBC,
        sbc_capabilities, sizeof(sbc_capabilities),
        sbc_configuration, sizeof(sbc_configuration));
    btstack_assert(endpoint != nullptr);
    (void)endpoint;

    std::memset(sdp_sink_record, 0, sizeof(sdp_sink_record));
    a2dp_sink_create_sdp_record(sdp_sink_record, sdp_create_service_record_handle(),
                                AVDTP_SINK_FEATURE_MASK_HEADPHONE, nullptr, nullptr);
    btstack_assert(de_get_len(sdp_sink_record) <= sizeof(sdp_sink_record));
    sdp_register_service(sdp_sink_record);

    // The address in the name is replaced by BTstack once it is known
    gap_set_local_name("Pico-ASHA Audio 00:00:00:00:00:00");
    // Service class: audio, major class: audio/video, minor class: headphones
    gap_set_class_of_device(0x200404);
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);

    // Secure Simple Pairing without PIN ("Just Works")
    gap_ssp_set_enable(1);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_authentication_requirement(SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING);
    gap_ssp_set_auto_accept(1);

    // Connectable (so a paired phone can reconnect) but not visible
    gap_connectable_control(1);
    gap_discoverable_control(0);

    hci_callback.callback = &hci_handler;
    hci_add_event_handler(&hci_callback);
}

static void send_info()
{
    comm::PhoneInfo info = {};
    info.flags = (uint8_t)((pairing_mode ? 1 : 0) | (phone_connected ? 2 : 0) | (asha_audio_phone_audible() ? 4 : 0));
    info.duck = settings.duck;
    info.level = settings.level;
    std::memcpy(info.addr, phone_addr, sizeof(info.addr));
    std::snprintf(info.name, sizeof(info.name), "%s", phone_name);
    comm::send_phone_info_packet(info);
}

void service()
{
    bool const audible = asha_audio_phone_audible();
    if (audible != last_audible) {
        last_audible = audible;
        dirty = true;
    }
    if (dirty && comm::usb_connected()) {
        send_info();
        dirty = false;
    }
}

void on_serial_connected()
{
    dirty = true;
}

} // namespace asha::phone

#endif // ASHA_A2DP
