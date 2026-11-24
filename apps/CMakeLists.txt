# Testbed programs (wip_server, wip_client), built together with the picoquic fork.
#
#   cmake -S apps -B build -DCMAKE_BUILD_TYPE=Release -DPICOQUIC_FETCH_PTLS=ON
#   cmake --build build -j
#
# Results: build/wip_server, build/wip_client, and picoquic's own tools in
# build/picoquic/ (picoquicdemo, pico_sim, picolog_t).

cmake_minimum_required(VERSION 3.13)
project(thesis_testbed LANGUAGES C CXX)

set(PICOQUIC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../picoquic)
get_filename_component(PICOQUIC_DIR ${PICOQUIC_DIR} ABSOLUTE)

# picoquic as a sub-project. When built that way it skips pico_sim by default;
# turn it on so that one build gives every tool we use.
set(BUILD_PICO_SIM ON CACHE BOOL "Build pico_sim")
add_subdirectory(${PICOQUIC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/picoquic)

find_package(Threads REQUIRED)

# Variables computed inside picoquic's CMakeLists, needed to link like picoquicdemo.
foreach(var PTLS_LIBRARIES OPENSSL_LIBRARIES MBEDTLS_LIBRARIES PICOQUIC_COMPILE_DEFINITIONS)
    get_directory_property(${var} DIRECTORY ${PICOQUIC_DIR} DEFINITION ${var})
endforeach()

foreach(app wip_server wip_client)
    add_executable(${app} ${app}.c wip_common.c)
    if(WIN32)
        target_sources(${app} PRIVATE ${PICOQUIC_DIR}/picoquicfirst/getopt.c)
        target_include_directories(${app} PRIVATE ${PICOQUIC_DIR}/picoquicfirst)
    endif()
    target_link_libraries(${app}
        PRIVATE
            ${PTLS_LIBRARIES}
            ${OPENSSL_LIBRARIES}
            ${MBEDTLS_LIBRARIES}
            picohttp-core
            picoquic-log
            picoquic-core
            Threads::Threads)
    target_include_directories(${app} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PICOQUIC_DIR}/picoquic
        ${PICOQUIC_DIR}/picohttp
        ${PICOQUIC_DIR}/loglib)
    # Default certificate and key: picoquic's test certificate.
    target_compile_definitions(${app} PRIVATE
        ${PICOQUIC_COMPILE_DEFINITIONS}
        WIP_PICOQUIC_DIR="${PICOQUIC_DIR}")
    set_picoquic_compile_settings(${app})
endforeach()
