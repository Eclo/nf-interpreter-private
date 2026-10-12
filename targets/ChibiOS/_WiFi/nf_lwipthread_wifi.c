//
// Copyright (c) .NET Foundation and Contributors
// Portions Copyright (c) 2001-2004 Swedish Institute of Computer Science.  All rights reserved.
// Portions Copyright (c) 2006..2015 Giovanni Di Sirio. All rights reserved.
// See LICENSE file in the project root for full license information.
//

// WiFi-aware lwIP thread for ChibiOS.
// This replaces nf_lwipthread.c, using a WiFi chip instead of
// ChibiOS ETHD1 (Ethernet MAC). The lwIP stack is reused as-is.
// This file is driver neutral; the WiFi driver backend implements the hooks in nf_wifi_driver.h.

#include <string.h>
#include "hal.h"
#include "evtimer.h"
#include "nf_lwipthread_wifi.h"
#include "nf_wifi_driver.h"
#include <nanoHAL_ConfigurationManager.h>

// CLR event system - fire SYSTEM_EVENT_FLAG_WIFI_STATION on link change
extern void Events_Set(uint32_t events);
#define SYSTEM_EVENT_FLAG_WIFI_STATION 0x01000000

#include <lwip/opt.h>
#include <lwip/def.h>
#include <lwip/mem.h>
#include <lwip/pbuf.h>
#include <lwip/sys.h>
#include <lwip/stats.h>
#include <lwip/snmp.h>
#include <lwip/tcpip.h>
#include <netif/etharp.h>
#include <lwip/netifapi.h>
#include <lwip/apps/sntp.h>

// Default SNTP server addresses (matches lwip_sntp_default_options.h)
#ifndef SNTP_SERVER0_DEFAULT_ADDRESS
#define SNTP_SERVER0_DEFAULT_ADDRESS "0.pool.ntp.org"
#endif
#ifndef SNTP_SERVER1_DEFAULT_ADDRESS
#define SNTP_SERVER1_DEFAULT_ADDRESS "1.pool.ntp.org"
#endif

#if LWIP_DHCP
#include <lwip/dhcp.h>
#endif

#if LWIP_AUTOIP
#include <lwip/autoip.h>
#endif

#define PERIODIC_TIMER_ID 1

// Suspension point for initialization
thread_reference_t lwip_trp = NULL;

// Stack area for the WiFi lwIP thread
static THD_WORKING_AREA(wa_lwip_thread, LWIP_THREAD_STACK_SIZE + 512);

// Single global netif for WiFi
static struct netif thisif = {0};
static net_addr_mode_t addressMode;
static ip4_addr_t ip, gateway, netmask;

// ---------------------------------------------------------------------------
// Scan results storage
// ---------------------------------------------------------------------------
#define MAX_SCAN_RESULTS 32

static wifi_scan_record_t scan_results[MAX_SCAN_RESULTS];
static volatile int scan_result_count = 0;

void nf_wifi_scan_reset(void)
{
    scan_result_count = 0;
}

// Called by the driver backend for each AP found
void nf_wifi_scan_add(
    const uint8_t *bssid,
    const uint8_t *ssid,
    uint8_t ssidLength,
    int16_t rssi,
    uint8_t authMode,
    uint16_t channel)
{
    // Deduplicate: if an AP with the same SSID and BSSID already exists,
    // update it only if the new result has a stronger (less negative) RSSI.
    uint8_t ssid_len = ssidLength < 32 ? ssidLength : 32;
    for (int i = 0; i < scan_result_count; i++)
    {
        if (memcmp(scan_results[i].bssid, bssid, 6) == 0 && memcmp(scan_results[i].ssid, ssid, ssid_len) == 0 &&
            scan_results[i].ssid[ssid_len] == 0)
        {
            // Duplicate found - keep the stronger signal
            if (rssi > scan_results[i].rssi)
            {
                scan_results[i].rssi = rssi;
                scan_results[i].auth_mode = authMode;
                scan_results[i].channel = channel;
            }
            return;
        }
    }

    if (scan_result_count >= MAX_SCAN_RESULTS)
    {
        return; // buffer full, ignore
    }

    wifi_scan_record_t *rec = &scan_results[scan_result_count];
    memcpy(rec->bssid, bssid, 6);
    memset(rec->ssid, 0, sizeof(rec->ssid));

    memcpy(rec->ssid, ssid, ssid_len);
    rec->rssi = rssi;
    rec->auth_mode = authMode;
    rec->channel = channel;
    scan_result_count++;
}

