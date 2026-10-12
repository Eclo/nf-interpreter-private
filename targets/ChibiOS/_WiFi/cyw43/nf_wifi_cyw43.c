//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// CYW43 backend for the driver neutral WiFi lwIP thread (nf_lwipthread_wifi.c).

#include "hal.h"
#include "nf_wifi_driver.h"
#include "cyw43_configport.h"
#include "cyw43.h"
#include "cyw43_ll.h"
#include "cyw43_internal.h"

#include <string.h>
#include <lwip/opt.h>
#include <lwip/snmp.h>
#include <lwip/stats.h>

// Forward declarations for CYW43 architecture layer (cyw43_arch_chibios.c)
extern void cyw43_arch_init(void);
extern void cyw43_arch_deinit(void);
extern int cyw43_ensure_wifi_up_impl(void);

// ---------------------------------------------------------------------------
// Driver hooks
// ---------------------------------------------------------------------------

void nf_wifi_drv_init(void)
{
    // Initialize CYW43 architecture layer (powers on the chip, loads firmware)
    cyw43_arch_init();
}

void nf_wifi_drv_deinit(void)
{
    cyw43_arch_deinit();
}

void nf_wifi_drv_get_mac(uint8_t *mac)
{
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
}

err_t nf_wifi_drv_linkoutput(struct netif *netif, struct pbuf *p)
{
    // only used by MIB2 stats
    (void)netif;

    // Linearize the pbuf chain into a contiguous buffer for CYW43
    uint8_t buf[1536]; // Max Ethernet frame
    uint16_t offset = 0;
    struct pbuf *q;

    for (q = p; q != NULL; q = q->next)
    {
        if (offset + q->len > sizeof(buf))
        {
            return ERR_BUF;
        }
        memcpy(buf + offset, q->payload, q->len);
        offset += q->len;
    }

    // Send the Ethernet frame to CYW43 for transmission
    // CYW43 driver will handle 802.11 encapsulation
    CYW43_THREAD_ENTER;

    // Use the CYW43 bus to send the frame
    cyw43_t *state = &cyw43_state;
    int ret = cyw43_send_ethernet(state, CYW43_ITF_STA, offset, buf, false);
    CYW43_THREAD_EXIT;
    if (ret != 0)
    {
        return ERR_IF;
    }

    MIB2_STATS_NETIF_ADD(netif, ifoutoctets, p->tot_len);
    LINK_STATS_INC(link.xmit);

    return ERR_OK;
}

// ---------------------------------------------------------------------------
// CYW43 driver callbacks
// ---------------------------------------------------------------------------

// Called by the CYW43 driver when an Ethernet frame is received from WiFi.
void cyw43_cb_process_ethernet(void *self, int itf, size_t len, const uint8_t *buf)
{
    (void)self;
    (void)itf;

    nf_wifi_netif_input(buf, len);
}

void cyw43_cb_tcpip_init(cyw43_t *self, int itf)
{
    (void)self;
    (void)itf;

    // CYW43 firmware is loaded - schedule MAC copy + DHCP start on tcpip thread.
    // Can't call lwIP APIs directly here (CYW43 mutex is held).
    nf_wifi_notify_driver_ready();
}

void cyw43_cb_tcpip_deinit(cyw43_t *self, int itf)
{
    (void)self;
    (void)itf;
}

void cyw43_cb_tcpip_set_link_up(cyw43_t *self, int itf)
{
    (void)self;
    (void)itf;

    // Link-up fires when WIFI_JOIN_STATE_ALL (auth + link + keyed) is reached.
    nf_wifi_notify_link_up();
}

void cyw43_cb_tcpip_set_link_down(cyw43_t *self, int itf)
{
    (void)self;
    (void)itf;

    nf_wifi_notify_link_down();
}

