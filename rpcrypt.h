#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Chiaki Target enum
enum class ChiakiTarget : uint32_t {
    CHIAKI_TARGET_PS5_UNKNOWN = 0,
    CHIAKI_TARGET_PS5_1 = 0x10000,
    CHIAKI_TARGET_PS4_10 = 0xa0000,
    CHIAKI_TARGET_PS4_9 = 0x90000,
    CHIAKI_TARGET_PS4_8 = 0x80000,
};

// RPCrypt structure
struct ChiakiRPCrypt {
    uint8_t ambassador[16];
    uint8_t bright[16];
};

// Bright Ambassador function (из paste.txt)
void bright_ambassador_ps5(uint8_t* bright, uint8_t* ambassador, 
                           const uint8_t* nonce, const uint8_t* morning);

// Parse regist payload
struct RegistPayload {
    std::vector<uint8_t> bright_key;
    std::vector<uint8_t> iv;
    std::vector<uint8_t> inner_encrypted;
};

bool parse_regist_payload(const std::vector<uint8_t>& payload, 
                          RegistPayload& out, bool is_ps5);

bool extract_ps5_aeropause(const std::vector<uint8_t>& payload,
                            std::vector<uint8_t>& aeropause,
                            size_t& key0off,
                            size_t& key1off);

// Decrypt inner payload
std::vector<uint8_t> decrypt_inner(const std::vector<uint8_t>& bright_key,
                                    const std::vector<uint8_t>& iv,
                                    const std::vector<uint8_t>& encrypted);

// Encrypt outer response
std::vector<uint8_t> encrypt_outer(const std::vector<uint8_t>& bright_key,
                                    const std::vector<uint8_t>& iv,
                                    const std::vector<uint8_t>& plaintext);

// PS5 Aeropause                                    
std::vector<uint8_t> aeropause_ps5(
    const std::vector<uint8_t>& ambassador,
    size_t key1off
);

std::string bytes_to_hex(const std::vector<uint8_t>& bytes);

// Build regist response
std::vector<uint8_t> build_regist_response(const std::vector<uint8_t>& regist_key,
                                            const std::vector<uint8_t>& rp_key,
                                            const std::vector<uint8_t>& mac,
                                            const std::string& nickname,
                                            bool is_ps5);