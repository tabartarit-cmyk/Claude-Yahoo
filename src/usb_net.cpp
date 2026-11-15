#include "usb_net.hpp"

#if ASHA_USB_NET

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <pico/stdlib.h>
#include <hardware/watchdog.h>
#include <tusb.h>

#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "netif/ethernet.h"

#include "asha_audio.h"
#include "runtime_settings.hpp"
#include "usb_common.hpp"
#include "web_ui_html.h"

// Address of the Pico on the USB network. The host gets 192.168.7.2 by DHCP.
// No gateway and no DNS server are announced, so the host never routes its
// Internet traffic through the Pico.
#define NET_IP_A 192
#define NET_IP_B 168
#define NET_IP_C 7
#define NET_IP_SERVER 1
#define NET_IP_CLIENT 2

// ---------------------------------------------------------------------------
// TinyUSB network driver glue
// ---------------------------------------------------------------------------

extern "C" const uint8_t tud_network_mac_address[6] = ASHA_NET_MAC_INIT;

namespace asha::usbnet
{

static struct netif netif_data;
static struct pbuf* received_frame = nullptr;

static bool reboot_pending = false;
static absolute_time_t reboot_time;

// Non-blocking: if the USB endpoint is busy the frame is dropped and TCP/ARP
// retry, so the audio loop is never stalled waiting for the network.
static err_t linkoutput_fn(struct netif* netif, struct pbuf* p)
{
    (void)netif;
    if (!tud_ready() || !tud_network_can_xmit(p->tot_len)) {
        return ERR_BUF;
    }
    tud_network_xmit(p, 0);
    return ERR_OK;
}

static err_t netif_init_cb(struct netif* netif)
{
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->state = nullptr;
    netif->name[0] = 'U';
    netif->name[1] = 'S';
    netif->linkoutput = linkoutput_fn;
    netif->output = etharp_output;
    netif->hwaddr_len = sizeof(tud_network_mac_address);
    std::memcpy(netif->hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
    // The lwIP side must not have the same MAC as the address announced to the host
    netif->hwaddr[5] ^= 0x01;
    return ERR_OK;
}

// ---------------------------------------------------------------------------
// Minimal DHCP server: always offers the same address to the host
// ---------------------------------------------------------------------------

struct __attribute__((packed)) DhcpMsg {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16];
    uint8_t sname[64];
    uint8_t file[128];
    uint32_t magic;
    uint8_t options[64];
};

static constexpr size_t dhcp_fixed_len = offsetof(DhcpMsg, options);

static void dhcp_recv(void*, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t*, u16_t)
{
    DhcpMsg in = {};
    size_t const len = std::min<size_t>(p->tot_len, sizeof(in));
    pbuf_copy_partial(p, &in, len, 0);
    pbuf_free(p);

    if (len < dhcp_fixed_len + 4 || in.op != 1 || in.magic != PP_HTONL(0x63825363u)) {
        return;
    }

    // Find the message type (option 53)
    uint8_t type = 0;
    size_t const opt_len = len - dhcp_fixed_len;
    for (size_t i = 0; i < opt_len;) {
        uint8_t const code = in.options[i];
        if (code == 255) break;
        if (code == 0) { i++; continue; }
        if (i + 1 >= opt_len) break;
        uint8_t const l = in.options[i + 1];
        if (code == 53 && l == 1 && i + 2 < opt_len) {
            type = in.options[i + 2];
        }
        i += 2u + l;
    }
    if (type != 1 && type != 3) {
        return; // only DISCOVER and REQUEST are answered
    }

    DhcpMsg out = {};
    out.op = 2;
    out.htype = 1;
    out.hlen = 6;
    out.xid = in.xid;
    out.flags = in.flags;
    out.yiaddr = PP_HTONL(LWIP_MAKEU32(NET_IP_A, NET_IP_B, NET_IP_C, NET_IP_CLIENT));
    out.siaddr = PP_HTONL(LWIP_MAKEU32(NET_IP_A, NET_IP_B, NET_IP_C, NET_IP_SERVER));
    std::memcpy(out.chaddr, in.chaddr, sizeof(out.chaddr));
    out.magic = PP_HTONL(0x63825363u);

    uint8_t* o = out.options;
    *o++ = 53; *o++ = 1; *o++ = (type == 1) ? 2 : 5;                 // OFFER / ACK
    *o++ = 54; *o++ = 4;                                             // server id
    *o++ = NET_IP_A; *o++ = NET_IP_B; *o++ = NET_IP_C; *o++ = NET_IP_SERVER;
    *o++ = 51; *o++ = 4; *o++ = 0x00; *o++ = 0x01; *o++ = 0x51; *o++ = 0x80; // lease 86400 s
    *o++ = 1;  *o++ = 4; *o++ = 255; *o++ = 255; *o++ = 255; *o++ = 0;       // subnet mask
    *o++ = 255;                                                      // end

    u16_t const out_len = (u16_t)(dhcp_fixed_len + (size_t)(o - out.options));
    struct pbuf* q = pbuf_alloc(PBUF_TRANSPORT, out_len, PBUF_RAM);
    if (q) {
        pbuf_take(q, &out, out_len);
        udp_sendto_if(pcb, q, IP_ADDR_BROADCAST, 68, &netif_data);
        pbuf_free(q);
    }
}

static void dhcp_init()
{
    struct udp_pcb* pcb = udp_new();
    if (!pcb) return;
    ip_set_option(pcb, SOF_BROADCAST);
    if (udp_bind(pcb, IP_ADDR_ANY, 67) != ERR_OK) {
        udp_remove(pcb);
        return;
    }
    udp_recv(pcb, dhcp_recv, nullptr);
}

// ---------------------------------------------------------------------------
// Minimal HTTP server
//   GET  /              settings page
//   GET  /api/settings  current settings (JSON)
//   POST /api/set       new settings (uac=..&min=..&max=..&thr=..), the Pico
//                       then restarts. The request must carry the header
//                       "X-Pico-ASHA", so that a web page from another site
//                       cannot change the settings (it would need a CORS
//                       preflight, which this server never grants).
// ---------------------------------------------------------------------------

struct HttpConn {
    struct tcp_pcb* pcb = nullptr;
    bool used = false;
    bool all_queued = false;
    uint8_t idle = 0;
    char req[640];
    size_t req_len = 0;
    char hdr[192];
    size_t hdr_len = 0, hdr_off = 0;
    const char* body = nullptr;
    size_t body_len = 0, body_off = 0;
    char json[160];
};

static HttpConn conns[3];

static err_t conn_close(HttpConn* c)
{
    struct tcp_pcb* pcb = c->pcb;
    c->used = false;
    c->pcb = nullptr;
    if (!pcb) return ERR_OK;
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_sent(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static bool write_seg(HttpConn* c, const char* data, size_t len, size_t& off)
{
    while (off < len) {
        u16_t const room = tcp_sndbuf(c->pcb);
        if (room == 0) return false;
        size_t const n = std::min<size_t>(room, len - off);
        if (tcp_write(c->pcb, data + off, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) {
            return false; // out of memory: retried from the sent/poll callbacks
        }
        off += n;
    }
    return true;
}

static void pump(HttpConn* c)
{
    if (c->all_queued || !c->pcb) return;
    if (write_seg(c, c->hdr, c->hdr_len, c->hdr_off)
        && write_seg(c, c->body, c->body_len, c->body_off)) {
        c->all_queued = true;
    }
    tcp_output(c->pcb);
}

static void respond(HttpConn* c, int code, const char* status, const char* ctype,
                    const char* body, size_t body_len)
{
    c->hdr_len = (size_t)snprintf(c->hdr, sizeof(c->hdr),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Connection: close\r\nCache-Control: no-store\r\n\r\n",
        code, status, ctype, (unsigned)body_len);
    c->hdr_off = 0;
    c->body = body;
    c->body_len = body_len;
    c->body_off = 0;
    c->all_queued = false;
    pump(c);
}

static void respond_text(HttpConn* c, int code, const char* status, const char* text)
{
    respond(c, code, status, "text/plain; charset=utf-8", text, strlen(text));
}

static const char* find_ci(const char* hay, size_t hay_len, const char* needle)
{
    size_t const n = strlen(needle);
    for (size_t i = 0; i + n <= hay_len; i++) {
        size_t j = 0;
        while (j < n && (char)tolower((unsigned char)hay[i + j]) == needle[j]) j++;
        if (j == n) return hay + i;
    }
    return nullptr;
}

static bool get_param(const char* body, const char* key, long& out)
{
    size_t const klen = strlen(key);
    for (const char* p = body; *p;) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            char* end = nullptr;
            out = strtol(p + klen + 1, &end, 10);
            return end != p + klen + 1;
        }
        const char* amp = strchr(p, '&');
        if (!amp) break;
        p = amp + 1;
    }
    return false;
}

static void handle_request(HttpConn* c, size_t header_end)
{
    char* const req = c->req;
    size_t const total = c->req_len;

    if (strncmp(req, "GET /api/settings ", 18) == 0) {
        auto const& s = usb_settings;
        int const n = snprintf(c->json, sizeof(c->json),
            "{\"uac\":%u,\"min\":%d,\"max\":%d,\"thr\":%d,\"fw\":\"%d.%d.%d\"}",
            (unsigned)s.uac_version,
            s.min_vol / ASHA_USB_VOL_RES, s.max_vol / ASHA_USB_VOL_RES,
            s.dig_thr / ASHA_USB_VOL_RES,
            PICO_ASHA_FW_VERS_MAJOR, PICO_ASHA_FW_VERS_MINOR, PICO_ASHA_FW_VERS_PATCH);
        respond(c, 200, "OK", "application/json", c->json, (size_t)n);
    } else if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /index.html ", 16) == 0) {
        respond(c, 200, "OK", "text/html; charset=utf-8", web_ui_html, sizeof(web_ui_html) - 1);
    } else if (strncmp(req, "POST /api/set ", 14) == 0) {
        if (!find_ci(req, header_end, "x-pico-asha:")) {
            respond_text(c, 403, "Forbidden", "forbidden");
            return;
        }
        const char* body = req + header_end + 4;
        long uac = 0, min = 0, max = 0, thr = 0;
        if (!get_param(body, "uac", uac) || !get_param(body, "min", min)
            || !get_param(body, "max", max) || !get_param(body, "thr", thr)
            || min < -240 || min > 0 || max < -240 || max > 0 || thr < -240 || thr >= 0) {
            respond_text(c, 400, "Bad Request", "invalid values");
            return;
        }
        USBSettings s = {
            .uac_version = (uint16_t)uac,
            .min_vol = (int16_t)(min * ASHA_USB_VOL_RES),
            .max_vol = (int16_t)(max * ASHA_USB_VOL_RES),
            .dig_thr = (int16_t)(thr * ASHA_USB_VOL_RES),
        };
        if (!s) {
            respond_text(c, 400, "Bad Request", "invalid values");
            return;
        }
        bool restart = false;
        if (s != runtime_settings.get_usb_settings()) {
            RuntimeSettings::defer_usb_settings(s);
            // Leave time for the answer to reach the browser before restarting
            reboot_time = make_timeout_time_ms(800);
            reboot_pending = true;
            restart = true;
        }
        int const n = snprintf(c->json, sizeof(c->json), "{\"ok\":true,\"restart\":%s}", restart ? "true" : "false");
        respond(c, 200, "OK", "application/json", c->json, (size_t)n);
    } else {
        respond_text(c, 404, "Not Found", "not found");
    }
    (void)total;
}

static err_t http_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err)
{
    auto* c = static_cast<HttpConn*>(arg);
    if (!p || err != ERR_OK) {
        if (p) pbuf_free(p);
        return conn_close(c);
    }
    tcp_recved(pcb, p->tot_len);
    c->idle = 0;

    if (c->hdr_len != 0) { // already answered: ignore the rest
        pbuf_free(p);
        return ERR_OK;
    }
    if (c->req_len + p->tot_len >= sizeof(c->req)) {
        pbuf_free(p);
        respond_text(c, 413, "Payload Too Large", "too large");
        return ERR_OK;
    }
    pbuf_copy_partial(p, c->req + c->req_len, p->tot_len, 0);
    c->req_len += p->tot_len;
    c->req[c->req_len] = '\0';
    pbuf_free(p);

    const char* hdr_end = strstr(c->req, "\r\n\r\n");
    if (!hdr_end) {
        return ERR_OK; // wait for the rest of the headers
    }
    size_t const header_end = (size_t)(hdr_end - c->req);

    if (strncmp(c->req, "POST ", 5) == 0) {
        size_t content_len = 0;
        const char* cl = find_ci(c->req, header_end, "content-length:");
        if (cl) content_len = (size_t)strtoul(cl + 15, nullptr, 10);
        if (c->req_len < header_end + 4 + content_len) {
            return ERR_OK; // wait for the body
        }
    }
    handle_request(c, header_end);
    return ERR_OK;
}

