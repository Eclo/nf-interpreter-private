#
# Copyright (c) .NET Foundation and Contributors
# See LICENSE file in the project root for full license information.
#

# Nordic platform: nanoCLR is built as a Zephyr module inside an nRF Connect SDK (NCS) application.
# The Zephyr build system takes care of the toolchain, compile/link options and the final images,
# so most of the macros here differ substantially from the other platforms.

######################################
# host tools

# find the host Python interpreter
macro(nf_nordic_find_python)

    set(Python3_FIND_STRATEGY LOCATION)
    set(Python3_FIND_REGISTRY NEVER)
    find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)

endmacro()

macro(nf_nordic_setup_west)

    set(WEST_EXE ${Python3_EXECUTABLE} -m west CACHE INTERNAL "West executable in CMake callable format")

    execute_process(
        COMMAND ${WEST_EXE} --version
        OUTPUT_QUIET
        ERROR_QUIET
        RESULT_VARIABLE WEST_CHECK_RESULT
    )

    if(NOT WEST_CHECK_RESULT EQUAL 0)

        if(NF_NORDIC_SKIP_PIP)
            message(FATAL_ERROR "\n\nZephyr 'west' tool not found for ${Python3_EXECUTABLE} and NF_NORDIC_SKIP_PIP is set. Please install it: pip install west\n\n")
        endif()

        message(STATUS "Installing Zephyr 'west' tool")

        execute_process(
            COMMAND ${Python3_EXECUTABLE} -m pip install --quiet west
            COMMAND_ECHO STDOUT
            RESULT_VARIABLE PIP_RESULT
        )

        if(NOT PIP_RESULT EQUAL 0)
            message(FATAL_ERROR "\n\nFailed to install Zephyr 'west' tool.\n\n")
        endif()

    endif()

endmacro()

function(nf_nordic_install_python_requirements ncs_base)

    if(NF_NORDIC_SKIP_PIP)
        message(STATUS "Skipping installation of Zephyr/NCS Python requirements (NF_NORDIC_SKIP_PIP)")
        return()
    endif()

    set(PIP_ARGS "")

    # only the requirements needed to build (no docs, tests, compliance, etc.)
    foreach(REQ_FILE
        zephyr/scripts/requirements-base.txt
        nrf/scripts/requirements-base.txt
        nrf/scripts/requirements-build.txt
    )
        if(EXISTS ${ncs_base}/${REQ_FILE})
            list(APPEND PIP_ARGS -r ${ncs_base}/${REQ_FILE})
        endif()
    endforeach()

    if(NOT PIP_ARGS)
        message(WARNING "Couldn't find any Zephyr/NCS Python requirements file in ${ncs_base}")
        return()
    endif()

    message(STATUS "Installing Zephyr/NCS Python requirements")

    execute_process(
        COMMAND ${Python3_EXECUTABLE} -m pip install --quiet ${PIP_ARGS}
        COMMAND_ECHO STDOUT
        RESULT_VARIABLE PIP_RESULT
    )

    if(NOT PIP_RESULT EQUAL 0)
        message(FATAL_ERROR "\n\nFailed to install Zephyr/NCS Python requirements.\n\n")
    endif()

endfunction()

