//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// ChibiOS implementation types for the Cypress/Infineon RTOS abstraction (cyabs_rtos.h) used by WHD.
// Replaces the placeholder cyabs_rtos_impl.h shipped with WHD.

#ifndef CYABS_RTOS_IMPL_H
#define CYABS_RTOS_IMPL_H

#include <stdint.h>
#include <stdbool.h>
#include <ch.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // RTOS thread priority (mapped around NORMALPRIO)
    typedef enum
    {
        CY_RTOS_PRIORITY_MIN,
        CY_RTOS_PRIORITY_LOW,
        CY_RTOS_PRIORITY_BELOWNORMAL,
        CY_RTOS_PRIORITY_NORMAL,
        CY_RTOS_PRIORITY_ABOVENORMAL,
        CY_RTOS_PRIORITY_HIGH,
        CY_RTOS_PRIORITY_REALTIME,
        CY_RTOS_PRIORITY_MAX
    } cy_thread_priority_t;

    // counting semaphore with a maximum count
    typedef struct
    {
        semaphore_t semaphore;
        cnt_t maxCount;
    } cy_semaphore_t;

    typedef thread_t *cy_thread_t;
    typedef void *cy_thread_arg_t;
    typedef uint32_t cy_time_t;
    typedef msg_t cy_rtos_error_t;

    // events are only used by the msgbuf protocol (not used with SDIO), not implemented
    typedef uint32_t cy_event_t;

#ifdef __cplusplus
}
#endif

#endif // CYABS_RTOS_IMPL_H
