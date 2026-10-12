#
# Copyright (c) .NET Foundation and Contributors
# See LICENSE file in the project root for full license information.
#

# Patches the Infineon Wi-Fi Host Driver (WHD) sources after fetching them.
# Run as: cmake -DWHD_SOURCE_DIR=<path> -P whd_patch.cmake
# The edits are idempotent and work regardless of the file line endings.

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
