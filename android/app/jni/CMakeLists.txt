cmake_minimum_required(VERSION 3.16)
project(NX-torrent-player)

get_filename_component(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build borealis and helper libraries statically into libNX-torrent-player.so;
# keep only SDL2 as a shared library (required by SDLActivity.getLibraries()).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)

set(PLATFORM_ANDROID ON CACHE BOOL "" FORCE)
set(USE_GLES3 ON CACHE BOOL "" FORCE)
set(USE_GLES2 OFF CACHE BOOL "" FORCE)

if (CMAKE_HOST_WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libromfs-generator.exe")
    set(LIBROMFS_PREBUILT_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/libromfs-generator.exe" CACHE FILEPATH "" FORCE)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libromfs-generator")
    set(LIBROMFS_PREBUILT_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/libromfs-generator" CACHE FILEPATH "" FORCE)
endif()

set(BOREALIS_LIBRARY ${ROOT_DIR}/library/borealis/library)
set(SDL_DIR ${BOREALIS_LIBRARY}/lib/extern/SDL)

# SDL2 library
add_subdirectory(${SDL_DIR} SDL)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# Stage trimmed romfs resources for Android (fonts, i18n, material icons, app assets, cacert)
set(ANDROID_ROMFS_DIR "${CMAKE_CURRENT_BINARY_DIR}/android_romfs")
file(REMOVE_RECURSE "${ANDROID_ROMFS_DIR}")
file(COPY "${ROOT_DIR}/library/borealis/resources/" DESTINATION "${ANDROID_ROMFS_DIR}")
file(REMOVE_RECURSE
    "${ANDROID_ROMFS_DIR}/img/pokemon"
    "${ANDROID_ROMFS_DIR}/xml"
    "${ANDROID_ROMFS_DIR}/icon"
    "${ANDROID_ROMFS_DIR}/img/tiles.png"
    "${ANDROID_ROMFS_DIR}/img/demo_icon.jpg"
    "${ANDROID_ROMFS_DIR}/img/borealis_256.png"
    "${ANDROID_ROMFS_DIR}/img/borealis_96.png"
    "${ANDROID_ROMFS_DIR}/.gitignore"
)
file(COPY
    "${ROOT_DIR}/assets/cacert.pem"
    "${ROOT_DIR}/assets/NX-torrent-player-bg-rounded.png"
    "${ROOT_DIR}/assets/video-icon.png"
    "${ROOT_DIR}/assets/magnet-icon.png"
    "${ROOT_DIR}/assets/stremio-icon.png"
    DESTINATION "${ANDROID_ROMFS_DIR}"
)
configure_file("${ROOT_DIR}/assets/local-tab-icon.png" "${ANDROID_ROMFS_DIR}/local-icon.png" COPYONLY)
if (EXISTS "${ROOT_DIR}/assets/patterns")
    file(COPY "${ROOT_DIR}/assets/patterns" DESTINATION "${ANDROID_ROMFS_DIR}")
endif()

# Must call add_libromfs() before borealis so -DUSE_LIBROMFS is active everywhere
add_libromfs("borealis" "${ANDROID_ROMFS_DIR}")
set(LIBROMFS_PROJECT_NAME "borealis" CACHE STRING "" FORCE)
set(LIBROMFS_RESOURCE_LOCATION "${ANDROID_ROMFS_DIR}" CACHE PATH "" FORCE)

# Borealis UI framework
add_subdirectory(${BOREALIS_LIBRARY} borealis)

# Android platform libraries
find_library(ANDROID_LIB android)
find_library(LOG_LIB log)
find_library(EGL_LIB EGL)
find_library(GLES3_LIB GLESv3)
find_library(DL_LIB dl)

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
    ${ENGINE_DIR}/hls_proxy.c
    ${ENGINE_DIR}/utp.cpp ${ENGINE_DIR}/utp_utils.cpp ${ENGINE_DIR}/utp_nb.cpp
    ${ROOT_DIR}/pctest/compat/sha1.c
    ${ROOT_DIR}/pctest/nbstub.c
    ${ROOT_DIR}/pctest/utpstub.c)

set_source_files_properties(${ENGINE_SRC} PROPERTIES COMPILE_DEFINITIONS "POSIX")
set_source_files_properties(${ROOT_DIR}/pctest/nbstub.c PROPERTIES COMPILE_DEFINITIONS "POSIX;ENGINE_HAS_UTP_NB")

list(APPEND MAIN_SRC ${ENGINE_SRC})

set(UPDATE_REPO "Langegen/NX-torrent-player" CACHE STRING "GitHub owner/name for updates")
set(UPDATE_API_URL "https://api.github.com/repos/${UPDATE_REPO}/releases/latest" CACHE STRING "")
set(UPDATE_TAG_URL "https://api.github.com/repos/${UPDATE_REPO}/releases/tags/" CACHE STRING "")
set(TMDB_API_KEY "8cf43ad9c085135b9479ad5cf6bbcbda" CACHE STRING "Default TMDB API key / read token")
set(TMDB_PROXY_URL "" CACHE STRING "Default TMDB proxy URL")
set(GDRIVE_CLIENT_ID "368701728051-fphf1cnob488rv09q5gsjfsjul7ekuid.apps.googleusercontent.com" CACHE STRING "Default Google Drive OAuth Client ID")
set(GDRIVE_CLIENT_SECRET "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" CACHE STRING "Default Google Drive OAuth Client Secret")

add_library(curl STATIC IMPORTED)
set_target_properties(curl PROPERTIES IMPORTED_LOCATION
    ${CMAKE_CURRENT_SOURCE_DIR}/../libs/${ANDROID_ABI}/libcurl.a)

if (NOT MPV_LIBS_DIR)
    set(MPV_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build/mpv-libs")
endif()
add_library(mpv SHARED IMPORTED)
set_target_properties(mpv PROPERTIES IMPORTED_LOCATION
    "${MPV_LIBS_DIR}/${ANDROID_ABI}/libmpv.so")

find_library(Z_LIB z)

# Build NX-torrent-player shared library
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_VERSION="1.3"
    UPDATE_API_URL="${UPDATE_API_URL}"
    UPDATE_TAG_URL="${UPDATE_TAG_URL}"
    TMDB_API_KEY="${TMDB_API_KEY}"
    TMDB_PROXY_URL="${TMDB_PROXY_URL}"
    GDRIVE_CLIENT_ID="${GDRIVE_CLIENT_ID}"
    GDRIVE_CLIENT_SECRET="${GDRIVE_CLIENT_SECRET}"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${ROOT_DIR}/pctest/compat
    ${ROOT_DIR}/app
    ${ROOT_DIR}/engine
    ${ROOT_DIR}/include
    ${BOREALIS_LIBRARY}/include
)

target_link_options(${PROJECT_NAME} PRIVATE -Wl,--exclude-libs,ALL)

target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis
    SDL2
    mpv
    curl
    ${Z_LIB}
    ${ANDROID_LIB}
    ${LOG_LIB}
    ${EGL_LIB}
    ${GLES3_LIB}
    ${DL_LIB}
)
