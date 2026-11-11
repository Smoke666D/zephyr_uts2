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
    src/logic_signal
  #  src/uts2/shell
    src/midelware
    src/lib/scpi/libscpi/inc
    src/uts2/spci_interface
)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "src/drivers/*.c"  
    "src/works/*.c"  
    #"src/uts2/shell/*.c" 
    "src/midelware/*c"  
    "src/deamons/*c" 
    "src/lib/scpi/libscpi/src/*.c"
    "src/uts2/spci_interface/*.c"
)

target_sources(app PRIVATE 
              
    src/main.c
    src/settings.c 
    src/scpi_init.c    
     ${APP_SOURCES} 
)


add_subdirectory(drivers/seq_mux_adc)
add_subdirectory(drivers/scan_cont_adc)
add_subdirectory(drivers/hc_595_chain)