//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// Contract between the driver neutral WiFi lwIP thread (nf_lwipthread_wifi.c)
// and a WiFi driver backend (e.g. _WiFi/cyw43/nf_wifi_cyw43.c).
//
// A backend implements the nf_wifi_drv_* hooks below plus the driver specific functions
// declared in nf_lwipthread_wifi.h (nf_wifi_ensure_up, nf_wifi_connect, nf_wifi_disconnect,
// nf_wifi_scan_start, nf_wifi_is_connected, nf_wifi_get_link_status, nf_wifi_get_join_state,
// nf_wifi_direct_poll).
// The core implements the nf_wifi_* services below for the backend to call.

#ifndef NF_WIFI_DRIVER_H
#define NF_WIFI_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include <lwip/err.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <nf_lwipthread_wifi.h>

#ifdef __cplusplus
extern "C"
{
#endif

    ///////////////////////////////////////
    // implemented by the driver backend //
    ///////////////////////////////////////

    // Initialize the driver/chip (called from the lwIP thread before the netif is added)
    void nf_wifi_drv_init(void);

    // De-initialize the driver/chip
    void nf_wifi_drv_deinit(void);

    // Get the station MAC address from the driver
    void nf_wifi_drv_get_mac(uint8_t *mac);

    // Transmit an Ethernet frame (netif linkoutput)
    err_t nf_wifi_drv_linkoutput(struct netif *netif, struct pbuf *p);

    /////////////////////////////////////////
    // implemented by nf_lwipthread_wifi.c //
    /////////////////////////////////////////

    // Pass a received Ethernet frame to lwIP (the frame is copied)
    void nf_wifi_netif_input(const uint8_t *frame, size_t length);

    // Driver is up and the chip MAC is valid: copy MAC to netif/config and start DHCP
    void nf_wifi_notify_driver_ready(void);

    // Link is up (associated and keyed) / link went down
    void nf_wifi_notify_link_up(void);
    void nf_wifi_notify_link_down(void);

    // Scan result storage
    void nf_wifi_scan_reset(void);
    void nf_wifi_scan_add(
        const uint8_t *bssid,
        const uint8_t *ssid,
        uint8_t ssidLength,
        int16_t rssi,
        uint8_t authMode,
        uint16_t channel);

#ifdef __cplusplus
}
#endif

#endif // NF_WIFI_DRIVER_H
