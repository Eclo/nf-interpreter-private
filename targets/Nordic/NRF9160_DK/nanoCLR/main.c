//
// Copyright (c) .NET Foundation and Contributors
// Portions Copyright (c) Ericsson AB 2020, all rights reserved
// See LICENSE file in the project root for full license information.
//

#include <zephyr/kernel.h>
#include <nf_lib.h>

// LED heartbeat to check that the firmware is running: comment out to disable
#define NANOCLR_HEARTBEAT_LED

// UART used by the Wire Protocol (Visual Studio debugger), chosen in the board overlay
#define WIRE_UART_NODE DT_CHOSEN(nanoframework_wire_uart)

#ifdef NANOCLR_HEARTBEAT_LED

#include <zephyr/drivers/gpio.h>

// LED1 on the DK (devicetree alias led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

// start_nf() never returns (the CLR runs on the main thread), so the heartbeat needs its own thread
static void heartbeat_thread(void *p1, void *p2, void *p3)
{
    (void)p1;
    (void)p2;
    (void)p3;

    if (!gpio_is_ready_dt(&led))
    {
        return;
    }

    if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) < 0)
    {
        return;
    }

    while (1)
    {
        gpio_pin_toggle_dt(&led);
        k_sleep(K_MSEC(500));
    }
}

// runs at a cooperative priority, above the CLR (main thread, priority 0): the CLR idle loop only yields to
// threads of equal or higher priority, so a lower priority heartbeat would never get scheduled
K_THREAD_DEFINE(heartbeat, 512, heartbeat_thread, NULL, NULL, NULL, K_PRIO_COOP(7), 0, 0);

#endif // NANOCLR_HEARTBEAT_LED

int main(void)
{
    printk("\nnrf9160DK greets\n");

    // Invoke the NF runtime
    int rc = 0;

    rc = start_nf(DEVICE_DT_GET(WIRE_UART_NODE));
    printk("start_nf returned %d\n", rc);

    while (1)
    {
        k_sleep(K_MSEC(30000));
    }

    return 0;
}
