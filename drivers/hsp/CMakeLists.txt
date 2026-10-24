# SPDX-License-Identifier: Apache-2.0

# hsp.h holds a __subsystem API; the syscall scan gives it its DEVICE_API section.
zephyr_syscall_include_directories(${CMAKE_CURRENT_LIST_DIR}/../../include)

zephyr_library()
zephyr_library_sources(
  hsp_stm32u3.c
  st/hsp_fw_ram.c
)
