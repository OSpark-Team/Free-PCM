# the minimum version of CMake.
cmake_minimum_required(VERSION 3.5.0)
project(free_pcm)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include)

add_library(library SHARED
    napi_init.cpp
    audio_decoder.cpp
    pcm_equalizer.cpp)
target_link_libraries(library PUBLIC libace_napi.z.so)
target_link_libraries(library PUBLIC libhilog_ndk.z.so)
target_link_libraries(library PUBLIC libnative_media_codecbase.so)
target_link_libraries(library PUBLIC libnative_media_core.so)
target_link_libraries(library PUBLIC libnative_media_acodec.so)
target_link_libraries(library PUBLIC libnative_media_avdemuxer.so)
target_link_libraries(library PUBLIC libnative_media_avsource.so)
