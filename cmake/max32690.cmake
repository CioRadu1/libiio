# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# CMake toolchain file for cross-compiling to MAX32690 (Cortex-M4F)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT DEFINED MSDK_DIR)
  set(MSDK_DIR $ENV{HOME}/MaximSDK)
endif()

set(TOOLCHAIN_BIN ${MSDK_DIR}/Tools/GNUTools/10.3/bin)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_BIN}/arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_BIN}/arm-none-eabi-gcc)
set(CMAKE_AR           ${TOOLCHAIN_BIN}/arm-none-eabi-ar)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_BIN}/arm-none-eabi-objcopy CACHE FILEPATH "objcopy")
set(CMAKE_SIZE         ${TOOLCHAIN_BIN}/arm-none-eabi-size CACHE FILEPATH "size")

set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=softfp -mfpu=fpv4-sp-d16")
set(CMAKE_C_FLAGS_INIT   "${CPU_FLAGS} -Os -ffunction-sections -fdata-sections -Wall")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -Wl,--gc-sections --specs=nosys.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
