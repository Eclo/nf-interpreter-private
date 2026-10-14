//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// lwIP options for the MXCHIP AZ3166 (BCM43362 WiFi through WHD)

#ifndef TARGET_LWIPOPTS_H
#define TARGET_LWIPOPTS_H

// software checksums (the WLAN chip doesn't offload them)
#define CHECKSUM_GEN_IP     1
#define CHECKSUM_GEN_UDP    1
#define CHECKSUM_GEN_TCP    1
#define CHECKSUM_GEN_ICMP   1
#define CHECKSUM_CHECK_IP   1
#define CHECKSUM_CHECK_UDP  1
#define CHECKSUM_CHECK_TCP  1
#define CHECKSUM_CHECK_ICMP 1

#define DHCP_DOES_ARP_CHECK      0
#define LWIP_DHCP_DOES_ACD_CHECK 0

#undef LWIP_THREAD_STACK_SIZE
#define LWIP_THREAD_STACK_SIZE 2048
#define TCPIP_THREAD_STACKSIZE 4096

#undef LWIP_IFNAME0
#undef LWIP_IFNAME1
#define LWIP_IFNAME0 'w'
#define LWIP_IFNAME1 'l'

#endif // TARGET_LWIPOPTS_H
