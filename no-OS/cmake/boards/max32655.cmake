# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT
#
# MAX32655 (EvKit): Cortex-M4F, 512 KiB flash, 128 KiB SRAM, ME17 die.
# Unlike the MAX32690 it has a rev-A ADC (fixed 1.22 V reference, no clock
# select) and an SIMO regulator that has to be brought up at init.

set(BOARD_ARCH cortex-m4f)

# cmake/bare-metal.cmake reads this file before the SDK or no-OS have been
# located, and only needs the line above.
if(BOARD_ARCH_QUERY)
  return()
endif()

set(MAXIM_PART MAX32655)
set(MAXIM_DIE  me17)

# MSDK peripheral modules: "<Source subdir> <file> <file> ...", no .c suffix.
# @DIE@ expands to MAXIM_DIE; include paths are derived from the subdirs.
set(MAXIM_PERIPH_MODULES
  "SYS   mxc_assert mxc_delay mxc_lock nvic_table pins_@DIE@ sys_@DIE@"
  "GPIO  gpio_common gpio_@DIE@ gpio_reva"
  "ICC   icc_@DIE@ icc_reva"
  "UART  uart_common uart_@DIE@ uart_revb"
  "DMA   dma_@DIE@ dma_reva"
  "ADC   adc_@DIE@ adc_reva"
  "SPI   spi_@DIE@ spi_reva1"
  "FLC   flc_common flc_@DIE@ flc_reva"
  "TMR   tmr_common tmr_@DIE@ tmr_revb"
  "RTC   rtc_@DIE@ rtc_reva"
  "SIMO  simo_@DIE@ simo_reva"
)

set(BOARD_DEFS
  FLASH_ORIGIN=0x10000000
  FLASH_SIZE=0x80000
  SRAM_ORIGIN=0x20000000
  SRAM_SIZE=0x20000
  __STACK_SIZE=0x4000
  IIO_ADC_REF_VOLTAGE_MV=1220
)

include(${CMAKE_CURRENT_LIST_DIR}/../platforms/maxim.cmake)
