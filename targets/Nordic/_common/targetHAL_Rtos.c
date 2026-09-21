//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#include <zephyr/kernel.h>
#include <nanoHAL_Rtos.h>

void RtosYield()
{
    k_yield();
}
