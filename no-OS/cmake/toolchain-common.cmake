# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# Boilerplate shared by every bare-metal architecture. cmake/bare-metal.cmake
# includes this after an cmake/arch/<core>.cmake fragment has described the
# core. Inputs, all mandatory, none defaulted here:
#
#   CMAKE_SYSTEM_PROCESSOR  arm, riscv, ...
#   TOOLCHAIN_PREFIX        arm-none-eabi-, riscv-none-elf-, ...
#   CPU_FLAGS               -mcpu=/-march= and the ABI flags that go with it
#
# Optional:
#
#   TOOLCHAIN_HINTS         directories searched for the compiler before PATH

set(CMAKE_SYSTEM_NAME Generic)

# A missing core description must stop the build. Defaulting it here is what
# used to let a board silently compile for the wrong core.
foreach(var CMAKE_SYSTEM_PROCESSOR TOOLCHAIN_PREFIX CPU_FLAGS)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR
      "cmake/arch/${BOARD_ARCH}.cmake does not set ${var}. An arch fragment "
      "must describe its core completely; there is no default.")
  endif()
endforeach()

find_program(TOOLCHAIN_GCC
  NAMES ${TOOLCHAIN_PREFIX}gcc
  HINTS ${TOOLCHAIN_HINTS}
  DOC "Bare-metal C compiler"
)

if(NOT TOOLCHAIN_GCC)
  message(FATAL_ERROR
    "${TOOLCHAIN_PREFIX}gcc not found, and BOARD_ARCH=${BOARD_ARCH} needs it. "
    "Put it on PATH, or pass -DTOOLCHAIN_HINTS=/path/to/toolchain/bin.")
endif()

get_filename_component(TOOLCHAIN_BIN "${TOOLCHAIN_GCC}" DIRECTORY)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_GCC})
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_GCC})
set(CMAKE_AR           ${TOOLCHAIN_BIN}/${TOOLCHAIN_PREFIX}ar)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_BIN}/${TOOLCHAIN_PREFIX}objcopy CACHE FILEPATH "objcopy")
set(CMAKE_SIZE         ${TOOLCHAIN_BIN}/${TOOLCHAIN_PREFIX}size    CACHE FILEPATH "size")

set(CMAKE_C_FLAGS_INIT          "${CPU_FLAGS} -ffunction-sections -fdata-sections -Wall")
set(CMAKE_ASM_FLAGS_INIT        "${CPU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -Wl,--gc-sections --specs=nosys.specs")

# Let CMAKE_BUILD_TYPE control optimization:
#   Debug   -> -Og -g3  (debuggable, variables visible, DWARF macro info)
#   Release -> -Os      (size-optimized)
set(CMAKE_C_FLAGS_DEBUG   "-Og -g3" CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE "-Os"     CACHE STRING "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
