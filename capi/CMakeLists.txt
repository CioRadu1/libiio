# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

# libiio for CAPI applications, the CAPI counterpart of zephyr/CMakeLists.txt.
# A CAPI application resolves libiio from its module.yml (as LIBIIO_DIR),
# then adds this directory and links libiio::capi:
#
#   add_subdirectory(${LIBIIO_DIR}/capi ${CMAKE_BINARY_DIR}/libiio)
#   target_link_libraries(<app> PRIVATE libiio::capi)

option(LIBIIO_CAPI_IIOD_UART "Serve IIOD over a capi_uart" ON)
option(LIBIIO_CAPI_ADC "IIO device over any capi_adc driver" ON)

get_filename_component(LIBIIO_CAPI_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# libiio's version, from its own top-level project(), and its git hash, as
# the top-level CMakeLists.txt sets them (this directory is not built from
# there).
file(READ "${LIBIIO_CAPI_ROOT}/CMakeLists.txt" _libiio_top)
string(REGEX MATCH "project\\([^)]*VERSION ([0-9]+)\\.([0-9]+)\\.([0-9]+)"
             _libiio_version "${_libiio_top}")
if (NOT _libiio_version)
    message(
        FATAL_ERROR
            "libiio version not found in ${LIBIIO_CAPI_ROOT}/CMakeLists.txt")
endif ()
set(LIBIIO_CAPI_VERSION_MAJOR ${CMAKE_MATCH_1})
set(LIBIIO_CAPI_VERSION_MINOR ${CMAKE_MATCH_2})
set(LIBIIO_CAPI_VERSION_PATCH ${CMAKE_MATCH_3})

find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${LIBIIO_CAPI_ROOT}
        OUTPUT_VARIABLE LIBIIO_CAPI_VERSION_GIT
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif ()
if (NOT LIBIIO_CAPI_VERSION_GIT)
    set(LIBIIO_CAPI_VERSION_GIT
        v${LIBIIO_CAPI_VERSION_MAJOR}.${LIBIIO_CAPI_VERSION_MINOR}.${LIBIIO_CAPI_VERSION_PATCH}
    )
endif ()

add_library(
    libiio_capi STATIC
    ${LIBIIO_CAPI_ROOT}/context.c
    ${LIBIIO_CAPI_ROOT}/library.c
    ${LIBIIO_CAPI_ROOT}/backend.c
    ${LIBIIO_CAPI_ROOT}/device.c
    ${LIBIIO_CAPI_ROOT}/channel.c
    ${LIBIIO_CAPI_ROOT}/attr.c
    ${LIBIIO_CAPI_ROOT}/utilities.c
    ${LIBIIO_CAPI_ROOT}/scan.c
    ${LIBIIO_CAPI_ROOT}/buffer.c
    ${LIBIIO_CAPI_ROOT}/stream.c
    ${LIBIIO_CAPI_ROOT}/task.c
    ${LIBIIO_CAPI_ROOT}/mask.c
    ${LIBIIO_CAPI_ROOT}/sort.c
    ${LIBIIO_CAPI_ROOT}/block.c
    ${LIBIIO_CAPI_ROOT}/events.c
    ${LIBIIO_CAPI_ROOT}/lock-dummy.c
    ${LIBIIO_CAPI_ROOT}/tinyiiod/tinyiiod.c
    ${LIBIIO_CAPI_ROOT}/iiod/responder.c
    ${LIBIIO_CAPI_ROOT}/iiod-responder.c
    ${LIBIIO_CAPI_ROOT}/iiod/rw.c
    backend.c)

if (LIBIIO_CAPI_IIOD_UART)
    target_sources(libiio_capi PRIVATE iiod/uart.c)
endif ()

if (LIBIIO_CAPI_ADC)
    target_sources(libiio_capi PRIVATE drivers/adc.c)
endif ()

add_library(libiio::capi ALIAS libiio_capi)

target_include_directories(
    libiio_capi PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
                       ${LIBIIO_CAPI_ROOT}/include ${LIBIIO_CAPI_ROOT})

target_compile_definitions(
    libiio_capi
    PUBLIC LIBIIO_STATIC=1
    PRIVATE NO_THREADS=1
            WITH_ZSTD=0
            WITH_LIBTINYIIOD=1
            WITH_EXTERNAL_BACKEND=1
            WITH_IIOD_USB_DMABUF=0
            WITH_IIOD_V0_COMPAT=0
            WITH_LOCAL_BACKEND=0
            WITH_NETWORK_BACKEND=0
            WITH_SERIAL_BACKEND=0
            WITH_USB_BACKEND=0
            WITH_XML_BACKEND=0
            WITH_NETWORK_BACKEND_DYNAMIC=0
            WITH_SERIAL_BACKEND_DYNAMIC=0
            WITH_USB_BACKEND_DYNAMIC=0
            WITH_EMU_BACKEND=0
            WITH_EMU_BACKEND_DYNAMIC=0
            HAVE_DNS_SD=0
            WITH_MODULES=0
            DEFAULT_LOG_LEVEL=4
            LIBIIO_VERSION_MAJOR=${LIBIIO_CAPI_VERSION_MAJOR}
            LIBIIO_VERSION_MINOR=${LIBIIO_CAPI_VERSION_MINOR}
            LIBIIO_VERSION_PATCH=${LIBIIO_CAPI_VERSION_PATCH}
            LIBIIO_VERSION_GIT=\"${LIBIIO_CAPI_VERSION_GIT}\"
            HAS_STRTOK_R=1
            LIBIIO_SCAN_BACKENDS=\"\")

target_link_libraries(libiio_capi PUBLIC capi::core)
