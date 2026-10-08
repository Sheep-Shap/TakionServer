cmake_minimum_required(VERSION 3.20)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
project(takion_host LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_MSVC_STATIC_RT TRUE)

find_package(OpenSSL REQUIRED)

# AMD AMF SDK headers
set(AMF_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/AMF/amf/public/include")

# Windows SDK (WinSock, DXGI)
if(WIN32)
    find_package(Threads REQUIRED)
endif()

add_subdirectory(third_party/ViGEmClient)

# Источники
set(SOURCES
    main.cpp
    amf_encoder.cpp
    dxgi_capture.cpp
    takion_server.cpp
    rpcrypt.cpp
    crypto.cpp
    http_client.cpp
    gkcrypt.cpp
    virtual_pad.cpp
    ps_keys.cpp
    ps_crypto.cpp
    full_server.cpp
    audio_streamer.cpp
    audio_capture_win.cpp
    server_config.cpp
    nvenc_encoder.cpp
    fec_rs.cpp
)

set(HEADERS
    amf_encoder.h
    dxgi_capture.h
    takion_server.h
    crypto.h
    takion_proto.h
    http_client.h
    virtual_pad.h
    full_server.h
    ps_crypto.h
    ps_keys.h
    audio_streamer.h
    audio_packet.h
    audio_capture.h
    server_config.h
    bitrate_controller.h
    video_encoder.h
    nvenc_encoder.h
    fec_rs.h
)

include(FetchContent)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(opus GIT_REPOSITORY https://github.com/xiph/opus.git GIT_TAG v1.5.2)
FetchContent_MakeAvailable(opus)

add_executable(takion_host ${SOURCES} ${HEADERS})

target_link_libraries(takion_host PRIVATE
    OpenSSL::Crypto
    OpenSSL::SSL
    Threads::Threads
    ws2_32
    d3d11
    dxgi
    crypt32
    bcrypt
    winmm
    winhttp
    ViGEmClient setupapi
    opus ole32
    advapi32 
    user32 
    gdi32
)

target_include_directories(takion_host PRIVATE
    third_party/ViGEmClient/include
    ${CMAKE_SOURCE_DIR}
    ${AMF_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/nv-codec-headers/include
)

# Копирование AMF DLL (если нужно)
if(WIN32)
   # add_custom_command(TARGET takion_host POST_BUILD
   #     COMMAND ${CMAKE_COMMAND} -E copy_if_different
   #     "${CMAKE_SOURCE_DIR}/third_party/AMF/amf/public/bin/x64/amfrt64.dll"
   #     $<TARGET_FILE_DIR:takion_host>/amfrt64.dll
   # )
endif()

set_target_properties(takion_host PROPERTIES OUTPUT_NAME "takion-host")