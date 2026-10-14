#
# Copyright (c) .NET Foundation and Contributors
# See LICENSE file in the project root for full license information.
#

# Patches the Infineon Wi-Fi Host Driver (WHD) sources after fetching them.

if(NOT WHD_SOURCE_DIR)
    message(FATAL_ERROR "WHD_SOURCE_DIR not set")
endif()

#######################################################################################
# BCM43362: add the backplane core base addresses (ARM CM3, SOCSRAM, SDIO device, PMU).
# The 43362 has the same core layout as the 43430/43439, WHD v3 just lists them for
# the chips it supports.
#######################################################################################
set(CHIP_CONSTANTS_FILE ${WHD_SOURCE_DIR}/WiFi_Host_Driver/src/whd_chip_constants.c)

file(READ ${CHIP_CONSTANTS_FILE} CHIP_CONSTANTS_CONTENT)

string(FIND "${CHIP_CONSTANTS_CONTENT}" "case 43362:" ALREADY_PATCHED)

if(ALREADY_PATCHED EQUAL -1)

    # the 4 switch statements that list 43430 are the core base address getters
    string(REGEX MATCHALL "case 43430:" MATCHES "${CHIP_CONSTANTS_CONTENT}")
    list(LENGTH MATCHES MATCH_COUNT)

    if(NOT MATCH_COUNT EQUAL 4)
        message(FATAL_ERROR "Unexpected WHD source: found ${MATCH_COUNT} 'case 43430:' entries in whd_chip_constants.c, expecting 4")
    endif()

    string(REPLACE "case 43430:" "case 43362:\n        case 43430:" CHIP_CONSTANTS_CONTENT "${CHIP_CONSTANTS_CONTENT}")

    file(WRITE ${CHIP_CONSTANTS_FILE} "${CHIP_CONSTANTS_CONTENT}")

    message(STATUS "WHD: added BCM43362 core base addresses")

endif()

#######################################################################################
# SDIO firmware download: take the reset instruction from the first word of the image only.
# Upstream keeps looking at the following blocks while it reads 0, and then writes it to RAM
# address 0. The BCM43362 image starts with 0 (CM3 initial stack pointer), so the first word of
# the second block was written there, corrupting the vector table and preventing the firmware
# from booting (HT clock timeout).
#######################################################################################
set(SDIO_PROTOCOL_FILE ${WHD_SOURCE_DIR}/WiFi_Host_Driver/src/bus_protocols/whd_bus_sdio_protocol.c)

file(READ ${SDIO_PROTOCOL_FILE} SDIO_PROTOCOL_CONTENT)

string(FIND "${SDIO_PROTOCOL_CONTENT}" "if (reset_instr == 0)" RESET_INSTR_FOUND)

if(NOT RESET_INSTR_FOUND EQUAL -1)

    string(REPLACE "if (reset_instr == 0)" "if (i == 0)" SDIO_PROTOCOL_CONTENT "${SDIO_PROTOCOL_CONTENT}")

    file(WRITE ${SDIO_PROTOCOL_FILE} "${SDIO_PROTOCOL_CONTENT}")

    message(STATUS "WHD: fixed reset instruction capture in SDIO firmware download")

endif()

#######################################################################################
# Remove the placeholder port headers shipped with WHD: they are included with quotes
# from their sibling headers, so they would shadow the ChibiOS port ones (_WiFi/whd/port).
#######################################################################################
foreach(PLACEHOLDER_HEADER
    External/hal/cyhal_hw_types.h
    External/rtos/cyabs_rtos_impl.h)

    if(EXISTS ${WHD_SOURCE_DIR}/${PLACEHOLDER_HEADER})
        file(REMOVE ${WHD_SOURCE_DIR}/${PLACEHOLDER_HEADER})
        message(STATUS "WHD: removed placeholder ${PLACEHOLDER_HEADER}")
    endif()

endforeach()