struct netif *nf_getNetif(void)
{
    return &thisif;
}

// ---------------------------------------------------------------------------
// WiFi netif low-level functions
// ---------------------------------------------------------------------------

// Called to initialize the WiFi network interface
// Matches SDK cyw43_netif_init - sets flags, MAC, MTU.
static void wifi_low_level_init(struct netif *netif)
{
    // Set MAC hardware address from the WiFi chip (read during firmware init).
    // Must be done here (inside netif_add callback) because netif_add may
    // zero the struct before calling the init function.
    nf_wifi_drv_get_mac(netif->hwaddr);
    netif->hwaddr_len = ETHARP_HWADDR_LEN;

    // Maximum transfer unit for WiFi
    netif->mtu = 1500;

    // Flags match the SDK exactly: BROADCAST | ETHARP | ETHERNET | IGMP
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
}

// Called by the driver backend when an Ethernet frame is received from WiFi.
void nf_wifi_netif_input(const uint8_t *frame, size_t length)
{
    if (length == 0 || frame == NULL)
        return;

    // Allocate a pbuf and copy the frame data
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)length, PBUF_POOL);
    if (p == NULL)
    {
        LINK_STATS_INC(link.memerr);
        LINK_STATS_INC(link.drop);
        return;
    }

    // Copy frame data into pbuf chain
    struct pbuf *q;
    uint16_t offset = 0;
    for (q = p; q != NULL; q = q->next)
    {
        uint16_t copy_len = q->len;
        if (offset + copy_len > length)
        {
            copy_len = (uint16_t)(length - offset);
        }
        memcpy(q->payload, frame + offset, copy_len);
        offset += copy_len;
    }

    MIB2_STATS_NETIF_ADD(&thisif, ifinoctets, p->tot_len);
    LINK_STATS_INC(link.recv);

    // Pass to tcpip_input for processing in the tcpip thread
    if (thisif.input(p, &thisif) != ERR_OK)
    {
        pbuf_free(p);
    }
}

// ---------------------------------------------------------------------------
// Network interface init (passed to netifapi_netif_add)
// ---------------------------------------------------------------------------

static err_t wifiif_init(struct netif *netif)
{
    osalDbgAssert((netif != NULL), "netif != NULL");

    MIB2_INIT_NETIF(netif, snmp_ifType_ieee80211, 54000000); // 54 Mbps WiFi

    netif->state = NULL;
    netif->name[0] = 'w';
    netif->name[1] = 'l';
    netif->output = etharp_output;
    netif->linkoutput = nf_wifi_drv_linkoutput;

    wifi_low_level_init(netif);

    return ERR_OK;
}

// ---------------------------------------------------------------------------
// lwIP callbacks for WiFi driver events
// ---------------------------------------------------------------------------

// Netif status callback - fires when IP address changes (e.g., DHCP completes).
// Signal the CLR so NativeConnect's WaitEvents loop wakes up, and start SNTP.
static void wifi_netif_status_cb(struct netif *nif)
{
    Events_Set(SYSTEM_EVENT_FLAG_WIFI_STATION);

#if SNTP_SERVER_DNS
    // Start SNTP once we have an IP address
    if (nif->ip_addr.addr != 0)
    {
        sntp_stop();
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, SNTP_SERVER0_DEFAULT_ADDRESS);
        sntp_setservername(1, SNTP_SERVER1_DEFAULT_ADDRESS);
        sntp_init();
    }
#endif
}

// Callback scheduled on tcpip thread from nf_wifi_notify_driver_ready.
// At this point the WiFi firmware is loaded and the chip MAC is valid.
static void wifi_tcpip_init_done(void *arg)
{
    struct netif *ifc = (struct netif *)arg;

    // Copy the real MAC from the WiFi chip to the netif
    nf_wifi_drv_get_mac(ifc->hwaddr);

    // Update the in-memory network config block so managed code reads
    // the real hardware MAC (the stored default is all zeros).
    if (g_TargetConfiguration.NetworkInterfaceConfigs != NULL &&
        g_TargetConfiguration.NetworkInterfaceConfigs->Count > 0)
    {
        memcpy(
            g_TargetConfiguration.NetworkInterfaceConfigs->Configs[0]->MacAddress,
            ifc->hwaddr,
            NETIF_MAX_HWADDR_LEN);
    }

#if SNTP_SERVER_DNS
    // Pre-configure SNTP server names early, before the CLR's
    // LWIP_SOCKETS_Driver replaces our netif status callback.
    // Server names persist across sntp_stop()/sntp_init() cycles,
    // so when the CLR callback later calls sntp_init(), it will
    // use these servers.
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, SNTP_SERVER0_DEFAULT_ADDRESS);
    sntp_setservername(1, SNTP_SERVER1_DEFAULT_ADDRESS);
