//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// WHD (Infineon Wi-Fi Host Driver) backend for the driver neutral WiFi lwIP thread (nf_lwipthread_wifi.c).
// Board specifics come from the target (g_wifiWhdPlatform, see nf_wifi_whd.h).

#include <string.h>
#include <hal.h>
#include <hal_nf_community.h>
#include <whd.h>
#include <whd_types.h>
#include <whd_wifi_api.h>
#include <whd_network_types.h>
#include <whd_events.h>
#include <cyhal_sdio.h>
#include <cyhal_gpio.h>
#include <cyabs_rtos.h>
#include <lwip/opt.h>
#include <lwip/pbuf.h>
#include <lwip/snmp.h>
#include <lwip/stats.h>
#include "nf_wifi_driver.h"
#include "nf_wifi_whd.h"


// stack size for the WHD thread (WHD requires at least MINIMUM_WHD_STACK_SIZE = 4724 bytes)
#ifndef NF_WIFI_WHD_THREAD_STACK_SIZE
#define NF_WIFI_WHD_THREAD_STACK_SIZE 4800
#endif

// scan timeout (ms)
#define NF_WIFI_WHD_SCAN_TIMEOUT 10000

// SDIO block size used by WHD (SDIO_64B_BLOCK)
#define NF_WIFI_WHD_SDIO_BLOCK_SIZE 64

// link status flag in the WLC_E_LINK event
#define NF_WIFI_WHD_EVENT_MSG_LINK 0x01

// authentication modes reported in scan results (same values as the ESP32 wifi_auth_mode_t)
#define NF_WIFI_AUTH_OPEN         0
#define NF_WIFI_AUTH_WEP          1
#define NF_WIFI_AUTH_WPA_PSK      2
#define NF_WIFI_AUTH_WPA2_PSK     3
#define NF_WIFI_AUTH_WPA_WPA2_PSK 4
#define NF_WIFI_AUTH_ENTERPRISE   5
#define NF_WIFI_AUTH_WPA3_PSK     6

extern whd_resource_source_t g_whdResourceOps;
extern whd_buffer_funcs_t g_whdBufferOps;

static void NetifProcessEthernetData(whd_interface_t ifp, whd_buffer_t buffer);

static whd_netif_funcs_t netifOps = {
    .whd_network_process_ethernet_data = NetifProcessEthernetData,
};

static THD_WORKING_AREA(waWhdThread, NF_WIFI_WHD_THREAD_STACK_SIZE);

static whd_driver_t whdDriver;
static whd_interface_t whdInterface;
static cyhal_sdio_t sdioObject;

static MUTEX_DECL(wifiMutex);
static volatile bool wifiUp;
static volatile bool linkUp;

static uint8_t macAddress[6];

// WLAN start status (inspect with the debugger when the WiFi doesn't come up):

static const uint32_t linkEvents[] = {WLC_E_LINK, WLC_E_DEAUTH_IND, WLC_E_DISASSOC_IND, WLC_E_NONE};
static uint16_t linkEventIndex;

// scan
static whd_scan_result_t scanResult;
static BSEMAPHORE_DECL(scanDone, true);

// ---------------------------------------------------------------------------
// WHD callbacks (called from the WHD thread)
// ---------------------------------------------------------------------------

// Received Ethernet frame: hand the pbuf over to lwIP
static void NetifProcessEthernetData(whd_interface_t ifp, whd_buffer_t buffer)
{
    struct pbuf *p = (struct pbuf *)buffer;
    struct netif *netif = nf_getNetif();

    (void)ifp;

    MIB2_STATS_NETIF_ADD(netif, ifinoctets, p->tot_len);
    LINK_STATS_INC(link.recv);

    if (netif->input == NULL || netif->input(p, netif) != ERR_OK)
    {
        pbuf_free(p);
    }
}