// Callback invoked by cyw43_wifi_scan for each AP found
static int wifi_scan_result_cb(void *env, const cyw43_ev_scan_result_t *result)
{
    (void)env;

    if (result != NULL)
    {
        nf_wifi_scan_add(
            result->bssid,
            result->ssid,
            result->ssid_len,
            result->rssi,
            result->auth_mode,
            result->channel);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// WiFi API (nf_lwipthread_wifi.h)
// ---------------------------------------------------------------------------

int nf_wifi_ensure_up(void)
{
    return cyw43_ensure_wifi_up_impl();
}

int nf_wifi_connect(const char *ssid, const char *password, uint32_t auth_type)
{
    (void)auth_type;

    if (ssid == NULL)
        return -1;

    // Ensure WiFi hardware is initialized
    if (cyw43_ensure_wifi_up_impl() != 0)
    {
        return -1;
    }

    cyw43_t *state = &cyw43_state;

    size_t ssid_len = strlen(ssid);
    size_t key_len = password ? strlen(password) : 0;

    // WPA2_AES_PSK = 0x00400004
    uint32_t auth = key_len > 0 ? 0x00400004 : 0; // Open or WPA2

    // Clean disconnect first - ensures AP doesn't see stale association state
    // and prevents BADAUTH on rapid re-join.  EV_DISASSOC will fire link_down
    // callback to stop any running DHCP.
    CYW43_THREAD_ENTER;
    cyw43_wifi_leave(state, CYW43_ITF_STA);
    CYW43_THREAD_EXIT;
    chThdSleepMilliseconds(100);

    // Disable power management during connect - PM2 may cause the radio
    // to sleep and miss the DHCP OFFER response from the AP.
    cyw43_wifi_pm(state, CYW43_NONE_PM);

    CYW43_THREAD_ENTER;
    int result =
        cyw43_wifi_join(state, ssid_len, (const uint8_t *)ssid, key_len, (const uint8_t *)password, auth, NULL, 0);
    CYW43_THREAD_EXIT;

    // After successful join, issue an ioctl to kick inline packet processing.
    // The ioctl's inline polling may process pending DATA_HEADER packets
    // (e.g., DHCP OFFER) that arrived during the join process.
    if (result == 0)
    {
        CYW43_THREAD_ENTER;
        uint8_t rssi_buf[4] = {0};
        cyw43_ll_ioctl(&state->cyw43_ll, 127 << 1, 4, rssi_buf, 0);
        CYW43_THREAD_EXIT;
    }

    return result;
}

int nf_wifi_disconnect(void)
{
    cyw43_t *state = &cyw43_state;

    CYW43_THREAD_ENTER;
    int result = cyw43_wifi_leave(state, CYW43_ITF_STA);
    CYW43_THREAD_EXIT;

    return result;
}

int nf_wifi_scan_start(void)
{
    // Ensure WiFi hardware is initialized
    if (cyw43_ensure_wifi_up_impl() != 0)
    {
        return -1;
    }

    cyw43_t *state = &cyw43_state;

    // Reset scan results
    nf_wifi_scan_reset();

    cyw43_wifi_scan_options_t scan_opts = {0};
    CYW43_THREAD_ENTER;
    int result = cyw43_wifi_scan(state, &scan_opts, NULL, wifi_scan_result_cb);
    CYW43_THREAD_EXIT;

    if (result != 0)
    {
        return result;
    }

    // Wait for scan to complete (wifi_scan_state goes from 1 to 2)
    // Timeout after 10 seconds
    int timeout_ms = 10000;
    int elapsed = 0;

    while (elapsed < timeout_ms)
    {
        CYW43_THREAD_ENTER;
        int scanning = cyw43_wifi_scan_active(state);
        CYW43_THREAD_EXIT;

        if (!scanning)
        {
            break;
        }

        chThdSleepMilliseconds(100);
        elapsed += 100;
    }

    return 0;
}

int nf_wifi_is_connected(void)
{
    return cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN;
}

int nf_wifi_get_link_status(void)
{
    return cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
}

int nf_wifi_get_join_state(void)
{
    return (int)cyw43_state.wifi_join_state;
}

// Direct poll from NativeConnect - calls cyw43_poll to drain SPI packets.
uint32_t nf_wifi_direct_poll(void)
{
    CYW43_THREAD_ENTER;

    if (cyw43_poll)
        cyw43_poll();

    CYW43_THREAD_EXIT;

    return 0;
}
