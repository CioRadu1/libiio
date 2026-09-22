# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# The bare-metal toolchain file. There is one, not one per architecture: BOARD
# is the only knob, the board file names its core, and the core fragment names
# the compiler and its flags.
#
#   cmake --preset uart                     # BOARD default, see below
#   cmake --preset uart -DBOARD=max32655
#
# CMake reads toolchain files before CMakeLists.txt, which is why BOARD's
# default lives here. Board files bail out early when BOARD_ARCH_QUERY is set,
# so the core can be read this early -- before NOOS_DIR or the vendor SDK have
# been located.

set(BOARD max32690 CACHE STRING "Target board (see cmake/boards/)")

set(_board_file ${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}.cmake)

if(NOT EXISTS ${_board_file})
  file(GLOB _board_files ${CMAKE_CURRENT_LIST_DIR}/boards/*.cmake)
  set(_known "")
  foreach(_file ${_board_files})
    get_filename_component(_name ${_file} NAME_WE)
    list(APPEND _known ${_name})
  endforeach()
  string(REPLACE ";" ", " _known "${_known}")
  message(FATAL_ERROR
    "unknown BOARD '${BOARD}'. Known boards: ${_known}. Add "
    "cmake/boards/${BOARD}.cmake to support a new one.")
endif()

# Phase 1: ask the board which core it has, and nothing else.
set(BOARD_ARCH_QUERY TRUE)
include(${_board_file})
unset(BOARD_ARCH_QUERY)

if(NOT DEFINED BOARD_ARCH)
  message(FATAL_ERROR
    "cmake/boards/${BOARD}.cmake must set BOARD_ARCH before anything else, "
    "e.g. set(BOARD_ARCH cortex-m4f).")
endif()

set(_arch_file ${CMAKE_CURRENT_LIST_DIR}/arch/${BOARD_ARCH}.cmake)

if(NOT EXISTS ${_arch_file})
  file(GLOB _arch_files ${CMAKE_CURRENT_LIST_DIR}/arch/*.cmake)
  set(_cores "")
  foreach(_file ${_arch_files})
    get_filename_component(_name ${_file} NAME_WE)
    list(APPEND _cores ${_name})
  endforeach()
  string(REPLACE ";" ", " _cores "${_cores}")
  message(FATAL_ERROR
    "board ${BOARD} asks for core '${BOARD_ARCH}', but cmake/arch/ has only: "
    "${_cores}.")
endif()

# A build dir is reused across boards (cmake --preset uart -DBOARD=max32655),
# but CMake caches the compiler on the first configure. Switching to a board
# with a *different* core in the same dir would keep the old compiler, so say so
# instead of building something that cannot run.
if(BOARD_ARCH_CONFIGURED AND NOT BOARD_ARCH_CONFIGURED STREQUAL BOARD_ARCH)
  message(FATAL_ERROR
    "this build dir was configured for core '${BOARD_ARCH_CONFIGURED}', and "
    "BOARD=${BOARD} needs '${BOARD_ARCH}'. The compiler is cached per build "
    "dir: delete it and configure again.")
endif()
set(BOARD_ARCH_CONFIGURED "${BOARD_ARCH}" CACHE INTERNAL
    "core this build dir was configured for")

# Phase 2: core flags, then the boilerplate that is the same for every core.
include(${_arch_file})
include(${CMAKE_CURRENT_LIST_DIR}/toolchain-common.cmake)

# try_compile() configures a throwaway project with a fresh cache. Without
# this, the compiler probe would fall back to BOARD's default above and could
# validate the wrong core.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  BOARD TOOLCHAIN_HINTS TOOLCHAIN_GCC MSDK_DIR NOOS_DIR)

unset(_board_file)
unset(_arch_file)
unset(_board_files)
unset(_arch_files)
unset(_known)
unset(_cores)
unset(_file)
unset(_name)