static void *LinkEventHandler(
    whd_interface_t ifp,
    const whd_event_header_t *event_header,
    const uint8_t *event_data,
    void *handler_user_data)
{
    (void)ifp;
    (void)event_data;

    switch (event_header->event_type)
    {
        case WLC_E_LINK:
            if ((event_header->flags & NF_WIFI_WHD_EVENT_MSG_LINK) == 0 && linkUp)
            {
                linkUp = false;
                nf_wifi_notify_link_down();
            }
            break;

        case WLC_E_DEAUTH_IND:
        case WLC_E_DISASSOC_IND:
            if (linkUp)
            {
                linkUp = false;
                nf_wifi_notify_link_down();
            }
            break;

        default:
            break;
    }

    return handler_user_data;
}

static uint8_t MapSecurity(whd_security_t security)
{
    if (security == WHD_SECURITY_OPEN)
    {
        return NF_WIFI_AUTH_OPEN;
    }

    if ((security & WPA3_SECURITY) != 0)
    {
        return NF_WIFI_AUTH_WPA3_PSK;
    }

    if ((security & ENTERPRISE_ENABLED) != 0)
    {
        return NF_WIFI_AUTH_ENTERPRISE;
    }

    if ((security & WPA2_SECURITY) != 0 && (security & WPA_SECURITY) != 0)
    {
        return NF_WIFI_AUTH_WPA_WPA2_PSK;
    }

    if ((security & WPA2_SECURITY) != 0)
    {
        return NF_WIFI_AUTH_WPA2_PSK;
    }

    if ((security & WPA_SECURITY) != 0)
    {
        return NF_WIFI_AUTH_WPA_PSK;
    }

    return NF_WIFI_AUTH_WEP;
}

static void ScanResultCallback(whd_scan_result_t **result_ptr, void *user_data, whd_scan_status_t status)
{
    (void)user_data;

    if (status == WHD_SCAN_INCOMPLETE)
    {
        if (result_ptr != NULL && *result_ptr != NULL)
        {
            whd_scan_result_t *result = *result_ptr;

            nf_wifi_scan_add(
                result->BSSID.octet,
                result->SSID.value,
                result->SSID.length,
                result->signal_strength,
                MapSecurity(result->security),
                result->channel);

            memset(result, 0, sizeof(whd_scan_result_t));
        }
    }
    else
    {
        // completed or aborted
        chBSemSignal(&scanDone);
    }
}

// ---------------------------------------------------------------------------
// WHD start
// ---------------------------------------------------------------------------

