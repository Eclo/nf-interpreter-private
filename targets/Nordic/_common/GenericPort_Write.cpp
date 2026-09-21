//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#include <zephyr/kernel.h>

#include <nanoCLR_Types.h>
#include <nanoCLR_Runtime.h>

// Overrides the debug out functionality. Use zephyr's printk
uint32_t GenericPort_Write(int portNum, const char *data, size_t size)
{
    (void)portNum;

    printk("%s", data);
    return (uint32_t)size;
}
