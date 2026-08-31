# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# CMake toolchain file for cross-compiling to a bare-metal ARM target.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# MSDK_DIR must be set (default: ~/MaximSDK)
if(NOT DEFINED MSDK_DIR)
  set(MSDK_DIR $ENV{HOME}/MaximSDK)
endif()

set(TOOLCHAIN_BIN ${MSDK_DIR}/Tools/GNUTools/10.3/bin)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_BIN}/arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_BIN}/arm-none-eabi-gcc)
set(CMAKE_AR           ${TOOLCHAIN_BIN}/arm-none-eabi-ar)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_BIN}/arm-none-eabi-objcopy CACHE FILEPATH "objcopy")
set(CMAKE_SIZE         ${TOOLCHAIN_BIN}/arm-none-eabi-size CACHE FILEPATH "size")

if(NOT DEFINED CPU_FLAGS)
  set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=softfp -mfpu=fpv4-sp-d16")
endif()
set(CMAKE_C_FLAGS_INIT   "${CPU_FLAGS} -ffunction-sections -fdata-sections -Wall")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -Wl,--gc-sections --specs=nosys.specs")

# Let CMAKE_BUILD_TYPE control optimization:
#   Debug   -> -Og -g3  (debuggable, variables visible, DWARF macro info)
#   Release -> -Os      (size-optimized)
set(CMAKE_C_FLAGS_DEBUG   "-Og -g3" CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE "-Os"     CACHE STRING "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
