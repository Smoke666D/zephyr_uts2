# SPDX-License-Identifier: Apache-2.0

# keep first

board_runner_args(jlink "--device=STM32H723VG" "--speed=4000")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
# keep first

