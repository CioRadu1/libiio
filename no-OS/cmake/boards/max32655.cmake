# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

if(NOT DEFINED MSDK_DIR)
  set(MSDK_DIR $ENV{HOME}/MaximSDK)
endif()

set(MSDK_PERIPH  ${MSDK_DIR}/Libraries/PeriphDrivers)
set(MSDK_CMSIS   ${MSDK_DIR}/Libraries/CMSIS)
set(PLATFORM_DIR ${NOOS_DIR}/drivers/platform/maxim/max32655)

set(BOARD_STARTUP_SRCS
  ${MSDK_CMSIS}/Device/Maxim/MAX32655/Source/GCC/startup_max32655.S
  ${MSDK_CMSIS}/Device/Maxim/MAX32655/Source/system_max32655.c
  ${MSDK_CMSIS}/Device/Maxim/MAX32655/Source/heap.c
)

set(BOARD_PERIPH_SRCS
  ${MSDK_PERIPH}/Source/SYS/mxc_assert.c
  ${MSDK_PERIPH}/Source/SYS/mxc_delay.c
  ${MSDK_PERIPH}/Source/SYS/mxc_lock.c
  ${MSDK_PERIPH}/Source/SYS/nvic_table.c
  ${MSDK_PERIPH}/Source/SYS/pins_me17.c
  ${MSDK_PERIPH}/Source/SYS/sys_me17.c
  ${MSDK_PERIPH}/Source/GPIO/gpio_common.c
  ${MSDK_PERIPH}/Source/GPIO/gpio_me17.c
  ${MSDK_PERIPH}/Source/GPIO/gpio_reva.c
  ${MSDK_PERIPH}/Source/ICC/icc_me17.c
  ${MSDK_PERIPH}/Source/ICC/icc_reva.c
  ${MSDK_PERIPH}/Source/UART/uart_common.c
  ${MSDK_PERIPH}/Source/UART/uart_me17.c
  ${MSDK_PERIPH}/Source/UART/uart_revb.c
  ${MSDK_PERIPH}/Source/DMA/dma_me17.c
  ${MSDK_PERIPH}/Source/DMA/dma_reva.c
  ${MSDK_PERIPH}/Source/ADC/adc_me17.c
  ${MSDK_PERIPH}/Source/ADC/adc_reva.c
  ${MSDK_PERIPH}/Source/SPI/spi_me17.c
  ${MSDK_PERIPH}/Source/SPI/spi_reva1.c
  ${MSDK_PERIPH}/Source/FLC/flc_common.c
  ${MSDK_PERIPH}/Source/FLC/flc_me17.c
  ${MSDK_PERIPH}/Source/FLC/flc_reva.c
  ${MSDK_PERIPH}/Source/TMR/tmr_common.c
  ${MSDK_PERIPH}/Source/TMR/tmr_me17.c
  ${MSDK_PERIPH}/Source/TMR/tmr_revb.c
  ${MSDK_PERIPH}/Source/RTC/rtc_me17.c
  ${MSDK_PERIPH}/Source/RTC/rtc_reva.c
  ${MSDK_PERIPH}/Source/SIMO/simo_me17.c
  ${MSDK_PERIPH}/Source/SIMO/simo_reva.c
)

set(BOARD_PLATFORM_SRCS
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

set(BOARD_INCLUDES
  ${MSDK_CMSIS}/Device/Maxim/MAX32655/Include
  ${MSDK_CMSIS}/5.9.0/Core/Include
  ${MSDK_PERIPH}/Include/MAX32655
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
  ${MSDK_PERIPH}/Source/SIMO
  ${PLATFORM_DIR}
  ${NOOS_DIR}/drivers/platform/maxim/common
)

set(BOARD_DEFS
  TARGET=MAX32655
  TARGET_NUM=32655
  TARGET_REV=0x4131
  MAX32655
  IIO_ADC_REF_VOLTAGE_MV=1220
  FLASH_ORIGIN=0x10000000
  FLASH_SIZE=0x80000
  SRAM_ORIGIN=0x20000000
  SRAM_SIZE=0x20000
  __STACK_SIZE=0x4000
)

set(BOARD_LINKER_SCRIPT
  ${MSDK_CMSIS}/Device/Maxim/MAX32655/Source/GCC/max32655.ld
)

set(BOARD_PLATFORM_NAME maxim)
