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
set(APP_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/samples/iiod/src/main.c
)

# ---------- Include directories (common) ----------
set(COMMON_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers
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
  WITH_LOCAL_BACKEND=0
  WITH_NETWORK_BACKEND=0
  WITH_SERIAL_BACKEND=0
  WITH_USB_BACKEND=0
  WITH_XML_BACKEND=0
  WITH_NETWORK_BACKEND_DYNAMIC=0
  WITH_SERIAL_BACKEND_DYNAMIC=0
  WITH_USB_BACKEND_DYNAMIC=0
  HAVE_DNS_SD=0
  WITH_MODULES=0
  DEFAULT_LOG_LEVEL=1
  LIBIIO_VERSION_MAJOR=1
  LIBIIO_VERSION_MINOR=0
  LIBIIO_VERSION_GIT="v1.0"
  HAS_STRTOK_R=1
  LIBIIO_SCAN_BACKENDS=""
  DISABLE_SECURE_SOCKET
)

###############################################################################
# Cross-compilation for MAX32690 (CMAKE_SYSTEM_NAME == "Generic")
###############################################################################
if(CMAKE_SYSTEM_NAME STREQUAL "Generic")

  if(NOT DEFINED MSDK_DIR)
    set(MSDK_DIR $ENV{HOME}/MaximSDK)
  endif()

  option(WITH_LWIP_NETWORKING "Enable lwIP + ADIN1110 network transport" ON)

  set(MSDK_PERIPH  ${MSDK_DIR}/Libraries/PeriphDrivers)
  set(MSDK_CMSIS   ${MSDK_DIR}/Libraries/CMSIS)
  set(PLATFORM_DIR ${NOOS_DIR}/drivers/platform/maxim/max32690)

  if(WITH_LWIP_NETWORKING)
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

  # --- MSDK startup & system sources ---
  set(MSDK_STARTUP_SRCS
    ${MSDK_CMSIS}/Device/Maxim/MAX32690/Source/GCC/startup_max32690.S
    ${MSDK_CMSIS}/Device/Maxim/MAX32690/Source/system_max32690.c
    ${MSDK_CMSIS}/Device/Maxim/MAX32690/Source/heap.c
  )

  # --- MSDK peripheral driver sources ---
  set(MSDK_PERIPH_SRCS
    # SYS
    ${MSDK_PERIPH}/Source/SYS/mxc_assert.c
    ${MSDK_PERIPH}/Source/SYS/mxc_delay.c
    ${MSDK_PERIPH}/Source/SYS/mxc_lock.c
    ${MSDK_PERIPH}/Source/SYS/nvic_table.c
    ${MSDK_PERIPH}/Source/SYS/pins_me18.c
    ${MSDK_PERIPH}/Source/SYS/sys_me18.c
    # GPIO
    ${MSDK_PERIPH}/Source/GPIO/gpio_common.c
    ${MSDK_PERIPH}/Source/GPIO/gpio_me18.c
    ${MSDK_PERIPH}/Source/GPIO/gpio_reva.c
    # ICC
    ${MSDK_PERIPH}/Source/ICC/icc_me18.c
    ${MSDK_PERIPH}/Source/ICC/icc_reva.c
    # UART
    ${MSDK_PERIPH}/Source/UART/uart_common.c
    ${MSDK_PERIPH}/Source/UART/uart_me18.c
    ${MSDK_PERIPH}/Source/UART/uart_revb.c
    # DMA
    ${MSDK_PERIPH}/Source/DMA/dma_me18.c
    ${MSDK_PERIPH}/Source/DMA/dma_reva.c
    # ADC
    ${MSDK_PERIPH}/Source/ADC/adc_me18.c
    ${MSDK_PERIPH}/Source/ADC/adc_revb.c
    # SPI (for ADIN1110)
    ${MSDK_PERIPH}/Source/SPI/spi_me18.c
    ${MSDK_PERIPH}/Source/SPI/spi_reva1.c
    # FLC (required by sys_me18.c)
    ${MSDK_PERIPH}/Source/FLC/flc_common.c
    ${MSDK_PERIPH}/Source/FLC/flc_me18.c
    ${MSDK_PERIPH}/Source/FLC/flc_reva.c
    # TMR (for maxim_timer)
    ${MSDK_PERIPH}/Source/TMR/tmr_me18.c
    ${MSDK_PERIPH}/Source/TMR/tmr_revb.c
    # RTC (required by maxim_irq)
    ${MSDK_PERIPH}/Source/RTC/rtc_me18.c
    ${MSDK_PERIPH}/Source/RTC/rtc_reva.c
  )

  # --- no-OS Maxim platform drivers ---
  set(NOOS_MAXIM_SRCS
    ${PLATFORM_DIR}/maxim_uart.c
    ${PLATFORM_DIR}/maxim_uart_stdio.c
    ${PLATFORM_DIR}/maxim_irq.c
    ${PLATFORM_DIR}/maxim_delay.c
    ${PLATFORM_DIR}/maxim_init.c
    ${PLATFORM_DIR}/maxim_gpio.c
    ${PLATFORM_DIR}/maxim_spi.c
    ${PLATFORM_DIR}/maxim_timer.c
    ${NOOS_DIR}/drivers/platform/maxim/common/maxim_dma.c
  )

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

  # --- Networking sources (ADIN1110 + lwIP) ---
  if(WITH_LWIP_NETWORKING)
    set(ADIN1110_SRCS
      ${NOOS_DIR}/drivers/net/adin1110/adin1110.c
      ${NOOS_DIR}/drivers/net/oa_tc6/oa_tc6.c
    )

    file(GLOB LWIP_CORE_SRCS    ${LWIP_DIR}/src/core/*.c)
    file(GLOB LWIP_CORE4_SRCS   ${LWIP_DIR}/src/core/ipv4/*.c)
    file(GLOB LWIP_API_SRCS     ${LWIP_DIR}/src/api/*.c)
    file(GLOB LWIP_NETIF_SRCS   ${LWIP_DIR}/src/netif/*.c)
    file(GLOB LWIP_MDNS_SRCS    ${LWIP_DIR}/src/apps/mdns/*.c)
    file(GLOB LWIP_PERF_SRCS    ${LWIP_DIR}/src/apps/lwiperf/*.c)

    set(LWIP_SOCKET_SRCS
      ${NOOS_DIR}/network/lwip_raw_socket/lwip_socket.c
      ${NOOS_DIR}/network/lwip_raw_socket/netdevs/adin1110/lwip_adin1110.c
    )
  endif()

  # --- Build the executable ---
  add_executable(libiio-noos
    ${LIBIIO_SRCS}
    ${NOOS_BACKEND_SRCS}
    ${NOOS_PLATFORM_SRCS}
    ${NOOS_EXTRA_SRCS}
    ${IIO_ADC_SRCS}
    ${MSDK_STARTUP_SRCS}
    ${MSDK_PERIPH_SRCS}
    ${NOOS_MAXIM_SRCS}
    ${ADIN1110_SRCS}
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

    # MSDK CMSIS & peripheral headers
    ${MSDK_CMSIS}/Device/Maxim/MAX32690/Include
    ${MSDK_CMSIS}/5.9.0/Core/Include
    ${MSDK_PERIPH}/Include/MAX32690

    # MSDK internal peripheral source headers
    ${MSDK_PERIPH}/Source/SYS
    ${MSDK_PERIPH}/Source/UART
    ${MSDK_PERIPH}/Source/GPIO
    ${MSDK_PERIPH}/Source/DMA
    ${MSDK_PERIPH}/Source/ICC
    ${MSDK_PERIPH}/Source/ADC
    ${MSDK_PERIPH}/Source/SPI
    ${MSDK_PERIPH}/Source/FLC
    ${MSDK_PERIPH}/Source/TMR
    ${MSDK_PERIPH}/Source/RTC

    # no-OS Maxim platform driver headers
    ${PLATFORM_DIR}
    ${NOOS_DIR}/drivers/platform/maxim/common

    # ADIN1110 driver headers
    ${NOOS_DIR}/drivers/net/adin1110
    ${NOOS_DIR}/drivers/net/oa_tc6

    # lwIP headers
    ${LWIP_DIR}/src/include
    ${LWIP_DIR}/src/include/compat
    ${LWIP_DIR}/src/include/netif
    ${NOOS_DIR}/libraries/lwip/configs
    ${NOOS_DIR}/libraries/lwip        # parent of arch/ so #include "arch/cc.h" works
    ${NOOS_DIR}/network/lwip_raw_socket
    ${NOOS_DIR}/network/lwip_raw_socket/netdevs/adin1110
  )

  target_compile_definitions(libiio-noos PRIVATE
    ${COMMON_DEFS}
    TARGET=MAX32690
    TARGET_NUM=32690
    TARGET_REV=0x4131
    MAX32690
    # Memory defines for startup .S
    FLASH_ORIGIN=0x10000000
    FLASH_SIZE=0x340000
    SRAM_ORIGIN=0x20000000
    SRAM_SIZE=0x100000
    # 32 KB stack for nested iiod_interpreter calls (network transport)
    __STACK_SIZE=0x8000
  )

  if(WITH_LWIP_NETWORKING)
    target_compile_definitions(libiio-noos PRIVATE
      NO_OS_LWIP_NETWORKING
      NO_OS_STATIC_IP
      LWIP_PROVIDE_ERRNO
      NO_OS_IP="${NO_OS_IP}"
      NO_OS_NETMASK="${NO_OS_NETMASK}"
      NO_OS_GATEWAY="${NO_OS_GATEWAY}"
    )
  endif()

  target_compile_options(libiio-noos PRIVATE -Wall)

  # --- Linker settings ---
  set(LINKER_SCRIPT ${MSDK_CMSIS}/Device/Maxim/MAX32690/Source/GCC/max32690.ld)

  target_link_options(libiio-noos PRIVATE
    -T${LINKER_SCRIPT}
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
