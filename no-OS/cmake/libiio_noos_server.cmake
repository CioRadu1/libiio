# Copyright (c) 2026 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# libiio_noos_add_server(<name> TRANSPORT uart|usb|network [V0_COMPAT])
#
# Adds <name>, an OBJECT library with libiio's iiod for a no-OS build: libiio
# core, the responder (+ the v0 parser with V0_COMPAT), this port's backend,
# table layer, transport, no-OS driver bridge and triggers, and the
# platform's glue. Call it after no-OS's no-os target exists; link <name>
# into the project's executable, which links no-os itself.
#
# Used by module.cmake (a no-OS project) and by this directory's
# CMakeLists.txt (the sample firmware). Sets IIOD_PLATFORM_DIR,
# IIOD_PLATFORM_DEFS and IIOD_PLATFORM_INCLUDES in the caller, for its own
# files that use the platform's values.

include_guard(GLOBAL)

get_filename_component(LIBIIO_NOOS_DIR "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
get_filename_component(LIBIIO_DIR "${LIBIIO_NOOS_DIR}/.." REALPATH)

function(libiio_noos_add_server name)
  cmake_parse_arguments(ARG "V0_COMPAT" "TRANSPORT" "" ${ARGN})
  set(transport ${ARG_TRANSPORT})
  if(NOT EXISTS ${LIBIIO_NOOS_DIR}/iiod/${transport}.c)
    message(FATAL_ERROR "libiio_noos_add_server: unknown TRANSPORT '${transport}' "
                        "(uart, usb or network).")
  endif()
  string(TOUPPER ${transport} _transport_upper)

  if(NOT TARGET no-os OR NOT PLATFORM)
    message(FATAL_ERROR "${name} is added from a no-OS build: the no-os "
                        "target and PLATFORM must already exist.")
  endif()
  if(NOT NO_OS_PATH)
    set(NO_OS_PATH ${NO_OS_DIR} PARENT_SCOPE)
    set(NO_OS_PATH ${NO_OS_DIR})
  endif()

  # Optional per-platform glue: native usb and the part's own ADC. Every
  # setting (uart, timers, network, usb ids) comes from the project.
  set(IIOD_PLATFORM_DIR ${LIBIIO_NOOS_DIR}/platform/${PLATFORM})
  if(transport STREQUAL "usb" AND NOT EXISTS ${IIOD_PLATFORM_DIR}/platform.cmake)
    message(FATAL_ERROR "the usb transport needs native usb glue: PLATFORM=${PLATFORM} "
                        "has no platform/${PLATFORM}/ (see platform/maxim/iio_usb_maxim.c).")
  endif()

  # libiio's responder is the only IIO server here: no-OS's own (iio.c,
  # iiod.c, iio_trigger.c) and iio_app must never be built into the image.
  get_target_property(_no_os_all_srcs no-os SOURCES)
  foreach(_src IN LISTS _no_os_all_srcs)
    if(_src MATCHES "/iio/(iio|iiod|iio_trigger)\\.c$" OR _src MATCHES "/iio_app/")
      message(FATAL_ERROR
        "no-OS's own IIO server is in the build (${_src}). Turn CONFIG_IIO "
        "off: this port serves IIO through libiio only.")
    endif()
  endforeach()

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
  if(ARG_V0_COMPAT)
    set(V0_COMPAT_ON 1)
    find_program(BISON_EXECUTABLE bison REQUIRED)
    find_program(FLEX_EXECUTABLE flex REQUIRED)

    # Generated into <build>/<name>/iiod so "../iiod/parser.h" (ascii-ops.c) and
    # "../iio-config.h" (parser.y) resolve without touching the iiod sources.
    set(V0_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/${name}/iiod)
    file(MAKE_DIRECTORY ${V0_GEN_DIR})
    file(WRITE ${V0_GEN_DIR}/lexer-prologue.l "%option always-interactive\n")
    configure_file(${LIBIIO_NOOS_DIR}/include/iio-config.h
                   ${CMAKE_CURRENT_BINARY_DIR}/${name}/iio-config.h COPYONLY)

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
                 INCLUDE_DIRECTORIES ${LIBIIO_NOOS_DIR}/include/compat)
    list(APPEND LIBIIO_SRCS ${V0_COMPAT_SRCS})
  else()
    set(V0_COMPAT_ON 0)
    set(V0_GEN_DIR "")
    # Only the stub of what ascii-ops.c would provide.
    list(APPEND LIBIIO_SRCS ${LIBIIO_DIR}/tinyiiod/ops-stubs.c)
  endif()

  # ---------- Existing no-OS IIO drivers (iio_<part>.c), served through libiio ----------
  set(IIOD_NOOS_DRIVER_SRCS
    ${LIBIIO_NOOS_DIR}/drivers/iio_noos_driver_desc.c
    ${LIBIIO_NOOS_DIR}/drivers/iio_noos_driver.c
  )
  # OBJECT, like no-os: its objects go straight into the project's
  # executable, and linking it to no-os carries only no-os's compile settings.
  add_library(${name} OBJECT
    ${LIBIIO_SRCS}
    ${IIOD_NOOS_DRIVER_SRCS}
    ${LIBIIO_NOOS_DIR}/backend.c
    ${LIBIIO_NOOS_DIR}/backend_table.c
    ${LIBIIO_NOOS_DIR}/iiod/${transport}.c
    ${LIBIIO_NOOS_DIR}/drivers/iio_trigger_timer.c
    ${IIOD_PLATFORM_SRCS}
  )

  # What the project's own files see: noos_iiod.h, iio_noos_driver.h and the
  # device classes' headers, none of which include libiio's iio.h.
  target_include_directories(${name} PUBLIC
    ${LIBIIO_NOOS_DIR}/include
    ${LIBIIO_NOOS_DIR}/drivers
  )
  target_include_directories(${name} PRIVATE
    ${LIBIIO_NOOS_DIR}/iiod
    ${IIOD_PLATFORM_DIR}
    ${LIBIIO_DIR}/include
    ${LIBIIO_DIR}
    ${LIBIIO_DIR}/iiod
    ${V0_GEN_DIR}
    ${IIOD_PLATFORM_INCLUDES}
  )

  target_compile_definitions(${name} PUBLIC
    IIOD_TRANSPORT_${_transport_upper}=1
  )
  target_compile_definitions(${name} PRIVATE
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


  # no-os is an OBJECT library so that its strong IRQ handlers override the
  # startup file's weak ones; its objects reach only targets linking it
  # directly, so the project's executable links no-os itself, as it already
  # does in every no-OS project.
  target_link_libraries(${name} PUBLIC no-os)

  set(IIOD_PLATFORM_DIR ${IIOD_PLATFORM_DIR} PARENT_SCOPE)
  set(IIOD_PLATFORM_DEFS ${IIOD_PLATFORM_DEFS} PARENT_SCOPE)
  set(IIOD_PLATFORM_INCLUDES ${IIOD_PLATFORM_INCLUDES} PARENT_SCOPE)
endfunction()
