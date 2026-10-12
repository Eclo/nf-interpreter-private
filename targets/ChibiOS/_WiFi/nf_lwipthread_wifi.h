//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// WiFi-aware lwIP thread header for ChibiOS.
// Replaces nf_lwipthread.h when using a WiFi chip instead of ChibiOS Ethernet MAC.
// The API below is driver neutral; the WiFi driver backend (CYW43, WHD, ...) is selected at build time.

#ifndef NF_LWIPTHREAD_WIFI_H
#define NF_LWIPTHREAD_WIFI_H

#include <nf_lwipthread.h>

// Re-export the standard lwIP thread types and API.
// The WiFi variant uses the same lwipthread_opts_t and API,
// but the underlying transport is the WiFi driver instead of ETHD1 MAC.

#ifdef __cplusplus
extern "C"
{
#endif

    // WiFi power management
    void nf_wifi_power_on(void);
    void nf_wifi_power_off(void);

    // Lazy hardware init — call before any WiFi operation
    int nf_wifi_ensure_up(void);

    // WiFi connection management — called from Target_Network.cpp
    int nf_wifi_connect(const char *ssid, const char *password, uint32_t auth_type);
    int nf_wifi_disconnect(void);
    int nf_wifi_scan_start(void);
    int nf_wifi_is_connected(void);
    int nf_wifi_get_link_status(void);
    int nf_wifi_get_join_state(void);
    uint32_t nf_wifi_get_ip4_address(void);

    // Direct poll — call from NativeConnect wait loop to drain pending packets
    uint32_t nf_wifi_direct_poll(void);

    // Set the lwIP netif MAC address (call after WiFi chip is initialized)
    void nf_wifi_set_netif_mac(const uint8_t *mac);

    // Scan results
    typedef struct
    {
        uint8_t bssid[6];
        uint8_t ssid[33];
        int16_t rssi;
        uint8_t auth_mode;
        uint16_t channel;
    } wifi_scan_record_t;

    int nf_wifi_scan_get_count(void);
    const wifi_scan_record_t *nf_wifi_scan_get_results(void);

    // Access WiFi MAC address (as set on the netif)
    void nf_wifi_get_mac_addr(uint8_t *mac);

    // Accessor for the WiFi netif
    struct netif *nf_getNetif(void);

#ifdef __cplusplus
}
#endif

#endif // NF_LWIPTHREAD_WIFI_H
