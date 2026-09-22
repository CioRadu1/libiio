# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# Maxim (MSDK) platform expansion. A board file under cmake/boards/ describes
# its part as data:
#
#   MAXIM_PART            MAX32690
#   MAXIM_DIE             me18   -- MSDK die suffix, substituted for @DIE@
#   MAXIM_PERIPH_MODULES  one "<Source subdir> <file> <file> ..." per module
#   MAXIM_TARGET_REV      optional, defaults below
#   BOARD_DEFS            part-specific defines (memory map, ADC wiring)
#
# and includes this file last. Everything the top-level CMakeLists.txt consumes
# is derived here: BOARD_STARTUP_SRCS, BOARD_PERIPH_SRCS, BOARD_PLATFORM_SRCS,
# BOARD_INCLUDES, BOARD_LINKER_SCRIPT, BOARD_PLATFORM_NAME, and the generic
# half of BOARD_DEFS.

foreach(var MAXIM_PART MAXIM_DIE MAXIM_PERIPH_MODULES)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "cmake/boards/${BOARD}.cmake does not set ${var}.")
  endif()
endforeach()

if(NOT DEFINED MAXIM_TARGET_REV)
  set(MAXIM_TARGET_REV 0x4131)
endif()

string(TOLOWER ${MAXIM_PART} MAXIM_PART_LC)
string(REGEX REPLACE "^MAX" "" MAXIM_PART_NUM ${MAXIM_PART})

# ---------- MaximSDK location ----------
if(NOT DEFINED MSDK_DIR)
  if(DEFINED ENV{MAXIM_PATH})
    set(MSDK_DIR $ENV{MAXIM_PATH})
  else()
    set(MSDK_DIR $ENV{HOME}/MaximSDK)
  endif()
endif()

if(NOT EXISTS ${MSDK_DIR}/Libraries/PeriphDrivers)
  message(FATAL_ERROR
    "MaximSDK not found at ${MSDK_DIR}. Export MAXIM_PATH, or pass "
    "-DMSDK_DIR=/path/to/MaximSDK.")
endif()

set(MSDK_PERIPH ${MSDK_DIR}/Libraries/PeriphDrivers)
set(MSDK_CMSIS  ${MSDK_DIR}/Libraries/CMSIS)
set(MSDK_DEVICE ${MSDK_CMSIS}/Device/Maxim/${MAXIM_PART})

if(NOT EXISTS ${MSDK_DEVICE}/Include)
  message(FATAL_ERROR
    "MaximSDK at ${MSDK_DIR} has no support for ${MAXIM_PART} "
    "(looked for ${MSDK_DEVICE}/Include).")
endif()

# The CMSIS core headers sit under a version directory that moves between MSDK
# releases, so probe instead of hardcoding one.
set(MSDK_CMSIS_CORE ${MSDK_CMSIS}/5.9.0/Core/Include)
if(NOT EXISTS ${MSDK_CMSIS_CORE}/cmsis_gcc.h)
  set(MSDK_CMSIS_CORE "")
  file(GLOB _cmsis_dirs ${MSDK_CMSIS}/[0-9]*/Core/Include)
  list(SORT _cmsis_dirs)
  list(REVERSE _cmsis_dirs)
  foreach(_dir ${_cmsis_dirs})
    if(EXISTS ${_dir}/cmsis_gcc.h)
      set(MSDK_CMSIS_CORE ${_dir})
      break()
    endif()
  endforeach()
endif()

if(NOT MSDK_CMSIS_CORE)
  message(FATAL_ERROR
    "CMSIS core headers not found under ${MSDK_CMSIS}. Looked for "
    "<version>/Core/Include/cmsis_gcc.h.")
endif()

# ---------- no-OS platform layer ----------
set(PLATFORM_DIR     ${NOOS_DIR}/drivers/platform/maxim/${MAXIM_PART_LC})
set(MAXIM_COMMON_DIR ${NOOS_DIR}/drivers/platform/maxim/common)

if(NOT EXISTS ${PLATFORM_DIR}/maxim_uart.c)
  message(FATAL_ERROR
    "the no-OS tree at ${NOOS_DIR} has no maxim platform for ${MAXIM_PART} "
    "(looked in ${PLATFORM_DIR}).")
endif()

set(BOARD_PLATFORM_SRCS
  ${PLATFORM_DIR}/maxim_uart.c
  ${PLATFORM_DIR}/maxim_uart_stdio.c
  ${PLATFORM_DIR}/maxim_irq.c
  ${PLATFORM_DIR}/maxim_delay.c
  ${PLATFORM_DIR}/maxim_init.c
  ${PLATFORM_DIR}/maxim_gpio.c
  ${PLATFORM_DIR}/maxim_spi.c
  ${PLATFORM_DIR}/maxim_timer.c
  ${MAXIM_COMMON_DIR}/maxim_dma.c
)

