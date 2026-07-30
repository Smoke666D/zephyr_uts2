# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uts2_work)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


target_sources(app PRIVATE 
        src/usb_thread.c
       
        src/ina228_stream_thread.c
       
        src/out_and_power_control.c
        src/main.cpp 
        src/os.cpp
        src/system_bridge.c
        src/seq_adc_processor.c
        src/seq_adc_shell.c
        src/out_and_power_control.c
        src/out_and_power_control_shell.c
        src/adc_mux_processor.cpp
)



add_subdirectory_ifdef(CONFIG_CUSTOM_SEQ_MUX_ADC drivers/seq_mux_adc)
