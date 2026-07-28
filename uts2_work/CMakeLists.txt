# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uts2_work)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)



target_sources(app PRIVATE 
        src/usb_thread.c
       
        src/ina228_stream_thread.c
       
        src/out_and_power_control.c
        src/main.cpp 
        src/os.cpp
        src/system_bridge.c
        src/seq_adc_processor.c
        src/seq_adc_shell.c
)



add_subdirectory_ifdef(CONFIG_CUSTOM_SEQ_MUX_ADC drivers/seq_mux_adc)
add_subdirectory_ifdef(CONFIG_CUSTOM_SEQ_MUX_ADC drivers/hc_595_chain)