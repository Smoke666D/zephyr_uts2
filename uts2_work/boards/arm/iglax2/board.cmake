
# SPDX-License-Identifier: Apache-2.0

# keep first

board_runner_args(jlink "--device" "STM32F413VG" "--speed=1000" "--reset-type=0")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)

# keep first