cmake_minimum_required(VERSION 3.15)
project(NX-torrent-player)

get_filename_component(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "")
set(PLATFORM_ANDROID ON CACHE BOOL "")
set(LIBROMFS_PREBUILT_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/libromfs-generator.exe" CACHE FILEPATH "" FORCE)
set(LIBROMFS_PROJECT_NAME "borealis" CACHE STRING "" FORCE)
set(LIBROMFS_RESOURCE_LOCATION "${ROOT_DIR}/library/borealis/resources" CACHE PATH "" FORCE)

set(BOREALIS_LIBRARY ${ROOT_DIR}/library/borealis/library)
set(SDL_DIR ${BOREALIS_LIBRARY}/lib/extern/SDL)

# SDL2 library
add_subdirectory(${SDL_DIR} SDL)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# Borealis UI framework
add_subdirectory(${BOREALIS_LIBRARY} borealis)

# Android platform libraries
find_library(ANDROID_LIB android)
find_library(LOG_LIB log)
find_library(EGL_LIB EGL)
find_library(GLES2_LIB GLESv2)

# Gather NX Torrent Player sources
file(GLOB_RECURSE MAIN_SRC ${ROOT_DIR}/app/*.cpp)
set(ENGINE_DIR ${ROOT_DIR}/engine)
set(ENGINE_SRC
    ${ENGINE_DIR}/bencode.c ${ENGINE_DIR}/torrent_meta.c ${ENGINE_DIR}/torrentfs.c
    ${ENGINE_DIR}/bt_peer.c ${ENGINE_DIR}/magnet.c
    ${ENGINE_DIR}/udp_tracker.c ${ENGINE_DIR}/dht.c ${ENGINE_DIR}/dhtclient.c
    ${ENGINE_DIR}/listen.c ${ENGINE_DIR}/upnp.c ${ENGINE_DIR}/engine_log.c
    ${ENGINE_DIR}/engine_services.c
    ${ENGINE_DIR}/udpdiag.c ${ENGINE_DIR}/stream.c ${ENGINE_DIR}/httpsrc.c
    ${ROOT_DIR}/pctest/compat/sha1.c
    ${ROOT_DIR}/pctest/nbstub.c
    ${ROOT_DIR}/pctest/utpstub.c
    ${ENGINE_DIR}/mpv_stub.c)

set_source_files_properties(${ENGINE_SRC} PROPERTIES COMPILE_DEFINITIONS "POSIX")

list(APPEND MAIN_SRC ${ENGINE_SRC})

# Build NX-torrent-player shared library
add_definitions(-DAPP_VERSION="1.0.0")

add_library(curl STATIC IMPORTED)
set_target_properties(curl PROPERTIES IMPORTED_LOCATION
    ${CMAKE_CURRENT_SOURCE_DIR}/../libs/${ANDROID_ABI}/libcurl.a)
find_library(Z_LIB z)

program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${ROOT_DIR}/pctest/compat
    ${ROOT_DIR}/app
    ${ROOT_DIR}/engine
    ${ROOT_DIR}/include
    ${ROOT_DIR}/compat
    ${BOREALIS_LIBRARY}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis
    SDL2
    curl
    ${Z_LIB}
    ${ANDROID_LIB}
    ${LOG_LIB}
    ${EGL_LIB}
    ${GLES2_LIB}
)
