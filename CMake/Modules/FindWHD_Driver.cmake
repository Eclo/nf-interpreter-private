#
# Copyright (c) .NET Foundation and Contributors
# See LICENSE file in the project root for full license information.
#

include(FetchContent)
FetchContent_GetProperties(whd_driver)

set(WHD_DRIVER_ROOT ${whd_driver_SOURCE_DIR}/WiFi_Host_Driver)
set(WHD_PORT_DIR ${CMAKE_SOURCE_DIR}/targets/ChibiOS/_WiFi/whd)

set(WHD_DRIVER_SRCS
    whd_ap.c
    whd_buffer_api.c
    whd_cdc_bdc.c
    whd_chip.c
    whd_chip_constants.c
    whd_clm.c
    whd_commonring.c
    whd_debug.c
    whd_events.c
    whd_flowring.c
    whd_logging.c
    whd_management.c
    whd_msgbuf_txrx.c
    whd_network_if.c
    whd_proto.c
    whd_resource_if.c
    whd_ring.c
    whd_sdpcm.c
    whd_thread.c
    whd_utils.c
    whd_wifi.c
    whd_wifi_api.c
    whd_wifi_p2p.c

    # bus protocols (SDIO only)
    whd_bus.c
    whd_bus_common.c
    whd_bus_sdio_protocol.c
)

# ChibiOS port for WHD and nanoFramework WiFi backend
set(WHD_PORT_SRCS
    cyabs_rtos_chibios.c
    cyhal_chibios.c
    whd_resources_chibios.c
    whd_buffer_chibios.c
    nf_wifi_whd.c
)

set(WHD_DRIVER_INCLUDE_DIRS "")
set(WHD_DRIVER_SOURCES "")

# port headers first: they replace the placeholder cyabs_rtos_impl.h and cyhal_hw_types.h shipped with WHD
list(APPEND WHD_DRIVER_INCLUDE_DIRS ${WHD_PORT_DIR}/port)
list(APPEND WHD_DRIVER_INCLUDE_DIRS ${WHD_PORT_DIR})
list(APPEND WHD_DRIVER_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/targets/ChibiOS/_WiFi)

list(APPEND WHD_DRIVER_INCLUDE_DIRS ${WHD_DRIVER_ROOT}/inc)
list(APPEND WHD_DRIVER_INCLUDE_DIRS ${WHD_DRIVER_ROOT}/src)
list(APPEND WHD_DRIVER_INCLUDE_DIRS ${WHD_DRIVER_ROOT}/src/include)
list(APPEND WHD_DRIVER_INCLUDE_DIRS ${WHD_DRIVER_ROOT}/src/bus_protocols)
list(APPEND WHD_DRIVER_INCLUDE_DIRS ${whd_driver_SOURCE_DIR}/External/bsp)
list(APPEND WHD_DRIVER_INCLUDE_DIRS ${whd_driver_SOURCE_DIR}/External/hal)
list(APPEND WHD_DRIVER_INCLUDE_DIRS ${whd_driver_SOURCE_DIR}/External/rtos)

foreach(SRC_FILE ${WHD_DRIVER_SRCS})

    set(WHD_SRC_FILE SRC_FILE-NOTFOUND)

    find_file(WHD_SRC_FILE ${SRC_FILE}
        PATHS
            ${WHD_DRIVER_ROOT}/src
            ${WHD_DRIVER_ROOT}/src/bus_protocols
        CMAKE_FIND_ROOT_PATH_BOTH
    )

    if(BUILD_VERBOSE)
        message("${SRC_FILE} >> ${WHD_SRC_FILE}")
    endif()

    list(APPEND WHD_DRIVER_SOURCES ${WHD_SRC_FILE})

endforeach()

set(WHD_DRIVER_THIRD_PARTY_SOURCES ${WHD_DRIVER_SOURCES})

foreach(SRC_FILE ${WHD_PORT_SRCS})
    list(APPEND WHD_DRIVER_SOURCES ${WHD_PORT_DIR}/${SRC_FILE})
endforeach()

# relax warnings for the WHD sources (third party, we can't modify them)
set_source_files_properties(${WHD_DRIVER_THIRD_PARTY_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

set(WHD_DRIVER_DEFINITIONS
    # bus interface: SDIO (CYBSP_SDIO_INTERFACE)
    -DCYBSP_WIFI_INTERFACE_TYPE=0
)

# WHD logging: error and info messages go to a RAM ring buffer (inspect 'logbuf' with the debugger)
option(WHD_LOGGING "WHD: log error and info messages to a RAM buffer (logbuf)" OFF)

# WHD verbose logging: adds the debug messages and the read back verification of the downloaded firmware and NVRAM
option(WHD_LOGGING_VERBOSE "WHD: also log debug messages and verify the firmware download (requires WHD_LOGGING)" OFF)

if(WHD_LOGGING)
    list(APPEND WHD_DRIVER_DEFINITIONS -DWHD_LOGGING_BUFFER_ENABLE -DWPRINT_ENABLE_WHD_INFO)

    if(WHD_LOGGING_VERBOSE)
        list(APPEND WHD_DRIVER_DEFINITIONS -DWPRINT_ENABLE_WHD_DEBUG)
    endif()
else()
    list(APPEND WHD_DRIVER_DEFINITIONS -DWHD_PRINT_DISABLE)
endif()

include(FindPackageHandleStandardArgs)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(WHD_Driver DEFAULT_MSG WHD_DRIVER_INCLUDE_DIRS WHD_DRIVER_SOURCES)
