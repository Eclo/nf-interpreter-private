//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

// ChibiOS port of the Cypress/Infineon RTOS abstraction (cyabs_rtos.h), the subset used by WHD:
// threads, semaphores, time and delay.

#include <ch.h>
#include <cyabs_rtos.h>

// max number of threads created through this API (WHD creates one)
#define CY_RTOS_MAX_THREADS 2

typedef struct
{
    cy_thread_entry_fn_t entry;
    cy_thread_arg_t arg;
    thread_t *thread;
    bool inUse;
} cy_thread_context_t;

static cy_thread_context_t threadContexts[CY_RTOS_MAX_THREADS];

static tprio_t cy_rtos_map_priority(cy_thread_priority_t priority)
{
    // CY_RTOS_PRIORITY_NORMAL maps to NORMALPRIO
    int32_t offset = (int32_t)priority - (int32_t)CY_RTOS_PRIORITY_NORMAL;

    return (tprio_t)((int32_t)NORMALPRIO + offset);
}

static sysinterval_t cy_rtos_map_timeout(cy_time_t timeout_ms)
{
    if (timeout_ms == CY_RTOS_NEVER_TIMEOUT)
    {
        return TIME_INFINITE;
    }

    if (timeout_ms == 0)
    {
        return TIME_IMMEDIATE;
    }

    return TIME_MS2I(timeout_ms);
}

static THD_FUNCTION(cy_rtos_thread_trampoline, p)
{
    cy_thread_context_t *context = (cy_thread_context_t *)p;

    context->entry(context->arg);

    // thread function returned without calling cy_rtos_exit_thread()
    chThdExit(MSG_OK);
}

/////////////
// threads //
/////////////

cy_rslt_t cy_rtos_create_thread(
    cy_thread_t *thread,
    cy_thread_entry_fn_t entry_function,
    const char *name,
    void *stack,
    uint32_t stack_size,
    cy_thread_priority_t priority,
    cy_thread_arg_t arg)
{
    cy_thread_context_t *context = NULL;

    if (thread == NULL || entry_function == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    chSysLock();

    for (uint32_t i = 0; i < CY_RTOS_MAX_THREADS; i++)
    {
        if (!threadContexts[i].inUse)
        {
            context = &threadContexts[i];
            context->inUse = true;
            break;
        }
    }

    chSysUnlock();

    if (context == NULL)
    {
        return CY_RTOS_NO_MEMORY;
    }

    context->entry = entry_function;
    context->arg = arg;

    if (stack != NULL)
    {
        // caller provided working area (has to be a THD_WORKING_AREA)
        *thread =
            chThdCreateStatic(stack, stack_size, cy_rtos_map_priority(priority), cy_rtos_thread_trampoline, context);
    }
    else
    {
        *thread = chThdCreateFromHeap(
            NULL,
            THD_WORKING_AREA_SIZE(stack_size),
            name,
            cy_rtos_map_priority(priority),
            cy_rtos_thread_trampoline,
            context);
    }

    if (*thread == NULL)
    {
        context->inUse = false;
        return CY_RTOS_NO_MEMORY;
    }

    context->thread = *thread;

#if (CH_CFG_USE_REGISTRY == TRUE)
    if (stack != NULL)
    {
        (*thread)->name = name;
    }
#else
    (void)name;
#endif

    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_exit_thread(void)
{
    chThdExit(MSG_OK);

    // never reached
    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_join_thread(cy_thread_t *thread)
{
    if (thread == NULL || *thread == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    (void)chThdWait(*thread);

    // release the context of the thread
    chSysLock();

    for (uint32_t i = 0; i < CY_RTOS_MAX_THREADS; i++)
    {
        if (threadContexts[i].inUse && threadContexts[i].thread == *thread)
        {
            threadContexts[i].inUse = false;
            threadContexts[i].thread = NULL;
        }
    }

    chSysUnlock();

    *thread = NULL;

    return CY_RSLT_SUCCESS;
}

////////////////
// semaphores //
////////////////

cy_rslt_t cy_rtos_init_semaphore(cy_semaphore_t *semaphore, uint32_t maxcount, uint32_t initcount)
{
    if (semaphore == NULL || maxcount == 0 || initcount > maxcount)
    {
        return CY_RTOS_BAD_PARAM;
    }

    semaphore->maxCount = (cnt_t)maxcount;
    chSemObjectInit(&semaphore->semaphore, (cnt_t)initcount);

    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_get_semaphore(cy_semaphore_t *semaphore, cy_time_t timeout_ms, bool in_isr)
{
    msg_t result;

    if (semaphore == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    if (in_isr)
    {
        // can't wait in ISR context, only take it if available
        chSysLockFromISR();

        if (chSemGetCounterI(&semaphore->semaphore) > 0)
        {
            chSemFastWaitI(&semaphore->semaphore);
            result = MSG_OK;
        }
        else
        {
            result = MSG_TIMEOUT;
        }

        chSysUnlockFromISR();
    }
    else
    {
        result = chSemWaitTimeout(&semaphore->semaphore, cy_rtos_map_timeout(timeout_ms));
    }

    if (result == MSG_TIMEOUT)
    {
        return CY_RTOS_TIMEOUT;
    }

    if (result != MSG_OK)
    {
        return CY_RTOS_GENERAL_ERROR;
    }

    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_set_semaphore(cy_semaphore_t *semaphore, bool in_isr)
{
    if (semaphore == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    if (in_isr)
    {
        chSysLockFromISR();

        // don't count over the maximum (binary semaphores)
        if (chSemGetCounterI(&semaphore->semaphore) < semaphore->maxCount)
        {
            chSemSignalI(&semaphore->semaphore);
        }

        chSysUnlockFromISR();
    }
    else
    {
        chSysLock();

        if (chSemGetCounterI(&semaphore->semaphore) < semaphore->maxCount)
        {
            chSemSignalI(&semaphore->semaphore);
            chSchRescheduleS();
        }

        chSysUnlock();
    }

    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_deinit_semaphore(cy_semaphore_t *semaphore)
{
    if (semaphore == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    // wake up any waiting thread
    chSemReset(&semaphore->semaphore, 0);

    return CY_RSLT_SUCCESS;
}

////////////////
// time/delay //
////////////////

cy_rslt_t cy_rtos_get_time(cy_time_t *tval)
{
    if (tval == NULL)
    {
        return CY_RTOS_BAD_PARAM;
    }

    *tval = (cy_time_t)TIME_I2MS(chVTGetSystemTimeX());

    return CY_RSLT_SUCCESS;
}

cy_rslt_t cy_rtos_delay_milliseconds(cy_time_t num_ms)
{
    if (num_ms == 0)
    {
        chThdYield();
    }
    else
    {
        chThdSleepMilliseconds(num_ms);
    }

    return CY_RSLT_SUCCESS;
}