static err_t http_sent(void* arg, struct tcp_pcb* pcb, u16_t)
{
    auto* c = static_cast<HttpConn*>(arg);
    if (!c->all_queued) {
        pump(c);
    } else if (tcp_sndqueuelen(pcb) == 0) {
        return conn_close(c);
    }
    return ERR_OK;
}

static err_t http_poll(void* arg, struct tcp_pcb*)
{
    auto* c = static_cast<HttpConn*>(arg);
    if (++c->idle > 10) { // about 20 s without any activity
        return conn_close(c);
    }
    if (c->hdr_len != 0 && !c->all_queued) {
        pump(c);
    }
    return ERR_OK;
}

static void http_err(void* arg, err_t)
{
    // The pcb is already freed by lwIP
    auto* c = static_cast<HttpConn*>(arg);
    c->pcb = nullptr;
    c->used = false;
}

static err_t http_accept(void*, struct tcp_pcb* newpcb, err_t err)
{
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    HttpConn* c = nullptr;
    for (auto& cc : conns) {
        if (!cc.used) { c = &cc; break; }
    }
    if (!c) {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }
    *c = HttpConn{};
    c->used = true;
    c->pcb = newpcb;
    tcp_arg(newpcb, c);
    tcp_recv(newpcb, http_recv);
    tcp_sent(newpcb, http_sent);
    tcp_err(newpcb, http_err);
    tcp_poll(newpcb, http_poll, 4);
    return ERR_OK;
}