#endif

#if LWIP_DHCP
    // Start DHCP - matches SDK flow (dhcp_start in cyw43_cb_tcpip_init).
    // DHCP DISCOVER will be sent immediately, but won't reach the AP until
    // the WPA handshake completes.  When netif_set_link_up fires,
    // dhcp_network_changed() restarts DHCP discovery automatically.
    dhcp_start(ifc);
#endif
}

void nf_wifi_notify_driver_ready(void)
{
    // Can't call lwIP APIs directly here (driver lock may be held).
    tcpip_callback_with_block(wifi_tcpip_init_done, &thisif, 0);
}

// Forward declaration - defined below
void lwipDefaultLinkUpCB(void *p);

// Callback queued to tcpip thread for non-blocking link-up.
// Matches SDK: cyw43_cb_tcpip_set_link_up just calls netif_set_link_up.
// lwIP internally fires dhcp_network_changed() which restarts DHCP
// discovery if it is already running.
// Only called from nf_wifi_notify_link_up (after the join is complete).
static void wifi_link_up_and_dhcp(void *arg)
{
    struct netif *ifc = (struct netif *)arg;

    // Refresh the chip's real MAC on the netif (should already be set from
    // wifiif_init, but re-copy in case of reconnect after MAC change).
    nf_wifi_drv_get_mac(ifc->hwaddr);

    // Set link up flag - lwIP fires dhcp_network_changed() automatically
    // which restarts DHCP discovery.  This matches the Pico SDK exactly.
    netif_set_link_up(ifc);
}

void nf_wifi_notify_link_up(void)
{
    // Queue link-up to the tcpip thread (NON-BLOCKING).
    // Link-up must only be signalled once auth + link + keyed is reached.
    // DHCP restart is handled by lwIP internally via dhcp_network_changed().
    tcpip_callback_with_block(wifi_link_up_and_dhcp, &thisif, 0);

    // Signal the CLR that WiFi station state changed (link up / connect complete)
    Events_Set(SYSTEM_EVENT_FLAG_WIFI_STATION);
}

// Callback queued to tcpip thread for non-blocking link-down + DHCP stop
static void wifi_link_down_and_dhcp_stop(void *arg)
{
    struct netif *ifc = (struct netif *)arg;

    netif_set_link_down(ifc);

#if LWIP_DHCP
    // Stop DHCP, then immediately re-start so that dhcp_network_changed()
    // (fired by the next netif_set_link_up) has an active DHCP session
    // to restart discovery on.  This matches the Pico SDK where DHCP is
    // started once during init and left running forever.
    dhcp_release_and_stop(ifc);
    dhcp_start(ifc);
#endif
}

void nf_wifi_notify_link_down(void)
{
    // Non-blocking - same reasoning as nf_wifi_notify_link_up
    tcpip_callback_with_block(wifi_link_down_and_dhcp_stop, &thisif, 0);

    // Signal the CLR that WiFi station state changed (link down / disconnect)
    Events_Set(SYSTEM_EVENT_FLAG_WIFI_STATION);
}

// ---------------------------------------------------------------------------
// Link up/down callbacks (same as nf_lwipthread.c)
// ---------------------------------------------------------------------------

void lwipDefaultLinkUpCB(void *p)
{
    struct netif *ifc = (struct netif *)p;

#if LWIP_AUTOIP
    if (addressMode == NET_ADDRESS_AUTO)
    {
        autoip_start(ifc);
    }
#endif

#if LWIP_DHCP
    if (addressMode == NET_ADDRESS_DHCP)
    {
        dhcp_start(ifc);
    }
#endif

#if SNTP_SERVER_DNS
    if (addressMode != NET_ADDRESS_DHCP)
    {
        sntp_init();
    }
#endif
}

void lwipDefaultLinkDownCB(void *p)
{
    struct netif *ifc = (struct netif *)p;

#if LWIP_AUTOIP
    if (addressMode == NET_ADDRESS_AUTO)
    {
        autoip_stop(ifc);
    }
#endif

#if LWIP_DHCP
    if (addressMode == NET_ADDRESS_DHCP)
    {
        dhcp_release_and_stop(ifc);
    }
#endif

#if SNTP_SERVER_DNS
    sntp_stop();
#endif
}

