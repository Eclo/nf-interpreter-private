//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#ifndef SYS_DEV_ACCONEER_H
#define SYS_DEV_ACCONEER_H

#include <nanoCLR_Interop.h>
#include <nanoCLR_Runtime.h>
#include <nanoPackStruct.h>
#include <corlib_native.h>

#include <target_system_device_acconeer_config.h>

#include <acc_definitions_a121.h>
#include <acc_detector_distance.h>
#include <acc_hal_definitions_a121.h>
#include <acc_hal_integration_a121.h>
#include <acc_integration.h>
#include <acc_integration_log.h>
#include <acc_rss_a121.h>
#include <acc_sensor.h>
#include <acc_version.h>

struct Library_sys_dev_acconeer_System_Device_Acconeer_Sensor
{
    static const int FIELD___disposedValue = 1;
    static const int FIELD___enabled = 2;
    static const int FIELD___sensorId = 3;
    static const int FIELD___spiDevice = 4;
    static const int FIELD___enablePinNumber = 5;
    static const int FIELD___interruptPinNumber = 6;

    NANOCLR_NATIVE_DECLARE(_ctor___VOID__U4);
    NANOCLR_NATIVE_DECLARE(PerformCalibration___BOOLEAN__I4);
    NANOCLR_NATIVE_DECLARE(NativeInit___VOID);
    NANOCLR_NATIVE_DECLARE(NativeDeInit___VOID);
    NANOCLR_NATIVE_DECLARE(NativeSetEnable___VOID__BOOLEAN);

    //--//
};

extern const CLR_RT_NativeAssemblyData g_CLR_AssemblyNative_System_Device_Acconeer;

#endif // SYS_DEV_ACCONEER_H
