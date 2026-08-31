# Copyright (c) 2025 Analog Devices, Inc.
# SPDX-License-Identifier: MIT

set(NET_CHIP_SRCS
  ${NOOS_DIR}/drivers/net/adin1110/adin1110.c
  ${NOOS_DIR}/drivers/net/oa_tc6/oa_tc6.c
  ${NOOS_DIR}/network/lwip_raw_socket/netdevs/adin1110/lwip_adin1110.c
)

set(NET_CHIP_INCLUDES
  ${NOOS_DIR}/drivers/net/adin1110
  ${NOOS_DIR}/drivers/net/oa_tc6
  ${NOOS_DIR}/network/lwip_raw_socket/netdevs/adin1110
)

set(NET_CHIP_HEADER "netdev_adin1110.h")