macro(nf_nordic_setup_zephyr_sdk)

    # Zephyr SDK 1.x layout: the GNU toolchains live under the 'gnu' folder
    if(ZEPHYR_SDK_VERSION VERSION_LESS "1.0.0")
        message(FATAL_ERROR "\n\nZephyr SDK ${ZEPHYR_SDK_VERSION} is not supported, minimum is 1.0.0.\n\n")
    endif()

    # candidate locations, in order: cache variable, environment variable, our downloads folder
    set(ZEPHYR_SDK_CANDIDATES "")
    if(ZEPHYR_SDK_INSTALL_DIR AND NOT "${ZEPHYR_SDK_INSTALL_DIR}" STREQUAL "")
        list(APPEND ZEPHYR_SDK_CANDIDATES "${ZEPHYR_SDK_INSTALL_DIR}")
    endif()
    if(DEFINED ENV{ZEPHYR_SDK_INSTALL_DIR} AND NOT "$ENV{ZEPHYR_SDK_INSTALL_DIR}" STREQUAL "")
        list(APPEND ZEPHYR_SDK_CANDIDATES "$ENV{ZEPHYR_SDK_INSTALL_DIR}")
    endif()
    list(APPEND ZEPHYR_SDK_CANDIDATES "${NF_NORDIC_DEPS_DIR}/zephyr-sdk-${ZEPHYR_SDK_VERSION}")

    set(ZEPHYR_SDK_FOUND_DIR "")
    foreach(CANDIDATE ${ZEPHYR_SDK_CANDIDATES})
        if(EXISTS "${CANDIDATE}/cmake/Zephyr-sdkConfig.cmake" AND EXISTS "${CANDIDATE}/gnu/arm-zephyr-eabi/bin")
            set(ZEPHYR_SDK_FOUND_DIR "${CANDIDATE}")
            break()
        endif()
    endforeach()

    if("${ZEPHYR_SDK_FOUND_DIR}" STREQUAL "")

        # not found, download minimal bundle and the arm-zephyr-eabi toolchain
        set(ZEPHYR_SDK_FOUND_DIR "${NF_NORDIC_DEPS_DIR}/zephyr-sdk-${ZEPHYR_SDK_VERSION}")

        if(CMAKE_HOST_WIN32)
            set(ZEPHYR_SDK_HOST "windows-x86_64")
            set(ZEPHYR_SDK_ARCHIVE_EXT "7z")
        elseif(CMAKE_HOST_APPLE)
            if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
                set(ZEPHYR_SDK_HOST "macos-aarch64")
            else()
                set(ZEPHYR_SDK_HOST "macos-x86_64")
            endif()
            set(ZEPHYR_SDK_ARCHIVE_EXT "tar.xz")
        else()
            if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
                set(ZEPHYR_SDK_HOST "linux-aarch64")
            else()
                set(ZEPHYR_SDK_HOST "linux-x86_64")
            endif()
            set(ZEPHYR_SDK_ARCHIVE_EXT "tar.xz")
        endif()

        set(ZEPHYR_SDK_BASE_URL "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}")
        set(ZEPHYR_SDK_BUNDLE "zephyr-sdk-${ZEPHYR_SDK_VERSION}_${ZEPHYR_SDK_HOST}_minimal.${ZEPHYR_SDK_ARCHIVE_EXT}")
        set(ZEPHYR_SDK_TOOLCHAIN "toolchain_gnu_${ZEPHYR_SDK_HOST}_arm-zephyr-eabi.${ZEPHYR_SDK_ARCHIVE_EXT}")

        message(STATUS "Zephyr SDK v${ZEPHYR_SDK_VERSION} not found. Downloading it to ${NF_NORDIC_DEPS_DIR}. This can take a while...")

        file(MAKE_DIRECTORY ${NF_NORDIC_DEPS_DIR})

        # 1. minimal bundle (no toolchains)
        file(DOWNLOAD
            ${ZEPHYR_SDK_BASE_URL}/${ZEPHYR_SDK_BUNDLE}
            ${NF_NORDIC_DEPS_DIR}/${ZEPHYR_SDK_BUNDLE}
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
        )
        list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
        if(NOT DOWNLOAD_RESULT EQUAL 0)
            file(REMOVE ${NF_NORDIC_DEPS_DIR}/${ZEPHYR_SDK_BUNDLE})
            message(FATAL_ERROR "Failed to download ${ZEPHYR_SDK_BASE_URL}/${ZEPHYR_SDK_BUNDLE}: ${DOWNLOAD_STATUS}")
        endif()

        file(ARCHIVE_EXTRACT
            INPUT ${NF_NORDIC_DEPS_DIR}/${ZEPHYR_SDK_BUNDLE}
            DESTINATION ${NF_NORDIC_DEPS_DIR}
        )
        file(REMOVE ${NF_NORDIC_DEPS_DIR}/${ZEPHYR_SDK_BUNDLE})

        if(NOT EXISTS "${ZEPHYR_SDK_FOUND_DIR}/cmake/Zephyr-sdkConfig.cmake")
            message(FATAL_ERROR "Zephyr SDK bundle didn't extract to the expected location: ${ZEPHYR_SDK_FOUND_DIR}")
        endif()

        # 2. arm-zephyr-eabi toolchain
        file(DOWNLOAD
            ${ZEPHYR_SDK_BASE_URL}/${ZEPHYR_SDK_TOOLCHAIN}
            ${NF_NORDIC_DEPS_DIR}/${ZEPHYR_SDK_TOOLCHAIN}
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
        )
        list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
        if(NOT DOWNLOAD_RESULT EQUAL 0)
            file(REMOVE ${NF_NORDIC_DEPS_DIR}/${ZEPHYR_SDK_TOOLCHAIN})
            message(FATAL_ERROR "Failed to download ${ZEPHYR_SDK_BASE_URL}/${ZEPHYR_SDK_TOOLCHAIN}: ${DOWNLOAD_STATUS}")
        endif()

        file(ARCHIVE_EXTRACT
            INPUT ${NF_NORDIC_DEPS_DIR}/${ZEPHYR_SDK_TOOLCHAIN}
            DESTINATION ${ZEPHYR_SDK_FOUND_DIR}/gnu
        )
        file(REMOVE ${NF_NORDIC_DEPS_DIR}/${ZEPHYR_SDK_TOOLCHAIN})

    endif()

    message(STATUS "Zephyr SDK: ${ZEPHYR_SDK_FOUND_DIR}")

    set(ZEPHYR_SDK_INSTALL_DIR "${ZEPHYR_SDK_FOUND_DIR}" CACHE PATH "Zephyr SDK location" FORCE)
    set(ENV{ZEPHYR_SDK_INSTALL_DIR} "${ZEPHYR_SDK_FOUND_DIR}")