static int WifiStart(void)
{
    whd_init_config_t initConfig;
    whd_sdio_config_t sdioConfig;
    cyhal_sdio_cfg_t sdioBusConfig;
    whd_mac_t mac;
    uint32_t result;

    memset(&initConfig, 0, sizeof(initConfig));
    initConfig.thread_stack_start = waWhdThread;
    initConfig.thread_stack_size = sizeof(waWhdThread);
    initConfig.thread_priority = (uint32_t)CY_RTOS_PRIORITY_HIGH;
    initConfig.country = WHD_COUNTRY_WORLD_WIDE_XX;

    memset(&sdioConfig, 0, sizeof(sdioConfig));
    sdioConfig.sdio_1bit_mode = WHD_FALSE;
    sdioConfig.high_speed_sdio_clock = WHD_FALSE;
    sdioConfig.oob_config.host_oob_pin = g_wifiWhdPlatform.oobPin;
    sdioConfig.oob_config.drive_mode = CYHAL_GPIO_DRIVE_NONE;
    sdioConfig.oob_config.init_drive_state = WHD_FALSE;
    sdioConfig.oob_config.dev_gpio_sel = 0;
    sdioConfig.oob_config.is_falling_edge = g_wifiWhdPlatform.oobFallingEdge ? WHD_TRUE : WHD_FALSE;
    sdioConfig.oob_config.intr_priority = 0;

    sdioBusConfig.frequencyhal_hz = g_wifiWhdPlatform.sdioClock;
    sdioBusConfig.block_size = NF_WIFI_WHD_SDIO_BLOCK_SIZE;

    // power up the WLAN chip and enumerate it
    g_wifiWhdPlatform.powerUp();

    sdioObject.driver = g_wifiWhdPlatform.sdioDriver;

    result = cyhal_sdio_init(
        &sdioObject,
        CYHAL_NC_PIN_VALUE,
        CYHAL_NC_PIN_VALUE,
        CYHAL_NC_PIN_VALUE,
        CYHAL_NC_PIN_VALUE,
        CYHAL_NC_PIN_VALUE,
        CYHAL_NC_PIN_VALUE);

    if (result != CY_RSLT_SUCCESS)
    {
        return -1;
    }

    cyhal_sdio_configure(&sdioObject, &sdioBusConfig);

    // WHD driver instance
    if (whdDriver == NULL)
    {
        result = whd_init(&whdDriver, &initConfig, &g_whdResourceOps, &g_whdBufferOps, &netifOps);

        if (result != WHD_SUCCESS)
        {
            whdDriver = NULL;
            return -1;
        }
    }

    // attach the SDIO bus
    result = whd_bus_sdio_attach(whdDriver, &sdioConfig, &sdioObject);

    if (result != WHD_SUCCESS)
    {
        return -1;
    }

    // download the firmware and NVRAM and bring up the WLAN interface
    result = whd_wifi_on(whdDriver, &whdInterface);

    if (result != WHD_SUCCESS)
    {
        return -1;
    }

    if (whd_wifi_get_mac_address(whdInterface, &mac) == WHD_SUCCESS)
    {
        memcpy(macAddress, mac.octet, sizeof(macAddress));
    }

    (void)whd_wifi_set_event_handler(whdInterface, linkEvents, LinkEventHandler, NULL, &linkEventIndex);

    wifiUp = true;

    // firmware is up and the MAC address is valid: update the netif and start DHCP
    nf_wifi_notify_driver_ready();

    return 0;
}

// ---------------------------------------------------------------------------
// Driver hooks (nf_wifi_driver.h)
// ---------------------------------------------------------------------------

void nf_wifi_drv_init(void)
{
    // nothing to do here: the WLAN chip is brought up lazily by nf_wifi_ensure_up()
    // because the firmware download takes a while
}

void nf_wifi_drv_deinit(void)
{
    chMtxLock(&wifiMutex);

    if (wifiUp)
    {
        linkUp = false;

        (void)whd_wifi_off(whdInterface);
        (void)whd_deinit(whdInterface);

        whdDriver = NULL;
        whdInterface = NULL;
        wifiUp = false;

        cyhal_sdio_free(&sdioObject);
        g_wifiWhdPlatform.powerDown();
    }

    chMtxUnlock(&wifiMutex);
}

void nf_wifi_drv_get_mac(uint8_t *mac)
{
    memcpy(mac, macAddress, sizeof(macAddress));
}

err_t nf_wifi_drv_linkoutput(struct netif *netif, struct pbuf *p)
{
    whd_buffer_t buffer;
    uint8_t *data;

    (void)netif;

    if (!wifiUp)
    {
        return ERR_IF;
    }

    // WHD needs a contiguous buffer with room in front for its headers
    if (g_whdBufferOps.whd_host_buffer_get(&buffer, WHD_NETWORK_TX, (uint16_t)(WHD_LINK_HEADER + p->tot_len), 0) !=
        WHD_SUCCESS)
    {
        LINK_STATS_INC(link.memerr);
        LINK_STATS_INC(link.drop);
        return ERR_MEM;
    }

    g_whdBufferOps.whd_buffer_add_remove_at_front(&buffer, WHD_LINK_HEADER);

    data = g_whdBufferOps.whd_buffer_get_current_piece_data_pointer(buffer);
    pbuf_copy_partial(p, data, p->tot_len, 0);

    // WHD takes ownership of the buffer
    if (whd_network_send_ethernet_data(whdInterface, buffer) != WHD_SUCCESS)
    {
        LINK_STATS_INC(link.drop);
        return ERR_IF;
    }

    MIB2_STATS_NETIF_ADD(netif, ifoutoctets, p->tot_len);
    LINK_STATS_INC(link.xmit);

    return ERR_OK;
}

