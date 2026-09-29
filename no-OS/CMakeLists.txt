# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# libiio iiod firmware on no-OS, built through no-OS's own CMake + Kconfig:
# the toolchain, board defconfigs, platform drivers, lwIP and flash tooling
# all come from an upstream no-OS checkout, found by cmake/no_os_locate.cmake.
# This file only adds the libiio sources and links them against no-os, the
# same way a project under no-OS/projects/ does.
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

###############################################################################
# Firmware build
###############################################################################
set(IIOD_TRANSPORT uart CACHE STRING "IIOD transport: uart, usb or network")
set_property(CACHE IIOD_TRANSPORT PROPERTY STRINGS uart usb network)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/configs/${IIOD_TRANSPORT}.conf)
  message(FATAL_ERROR "unknown IIOD_TRANSPORT '${IIOD_TRANSPORT}' (uart, usb or network).")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/no_os_locate.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/no_os_board.cmake)

set(IIOD_PLATFORM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/platform/${PLATFORM})
if(NOT EXISTS ${IIOD_PLATFORM_DIR}/parameters.h)
  message(FATAL_ERROR
    "${BOARD} is a PLATFORM=${PLATFORM} board, which has no iiod glue yet: add "
    "platform/${PLATFORM}/ with parameters.h and timers.h (UART, trigger timer "
    "and IRQ wiring, see platform/maxim/). no-OS itself supports the platform; "
    "the port does not.")
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

include(${IIOD_PLATFORM_DIR}/platform.cmake OPTIONAL)

# ---------- libiio core ----------
set(LIBIIO_SRCS
  ${LIBIIO_DIR}/context.c
  ${LIBIIO_DIR}/library.c
  ${LIBIIO_DIR}/backend.c
  ${LIBIIO_DIR}/device.c
  ${LIBIIO_DIR}/channel.c
  ${LIBIIO_DIR}/attr.c
  ${LIBIIO_DIR}/utilities.c
  ${LIBIIO_DIR}/lock-dummy.c
  ${LIBIIO_DIR}/scan.c
  ${LIBIIO_DIR}/buffer.c
  ${LIBIIO_DIR}/stream.c
  ${LIBIIO_DIR}/task.c
  ${LIBIIO_DIR}/mask.c
  ${LIBIIO_DIR}/sort.c
  ${LIBIIO_DIR}/block.c
  ${LIBIIO_DIR}/events.c
  ${LIBIIO_DIR}/tinyiiod/tinyiiod.c
  ${LIBIIO_DIR}/iiod/responder.c
  ${LIBIIO_DIR}/iiod-responder.c
  ${LIBIIO_DIR}/iiod/rw.c
)

# ---------- v0 (ASCII) protocol, so libiio v0.x clients can connect ----------
option(WITH_IIOD_V0_COMPAT "Serve libiio v0.x clients (ASCII protocol)" ON)
if(WITH_IIOD_V0_COMPAT)
  set(V0_COMPAT_ON 1)
  find_program(BISON_EXECUTABLE bison REQUIRED)
  find_program(FLEX_EXECUTABLE flex REQUIRED)

  # Generated into <build>/iiod so "../iiod/parser.h" (ascii-ops.c) and
  # "../iio-config.h" (parser.y) resolve without touching the iiod sources.
  set(V0_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/iiod)
  file(MAKE_DIRECTORY ${V0_GEN_DIR})
  file(WRITE ${V0_GEN_DIR}/lexer-prologue.l "%option always-interactive\n")
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/iio-config.h
                 ${CMAKE_CURRENT_BINARY_DIR}/iio-config.h COPYONLY)

  add_custom_command(
    OUTPUT ${V0_GEN_DIR}/parser.c ${V0_GEN_DIR}/parser.h
    COMMAND ${BISON_EXECUTABLE} -d -o ${V0_GEN_DIR}/parser.c
            ${LIBIIO_DIR}/iiod/parser.y
    DEPENDS ${LIBIIO_DIR}/iiod/parser.y
    COMMENT "bison: iiod/parser.y"
  )
  add_custom_command(
    OUTPUT ${V0_GEN_DIR}/lexer.c
    COMMAND ${FLEX_EXECUTABLE} -o ${V0_GEN_DIR}/lexer.c
            ${V0_GEN_DIR}/lexer-prologue.l ${LIBIIO_DIR}/iiod/lexer.l
    DEPENDS ${LIBIIO_DIR}/iiod/lexer.l ${V0_GEN_DIR}/parser.h
    COMMENT "flex: iiod/lexer.l"
  )

  set(V0_COMPAT_SRCS
    ${LIBIIO_DIR}/tinyiiod/ascii-ops.c
    ${V0_GEN_DIR}/parser.c
    ${V0_GEN_DIR}/lexer.c
  )
  set_source_files_properties(${V0_COMPAT_SRCS}
    PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare"
               INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include/compat)
  list(APPEND LIBIIO_SRCS ${V0_COMPAT_SRCS})
