# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# libiio iiod firmware on no-OS, built through no-OS's own CMake + Kconfig:
# the toolchain, board defconfigs, platform drivers, lwIP and flash tooling
# all come from an upstream no-OS checkout, found by cmake/no_os_locate.cmake.
# This file only adds the libiio sources and links them against no-os, the
# same way a project under no-OS/projects/ does.
#
# This file builds the iiod sample firmware (samples/iiod). A no-OS project
# takes libiio through module.cmake instead, the way no-OS takes lwIP; both
# build libiio-iiod with libiio_noos_add_server() (cmake/).
#
#   cmake --workflow --preset usb                      # default board
#   cmake --preset usb --fresh -DBOARD=max32690fthr    # any no-OS board
#   cmake --build --preset flash-usb
#
# BOARD is any board no-OS lists in board_configs/<platform>/; the platform,
# part and defconfig come from there (cmake/no_os_board.cmake).
#
# Without BOARD it is a host build: only the PC-side API tests.

# no-OS's CMake needs 3.27.
cmake_minimum_required(VERSION 3.27)

set(LIBIIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
get_filename_component(LIBIIO_DIR "${LIBIIO_DIR}" REALPATH)

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  message(FATAL_ERROR "a no-OS project takes libiio through ${CMAKE_CURRENT_SOURCE_DIR}/"
                      "module.cmake, not add_subdirectory().")
endif()

###############################################################################
# Host build: the API tests that talk to a board
###############################################################################
if(NOT BOARD)
  project(libiio-noos-tests C)

  option(WITH_TESTS "Build the no-OS libiio API tests" ON)
  if(WITH_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()

  return()
endif()

set(IIOD_TRANSPORT uart CACHE STRING "IIOD transport: uart, usb or network")
set_property(CACHE IIOD_TRANSPORT PROPERTY STRINGS uart usb network)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/iiod/${IIOD_TRANSPORT}.c)
  message(FATAL_ERROR "unknown IIOD_TRANSPORT '${IIOD_TRANSPORT}' (uart, usb or network).")
endif()

###############################################################################
# Sample firmware: find no-OS, take the board, add no-OS
###############################################################################
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/no_os_locate.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/no_os_board.cmake)

set(IIOD_PLATFORM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/${PLATFORM})
if(NOT EXISTS ${IIOD_PLATFORM_DIR}/parameters.h)
  message(FATAL_ERROR
    "${BOARD} is a PLATFORM=${PLATFORM} board, which has no sample settings "
    "yet: add platform/${PLATFORM}/parameters.h (the sample's UART and network "
    "values, see platform/maxim/).")
endif()

# The toolchain is read by project(), before no-OS is added below, so point
# at it here; no-OS only picks one itself when it is the top-level project.
if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${NO_OS_PATH}/drivers/platform/${PLATFORM}/toolchain.cmake)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type")
endif()

project(libiio-noos C CXX ASM)

# The project defconfig is the transport fragment plus the platform's share of
# it (e.g. the Maxim USB stack), merged by no-OS after the board defconfig.
# no-OS reads it relative to this directory, and derives the variant from the
# file name: boards/<transport>/<BOARD>.conf, when present, overrides both.
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/configs/${IIOD_TRANSPORT}.conf)
  message(FATAL_ERROR "no configs/${IIOD_TRANSPORT}.conf for the sample.")
endif()
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/configs/${IIOD_TRANSPORT}.conf _defconfig)
if(EXISTS ${IIOD_PLATFORM_DIR}/configs/${IIOD_TRANSPORT}.conf)
  file(READ ${IIOD_PLATFORM_DIR}/configs/${IIOD_TRANSPORT}.conf _platform_defconfig)
  string(APPEND _defconfig "${_platform_defconfig}")
endif()
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/iiod/${IIOD_TRANSPORT}.conf
     CONTENT "${_defconfig}" @ONLY)
file(RELATIVE_PATH PROJECT_DEFCONFIG ${CMAKE_SOURCE_DIR}
     ${CMAKE_BINARY_DIR}/iiod/${IIOD_TRANSPORT}.conf)

add_subdirectory(${NO_OS_PATH} no-os)

# The same module path no-OS's own CMakeLists.txt uses, plus the platform's
# cmake/<platform>/ dir; config_platform_sdk() below includes that dir's
# <platform>_platform_sdk.cmake itself.
list(APPEND CMAKE_MODULE_PATH
  ${NO_OS_DIR}/cmake
  ${NO_OS_DIR}/cmake/stm32
  ${NO_OS_DIR}/cmake/libraries
)
if(IS_DIRECTORY ${NO_OS_DIR}/cmake/${PLATFORM})
  list(APPEND CMAKE_MODULE_PATH ${NO_OS_DIR}/cmake/${PLATFORM})
endif()
list(REMOVE_DUPLICATES CMAKE_MODULE_PATH)
include(project_utils)

###############################################################################
# libiio-iiod: the same library a no-OS project gets from module.cmake
###############################################################################
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/libiio_noos_server.cmake)

option(WITH_IIOD_V0_COMPAT "Serve libiio v0.x clients (ASCII protocol)" ON)
set(_v0_compat "")
if(WITH_IIOD_V0_COMPAT)
  set(_v0_compat V0_COMPAT)
endif()
libiio_noos_add_server(libiio-iiod TRANSPORT ${IIOD_TRANSPORT} ${_v0_compat})

###############################################################################
# The sample itself: a project using libiio-iiod, as any no-OS project would
###############################################################################
set(IIOD_SAMPLE_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/samples/iiod/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/samples/iiod/common_data.c
)
set(IIOD_SAMPLE_INCLUDES ${IIOD_PLATFORM_DIR})
set(IIOD_SAMPLE_DEFS ${IIOD_PLATFORM_DEFS})
string(TOUPPER ${BOARD} _board_upper)
list(APPEND IIOD_SAMPLE_DEFS IIOD_USB_PRODUCT="${_board_upper} IIO")

# Off by default: no-OS's iio_demo project instead of the board's ADC, its
# unmodified adc_demo and dac_demo drivers served through libiio.
option(IIOD_IIO_DEMO "Serve no-OS's iio_demo (adc_demo + dac_demo)" OFF)
if(IIOD_IIO_DEMO)
  list(APPEND IIOD_SAMPLE_SRCS
    ${NO_OS_PATH}/drivers/adc/adc_demo/adc_demo.c
    ${NO_OS_PATH}/drivers/adc/adc_demo/iio_adc_demo.c
    ${NO_OS_PATH}/drivers/dac/dac_demo/dac_demo.c
    ${NO_OS_PATH}/drivers/dac/dac_demo/iio_dac_demo.c
    ${NO_OS_PATH}/drivers/adc/adc_demo/iio_adc_demo_trig.c
    ${NO_OS_PATH}/drivers/dac/dac_demo/iio_dac_demo_trig.c
  )
  list(APPEND IIOD_SAMPLE_INCLUDES
    ${NO_OS_PATH}/drivers/adc/adc_demo
    ${NO_OS_PATH}/drivers/dac/dac_demo
  )
  list(APPEND IIOD_SAMPLE_DEFS IIOD_IIO_DEMO=1)
endif()

add_executable(libiio-noos ${IIOD_SAMPLE_SRCS})
target_include_directories(libiio-noos PRIVATE ${IIOD_SAMPLE_INCLUDES})
target_compile_definitions(libiio-noos PRIVATE ${IIOD_SAMPLE_DEFS})
target_link_libraries(libiio-noos PRIVATE libiio-iiod no-os)

config_platform_sdk(libiio-noos)
add_flash_target(libiio-noos)
post_build_config(libiio-noos)
