#pragma once

#include <cstdint>
#include <vector>

// takion_proto.h
#pragma once

namespace TakionConstants {
    constexpr uint16_t PORT = 9296;
    constexpr uint32_t ARWND = 0x19000;
    constexpr uint16_t OUTBOUND_STREAMS = 0x64;
    constexpr uint16_t INBOUND_STREAMS = 0x64;
    
    constexpr uint8_t CHUNK_DATA = 0x00;
    constexpr uint8_t CHUNK_INIT = 0x01;
    constexpr uint8_t CHUNK_INIT_ACK = 0x02;
    constexpr uint8_t CHUNK_DATA_ACK = 0x03;
    constexpr uint8_t CHUNK_COOKIE = 0x0A;
    constexpr uint8_t CHUNK_COOKIE_ACK = 0x0B;
    
    constexpr uint8_t VIDEO_CODEC_H265 = 1;
    constexpr uint8_t AUDIO_CODEC_ID = 2;
    
    constexpr int VIDEO_WIDTH = 1280;
    constexpr int VIDEO_HEIGHT = 720;
    constexpr int VIDEO_FPS = 30;
    constexpr int VIDEO_UNIT_HEVC_SIZE = 4;
    
    constexpr uint8_t MESSAGE_TYPE_STREAMINFO = 1;
    constexpr uint8_t MESSAGE_TYPE_STREAMINFO_ACK = 2;
    constexpr uint8_t MESSAGE_TYPE_BIG = 3;
    constexpr uint8_t MESSAGE_TYPE_DISCONNECT = 4;
    constexpr uint8_t MESSAGE_TYPE_IDR_REQUEST = 5;
}

// Takion message types (protobuf)
constexpr int MESSAGE_TYPE_STREAMINFO = 0;
constexpr int MESSAGE_TYPE_STREAMINFO_ACK = 14;
constexpr int MESSAGE_TYPE_BIG = 0x0F;
constexpr int MESSAGE_TYPE_DISCONNECT = 8;
constexpr int MESSAGE_TYPE_IDR_REQUEST = 25;

// HEVC header (из Python-версии, первый IDR)
extern const std::vector<uint8_t> HEVC_HEADER;
extern const std::vector<uint8_t> FIRST_IDR;

// Protobuf helpers (minimal varint encoding)
std::vector<uint8_t> encode_varint(uint64_t value);
std::vector<uint8_t> encode_bytes_field(uint8_t field_number, const std::vector<uint8_t>& data);
std::vector<uint8_t> encode_uint_field(uint8_t field_number, uint64_t value);

// Build StreamInfo protobuf
std::vector<uint8_t> build_stream_info(const std::vector<uint8_t>& hevc_header);

// Build Takion message header
std::vector<uint8_t> build_message_header(uint32_t msg_tag, uint8_t chunk_type, 
                                          uint8_t chunk_flags, const std::vector<uint8_t>& payload);