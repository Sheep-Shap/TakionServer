#pragma once
// Layout of a Takion v12 AUDIO packet (one Opus frame = one unit, no FEC):
//   0x00       0x03 (AUDIO)
//   0x01-0x02  packet_index (u16 BE)
//   0x03-0x04  frame_index  (u16 BE)
//   0x05-0x08  dword2 = unit_index<<24 | (units_total-1)<<16 | unit_size<<8 | fec_units<<4 | source_units
//   0x09       codec = 5
//   0x0A-0x0D  GMAC (zero while the MAC is being computed)
//   0x0E-0x11  key_pos (u32 BE)
//   0x12       0
//   0x13       0 (bit 0x02 = haptics)
//   0x14...    encrypted Opus frame (unit_size bytes)
#include <cstddef>
#include <cstdint>

constexpr size_t kAudioHeaderSize = 0x14;
constexpr uint8_t kAudioCodecOpus = 5;

// pkt must have room for kAudioHeaderSize + unit_size bytes and be zero-initialised.
inline void fill_audio_header(uint8_t* pkt, uint16_t packet_index, uint16_t frame_index, uint8_t unit_size) {
    pkt[0] = 0x03;
    pkt[1] = static_cast<uint8_t>(packet_index >> 8);
    pkt[2] = static_cast<uint8_t>(packet_index);
    pkt[3] = static_cast<uint8_t>(frame_index >> 8);
    pkt[4] = static_cast<uint8_t>(frame_index);
    const uint32_t dword2 = (0u << 24) | (0u << 16) | (static_cast<uint32_t>(unit_size) << 8) | (0u << 4) | 1u;
    pkt[5] = static_cast<uint8_t>(dword2 >> 24);
    pkt[6] = static_cast<uint8_t>(dword2 >> 16);
    pkt[7] = static_cast<uint8_t>(dword2 >> 8);
    pkt[8] = static_cast<uint8_t>(dword2);
    pkt[9] = kAudioCodecOpus;
}
