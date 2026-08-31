# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# CMake toolchain file for cross-compiling to a bare-metal RISC-V target.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

if(NOT DEFINED RISCV_TOOLCHAIN_BIN)
  set(RISCV_TOOLCHAIN_BIN $ENV{HOME}/MaximSDK/Tools/xPack/riscv-none-elf-gcc/12.2.0-3.1/bin)
endif()

set(RISCV_PREFIX riscv-none-elf-)
set(CMAKE_C_COMPILER   ${RISCV_TOOLCHAIN_BIN}/${RISCV_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${RISCV_TOOLCHAIN_BIN}/${RISCV_PREFIX}gcc)
set(CMAKE_AR           ${RISCV_TOOLCHAIN_BIN}/${RISCV_PREFIX}ar)
set(CMAKE_OBJCOPY      ${RISCV_TOOLCHAIN_BIN}/${RISCV_PREFIX}objcopy CACHE FILEPATH "objcopy")
set(CMAKE_SIZE         ${RISCV_TOOLCHAIN_BIN}/${RISCV_PREFIX}size CACHE FILEPATH "size")

if(NOT DEFINED CPU_FLAGS)
  set(CPU_FLAGS "-march=rv32imac -mabi=ilp32")
endif()
set(CMAKE_C_FLAGS_INIT   "${CPU_FLAGS} -ffunction-sections -fdata-sections -Wall")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -Wl,--gc-sections --specs=nosys.specs")

set(CMAKE_C_FLAGS_DEBUG   "-Og -g3" CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE "-Os"     CACHE STRING "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
