//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#include <zephyr/kernel.h>
#include <nanoCLR_Headers.h>

//#define MALLOC_TRACE 1

void *platform_malloc(size_t size)
{
    void *p;

#ifdef MALLOC_TRACE

    printk("k_malloc s:%d ", size);
    p = k_malloc(size);
    printk("ptr: %x\n", (uint32_t)p);

#else

    p = k_malloc(size);

    if (p == 0)
    {
        printk("**** Malloc Fail size %d\n ****\n", size);
    }

#endif

    return p;
}

void platform_free(void *ptr)
{
#ifdef MALLOC_TRACE
    printk("k_free %x\n", (uint32_t)ptr);
#endif
    k_free(ptr);
}
