#
# Copyright (c) .NET Foundation and Contributors
# See LICENSE file in the project root for full license information.
#

include(binutils.Nordic)

##########################################
# WHEN CHANGING THESE MAKE SURE TO UPDATE:
# - THE DEV CONTAINERS
# - azure-pipelines-templates\download-install-ncs-zephyr-toolchain.yml
# - targets\Nordic\README.md
#
# nRF Connect SDK version (git tag in https://github.com/nrfconnect/sdk-nrf)
set(NRF_CONNECT_SDK_DEFAULT_VERSION "v3.4.1")
# Zephyr SDK version (must satisfy the SDK_VERSION file of the Zephyr revision used by NCS)
set(ZEPHYR_SDK_DEFAULT_VERSION "1.0.1")
##########################################

# location for downloaded dependencies (NCS west workspace and Zephyr SDK)
# can be overridden to keep the multi-GB downloads across build folder cleanups
if(NOT NF_NORDIC_DEPS_DIR)
    set(NF_NORDIC_DEPS_DIR ${CMAKE_BINARY_DIR}/_deps CACHE PATH "Location for downloaded Nordic build dependencies")
endif()

######################################
# host tools: Python + west

nf_nordic_find_python()
nf_nordic_setup_west()

######################################
# handle nRF Connect SDK installation

if(NOT NRF_CONNECT_SDK_VERSION OR "${NRF_CONNECT_SDK_VERSION}" STREQUAL "")
    set(NRF_CONNECT_SDK_VERSION ${NRF_CONNECT_SDK_DEFAULT_VERSION})
endif()
set(NRF_CONNECT_SDK_VERSION ${NRF_CONNECT_SDK_VERSION} CACHE STRING "nRF Connect SDK version" FORCE)

if(NRF_CONNECT_SDK_SOURCE_FOLDER AND NOT "${NRF_CONNECT_SDK_SOURCE_FOLDER}" STREQUAL "")

    # nRF Connect SDK source (west workspace) was specified
    if(NOT EXISTS "${NRF_CONNECT_SDK_SOURCE_FOLDER}/zephyr/")
        message(FATAL_ERROR "Couldn't find nRF Connect SDK west workspace at ${NRF_CONNECT_SDK_SOURCE_FOLDER}/ (expecting 'zephyr' and 'nrf' folders there)")
    endif()

    message(STATUS "nRF Connect SDK (source from: ${NRF_CONNECT_SDK_SOURCE_FOLDER})")

    # sanity check on the version found
    if(EXISTS ${NRF_CONNECT_SDK_SOURCE_FOLDER}/nrf/VERSION)
        file(READ ${NRF_CONNECT_SDK_SOURCE_FOLDER}/nrf/VERSION NRF_CONNECT_SDK_FOUND_VERSION)
        string(STRIP "${NRF_CONNECT_SDK_FOUND_VERSION}" NRF_CONNECT_SDK_FOUND_VERSION)

        if(NOT "v${NRF_CONNECT_SDK_FOUND_VERSION}" STREQUAL "${NRF_CONNECT_SDK_VERSION}")
            message(WARNING "\n\nFound nRF Connect SDK v${NRF_CONNECT_SDK_FOUND_VERSION}.\nExpecting ${NRF_CONNECT_SDK_VERSION}.\n\n")
        endif()
    endif()

    set(NCS_BASE_LOCATION ${NRF_CONNECT_SDK_SOURCE_FOLDER} CACHE INTERNAL "NCS base location")

else()

    # no nRF Connect SDK source specified, use west to init and download everything
    # unless this has been done before
    set(NCS_BASE_LOCATION ${NF_NORDIC_DEPS_DIR}/ncs CACHE INTERNAL "NCS base location")

    if(NOT NCS_SOURCES_DOWNLOADED OR NOT EXISTS ${NCS_BASE_LOCATION}/zephyr/)

        message(STATUS "Downloading nRF Connect SDK ${NRF_CONNECT_SDK_VERSION} and friends to ${NCS_BASE_LOCATION}. This can take a while...")

        file(MAKE_DIRECTORY ${NCS_BASE_LOCATION})

        # 1. init west workspace with the sdk-nrf manifest repository
        # (cloned here and initialized with 'west init -l' to avoid west moving folders around, which is fragile on Windows)
        if(NOT EXISTS ${NCS_BASE_LOCATION}/.west/config)

            if(NOT EXISTS ${NCS_BASE_LOCATION}/nrf/west.yml)
                execute_process(
                    COMMAND git clone --depth 1 --branch ${NRF_CONNECT_SDK_VERSION} https://github.com/nrfconnect/sdk-nrf nrf
                    WORKING_DIRECTORY ${NCS_BASE_LOCATION}
                    COMMAND_ECHO STDOUT
                    RESULT_VARIABLE GIT_RESULT
                )
                if(NOT GIT_RESULT EQUAL 0)
                    message(FATAL_ERROR "git clone of sdk-nrf ${NRF_CONNECT_SDK_VERSION} failed (${GIT_RESULT})")
                endif()
            endif()

            execute_process(
                COMMAND ${WEST_EXE} init -l nrf
                WORKING_DIRECTORY ${NCS_BASE_LOCATION}
                COMMAND_ECHO STDOUT
                RESULT_VARIABLE WEST_RESULT
            )
            if(NOT WEST_RESULT EQUAL 0)
                message(FATAL_ERROR "west init failed (${WEST_RESULT})")
            endif()
        endif()

        # 2. clone the project repositories (shallow, only the ones needed by the manifest)
        execute_process(
            COMMAND ${WEST_EXE} update --narrow -o=--depth=1
            WORKING_DIRECTORY ${NCS_BASE_LOCATION}
            COMMAND_ECHO STDOUT
            RESULT_VARIABLE WEST_RESULT
        )
        if(NOT WEST_RESULT EQUAL 0)
            message(FATAL_ERROR "west update failed (${WEST_RESULT})")
        endif()

        # 3. export Zephyr CMake package
        execute_process(
            COMMAND ${WEST_EXE} zephyr-export
            WORKING_DIRECTORY ${NCS_BASE_LOCATION}
            COMMAND_ECHO STDOUT
        )

        # 4. install Python packages required by the Zephyr/NCS build system
        nf_nordic_install_python_requirements(${NCS_BASE_LOCATION})

        set(NCS_SOURCES_DOWNLOADED TRUE CACHE INTERNAL "Flag to indicate that NCS sources have been downloaded" FORCE)

    endif()

endif()

######################################
# handle Zephyr SDK (toolchain) installation

if(NOT ZEPHYR_SDK_VERSION OR "${ZEPHYR_SDK_VERSION}" STREQUAL "")
    set(ZEPHYR_SDK_VERSION ${ZEPHYR_SDK_DEFAULT_VERSION})
endif()

nf_nordic_setup_zephyr_sdk()

######################################
# environment for the Zephyr build system

# need to set this so that Zephyr is configured for cross compilation using it's own GCC
set(ZEPHYR_TOOLCHAIN_VARIANT "zephyr" CACHE STRING "Zephyr toolchain variant")
set(ENV{ZEPHYR_TOOLCHAIN_VARIANT} "zephyr")

# need to set this to let Zephyr build system know the location we're using
set(ENV{ZEPHYR_BASE} ${NCS_BASE_LOCATION}/zephyr)

# need to set this as environment variable to be available for Zephyr modules
set(ENV{PLATFORM_NORDIC_ROOT} ${CMAKE_CURRENT_SOURCE_DIR})