// ---------------------------------------------------------------------------
// WiFi lwIP thread
// ---------------------------------------------------------------------------

static THD_FUNCTION(lwip_thread, p)
{
    event_timer_t evt;
    event_listener_t el0;
    err_t result;
    tcpip_callback_fn link_up_cb = NULL;
    tcpip_callback_fn link_down_cb = NULL;
    bool prev_link_status = false;

    chRegSetThreadName("lwip_wifi");

    // TCP/IP parameters from configuration
    if (p)
    {
        lwipthread_opts_t *opts = p;
        unsigned i;

        for (i = 0; i < 6; i++)
        {
            thisif.hwaddr[i] = opts->macaddress[i];
        }

        ip.addr = opts->address;
        gateway.addr = opts->gateway;
        netmask.addr = opts->netmask;
        addressMode = opts->addrMode;

#if LWIP_NETIF_HOSTNAME
        thisif.hostname = opts->ourHostName;
#endif

        link_up_cb = opts->link_up_cb;
        link_down_cb = opts->link_down_cb;
    }
    else
    {
        thisif.hwaddr[0] = LWIP_ETHADDR_0;
        thisif.hwaddr[1] = LWIP_ETHADDR_1;
        thisif.hwaddr[2] = LWIP_ETHADDR_2;
        thisif.hwaddr[3] = LWIP_ETHADDR_3;
        thisif.hwaddr[4] = LWIP_ETHADDR_4;
        thisif.hwaddr[5] = LWIP_ETHADDR_5;

        LWIP_IPADDR(&ip);
        LWIP_GATEWAY(&gateway);
        LWIP_NETMASK(&netmask);
    }

#if LWIP_NETIF_HOSTNAME
    if (thisif.hostname == NULL)
    {
        thisif.hostname = LWIP_NETIF_HOSTNAME_STRING;
    }
#endif

    if (!link_up_cb)
    {
        link_up_cb = lwipDefaultLinkUpCB;
    }

    if (!link_down_cb)
    {
        link_down_cb = lwipDefaultLinkDownCB;
    }

    // Initialize the WiFi driver (powers on the chip, loads firmware)
    nf_wifi_drv_init();

    // Add the WiFi network interface
    result = netifapi_netif_add(&thisif, &ip, &netmask, &gateway, NULL, wifiif_init, tcpip_input);
    if (result != ERR_OK)
    {
        chThdSleepMilliseconds(1000);
        osalSysHalt("wifiif_add error");
    }

    netifapi_netif_set_default(&thisif);
    netifapi_netif_set_up(&thisif);

    // NOTE: DHCP is NOT started here - the netif MAC may still be a placeholder
    // because the WiFi firmware may not have been loaded yet (lazy init).
    // DHCP is started later from nf_wifi_notify_driver_ready, which the driver
    // fires once the firmware is up and the real MAC is available.

    // Register status callback so that DHCP IP assignment fires Event_Wifi_Station,
    // waking NativeConnect's WaitEvents loop.
    netif_set_status_callback(&thisif, wifi_netif_status_cb);

    // Set up periodic timer for link status polling
    evtObjectInit(&evt, LWIP_LINK_POLL_INTERVAL);
    evtStart(&evt);
    chEvtRegisterMask(&evt.et_es, &el0, PERIODIC_TIMER_ID);
    chEvtAddEvents(PERIODIC_TIMER_ID);

    // Resume the caller (lwIPInit is waiting)
    chThdResume(&lwip_trp, MSG_OK);
    chThdSetPriority(LWIP_THREAD_PRIORITY);

    // Setup SNTP - only after WiFi is connected and DHCP has IP.
    // SNTP is started from wifi_netif_status_cb when IP becomes non-zero.
    // Starting it here at boot causes ARP/DNS probes on a non-connected interface.

    // Main event loop
    while (true)
    {
        eventmask_t mask = chEvtWaitAny(ALL_EVENTS);

        if (mask & PERIODIC_TIMER_ID)
        {
            // Check WiFi link status (driver connection state).
            // IMPORTANT: Only handle link-DOWN here.  Link-UP is handled
            // exclusively by the driver's link up notification,
            // which fires when auth + link + keyed is reached.
            // Starting DHCP before the WPA 4-way handshake completes sends unencrypted
            // frames that the AP drops, poisoning the DHCP server state.
            bool current_link_status = nf_wifi_is_connected();

            if (prev_link_status && !current_link_status)
            {
                prev_link_status = false;
                tcpip_callback_with_block(wifi_link_down_and_dhcp_stop, &thisif, 0);
            }
            else if (current_link_status)
            {
                prev_link_status = true;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// tcpip init completion callback
// ---------------------------------------------------------------------------

static void tcpIpInitDone(void *arg)
{
    semaphore_t *initDone = arg;
    chSemSignal(initDone);
}

// ---------------------------------------------------------------------------
// lwIPInit - same API as nf_lwipthread.c
// ---------------------------------------------------------------------------

void lwIPInit(const lwipthread_opts_t *opts)
{
    // Create the WiFi lwIP thread
    chThdCreateStatic(wa_lwip_thread, sizeof(wa_lwip_thread), chThdGetPriorityX() - 1, lwip_thread, (void *)opts);

    // Create semaphore for TCP/IP init completion
    semaphore_t initDone;
    chSemObjectInit(&initDone, 0);

    // Initialize TCP/IP stack
    tcpip_init(tcpIpInitDone, &initDone);

    // Wait for lwIP thread initialization
    chSysLock();
    chThdSuspendS(&lwip_trp);
    chSysUnlock();

    // Wait for TCP/IP stack init
    chSemWait(&initDone);
}

// ---------------------------------------------------------------------------
// lwipReconfigure - same API as nf_lwipthread.c
// ---------------------------------------------------------------------------

typedef struct lwip_reconf_params
{
    const lwipreconf_opts_t *opts;
    semaphore_t completion;
} lwip_reconf_params_t;

static void do_reconfigure(void *p)
{
    lwip_reconf_params_t *reconf = (lwip_reconf_params_t *)p;

    switch (addressMode)
    {
#if LWIP_DHCP
        case NET_ADDRESS_DHCP:
            if (netif_is_up(&thisif))
            {
                dhcp_release_and_stop(&thisif);
            }
            break;
#endif
        case NET_ADDRESS_STATIC:
        {
            ip4_addr_t zero = {0};
            netif_set_ipaddr(&thisif, &zero);
            netif_set_netmask(&thisif, &zero);
            netif_set_gw(&thisif, &zero);
            break;
        }
#if LWIP_AUTOIP
        case NET_ADDRESS_AUTO:
            if (netif_is_up(&thisif))
                autoip_stop(&thisif);
            break;
#endif
    }

    ip.addr = reconf->opts->address;
    gateway.addr = reconf->opts->gateway;
    netmask.addr = reconf->opts->netmask;
    addressMode = reconf->opts->addrMode;

    switch (addressMode)
    {
#if LWIP_DHCP
        case NET_ADDRESS_DHCP:
            dhcp_start(&thisif);
            break;
#endif
        case NET_ADDRESS_STATIC:
            netif_set_ipaddr(&thisif, &ip);
            netif_set_netmask(&thisif, &netmask);
            netif_set_gw(&thisif, &gateway);
            break;
#if LWIP_AUTOIP
        case NET_ADDRESS_AUTO:
            if (netif_is_up(&thisif))
                autoip_start(&thisif);
            break;
#endif
    }

    chSemSignal(&reconf->completion);
}

void lwipReconfigure(const lwipreconf_opts_t *opts)
{
    lwip_reconf_params_t params;
    params.opts = opts;
    chSemObjectInit(&params.completion, 0);
    tcpip_callback_with_block(do_reconfigure, &params, 0);
    chSemWait(&params.completion);
}

// ---------------------------------------------------------------------------
// WiFi-specific helper functions
// ---------------------------------------------------------------------------

static volatile bool wifi_powered = false;

void nf_wifi_power_on(void)
{
    if (!wifi_powered)
    {
        // WiFi driver is initialized in the lwip_thread via nf_wifi_drv_init()
        wifi_powered = true;
    }
}

void nf_wifi_power_off(void)
{
    if (wifi_powered)
    {
        nf_wifi_drv_deinit();
        wifi_powered = false;
    }
}

int nf_wifi_scan_get_count(void)
{
    return scan_result_count;
}

const wifi_scan_record_t *nf_wifi_scan_get_results(void)
{
    return scan_results;
}

void nf_wifi_get_mac_addr(uint8_t *mac)
{
    // Copy from the netif (set during initialization)
    memcpy(mac, thisif.hwaddr, 6);
}

void nf_wifi_set_netif_mac(const uint8_t *mac)
{
    memcpy(thisif.hwaddr, mac, 6);
}

uint32_t nf_wifi_get_ip4_address(void)
{
    return thisif.ip_addr.addr;
}
