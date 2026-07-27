# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uts2_work)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)



target_sources(app PRIVATE 
        src/usb_thread.c
        src/adc_thread.cpp
        src/adc_monitor.cpp
        src/ina228_stream_thread.c
        src/param_server.c 
        src/out_and_power_control.c
        src/main.cpp 
        src/os/os.cpp
        src/os/mediator.cpp
        
)

target_include_directories(app PRIVATE src/os)
target_include_directories(app PRIVATE src)

add_subdirectory_ifdef(CONFIG_CUSTOM_SEQ_MUX_ADC drivers/seq_mux_adc)
add_subdirectory_ifdef(CONFIG_CUSTOM_SEQ_MUX_ADC drivers/hc_595_chain)