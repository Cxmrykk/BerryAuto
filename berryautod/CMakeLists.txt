cmake_minimum_required(VERSION 3.10)
project(OpenGALEmitter C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread -O2")

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(PROTOBUF REQUIRED protobuf)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavutil libswscale)
pkg_check_modules(X11 REQUIRED x11 xext)
pkg_check_modules(WAYLAND REQUIRED wayland-client)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3 libspa-0.2)
pkg_check_modules(GLIB REQUIRED glib-2.0 gio-2.0 gio-unix-2.0)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS 
    "${CMAKE_SOURCE_DIR}/certs/android_auto.crt" 
    "${CMAKE_SOURCE_DIR}/certs/android_auto.key"
)

file(READ "${CMAKE_SOURCE_DIR}/certs/android_auto.crt" AAP_CERT_CONTENT)
file(READ "${CMAKE_SOURCE_DIR}/certs/android_auto.key" AAP_KEY_CONTENT)

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")
file(WRITE "${GENERATED_DIR}/certs_data.hpp"
"#pragma once\n\n"
"constexpr const char* AAP_CERT_GENERATED = R\"CERT_DELIM(${AAP_CERT_CONTENT})CERT_DELIM\";\n\n"
"constexpr const char* AAP_KEY_GENERATED = R\"KEY_DELIM(${AAP_KEY_CONTENT})KEY_DELIM\";\n"
)

set(PROTO_SRC_DIR ${CMAKE_SOURCE_DIR}/proto)
set(PROTO_BUILD_DIR ${CMAKE_BINARY_DIR}/proto_gen)
file(MAKE_DIRECTORY ${PROTO_BUILD_DIR})

file(GLOB PROTO_FILES "${PROTO_SRC_DIR}/*.proto")
foreach(PROTO_FILE ${PROTO_FILES})
    execute_process(
        COMMAND protoc --proto_path=${PROTO_SRC_DIR} --cpp_out=${PROTO_BUILD_DIR} ${PROTO_FILE}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endforeach()

set(WLR_SCREENCOPY_XML "${CMAKE_SOURCE_DIR}/protocols/wlr-screencopy-unstable-v1.xml")

execute_process(
    COMMAND wayland-scanner client-header ${WLR_SCREENCOPY_XML} ${GENERATED_DIR}/wlr-screencopy-unstable-v1-client-protocol.h
)
execute_process(
    COMMAND wayland-scanner private-code ${WLR_SCREENCOPY_XML} ${GENERATED_DIR}/wlr-screencopy-unstable-v1-protocol.c
)

include_directories(
    ${PROTO_BUILD_DIR} 
    ${OPENSSL_INCLUDE_DIRS} 
    ${PROTOBUF_INCLUDE_DIRS} 
    ${FFMPEG_INCLUDE_DIRS} 
    ${X11_INCLUDE_DIRS}
    ${WAYLAND_INCLUDE_DIRS}
    ${PIPEWIRE_INCLUDE_DIRS}
    ${GLIB_INCLUDE_DIRS}
    src 
    ${GENERATED_DIR}
)

file(GLOB SOURCES "src/*.cpp")
file(GLOB PROTO_SRCS "${PROTO_BUILD_DIR}/*.pb.cc")

add_executable(opengal_emitter ${SOURCES} ${GENERATED_DIR}/wlr-screencopy-unstable-v1-protocol.c ${PROTO_SRCS})
target_link_libraries(opengal_emitter 
    ${OPENSSL_LIBRARIES} 
    ${PROTOBUF_LIBRARIES} 
    ${FFMPEG_LIBRARIES}
    ${X11_LIBRARIES}
    ${WAYLAND_LIBRARIES}
    ${PIPEWIRE_LIBRARIES}
    ${GLIB_LIBRARIES}
    pthread
)
