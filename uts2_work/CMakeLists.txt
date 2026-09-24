# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)


list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uts2_work)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_include_directories(app PRIVATE 
    src 
    src/drivers
    src/lua
)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "src/drivers/*.c"    
"src/lua/*.c"    
)

target_sources(app PRIVATE 
       # src/adc_thread.cpp
       # src/adc_monitor.cpp
       src/ina228_stream_thread.c
       src/app_worker.c
       src/driver_ads1115.c
       # src/param_server.c 
       # src/out_and_power_control.c
        src/main.c
        src/settings.c
      #  src/os.cpp
      ${APP_SOURCES} 
)



add_subdirectory_ifdef(CONFIG_CUSTOM_SEQ_MUX_ADC drivers/seq_mux_adc)
add_subdirectory_ifdef(CONFIG_CUSTOM_SEQ_MUX_ADC drivers/hc_595_chain)