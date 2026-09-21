//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#include <zephyr/kernel.h>
#include <nanoHAL_v2.h>

#include <zephyr/sys/reboot.h>

void CPU_Reset()
{
    printk("Rebooting device...\n");

    sys_reboot(SYS_REBOOT_COLD);

    printk("reboot failed. Halting\n");
    while (1)
    {
        k_sleep(K_MSEC(30000));
    }
}

// CPU sleep is not currently implemented in this target
void CPU_Sleep(SLEEP_LEVEL_type level, uint64_t wakeEvents)
{
    (void)level;
    (void)wakeEvents;
};

bool CPU_IsSoftRebootSupported()
{
    return true;
};

void CPU_SetPowerMode(PowerLevel_type powerLevel)
{
    // TODO power modes support
    (void)powerLevel;
}
