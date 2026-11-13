# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)


list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(uts2_work)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_compile_definitions(app PRIVATE 
    USE_FULL_ERROR_LIST=1
)

target_include_directories(app PRIVATE 
    src 
    src/drivers
    src/midelware
    src/lib/scpi/libscpi/inc
    src/uts2/spci_interface
    src/uts2/app
    src/uts2
)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "src/drivers/*.c"   
    "src/midelware/*c"  
    "src/uts2/deamons/*c" 
    "src/lib/scpi/libscpi/src/*.c"
    "src/uts2/spci_interface/*.c"
    "src/uts2/app/*.c"
)

target_sources(app PRIVATE 
              
    src/main.c  
     ${APP_SOURCES} 
)


add_subdirectory(drivers/seq_mux_adc)
add_subdirectory(drivers/scan_cont_adc)
add_subdirectory(drivers/hc_595_chain)