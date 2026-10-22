#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* NO_SYS mode: No lwIP OS abstraction, use raw API called from a single net thread */
#define NO_SYS                  1
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0
#define LWIP_NETIF_API          0
#define SYS_LIGHTWEIGHT_PROT    0

#define LWIP_IPV4               1
#define LWIP_IPV6               0
#define LWIP_ARP                1
#define LWIP_ETHERNET           1
#define LWIP_ICMP               1       /* Respond to ping */
#define ARP_QUEUEING            1       /* Queue packets waiting for ARP to send later, instead of dropping them */
#define LWIP_UDP                1
#define LWIP_TCP                1
#define LWIP_DHCP               0       /* Use static IP (see net/net.c) */
#define LWIP_DNS                0
#define LWIP_STATS              0
#define LWIP_PROVIDE_ERRNO      1

#define MEM_ALIGNMENT           8
#define MEM_SIZE                (2 * 1024 * 1024)
#define MEMP_NUM_PBUF           128
#define PBUF_POOL_SIZE          256
#define TCP_MSS                 1460
#define TCP_SND_BUF             (32 * TCP_MSS)
#define TCP_WND                 (32 * TCP_MSS)
#define TCP_SND_QUEUELEN        ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define MEMP_NUM_TCP_PCB        48
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_TCP_SEG        512

#define LWIP_RAND()             (lwip_rand32())
#ifdef __ASSEMBLER__
#else
unsigned int lwip_rand32(void);
#endif

#endif