static void http_init()
{
    struct tcp_pcb* pcb = tcp_new();
    if (!pcb) return;
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) {
        tcp_abort(pcb);
        return;
    }
    pcb = tcp_listen_with_backlog(pcb, 2);
    if (!pcb) return;
    tcp_accept(pcb, http_accept);
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void init()
{
    lwip_init();

    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip, NET_IP_A, NET_IP_B, NET_IP_C, NET_IP_SERVER);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);
    netif_add(&netif_data, &ip, &mask, &gw, nullptr, netif_init_cb, ip_input);
    netif_set_default(&netif_data);

    dhcp_init();
    http_init();
}

void task()
{
    // Frames received from the host are handed to lwIP from here, never from
    // inside the TinyUSB callback.
    if (received_frame) {
        ethernet_input(received_frame, &netif_data);
        received_frame = nullptr; // ownership passed to lwIP
        tud_network_recv_renew();
    }
    sys_check_timeouts();

    if (reboot_pending && absolute_time_diff_us(get_absolute_time(), reboot_time) <= 0) {
        reboot_pending = false;
        watchdog_enable(10, true);
    }
}

} // namespace asha::usbnet

extern "C" {

bool tud_network_recv_cb(const uint8_t* src, uint16_t size)
{
    using namespace asha::usbnet;
    if (received_frame) {
        return false; // previous frame not processed yet
    }
    if (size) {
        struct pbuf* p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (!p) {
            return false;
        }
        pbuf_take(p, src, size);
        received_frame = p;
    }
    return true;
}

uint16_t tud_network_xmit_cb(uint8_t* dst, void* ref, uint16_t arg)
{
    (void)arg;
    struct pbuf* p = static_cast<struct pbuf*>(ref);
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

void tud_network_init_cb(void)
{
    using namespace asha::usbnet;
    if (received_frame) {
        pbuf_free(received_frame);
        received_frame = nullptr;
    }
}

} // extern "C"

#endif // ASHA_USB_NET