# ---------- MSDK peripheral drivers ----------
# Each module line expands to ${MSDK_PERIPH}/Source/<subdir>/<file>.c, and the
# subdir doubles as an include path -- the MSDK keeps private headers next to
# the sources, which is why every module dir has to be on the include path.
set(BOARD_PERIPH_SRCS "")
set(_periph_includes "")

foreach(_module ${MAXIM_PERIPH_MODULES})
  string(REPLACE "@DIE@" "${MAXIM_DIE}" _module "${_module}")
  separate_arguments(_fields UNIX_COMMAND "${_module}")

  list(GET _fields 0 _dir)
  list(REMOVE_AT _fields 0)

  if(NOT _fields)
    message(FATAL_ERROR
      "MAXIM_PERIPH_MODULES entry '${_module}' names a directory but no "
      "source files.")
  endif()

  foreach(_file ${_fields})
    set(_src ${MSDK_PERIPH}/Source/${_dir}/${_file}.c)
    if(NOT EXISTS ${_src})
      message(FATAL_ERROR
        "${MAXIM_PART}: MSDK source ${_dir}/${_file}.c not found under "
        "${MSDK_PERIPH}/Source. Wrong MAXIM_DIE, or an MSDK version skew?")
    endif()
    list(APPEND BOARD_PERIPH_SRCS ${_src})
  endforeach()

  list(APPEND _periph_includes ${MSDK_PERIPH}/Source/${_dir})
endforeach()

list(REMOVE_DUPLICATES _periph_includes)

# ---------- Startup, linker script, includes, defines ----------
set(BOARD_STARTUP_SRCS
  ${MSDK_DEVICE}/Source/GCC/startup_${MAXIM_PART_LC}.S
  ${MSDK_DEVICE}/Source/system_${MAXIM_PART_LC}.c
  ${MSDK_DEVICE}/Source/heap.c
)

set(BOARD_LINKER_SCRIPT ${MSDK_DEVICE}/Source/GCC/${MAXIM_PART_LC}.ld)

set(BOARD_INCLUDES
  ${MSDK_DEVICE}/Include
  ${MSDK_CMSIS_CORE}
  ${MSDK_PERIPH}/Include/${MAXIM_PART}
  ${_periph_includes}
  ${PLATFORM_DIR}
  ${MAXIM_COMMON_DIR}
)

list(APPEND BOARD_DEFS
  TARGET=${MAXIM_PART}
  TARGET_NUM=${MAXIM_PART_NUM}
  TARGET_REV=${MAXIM_TARGET_REV}
  ${MAXIM_PART}
)

set(BOARD_PLATFORM_NAME maxim)

# ---------- Flashing ----------
# The platform owns the recipe; CMakeLists.txt only wraps BOARD_FLASH_COMMAND in
# a target. The MSDK ships OpenOCD and its own probe/target configs.
set(FLASH_INTERFACE cmsis-dap CACHE STRING
    "OpenOCD debug probe config name, no .cfg (see <openocd>/scripts/interface)")

# Maxim names its target configs after the part, with a _riscv variant for the
# coprocessor -- so the config depends on the *core*, not on BOARD alone.
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv")
  set(_ocd_target ${MAXIM_PART_LC}_riscv)
else()
  set(_ocd_target ${MAXIM_PART_LC})
endif()

find_program(OPENOCD openocd
  HINTS ${MSDK_DIR}/Tools/OpenOCD
  DOC "OpenOCD executable used by the flash target"
)

set(BOARD_FLASH_COMMAND "")
set(BOARD_FLASH_ERROR "")

if(NOT OPENOCD)
  set(BOARD_FLASH_ERROR
      "OpenOCD not found. Set MAXIM_PATH to your MaximSDK root, or pass -DOPENOCD=/path/to/openocd.")
else()
  get_filename_component(_ocd_bin ${OPENOCD} DIRECTORY)
  set(_ocd_scripts ${_ocd_bin}/scripts)

  if(NOT EXISTS ${_ocd_scripts}/target/${_ocd_target}.cfg)
    set(BOARD_FLASH_ERROR
        "no OpenOCD target config target/${_ocd_target}.cfg under ${_ocd_scripts}")
  elseif(NOT EXISTS ${_ocd_scripts}/interface/${FLASH_INTERFACE}.cfg)
    set(BOARD_FLASH_ERROR
        "no OpenOCD probe config interface/${FLASH_INTERFACE}.cfg under ${_ocd_scripts}. Pass -DFLASH_INTERFACE=<name>.")
  else()
    set(BOARD_FLASH_COMMAND
      ${OPENOCD} -s ${_ocd_scripts}
      -f interface/${FLASH_INTERFACE}.cfg
      -f target/${_ocd_target}.cfg
      -c "program @FLASH_IMAGE@ verify reset exit"
    )
  endif()
endif()

unset(_ocd_target)
unset(_ocd_bin)
unset(_ocd_scripts)

unset(_periph_includes)
unset(_cmsis_dirs)
unset(_module)
unset(_fields)
unset(_file)
unset(_dir)
unset(_src)
