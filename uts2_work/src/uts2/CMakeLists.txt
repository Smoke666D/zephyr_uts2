target_include_directories(app PRIVATE 
    ${CMAKE_SOURCE_DIR}/src 
    ${CMAKE_SOURCE_DIR}/src/drivers
    ${CMAKE_SOURCE_DIR}/src/midelware
    ${CMAKE_SOURCE_DIR}/src/lib/scpi/libscpi/inc
    ${CMAKE_SOURCE_DIR}/src/uts2/spci_interface
    ${CMAKE_SOURCE_DIR}/src/uts2/app
    ${CMAKE_SOURCE_DIR}/src/uts2
)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/drivers/*.c"   
    "${CMAKE_SOURCE_DIR}/src/midelware/*.c"  
    "${CMAKE_SOURCE_DIR}/src/uts2/deamons/*.c" 
    "${CMAKE_SOURCE_DIR}/src/lib/scpi/libscpi/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/uts2/spci_interface/*.c"
    "${CMAKE_SOURCE_DIR}/src/uts2/app/*.c"
)

target_sources(app PRIVATE ${APP_SOURCES})

add_subdirectory(${CMAKE_SOURCE_DIR}/drivers/seq_mux_adc   ${CMAKE_BINARY_DIR}/drivers/seq_mux_adc)
add_subdirectory(${CMAKE_SOURCE_DIR}/drivers/scan_cont_adc ${CMAKE_BINARY_DIR}/drivers/scan_cont_adc)
add_subdirectory(${CMAKE_SOURCE_DIR}/drivers/hc_595_chain  ${CMAKE_BINARY_DIR}/drivers/hc_595_chain)