board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
board_runner_args(jlink "--device=STM32F413VG" "--speed=4000")

# keep first
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