// ---------------------------------------------------------------------------
// WiFi API (nf_lwipthread_wifi.h)
// ---------------------------------------------------------------------------

int nf_wifi_ensure_up(void)
{
    int result = 0;

    chMtxLock(&wifiMutex);

    if (!wifiUp)
    {
        result = WifiStart();
    }

    chMtxUnlock(&wifiMutex);

    return result;
}

int nf_wifi_connect(const char *ssid, const char *password, uint32_t auth_type)
{
    whd_ssid_t whdSsid;
    whd_security_t security;
    size_t ssidLength;
    size_t keyLength;
    whd_result_t result;

    if (ssid == NULL)
    {
        return -1;
    }

    if (nf_wifi_ensure_up() != 0)
    {
        return -1;
    }

    ssidLength = strlen(ssid);
    keyLength = password ? strlen(password) : 0;

    if (ssidLength > sizeof(whdSsid.value) || keyLength > 64)
    {
        return -1;
    }

    whdSsid.length = (uint8_t)ssidLength;
    memcpy(whdSsid.value, ssid, ssidLength);

    if (keyLength == 0)
    {
        security = WHD_SECURITY_OPEN;
    }
    else if (auth_type != 0)
    {
        // same encoding as the WHD security flags (e.g. 0x00400004 = WPA2 AES PSK)
        security = (whd_security_t)auth_type;
    }
    else
    {
        security = WHD_SECURITY_WPA2_AES_PSK;
    }

    // clean disconnect first
    if (linkUp)
    {
        (void)whd_wifi_leave(whdInterface);
        linkUp = false;
        nf_wifi_notify_link_down();
    }

    result = whd_wifi_join(whdInterface, &whdSsid, security, (const uint8_t *)password, (uint8_t)keyLength);

    if (result != WHD_SUCCESS)
    {
        return (int)result;
    }

    linkUp = true;
    nf_wifi_notify_link_up();

    return 0;
}

int nf_wifi_disconnect(void)
{
    whd_result_t result;

    if (!wifiUp)
    {
        return 0;
    }

    result = whd_wifi_leave(whdInterface);

    if (linkUp)
    {
        linkUp = false;
        nf_wifi_notify_link_down();
    }

    return (result == WHD_SUCCESS) ? 0 : (int)result;
}

int nf_wifi_scan_start(void)
{
    if (nf_wifi_ensure_up() != 0)
    {
        return -1;
    }

    nf_wifi_scan_reset();
    chBSemReset(&scanDone, true);
    memset(&scanResult, 0, sizeof(scanResult));

    if (whd_wifi_scan(
            whdInterface,
            WHD_SCAN_TYPE_ACTIVE,
            WHD_BSS_TYPE_ANY,
            NULL,
            NULL,
            NULL,
            NULL,
            ScanResultCallback,
            &scanResult,
            NULL) != WHD_SUCCESS)
    {
        return -1;
    }

    if (chBSemWaitTimeout(&scanDone, TIME_MS2I(NF_WIFI_WHD_SCAN_TIMEOUT)) == MSG_TIMEOUT)
    {
        (void)whd_wifi_stop_scan(whdInterface);
    }

    return 0;
}

int nf_wifi_is_connected(void)
{
    return (wifiUp && linkUp && whd_wifi_is_ready_to_transceive(whdInterface) == WHD_SUCCESS) ? 1 : 0;
}

int nf_wifi_get_link_status(void)
{
    return linkUp ? 1 : 0;
}

int nf_wifi_get_join_state(void)
{
    return linkUp ? 1 : 0;
}

uint32_t nf_wifi_direct_poll(void)
{
    // the WHD thread services the bus
    return 0;
}
