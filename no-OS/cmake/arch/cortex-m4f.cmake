# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# ARM Cortex-M4 with single-precision FPU (ARMv7E-M).

set(CMAKE_SYSTEM_PROCESSOR arm)
set(TOOLCHAIN_PREFIX       arm-none-eabi-)
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=softfp -mfpu=fpv4-sp-d16")

# Vendor SDKs ship a validated arm-none-eabi-gcc; prefer it over whatever is on
# PATH, but fall back to PATH so a plain apt/brew toolchain works too.
list(APPEND TOOLCHAIN_HINTS
  $ENV{MAXIM_PATH}/Tools/GNUTools/10.3/bin
  $ENV{HOME}/MaximSDK/Tools/GNUTools/10.3/bin
)
