board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")


board_runner_args(pyocd "--flash-opt=-O reset_type=hw")
board_runner_args(pyocd "--flash-opt=-O connect_mode=under-reset")
board_runner_args(jlink "--device=STM32F413VG" "--speed=4000")

# keep first
include(${ZEPHYR_BASE}/boards/common/openocd-stm32.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
