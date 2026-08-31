# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.13)
project(libiio-noos C ASM)

# LIBIIO_DIR is now the parent directory (libiio root)
set(LIBIIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(NOT DEFINED NOOS_DIR)
  message(FATAL_ERROR "NOOS_DIR must point to the no-OS root directory")
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
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers/iio_val.c
)

# ---------- Application ----------
set(APP_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/samples/iiod/src/main.c
)

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
  WITH_IIOD_V0_COMPAT=0
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
  LIBIIO_VERSION_GIT="v1.0"
  HAS_STRTOK_R=1
  LIBIIO_SCAN_BACKENDS=""
  DISABLE_SECURE_SOCKET
)

###############################################################################
# Cross-compilation for a bare-metal board (CMAKE_SYSTEM_NAME == "Generic")
###############################################################################
if(CMAKE_SYSTEM_NAME STREQUAL "Generic")

  set(BOARD max32690 CACHE STRING "Target board")
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/boards/${BOARD}.cmake)

  option(WITH_LWIP_NETWORKING "Enable lwIP + ADIN1110 network transport" ON)

  # --- MAXUSB library (for USB transport) ---
  if(WITH_IIOD_USB)
    set(MAXUSB_DIR ${MSDK_DIR}/Libraries/MAXUSB)
    set(MAXUSB_SRCS
      ${MAXUSB_DIR}/src/core/musbhsfc/usb.c
      ${MAXUSB_DIR}/src/core/usb_event.c
      ${MAXUSB_DIR}/src/enumerate/enumerate.c
    )
    set(MAXUSB_INCLUDES
      ${MAXUSB_DIR}/include/core
      ${MAXUSB_DIR}/include/core/musbhsfc
      ${MAXUSB_DIR}/include/enumerate
    )
  endif()

  if(WITH_LWIP_NETWORKING)
    set(NET_CHIP adin1110 CACHE STRING "Ethernet netdev chip")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/netdevs/${NET_CHIP}.cmake)
    set(LWIP_DIR ${NOOS_DIR}/libraries/lwip/lwip)
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

    ${CMAKE_CURRENT_SOURCE_DIR}/samples/iiod/src/platform/${BOARD_PLATFORM_NAME}

    ${MAXUSB_INCLUDES}
    ${NET_CHIP_INCLUDES}

    ${LWIP_DIR}/src/include
    ${LWIP_DIR}/src/include/compat
    ${LWIP_DIR}/src/include/netif
    ${NOOS_DIR}/libraries/lwip/configs
    ${NOOS_DIR}/libraries/lwip
    ${NOOS_DIR}/network/lwip_raw_socket
  )

  target_compile_definitions(libiio-noos PRIVATE
    ${COMMON_DEFS}
    ${BOARD_DEFS}
  )

  if(WITH_IIOD_USB)
    target_compile_definitions(libiio-noos PRIVATE NO_OS_USB_TRANSPORT)
  endif()

  if(WITH_LWIP_NETWORKING)
    target_compile_definitions(libiio-noos PRIVATE
      NO_OS_LWIP_NETWORKING
      NO_OS_STATIC_IP
      LWIP_PROVIDE_ERRNO
      NO_OS_IP="${NO_OS_IP}"
      NO_OS_NETMASK="${NO_OS_NETMASK}"
      NO_OS_GATEWAY="${NO_OS_GATEWAY}"
      NETDEV_HEADER="${NET_CHIP_HEADER}"
    )
  endif()

  target_compile_options(libiio-noos PRIVATE -Wall -Og -g)

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

endif()
