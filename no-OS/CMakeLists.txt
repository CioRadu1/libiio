# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.13)
project(libiio-noos C ASM)

# LIBIIO_DIR is now the parent directory (libiio root)
set(LIBIIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The no-OS checkout: -DNOOS_DIR first, then the NOOS_DIR environment variable,
# then the two conventional locations. Nothing here may name a personal
# directory -- this has to configure on anyone's machine.
if(NOT DEFINED NOOS_DIR AND DEFINED ENV{NOOS_DIR})
  set(NOOS_DIR "$ENV{NOOS_DIR}" CACHE PATH "no-OS source tree")
endif()

# This port lives inside the libiio tree as no-OS/, so "../no-OS" relative to
# here is this very directory -- never a candidate. Only a checkout beside
# libiio or under $HOME can be found without being told where it is.
if(NOT DEFINED NOOS_DIR)
  foreach(candidate
      "${CMAKE_CURRENT_SOURCE_DIR}/../../no-OS"
      "$ENV{HOME}/no-OS")
    if(EXISTS "${candidate}/include/no_os_uart.h")
      set(NOOS_DIR "${candidate}" CACHE PATH "no-OS source tree")
      set(NOOS_DIR_GUESSED TRUE)
      break()
    endif()
  endforeach()
endif()

if(NOT NOOS_DIR OR NOT EXISTS "${NOOS_DIR}/include/no_os_uart.h")
  message(FATAL_ERROR
    "no-OS source tree not found. Pass -DNOOS_DIR=/path/to/no-OS "
    "or export NOOS_DIR in your shell.")
endif()

get_filename_component(NOOS_DIR "${NOOS_DIR}" REALPATH)

# Say when the tree was guessed: a machine with several no-OS checkouts would
# otherwise switch trees silently.
if(NOOS_DIR_GUESSED)
  message(STATUS "NOOS_DIR = ${NOOS_DIR} (guessed -- export NOOS_DIR or pass "
                 "-DNOOS_DIR to build against another checkout)")
else()
  message(STATUS "NOOS_DIR = ${NOOS_DIR}")
endif()

# ---------- libiio core sources ----------
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

# ---------- transport options ----------
option(WITH_IIOD_UART    "Build UART transport"    ON)
option(WITH_IIOD_NETWORK "Build network transport"  ON)
option(WITH_IIOD_USB     "Build USB transport"     OFF)

option(WITH_IIOD_V0_COMPAT "Build v0 (ASCII) protocol compat for UART" ON)
if(WITH_IIOD_V0_COMPAT AND WITH_IIOD_UART)
  set(V0_COMPAT_ON 1)
else()
  set(V0_COMPAT_ON 0)
endif()

# ---------- no-OS backend / transport sources ----------
set(NOOS_BACKEND_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/backend.c
)

if(WITH_IIOD_UART)
  list(APPEND NOOS_BACKEND_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/iiod/uart.c
  )
endif()

if(WITH_IIOD_NETWORK)
  list(APPEND NOOS_BACKEND_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/iiod/network.c
  )
endif()

if(WITH_IIOD_USB)
  list(APPEND NOOS_BACKEND_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/iiod/usb.c
  )
endif()

# ---------- no-OS platform API sources ----------
set(NOOS_PLATFORM_SRCS
  ${NOOS_DIR}/drivers/api/no_os_uart.c
  ${NOOS_DIR}/util/no_os_alloc.c
  ${NOOS_DIR}/util/no_os_mutex.c
  ${NOOS_DIR}/util/no_os_lf256fifo.c
)

if(WITH_IIOD_NETWORK)
  list(APPEND NOOS_PLATFORM_SRCS
    ${NOOS_DIR}/network/tcp_socket.c
  )
endif()

# ---------- IIO ADC device driver ----------
set(IIO_ADC_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers/iio_adc.c
)

# ---------- Application ----------
set(APP_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/samples/iiod/main.c)

# ---------- Include directories (common) ----------
set(COMMON_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers
  ${CMAKE_CURRENT_SOURCE_DIR}/iiod
  ${LIBIIO_DIR}/include
  ${LIBIIO_DIR}
  ${LIBIIO_DIR}/iiod
  ${NOOS_DIR}/include
  ${NOOS_DIR}/network
)

# ---------- Compile definitions (common) ----------
set(COMMON_DEFS
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
  DISABLE_SECURE_SOCKET
)

# ---------- v0 (ASCII) protocol compat sources ----------
if(V0_COMPAT_ON)
  find_program(BISON_EXECUTABLE bison REQUIRED)
  find_program(FLEX_EXECUTABLE  flex  REQUIRED)

  set(V0_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/v0-compat)
  file(MAKE_DIRECTORY ${V0_GEN_DIR})

  add_custom_command(
    OUTPUT  ${V0_GEN_DIR}/parser.c ${V0_GEN_DIR}/parser.h
    COMMAND ${BISON_EXECUTABLE} -d -o ${V0_GEN_DIR}/parser.c ${LIBIIO_DIR}/iiod/parser.y
    DEPENDS ${LIBIIO_DIR}/iiod/parser.y
    COMMENT "bison: iiod/parser.y"
  )
  add_custom_command(
    OUTPUT  ${V0_GEN_DIR}/lexer.c
    COMMAND ${FLEX_EXECUTABLE} -o ${V0_GEN_DIR}/lexer.c ${LIBIIO_DIR}/iiod/lexer.l
    DEPENDS ${LIBIIO_DIR}/iiod/lexer.l ${V0_GEN_DIR}/parser.h
    COMMENT "flex: iiod/lexer.l"
  )

  list(APPEND NOOS_BACKEND_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/iiod/ascii-ops.c
    ${V0_GEN_DIR}/parser.c
    ${V0_GEN_DIR}/lexer.c
  )
  list(APPEND COMMON_INCLUDES ${V0_GEN_DIR})

  set_source_files_properties(
    ${V0_GEN_DIR}/parser.c
    ${V0_GEN_DIR}/lexer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/iiod/ascii-ops.c
    PROPERTIES COMPILE_OPTIONS "-Wno-sign-compare"
  )
endif()

###############################################################################
# Cross-compilation for a bare-metal board (CMAKE_SYSTEM_NAME == "Generic")
###############################################################################
if(CMAKE_SYSTEM_NAME STREQUAL "Generic")

  # BOARD, and the core it implies, are resolved by cmake/bare-metal.cmake
  # before this file is read -- toolchain files run first, and the compiler
  # flags depend on the board. Here we load the rest of the board description.
  if(NOT DEFINED BOARD)
    message(FATAL_ERROR
      "BOARD is not set. Cross builds must go through the bare-metal "
      "toolchain file: cmake --preset uart, or "
      "cmake --toolchain cmake/bare-metal.cmake -DBOARD=max32690 ...")
  endif()

  message(STATUS "BOARD = ${BOARD} (${BOARD_ARCH})")
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/boards/${BOARD}.cmake)

  set(ADC common_api CACHE STRING "ADC device HAL (defaults to the board's internal ADC)")
  set(BOARD_ADC_SRC ${CMAKE_CURRENT_SOURCE_DIR}/drivers/adc/${ADC}/${ADC}.c)

  option(WITH_LWIP_NETWORKING "Enable lwIP + ADIN1110 network transport" ON)

  # --- MAXUSB library (for USB transport) ---
  if(WITH_IIOD_USB)
    set(MAXUSB_DIR ${MSDK_DIR}/Libraries/MAXUSB)
    set(MAXUSB_SRCS
      ${MAXUSB_DIR}/src/core/musbhsfc/usb.c
      ${MAXUSB_DIR}/src/core/usb_event.c
      ${MAXUSB_DIR}/src/enumerate/enumerate.c
      ${MAXUSB_DIR}/src/devclass/cdc_acm.c
      ${PLATFORM_DIR}/maxim_usb_uart.c
    )
    set(MAXUSB_INCLUDES
      ${MAXUSB_DIR}/include/core
      ${MAXUSB_DIR}/include/core/musbhsfc
      ${MAXUSB_DIR}/include/enumerate
      ${MAXUSB_DIR}/include/devclass
    )
  endif()

  if(WITH_LWIP_NETWORKING)
    set(NET_CHIP adin1110 CACHE STRING "Ethernet netdev chip")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/netdevs/${NET_CHIP}.cmake)
    set(LWIP_DIR ${NOOS_DIR}/libraries/lwip/lwip)

    # no-OS ships the lwIP port glue (arch/cc.h, configs/lwipopts.h) under two
    # different roots depending on the checkout's age. Same files either way.
    foreach(glue "${NOOS_DIR}/libraries/lwip-glue" "${NOOS_DIR}/libraries/lwip")
      if(EXISTS "${glue}/configs/lwipopts.h")
        set(LWIP_GLUE_DIR "${glue}")
        break()
      endif()
    endforeach()

    if(NOT LWIP_GLUE_DIR)
      message(FATAL_ERROR
        "lwIP port glue not found under ${NOOS_DIR}/libraries. Looked for "
        "lwip-glue/configs/lwipopts.h and lwip/configs/lwipopts.h. Update the "
        "no-OS checkout (git submodule update --init) or build with "
        "-DWITH_LWIP_NETWORKING=OFF.")
    endif()

    message(STATUS "lwIP glue = ${LWIP_GLUE_DIR}")
    if(NOT DEFINED NO_OS_IP)
      set(NO_OS_IP "192.0.2.1")
    endif()
    if(NOT DEFINED NO_OS_NETMASK)
      set(NO_OS_NETMASK "255.255.255.0")
    endif()
    if(NOT DEFINED NO_OS_GATEWAY)
      set(NO_OS_GATEWAY "192.0.2.2")
    endif()
  endif()

  # --- Additional no-OS API & utility sources ---
  set(NOOS_EXTRA_SRCS
    ${NOOS_DIR}/drivers/api/no_os_irq.c
    ${NOOS_DIR}/drivers/api/no_os_spi.c
    ${NOOS_DIR}/drivers/api/no_os_gpio.c
    ${NOOS_DIR}/drivers/api/no_os_timer.c
    ${NOOS_DIR}/drivers/api/no_os_dma.c
    ${NOOS_DIR}/util/no_os_fifo.c
    ${NOOS_DIR}/util/no_os_list.c
    ${NOOS_DIR}/util/no_os_util.c
    ${NOOS_DIR}/util/no_os_crc8.c
  )

  # --- Networking sources (netdev chip + lwIP) ---
  if(WITH_LWIP_NETWORKING)
    file(GLOB LWIP_CORE_SRCS    ${LWIP_DIR}/src/core/*.c)
    file(GLOB LWIP_CORE4_SRCS   ${LWIP_DIR}/src/core/ipv4/*.c)
    file(GLOB LWIP_API_SRCS     ${LWIP_DIR}/src/api/*.c)
    file(GLOB LWIP_NETIF_SRCS   ${LWIP_DIR}/src/netif/*.c)
    file(GLOB LWIP_MDNS_SRCS    ${LWIP_DIR}/src/apps/mdns/*.c)
    file(GLOB LWIP_PERF_SRCS    ${LWIP_DIR}/src/apps/lwiperf/*.c)

    set(LWIP_SOCKET_SRCS
      ${NOOS_DIR}/network/lwip_raw_socket/lwip_socket.c
    )
  endif()

  # --- Build the executable ---
  add_executable(libiio-noos
    ${LIBIIO_SRCS}
    ${NOOS_BACKEND_SRCS}
    ${NOOS_PLATFORM_SRCS}
    ${NOOS_EXTRA_SRCS}
    ${IIO_ADC_SRCS}
    ${BOARD_ADC_SRC}
    ${BOARD_STARTUP_SRCS}
    ${BOARD_PERIPH_SRCS}
    ${BOARD_PLATFORM_SRCS}
    ${MAXUSB_SRCS}
    ${NET_CHIP_SRCS}
    ${LWIP_CORE_SRCS}
    ${LWIP_CORE4_SRCS}
    ${LWIP_API_SRCS}
    ${LWIP_NETIF_SRCS}
    ${LWIP_MDNS_SRCS}
    ${LWIP_PERF_SRCS}
    ${LWIP_SOCKET_SRCS}
    ${APP_SRCS}
  )

  target_include_directories(libiio-noos PRIVATE
    ${COMMON_INCLUDES}
    ${BOARD_INCLUDES}

    ${CMAKE_CURRENT_SOURCE_DIR}/platform/${BOARD_PLATFORM_NAME}

    ${MAXUSB_INCLUDES}
  )

  # LWIP_DIR and LWIP_GLUE_DIR are empty unless lwIP is on, and an empty prefix
  # would put /src/include and /configs on every other build's include path.
  if(WITH_LWIP_NETWORKING)
    target_include_directories(libiio-noos PRIVATE
      ${NET_CHIP_INCLUDES}

      ${LWIP_DIR}/src/include
      ${LWIP_DIR}/src/include/compat
      ${LWIP_DIR}/src/include/netif
      ${LWIP_GLUE_DIR}/configs
      ${LWIP_GLUE_DIR}
      ${NOOS_DIR}/network/lwip_raw_socket
    )
  endif()

  target_compile_definitions(libiio-noos PRIVATE
    ${COMMON_DEFS}
    ${BOARD_DEFS}
  )

  if(WITH_IIOD_USB)
    string(TOUPPER ${BOARD} BOARD_UPPER)
    target_compile_definitions(libiio-noos PRIVATE
      NO_OS_USB_TRANSPORT
      NO_OS_USB_PRODUCT="${BOARD_UPPER} IIO"
    )
  endif()

  if(WITH_LWIP_NETWORKING)
    target_compile_definitions(libiio-noos PRIVATE
      NO_OS_LWIP_NETWORKING
      LWIP_PROVIDE_ERRNO
      NO_OS_IP="${NO_OS_IP}"
      NO_OS_NETMASK="${NO_OS_NETMASK}"
      NO_OS_GATEWAY="${NO_OS_GATEWAY}"
      NETDEV_HEADER="${NET_CHIP_HEADER}"
    )
  endif()

  target_link_options(libiio-noos PRIVATE
    -T${BOARD_LINKER_SCRIPT}
    -Wl,--entry=Reset_Handler
    -Wl,--gc-sections
  )

  # --- Post-build: generate .bin and .hex ---
  add_custom_command(TARGET libiio-noos POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:libiio-noos> libiio-noos.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:libiio-noos> libiio-noos.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:libiio-noos>
    COMMENT "Generating .bin and .hex"
  )

  # --- Flash ---
  # Which tool flashes a board is vendor knowledge, so the recipe comes from
  # cmake/platforms/<vendor>.cmake as BOARD_FLASH_COMMAND, with @FLASH_IMAGE@
  # standing in for the image this build produces. A platform that cannot flash
  # sets BOARD_FLASH_ERROR instead, and the target reports it when invoked
  # rather than failing the configure -- building without a probe attached is
  # normal.
  if(NOT BOARD_FLASH_COMMAND AND NOT BOARD_FLASH_ERROR)
    set(BOARD_FLASH_ERROR
        "platform '${BOARD_PLATFORM_NAME}' defines no flash command")
  endif()

  if(BOARD_FLASH_ERROR)
    add_custom_target(flash
      COMMAND ${CMAKE_COMMAND} -E echo "ERROR: ${BOARD_FLASH_ERROR}"
      COMMAND ${CMAKE_COMMAND} -E false
      VERBATIM
    )
  else()
    string(REPLACE "@FLASH_IMAGE@" "libiio-noos.hex"
           FLASH_COMMAND "${BOARD_FLASH_COMMAND}")
    add_custom_target(flash
      COMMAND ${FLASH_COMMAND}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
      COMMENT "Flashing libiio-noos.hex to ${BOARD}"
      USES_TERMINAL
      VERBATIM
    )
    add_dependencies(flash libiio-noos)
  endif()

###############################################################################
# Host / native build (for development / testing)
###############################################################################
else()

  add_executable(libiio-noos
    ${LIBIIO_SRCS}
    ${NOOS_BACKEND_SRCS}
    ${NOOS_PLATFORM_SRCS}
    ${APP_SRCS}
  )

  target_include_directories(libiio-noos PRIVATE
    ${COMMON_INCLUDES}
  )

  target_compile_definitions(libiio-noos PRIVATE
    ${COMMON_DEFS}
  )

  target_compile_options(libiio-noos PRIVATE -Wall)

  option(WITH_TESTS "Build the no-OS libiio API tests" OFF)

  if(WITH_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()

endif()
