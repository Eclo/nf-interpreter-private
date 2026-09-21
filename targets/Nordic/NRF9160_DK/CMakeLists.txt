#
# Copyright (c) .NET Foundation and Contributors
# See LICENSE file in the project root for full license information.
#

# NOTE: this file is included from the root CMakeLists.txt (not added as a subdirectory)
# so that the Zephyr build system is the top-level CMake project

# invoke the Zephyr build (board comes from CONFIG_NORDIC_ZEPHYR_BOARD in the defconfig)
nf_setup_target_build()

nf_copy_zephyr_build_artifacts()