else()
  set(V0_COMPAT_ON 0)
  # Only the stub of what ascii-ops.c would provide.
  list(APPEND LIBIIO_SRCS ${LIBIIO_DIR}/tinyiiod/ops-stubs.c)
endif()

# ---------- ADC HAL: the platform's own, else the hardware-free demo ----------
if(NOT IIOD_PLATFORM_ADC)
  set(IIOD_PLATFORM_ADC adc_demo)
endif()
set(ADC ${IIOD_PLATFORM_ADC} CACHE STRING "ADC HAL under drivers/adc/")
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/drivers/adc/${ADC}/${ADC}.c)
  message(FATAL_ERROR "no ADC HAL drivers/adc/${ADC}/${ADC}.c")
endif()

add_executable(libiio-noos
  ${LIBIIO_SRCS}
  ${CMAKE_CURRENT_SOURCE_DIR}/backend.c
  ${CMAKE_CURRENT_SOURCE_DIR}/iiod/${IIOD_TRANSPORT}.c
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers/iio_adc.c
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers/iio_trigger_timer.c
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers/adc/${ADC}/${ADC}.c
  ${CMAKE_CURRENT_SOURCE_DIR}/samples/iiod/main.c
  ${IIOD_PLATFORM_SRCS}
)

target_include_directories(libiio-noos PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers
  ${CMAKE_CURRENT_SOURCE_DIR}/iiod
  ${IIOD_PLATFORM_DIR}
  ${LIBIIO_DIR}/include
  ${LIBIIO_DIR}
  ${LIBIIO_DIR}/iiod
  ${V0_GEN_DIR}
  ${IIOD_PLATFORM_INCLUDES}
)

target_compile_definitions(libiio-noos PRIVATE
  LIBIIO_STATIC=1
  NO_THREADS=1
  WITH_ZSTD=0
  WITH_LIBTINYIIOD=1
  WITH_EXTERNAL_BACKEND=1
  WITH_IIOD_USB_DMABUF=0
  WITH_IIOD_V0_COMPAT=${V0_COMPAT_ON}
  WITH_LOCAL_BACKEND=0
  WITH_NETWORK_BACKEND=0
  WITH_SERIAL_BACKEND=0
  WITH_USB_BACKEND=0
  WITH_XML_BACKEND=0
  WITH_NETWORK_BACKEND_DYNAMIC=0
  WITH_SERIAL_BACKEND_DYNAMIC=0
  WITH_USB_BACKEND_DYNAMIC=0
  HAVE_DNS_SD=0
  WITH_EMU_BACKEND=0
  WITH_EMU_BACKEND_DYNAMIC=0
  WITH_MODULES=0
  DEFAULT_LOG_LEVEL=0
  LIBIIO_VERSION_MAJOR=1
  LIBIIO_VERSION_MINOR=0
  LIBIIO_VERSION_PATCH=0
  LIBIIO_VERSION_GIT="v1.0"
  HAS_STRTOK_R=1
  LIBIIO_SCAN_BACKENDS=""
  DISABLE_SECURE_SOCKET=1
  ${IIOD_PLATFORM_DEFS}
)

if(IIOD_TRANSPORT STREQUAL "usb")
  string(TOUPPER ${BOARD} _board_upper)
  target_compile_definitions(libiio-noos PRIVATE
    NO_OS_USB_TRANSPORT
    NO_OS_USB_PRODUCT="${_board_upper} IIO"
  )
endif()

target_link_libraries(libiio-noos PRIVATE no-os)

config_platform_sdk(libiio-noos)
add_flash_target(libiio-noos)
post_build_config(libiio-noos)