endmacro()

######################################
# platform macros (nanoFramework build system contract)

# Add platform packages specific to Zephyr
# To be called from target CMakeList.txt
# optional TARGET argument with target name
macro(nf_add_platform_packages)

    # parse arguments
    cmake_parse_arguments(NFAPP "" "TARGET" "" ${ARGN})

    # packages common to all targets
    nf_add_common_packages()

    # packages specific for nanoCLR
    if("${NFAPP_TARGET}" STREQUAL "${NANOCLR_PROJECT_NAME}")

        if(USE_NETWORKING_OPTION)
            find_package(NF_Network REQUIRED QUIET)
        endif()

    endif()

endmacro()

macro(nf_add_platform_dependencies target)

    message(FATAL_ERROR "nf_add_platform_dependencies not used on this platform.")

endmacro()

macro(nf_add_platform_include_directories target)

    target_include_directories(${target}.elf PUBLIC

        ${TARGET_NORDIC_COMMON_INCLUDE_DIRS}
    )

    if(${target} STREQUAL ${NANOCLR_PROJECT_NAME})

        target_include_directories(${target}.elf PUBLIC

            ${NANOCLR_PROJECT_INCLUDE_DIRS}
            ${TARGET_NORDIC_NANOCLR_INCLUDE_DIRS}
            ${CMAKE_CURRENT_BINARY_DIR}

        )

    endif()
endmacro()

macro(nf_add_platform_sources target)

    configure_file(${TARGET_BASE_LOCATION}/target_common.h.in
                   ${CMAKE_BINARY_DIR}/targets/${RTOS}/${TARGET_BOARD}/target_common.h @ONLY)

    if(${target} STREQUAL ${NANOCLR_PROJECT_NAME})

        configure_file(${TARGET_BASE_LOCATION}/nanoCLR/target_board.h.in
                       ${CMAKE_BINARY_DIR}/targets/${RTOS}/${TARGET_BOARD}/nanoCLR/target_board.h @ONLY)

    endif()

endmacro()

