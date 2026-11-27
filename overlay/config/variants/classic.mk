# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 Open25-DM32 Contributors
## title: Open25 Classic
## summary: FM, DMR, conventional P25 and NXDN; local test profile.
CONFIG_P25_TRUNK ?= n
CONFIG_DMR_TRUNK ?= n
CONFIG_DMR_GPS ?= n
CONFIG_DMR_SMS ?= n
CONFIG_DMR_TMS ?= n
CONFIG_DMR_ARS ?= n
CONFIG_DMR_LRRP ?= n
CONFIG_DMR_SIGNALING ?= n
CONFIG_DMR_EMERGENCY ?= n
CONFIG_DMR_ROAMING ?= n
CONFIG_ZONE_SHARE ?= n
CONFIG_DMR_UDP_COMPRESS ?= n
CONFIG_DMR_CONTROL_STATION ?= n
CONFIG_NET_PPP ?= n
CONFIG_DMR_IP_GATEWAY ?= n
CONFIG_HOTSPOT ?= n
CONFIG_SFR ?= n
CONFIG_APRS ?= n
CONFIG_AM_RX ?= n
CONFIG_BCAST_FM ?= n
CONFIG_VOX ?= n
CONFIG_LANG_ES ?= n
CONFIG_LANG_IT ?= n
CONFIG_LANG_FR ?= n
CONFIG_LANG_ZH ?= n
CONFIG_CRASH_REPORT ?= n

# Fixed receive coverage and protocol-specific transmit policy.
CONFIG_RX_EXT_ALWAYS := y
CONFIG_P25_TX_BAND_POLICY := y
