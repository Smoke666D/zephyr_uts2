
# SPDX-License-Identifier: Apache-2.0

# keep first

board_runner_args(jlink "--device=STM32F413VG" "--speed=1000" "--commander=C:/Program Files/SEGGER/JLink_V818/JLink.exe" "--gdbserver=C:/Program Files/SEGGER/JLink_V818/JLinkGDBServerCL.exe")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)

# keep first