# macro to setup the build for a target
# This macro is vastly different from other NF targets as the Zephyr
# build system takes care of all the linking and other complexities.
# To be called from the board CMakeLists.txt, which is included from the root CMakeLists.txt scope
# (Zephyr has to be the top-level project) and which must NOT have called project() before.
macro(nf_setup_target_build)

    if(NOT NORDIC_ZEPHYR_BOARD OR "${NORDIC_ZEPHYR_BOARD}" STREQUAL "")
        message(FATAL_ERROR "\n\nCONFIG_NORDIC_ZEPHYR_BOARD is not set in the target defconfig.\n\n")
    endif()
    set(BOARD ${NORDIC_ZEPHYR_BOARD})

    if(CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
        list(APPEND EXTRA_CONF_FILE ${CMAKE_SOURCE_DIR}/targets/Nordic/_common/overlay-debugging.conf)
    endif()

    if(USE_NETWORKING_OPTION AND EXISTS ${TARGET_BASE_LOCATION}/overlay-net.conf)
        list(APPEND EXTRA_CONF_FILE ${TARGET_BASE_LOCATION}/overlay-net.conf)
    endif()

    add_subdirectory(${TARGET_BASE_LOCATION}/common ${CMAKE_BINARY_DIR}/targets/${RTOS}/${TARGET_BOARD}/common)
    add_subdirectory(${TARGET_BASE_LOCATION}/nanoCLR ${CMAKE_BINARY_DIR}/targets/${RTOS}/${TARGET_BOARD}/nanoCLR)

    list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_SOURCE_DIR}/targets/Nordic/_nanoCLR_module)

    nf_zephyr_compose_nano_version()

    nf_nordic_remove_nf_config_include()

    # optimization level is controlled by Zephyr Kconfig (CONFIG_*_OPTIMIZATIONS), not by CMAKE_BUILD_TYPE
    set(NO_BUILD_TYPE_WARNING ON)

    nf_nordic_remove_config_cache_entries()

    set(APPLICATION_SOURCE_DIR ${TARGET_BASE_LOCATION} CACHE PATH "Zephyr application source directory")
    set(APPLICATION_BINARY_DIR ${CMAKE_BINARY_DIR} CACHE PATH "Zephyr application binary directory")

    find_package(Zephyr REQUIRED HINTS ${NCS_BASE_LOCATION}/zephyr)

    if(NOT Zephyr_FOUND)
        message(FATAL_ERROR "\n\nCouldn't find Zephyr at ${NCS_BASE_LOCATION}/zephyr!\n\n")
    endif()

    # The Zephyr build system requires that the CMake project is defined here
    project(nanoFramework VERSION ${BUILD_VERSION} LANGUAGES C CXX ASM)

    zephyr_compile_definitions(PLATFORM_ARM)

    # the Zephyr application itself is just the entry point that starts nanoCLR
    target_sources(app PRIVATE ${TARGET_BASE_LOCATION}/nanoCLR/main.c)

    nf_nordic_check_kconfig_overlap()

    nf_clear_output_files_nanoclr()

endmacro()

# remove CONFIG_* variables from the CMake cache
macro(nf_nordic_remove_config_cache_entries)

    get_cmake_property(NF_CACHE_VARIABLES CACHE_VARIABLES)
    list(FILTER NF_CACHE_VARIABLES INCLUDE REGEX "^(CLI_)?CONFIG_")

    foreach(NF_CACHE_VARIABLE ${NF_CACHE_VARIABLES})
        unset(${NF_CACHE_VARIABLE} CACHE)
    endforeach()

endmacro()

macro(nf_nordic_remove_nf_config_include)

    get_directory_property(NF_DIR_COMPILE_OPTIONS COMPILE_OPTIONS)

    set(NF_FILTERED_COMPILE_OPTIONS "")
    set(NF_SKIP_NEXT FALSE)

    foreach(OPTION ${NF_DIR_COMPILE_OPTIONS})
        if(NF_SKIP_NEXT)
            set(NF_SKIP_NEXT FALSE)
        elseif("${OPTION}" STREQUAL "-include")
            set(NF_SKIP_NEXT TRUE)
        else()
            list(APPEND NF_FILTERED_COMPILE_OPTIONS "${OPTION}")
        endif()
    endforeach()

    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${NF_FILTERED_COMPILE_OPTIONS}")

endmacro()

