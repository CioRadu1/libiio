# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# RISC-V RV32IMAC, ILP32 ABI. Several MAX326xx parts carry a RISC-V
# coprocessor alongside the Cortex-M core; this fragment goes live the moment a
# board file declares "set(BOARD_ARCH rv32imac)".

set(CMAKE_SYSTEM_PROCESSOR riscv)
set(TOOLCHAIN_PREFIX       riscv-none-elf-)
set(CPU_FLAGS "-march=rv32imac -mabi=ilp32")

list(APPEND TOOLCHAIN_HINTS
  $ENV{MAXIM_PATH}/Tools/xPack/riscv-none-elf-gcc/12.2.0-3.1/bin
  $ENV{HOME}/MaximSDK/Tools/xPack/riscv-none-elf-gcc/12.2.0-3.1/bin
)
