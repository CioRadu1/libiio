# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

# libiio for CAPI applications.
# A CAPI application resolves libiio from its module.yml (as LIBIIO_DIR),
# then adds this directory and links libiio::capi:
#
#   add_subdirectory(${LIBIIO_DIR}/capi ${CMAKE_BINARY_DIR}/libiio)
#   target_link_libraries(<app> PRIVATE libiio::capi)

option(LIBIIO_CAPI_IIOD_UART "Serve IIOD over a capi_uart" ON)

# capi::core comes from capi_create_application(), which adds CAPI's core.
if (NOT TARGET capi::core)
    message(FATAL_ERROR "libiio/capi: add it after capi_create_application(), "
                        "which provides capi::core.")
endif ()

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
    backend.c
    backend_table.c)

if (LIBIIO_CAPI_IIOD_UART)
    target_sources(libiio_capi PRIVATE iiod/uart.c)
endif ()

# The ADC adapter, for a CAPI whose capi_adc has the calls it serves scale,
# offset, sampling_frequency and captures from (capi_adc_chan_get_scale() ...).
file(STRINGS "${CAPI_DIR}/inc/capi_adc.h" _capi_adc_scale REGEX "capi_adc_chan_get_scale")
if (_capi_adc_scale)
    target_sources(libiio_capi PRIVATE iio_capi_adc.c)
endif ()

if (EXISTS "${CAPI_DIR}/inc/capi_dac.h")
    target_sources(libiio_capi PRIVATE iio_capi_dac.c)
endif ()

# The temperature sensor adapter, for a CAPI that has capi_temp, and the
# accelerometer one, which serves the part's temperature sensor too, for one
# that also has capi_accel.
if (EXISTS "${CAPI_DIR}/inc/capi_temp.h")
    target_sources(libiio_capi PRIVATE iio_capi_temp.c)
    if (EXISTS "${CAPI_DIR}/inc/capi_accel.h")
        target_sources(libiio_capi PRIVATE iio_capi_accel.c)
    endif ()
endif ()

add_library(libiio::capi ALIAS libiio_capi)

# The application sees this port's headers and libiio's public ones;
# libiio's internal headers (iio-private.h, tinyiiod/ ...) stay private.
target_include_directories(libiio_capi PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
                                              ${LIBIIO_CAPI_ROOT}/include)
target_include_directories(libiio_capi PRIVATE ${LIBIIO_CAPI_ROOT})

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
            # libiio's own logs go to stderr, which may be the IIOD UART:
            # off. This port logs through CAPI_LOG.
            DEFAULT_LOG_LEVEL=0
            LIBIIO_VERSION_MAJOR=${LIBIIO_CAPI_VERSION_MAJOR}
            LIBIIO_VERSION_MINOR=${LIBIIO_CAPI_VERSION_MINOR}
            LIBIIO_VERSION_PATCH=${LIBIIO_CAPI_VERSION_PATCH}
            LIBIIO_VERSION_GIT=\"${LIBIIO_CAPI_VERSION_GIT}\"
            HAS_STRTOK_R=1
            LIBIIO_SCAN_BACKENDS=\"\")

target_link_libraries(libiio_capi PUBLIC capi::core)
