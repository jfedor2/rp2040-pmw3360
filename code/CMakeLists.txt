cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(trackball)

pico_sdk_init()

add_compile_options(-Wall)

add_executable(trackball src/trackball.c src/pmw3360.c src/srom.c)

target_include_directories(trackball PRIVATE src)

target_link_libraries(trackball pico_stdlib hardware_spi tinyusb_device tinyusb_board)

pico_add_extra_outputs(trackball)
