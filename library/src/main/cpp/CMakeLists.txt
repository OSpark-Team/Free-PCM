# the minimum version of CMake.
cmake_minimum_required(VERSION 3.5.0)
project(free_pcm)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include
                    ${NATIVERENDER_ROOT_PATH}/buffer
                    ${NATIVERENDER_ROOT_PATH}/napi
                    ${NATIVERENDER_ROOT_PATH}/types)

# Core decoder modules
add_library(library SHARED
    # NAPI interface
    napi_init.cpp
    napi/napi_utils.cpp
    napi/napi_decoder.cpp
    napi/napi_stream_decoder.cpp

    # Audio decoder
    audio_decoder.cpp
    pcm_equalizer.cpp
    drc_processor.cpp

    # Buffer module
    buffer/ring_buffer.cpp)

target_link_libraries(library PUBLIC libace_napi.z.so)
target_link_libraries(library PUBLIC libhilog_ndk.z.so)
target_link_libraries(library PUBLIC libnative_media_codecbase.so)
target_link_libraries(library PUBLIC libnative_media_core.so)
target_link_libraries(library PUBLIC libnative_media_acodec.so)
target_link_libraries(library PUBLIC libnative_media_avdemuxer.so)
target_link_libraries(library PUBLIC libnative_media_avsource.so)
