# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uts2_work)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)



target_sources(app PRIVATE 
        src/main.c
        src/usb_thread.c
        src/dio_thread.c
        src/adc_thread.c
        src/adc_monitor.c
        src/ina228_stream_thread.c
        

)


zephyr_iterable_section(NAME param_handler GROUP RODATA_REGION)


add_subdirectory_ifdef(CONFIG_CUSTOM_SEQ_MUX_ADC drivers/seq_mux_adc)
add_subdirectory_ifdef(CONFIG_CUSTOM_SEQ_MUX_ADC drivers/hc_595_chain)