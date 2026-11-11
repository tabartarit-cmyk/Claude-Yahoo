// Bluetooth Classic probe (pico_asha_classic variant only).
//
// Cycle: inquiry -> for each device found: remote name request, then one SDP
// query per audio related service -> pause -> inquiry again.
// Everything is reported with short log events (32 characters at most).

#include <cstddef>
#include <cstdint>

#include <btstack.h>

#include "asha_common.hpp"
#include "asha_comms.hpp"
#include "classic_bt.hpp"

namespace asha
{

namespace
{

// Inquiry length, in units of 1.28 s
constexpr uint8_t  inquiry_length  = 8;
// Pause between two probe cycles
constexpr uint32_t cycle_pause_ms  = 15000;
// Delay between two steps, so that BTstack can close the previous SDP channel
constexpr uint32_t step_delay_ms   = 300;
constexpr size_t   max_devices     = 8;
constexpr int      max_sdp_retries = 5;

struct SdpTarget
{
    uint16_t    uuid;
    const char* name;
};

// Bluetooth Classic services which could carry audio to a hearing aid
constexpr SdpTarget sdp_targets[] = {
    {0x110B, "A2DP sink"},
    {0x110A, "A2DP source"},
    {0x111E, "HFP hands-free"},
    {0x1108, "HSP headset"},
    {0x110E, "AVRCP"},
    {0x1101, "SPP"},
};
constexpr size_t num_sdp_targets = sizeof(sdp_targets) / sizeof(sdp_targets[0]);

struct Device
{
    bd_addr_t addr;
    uint8_t   page_scan_repetition_mode;
    uint16_t  clock_offset;
    uint32_t  class_of_device;
};

enum class Step { Idle, StartInquiry, NextDevice, NextSdp };

Device device[max_devices] = {};
size_t num_devices     = 0;
size_t cur_device      = 0;
size_t cur_target      = 0;
int    sdp_retries     = 0;
bool   sdp_record_seen = false;
bool   sdp_failed      = false;
Step   next_step       = Step::Idle;

btstack_timer_source_t                 step_timer = {};
btstack_packet_callback_registration_t hci_cb_reg = {};

void step_handler(btstack_timer_source_t* timer);

void schedule(Step step, uint32_t delay_ms)
{
    next_step = step;
    btstack_run_loop_remove_timer(&step_timer);
    btstack_run_loop_set_timer_handler(&step_timer, &step_handler);
    btstack_run_loop_set_timer(&step_timer, delay_ms);
    btstack_run_loop_add_timer(&step_timer);
}

void sdp_handler(PACKET_HANDLER_PARAMS)
{
    using comm::short_log;
    using comm::unset_conn_id;

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case SDP_EVENT_QUERY_ATTRIBUTE_VALUE:
            // At least one service record matches the queried UUID
            sdp_record_seen = true;
            break;
        case SDP_EVENT_QUERY_COMPLETE: {
            uint8_t const status = sdp_event_query_complete_get_status(packet);
            if (status != 0) {
                // The device does not accept the SDP connection (or is not
                // reachable): the next queries would fail the same way.
                short_log(unset_conn_id, "BTC SDP fail 0x%02x", (unsigned)status);
                sdp_failed = true;
            } else {
                short_log(unset_conn_id, "BTC %s: %s", sdp_targets[cur_target].name,
                          sdp_record_seen ? "yes" : "no");
            }
            ++cur_target;
            schedule(Step::NextSdp, step_delay_ms);
            break;
        }
        default:
            break;
    }
}

void step_handler([[maybe_unused]] btstack_timer_source_t* timer)
{
    using comm::short_log;
    using comm::unset_conn_id;

    Step const step = next_step;
    next_step = Step::Idle;

    switch (step) {
        case Step::StartInquiry:
            num_devices = 0;
            short_log(unset_conn_id, "%s", "BTC inquiry start");
            // The result is delivered by GAP_EVENT_INQUIRY_COMPLETE
            if (gap_inquiry_start(inquiry_length) != 0) {
                short_log(unset_conn_id, "%s", "BTC inquiry error");
                schedule(Step::StartInquiry, cycle_pause_ms);
            }
            break;

        case Step::NextDevice:
            if (cur_device >= num_devices) {
                short_log(unset_conn_id, "BTC %u device(s) found", (unsigned)num_devices);
                schedule(Step::StartInquiry, cycle_pause_ms);
                break;
            }
            {
                Device const& d = device[cur_device];
                short_log(unset_conn_id, "BTC %s %06x", bd_addr_to_str(d.addr), (unsigned)d.class_of_device);
                // The result is delivered by HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE
                if (gap_remote_name_request(d.addr, d.page_scan_repetition_mode, d.clock_offset | 0x8000) != 0) {
                    short_log(unset_conn_id, "%s", "BTC name request error");
                    cur_target  = 0;
                    sdp_retries = 0;
                    sdp_failed  = false;
                    schedule(Step::NextSdp, step_delay_ms);
                }
            }
            break;

        case Step::NextSdp:
            if (cur_target >= num_sdp_targets || sdp_failed) {
                ++cur_device;
                schedule(Step::NextDevice, step_delay_ms);
                break;
            }
            {
                sdp_record_seen = false;
                // The result is delivered to sdp_handler
                uint8_t const status = sdp_client_query_uuid16(&sdp_handler, device[cur_device].addr,
                                                               sdp_targets[cur_target].uuid);
                if (status != ERROR_CODE_SUCCESS) {
                    if (++sdp_retries <= max_sdp_retries) {
                        // SDP client busy: try again shortly
                        schedule(Step::NextSdp, step_delay_ms * 2);
                    } else {
                        short_log(unset_conn_id, "BTC SDP busy 0x%02x", (unsigned)status);
                        sdp_failed = true;
                        schedule(Step::NextSdp, step_delay_ms);
                    }
                } else {
                    sdp_retries = 0;
                }
            }
            break;

        case Step::Idle:
        default:
            break;
    }
}

void classic_hci_handler(PACKET_HANDLER_PARAMS)
{
    using comm::short_log;
    using comm::unset_conn_id;

    if (packet_type != HCI_EVENT_PACKET) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case GAP_EVENT_INQUIRY_RESULT: {
            bd_addr_t addr;
            gap_event_inquiry_result_get_bd_addr(packet, addr);
            // Several reports can be received for the same device
            bool known = false;
            for (size_t i = 0; i < num_devices; ++i) {
                if (bd_addr_cmp(device[i].addr, addr) == 0) {
                    known = true;
                    break;
                }
            }
            if (known || num_devices >= max_devices) {
                break;
            }
            Device& d = device[num_devices++];
            bd_addr_copy(d.addr, addr);
            d.page_scan_repetition_mode = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
            d.clock_offset              = gap_event_inquiry_result_get_clock_offset(packet);
            d.class_of_device           = gap_event_inquiry_result_get_class_of_device(packet);
            break;
        }
        case GAP_EVENT_INQUIRY_COMPLETE:
            cur_device = 0;
            schedule(Step::NextDevice, step_delay_ms);
            break;
        case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
            // HCI event layout: status (packet[2]), BD_ADDR (packet[3..8]), name (packet[9..])
            if (packet[2] == 0) {
                short_log(unset_conn_id, "BTC n:%.24s", (const char*)&packet[9]);
            } else {
                short_log(unset_conn_id, "BTC name fail 0x%02x", (unsigned)packet[2]);
            }
            cur_target  = 0;
            sdp_retries = 0;
            sdp_failed  = false;
            schedule(Step::NextSdp, step_delay_ms);
            break;
        default:
            break;
    }
}

} // namespace

void classic_probe_init()
{
    hci_cb_reg.callback = &classic_hci_handler;
    hci_add_event_handler(&hci_cb_reg);
}

void classic_probe_start()
{
    comm::short_log(comm::unset_conn_id, "%s", "BTC probe start");
    schedule(Step::StartInquiry, 0);
}

} // namespace asha
