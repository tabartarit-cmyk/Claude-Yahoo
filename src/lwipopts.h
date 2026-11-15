#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// lwIP configuration for the USB network interface (no OS, no Wi-Fi).
// Only IPv4, one Ethernet interface, a DHCP server and a small TCP server.

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define LWIP_TIMERS                 1

#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    8192

#define LWIP_ETHERNET               1
#define LWIP_ARP                    1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ICMP                   1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_RAW                    0
#define LWIP_DHCP                   0
#define LWIP_AUTOIP                 0
#define LWIP_DNS                    0
#define LWIP_IGMP                   0
#define LWIP_STATS                  0

#define MEMP_NUM_PBUF               8
#define MEMP_NUM_UDP_PCB            3
#define MEMP_NUM_TCP_PCB            6
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG            16

#define PBUF_POOL_SIZE              8

#define TCP_MSS                     (1500 - 20 - 20)
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_SND_QUEUELEN            8
#define TCP_WND                     (2 * TCP_MSS)

#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_NETIF_STATUS_CALLBACK  0
#define LWIP_NETIF_LINK_CALLBACK    0

#endif /* _LWIPOPTS_H */