function(nf_nordic_check_kconfig_overlap)

    set(NF_CONFIG_HEADER ${CMAKE_BINARY_DIR}/nf_config.h)
    set(ZEPHYR_AUTOCONF_HEADER ${PROJECT_BINARY_DIR}/zephyr/include/generated/zephyr/autoconf.h)
    if(NOT EXISTS ${ZEPHYR_AUTOCONF_HEADER})
        set(ZEPHYR_AUTOCONF_HEADER ${PROJECT_BINARY_DIR}/zephyr/include/generated/autoconf.h)
    endif()

    if(NOT EXISTS ${NF_CONFIG_HEADER} OR NOT EXISTS ${ZEPHYR_AUTOCONF_HEADER})
        return()
    endif()

    file(STRINGS ${NF_CONFIG_HEADER} NF_CONFIG_LINES REGEX "^#define CONFIG_")
    file(STRINGS ${ZEPHYR_AUTOCONF_HEADER} ZEPHYR_CONFIG_LINES REGEX "^#define CONFIG_")

    set(ZEPHYR_SYMBOLS "")
    foreach(LINE ${ZEPHYR_CONFIG_LINES})
        string(REGEX REPLACE "^#define (CONFIG_[A-Za-z0-9_]+).*" "\\1" SYMBOL "${LINE}")
        list(APPEND ZEPHYR_SYMBOLS ${SYMBOL})
    endforeach()

    set(OVERLAP "")
    foreach(LINE ${NF_CONFIG_LINES})
        string(REGEX REPLACE "^#define (CONFIG_[A-Za-z0-9_]+).*" "\\1" SYMBOL "${LINE}")
        if(SYMBOL IN_LIST ZEPHYR_SYMBOLS)
            list(APPEND OVERLAP ${SYMBOL})
        endif()
    endforeach()

    if(OVERLAP)
        message(WARNING "\n\nKconfig symbols defined by both nanoFramework and Zephyr: ${OVERLAP}\nThese will have the nanoFramework value in nanoCLR sources.\n\n")
    endif()

endfunction()

macro(nf_copy_zephyr_build_artifacts)

    # need to adjust these depending on the artifacts produced by Zephyr build
    # non-secure targets get the TF-M image merged in, others stick with zephyr.hex
    # (checking the board id from Kconfig because Zephyr strips the qualifiers from BOARD)
    if(NORDIC_ZEPHYR_BOARD MATCHES "/ns$")
        set(HEX_NAME "tfm_merged")
    else()
        set(HEX_NAME "zephyr")
    endif()

    add_custom_command(
        OUTPUT
            ${CMAKE_BINARY_DIR}/nanoCLR.hex

        COMMAND ${CMAKE_COMMAND} -E copy
            ${PROJECT_BINARY_DIR}/zephyr/${HEX_NAME}.hex
            ${CMAKE_BINARY_DIR}/nanoCLR.hex

        COMMAND ${CMAKE_COMMAND} -E copy
            ${PROJECT_BINARY_DIR}/zephyr/zephyr.elf
            ${CMAKE_BINARY_DIR}/nanoCLR.elf

        DEPENDS
            ${PROJECT_BINARY_DIR}/zephyr/${HEX_NAME}.hex

        COMMENT "Copy build artifacts"
    )

    add_custom_target(
        copy_build_artifacts ALL

        DEPENDS
            ${CMAKE_BINARY_DIR}/nanoCLR.hex
    )

    add_dependencies(
        copy_build_artifacts
        app)

endmacro()

macro(nf_clear_output_files_nanoclr)
    list(APPEND CLR_BUILD_FILES_TO_REMOVE ${CMAKE_BINARY_DIR}/nanoCLR.bin)
    list(APPEND CLR_BUILD_FILES_TO_REMOVE ${CMAKE_BINARY_DIR}/nanoCLR.hex)
    list(APPEND CLR_BUILD_FILES_TO_REMOVE ${CMAKE_BINARY_DIR}/nanoCLR.elf)

    add_custom_command(
        TARGET app PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f ${CLR_BUILD_FILES_TO_REMOVE}
        COMMAND_EXPAND_LISTS
        COMMENT "Removing nanoCLR artifacts from build folder"
    )

endmacro()

macro(nf_zephyr_compose_nano_version)

    string(REPLACE "." ";" BV1 ${BUILD_VERSION})
    set(BVLIST ${BV1})
    list(GET BVLIST 0 nanoFramework_VERSION_MAJOR)
    list(GET BVLIST 1 nanoFramework_VERSION_MINOR)
    list(GET BVLIST 2 nanoFramework_VERSION_PATCH)
    list(GET BVLIST 3 nanoFramework_VERSION_TWEAK)

endmacro()
