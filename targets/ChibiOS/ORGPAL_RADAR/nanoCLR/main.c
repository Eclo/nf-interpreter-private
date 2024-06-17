//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#include <ch.h>
#include <hal.h>
#include <hal_nf_community.h>
#include <cmsis_os.h>

#include "usbcfg.h"
#include <swo.h>
#include <CLR_Startup_Thread.h>
#include <WireProtocol_ReceiverThread.h>
#include <nanoCLR_Application.h>
#include <nanoPAL_BlockStorage.h>
#include <nanoHAL_v2.h>
#include <targetPAL.h>

// need to declare the Receiver thread here
osThreadDef(ReceiverThread, osPriorityHigh, 2048, "ReceiverThread");
// declare CLRStartup thread here
osThreadDef(CLRStartupThread, osPriorityNormal, 4096, "CLRStartupThread");

//  Application entry point.
int main(void)
{
    // find out wakeup reason
    if ((PWR->CSR1 & PWR_CSR1_WUIF) == PWR_CSR1_WUIF)
    {
        // standby, match WakeupReason_FromStandby enum
        WakeupReasonStore = 1;
    }
    else if ((PWR->CSR2 & (PWR_CSR2_WUPF3 | PWR_CSR2_WUPF2 | PWR_CSR2_WUPF1)))
    {
        // wake from pin, match WakeupReason_FromPin enum
        WakeupReasonStore = 2;
    }
    else
    {
        // undetermined reason, match WakeupReason_Undetermined enum
        WakeupReasonStore = 0;
    }

    // first things first: need to clear any possible wakeup flags
    // clear wakeup flags from GPIOs
    PWR->CR2 |= (PWR_CR2_CWUPF6 | PWR_CR2_CWUPF5 | PWR_CR2_CWUPF4 | PWR_CR2_CWUPF3 | PWR_CR2_CWUPF2 | PWR_CR2_CWUPF1);
    // clear standby Flag
    PWR->CR1 |= PWR_CR1_CSBF;

    __ISB();

    // HAL initialization, this also initializes the configured device drivers
    // and performs the board-specific initializations.
    halInit();

    // init boot clipboard
    InitBootClipboard();

    // set default values for GPIOs
    palClearLine(LINE_LED1);
    palClearLine(LINE_LED2);
    palClearLine(LINE_LED3);
    palClearLine(LINE_RELAY_CTRL);
    palClearLine(LINE_SPI_SEL0);
    palClearLine(LINE_SPI_SEL1);
    palClearLine(LINE_SPI_SEL2);
    palClearLine(LINE_A121_SPI_SS);

// init SWO as soon as possible to make it available to output ASAP
#if (SWO_OUTPUT == TRUE)
    SwoInit();
#endif

    // The kernel is initialized but not started yet, this means that
    // main() is executing with absolute priority but interrupts are already enabled.
    osKernelInitialize();

    // start watchdog
    Watchdog_Init();

#if (HAL_NF_USE_STM32_CRC == TRUE)
    // startup crc
    crcStart(NULL);
#endif

    //  Initializes a serial-over-USB CDC driver.
    sduObjectInit(&SERIAL_DRIVER);
    sduStart(&SERIAL_DRIVER, &serusbcfg);

    // Activates the USB driver and then the USB bus pull-up on D+.
    // Note, a delay is inserted in order to not have to disconnect the cable after a reset
    usbDisconnectBus(serusbcfg.usbp);
    chThdSleepMilliseconds(100);
    usbStart(serusbcfg.usbp, &usbcfg);
    usbConnectBus(serusbcfg.usbp);

    // create the receiver thread
    osThreadCreate(osThread(ReceiverThread), NULL);

    // CLR settings to launch CLR thread
    CLR_SETTINGS clrSettings;
    (void)memset(&clrSettings, 0, sizeof(CLR_SETTINGS));

    clrSettings.MaxContextSwitches = 50;
    clrSettings.WaitForDebugger = false;
    clrSettings.EnterDebuggerLoopAfterExit = true;

    // create the CLR Startup thread
    osThreadCreate(osThread(CLRStartupThread), &clrSettings);

    // start kernel, after this main() will behave like a thread with priority osPriorityNormal
    osKernelStart();

    while (true)
    {
        /////////////////////////////////////////////////////
        // uncomment to test successful loading of nanoCLR //
        /////////////////////////////////////////////////////
        palToggleLine(LINE_LED1);
        /////////////////////////////////////////////////////

        osDelay(100);
    }
}