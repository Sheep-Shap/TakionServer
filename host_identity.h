#pragma once
// Stable identity of this PC as a "console": the same 6-byte MAC is sent in the registration reply
// (PS5-Mac) and shown as host-id in the discovery reply, so clients recognise one console instead of
// creating a new entry on every registration.
#include <cstdint>
#include <cstdio>
#include <string>

inline uint64_t host_identity_hash(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;                       // FNV-1a
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

// nickname + rp_key (lower-case hex) -> locally administered unicast MAC
inline void derive_host_mac(const std::string& nickname, const std::string& rp_key_hex, uint8_t mac[6]) {
    const uint64_t h = host_identity_hash(nickname + "|" + rp_key_hex);
    for (int i = 0; i < 6; ++i) mac[i] = uint8_t((h >> (8 * i)) & 0xFF);
    mac[0] = uint8_t((mac[0] | 0x02) & 0xFE);
}

inline std::string host_id_from_mac(const uint8_t mac[6]) {
    char b[16];
    std::snprintf(b, sizeof(b), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return b;
}
