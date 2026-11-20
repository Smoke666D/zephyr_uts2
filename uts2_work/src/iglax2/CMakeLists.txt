target_include_directories(app PRIVATE 
    ${CMAKE_SOURCE_DIR}/src  
    ${CMAKE_SOURCE_DIR}/src/iglax2/spci_interface
    ${CMAKE_SOURCE_DIR}/src/iglax2/app
    ${CMAKE_SOURCE_DIR}/src/iglax2
    ${CMAKE_SOURCE_DIR}/src/midelware
    ${CMAKE_SOURCE_DIR}/src/lib/scpi/libscpi/inc
)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/iglax2/*.c"       
    "${CMAKE_SOURCE_DIR}/src/midelware/*.c"  
    "${CMAKE_SOURCE_DIR}/src/lib/scpi/libscpi/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/iglax2/spci_interface/*.c"
    "${CMAKE_SOURCE_DIR}/src/iglax2/app/*.c"
)

target_sources(app PRIVATE ${APP_SOURCES})
