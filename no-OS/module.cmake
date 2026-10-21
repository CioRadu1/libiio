# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# libiio as a no-OS library, the way no-OS takes lwIP: with CONFIG_LIBIIO,
# no-OS's cmake/libraries/libiio.cmake includes this file. It builds
# libiio-iiod (libiio's iiod, this port's backend and transport) and links it
# into the project, whose main() then calls noos_iiod_run() with its device
# table (include/noos_iiod.h, PORTING.md).
#
#   IIOD_TRANSPORT       uart (default), usb or network
#   WITH_IIOD_V0_COMPAT  also serve libiio v0.x clients (default ON)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/libiio_noos_server.cmake)

set(IIOD_TRANSPORT uart CACHE STRING "IIOD transport: uart, usb or network")
set_property(CACHE IIOD_TRANSPORT PROPERTY STRINGS uart usb network)
option(WITH_IIOD_V0_COMPAT "Serve libiio v0.x clients (ASCII protocol)" ON)

set(_libiio_v0_compat "")
if(WITH_IIOD_V0_COMPAT)
  set(_libiio_v0_compat V0_COMPAT)
endif()
libiio_noos_add_server(libiio-iiod TRANSPORT ${IIOD_TRANSPORT} ${_libiio_v0_compat})

if(DEFINED NO_OS_PROJECT_NAME AND TARGET ${NO_OS_PROJECT_NAME})
  set_property(TARGET ${NO_OS_PROJECT_NAME} APPEND PROPERTY LINK_LIBRARIES libiio-iiod)
else()
  message(STATUS "libiio: no project executable to link; link libiio-iiod yourself")
endif()
