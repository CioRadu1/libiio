# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# MAX32690 (APARD32690-SL): Cortex-M4F, 3 MiB flash, 1 MiB SRAM, ME18 die.

set(BOARD_ARCH cortex-m4f)

# cmake/bare-metal.cmake reads this file before the SDK or no-OS have been
# located, and only needs the line above.
if(BOARD_ARCH_QUERY)
  return()
endif()

set(MAXIM_PART MAX32690)
set(MAXIM_DIE  me18)

# MSDK peripheral modules: "<Source subdir> <file> <file> ...", no .c suffix.
# @DIE@ expands to MAXIM_DIE; include paths are derived from the subdirs.
set(MAXIM_PERIPH_MODULES
  "SYS   mxc_assert mxc_delay mxc_lock nvic_table pins_@DIE@ sys_@DIE@"
  "GPIO  gpio_common gpio_@DIE@ gpio_reva"
  "ICC   icc_@DIE@ icc_reva"
  "UART  uart_common uart_@DIE@ uart_revb"
  "DMA   dma_@DIE@ dma_reva"
  "ADC   adc_@DIE@ adc_revb"
  "SPI   spi_@DIE@ spi_reva1"
  "FLC   flc_common flc_@DIE@ flc_reva"
  "TMR   tmr_@DIE@ tmr_revb"
  "RTC   rtc_@DIE@ rtc_reva"
)

set(BOARD_DEFS
  FLASH_ORIGIN=0x10000000
  FLASH_SIZE=0x340000
  SRAM_ORIGIN=0x20000000
  SRAM_SIZE=0x100000
  __STACK_SIZE=0x10000
  IIO_ADC_CLOCK=MXC_ADC_CLK_IBRO
)

include(${CMAKE_CURRENT_LIST_DIR}/../platforms/maxim.cmake)
