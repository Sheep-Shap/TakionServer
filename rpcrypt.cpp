#include "rpcrypt.h"
#include "crypto.h"
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>

// Ключи из paste.txt (keys_a_ps5 и keys_b_ps5)
static const uint8_t keys_a_ps5[0x70 * 0x20] = {
#include "keys_a_ps5.inc"
};

static const uint8_t keys_b_ps5[0x70 * 0x20] = {
#include "keys_b_ps5.inc"
};

static const uint8_t ps5_keys_0[512] = {
#include "ps5_keys_0.inc"
};

static const uint8_t ps5_keys_1[512] = {
#include "ps5_keys_1.inc"
};

void bright_ambassador_ps5(uint8_t* bright,
                           uint8_t* ambassador,
                           const uint8_t* nonce,
                           const uint8_t* morning) {
    const size_t key_a_index = static_cast<size_t>(nonce[0] >> 3);
    const size_t key_b_index = static_cast<size_t>(nonce[7] >> 3);

    printf("  [bright] key_a_index=%zu key_b_index=%zu\n", key_a_index, key_b_index);
    printf("  [bright] nonce: ");
    for (int i = 0; i < 16; i++) printf("%02x", nonce[i]);
    printf("\n");
    printf("  [bright] morning: ");
    for (int i = 0; i < 16; i++) printf("%02x", morning[i]);
    printf("\n");

    const uint8_t* key_a = keys_a_ps5 + key_a_index * 0x20;
    const uint8_t* key_b = keys_b_ps5 + key_b_index * 0x20;

    for (size_t i = 0; i < 16; ++i) {
        uint8_t value = nonce[i];
        value = static_cast<uint8_t>(value - 0x2d - i);
        ambassador[i] = static_cast<uint8_t>(value ^ key_a[i]);
    }

    for (size_t i = 0; i < 16; ++i) {
        uint8_t value = morning[i];
        value = static_cast<uint8_t>(value + 0x18 + i);
        value = static_cast<uint8_t>(value ^ nonce[i]);
        bright[i] = static_cast<uint8_t>(value ^ key_b[i]);
    }
}

void recover_ambassador_from_aeropause(
    const std::vector<uint8_t>& aeropause,
    size_t key0off,
    size_t key1off,
    uint8_t* ambassador
) {
    if (aeropause.size() < 16 || key0off >= 32 || key1off >= 32) {
        return;
    }

    for (size_t i = 0; i < 16; ++i) {
        const uint8_t k = ps5_keys_1[i * 0x20 + key1off];
        uint8_t a = static_cast<uint8_t>(aeropause[i] - k + 0x2d + i);
        ambassador[i] = a;
    }
}

bool extract_ps5_aeropause(const std::vector<uint8_t>& payload,
                            std::vector<uint8_t>& aeropause,
                            size_t& key0off,
                            size_t& key1off) {
    constexpr size_t kAeropauseHighOffset = 0xc7;
    constexpr size_t kKey0Offset = 0x18d;
    constexpr size_t kAeropauseLowOffset = 0x191;

    printf("  [extract_ps5_aeropause] payload.size() = %zu\n", payload.size());
    printf("  [extract_ps5_aeropause] required min = %zu\n", kAeropauseLowOffset + 8);

    if (payload.size() < kAeropauseHighOffset + 8 ||
        payload.size() < kAeropauseLowOffset + 8) {
        printf("  [extract_ps5_aeropause] size check failed\n");
        return false;
    }

    key0off = static_cast<size_t>(payload[kKey0Offset] & 0x1f);
    key1off = static_cast<size_t>(payload[0] >> 3);

    aeropause.resize(16);

    for (size_t i = 0; i < 8; ++i) {
        aeropause[i] = payload[kAeropauseLowOffset + i];
        aeropause[8 + i] = payload[kAeropauseHighOffset + i];
    }

    printf("  [extract_ps5_aeropause] key0off=%zu key1off=%zu\n", key0off, key1off);
    return true;
}

bool parse_regist_payload(const std::vector<uint8_t>& payload,
                          RegistPayload& out,
                          bool is_ps5) {
    constexpr size_t bright_offset = 0xc7;
    constexpr size_t bright_size = 8;
    constexpr size_t iv_offset = 0x191;
    constexpr size_t iv_size = 8;
    constexpr size_t encrypted_offset = 0x1e0;

    if (payload.size() < encrypted_offset) {
        std::cerr << "Regist payload too small: "
                  << payload.size() << " bytes\n";
        return false;
    }

    out.bright_key.assign(
        payload.begin() + bright_offset,
        payload.begin() + bright_offset + bright_size
    );

    out.iv.assign(
        payload.begin() + iv_offset,
        payload.begin() + iv_offset + iv_size
    );

    out.inner_encrypted.assign(
        payload.begin() + encrypted_offset,
        payload.end()
    );

    return true;
}

std::vector<uint8_t> decrypt_inner(const std::vector<uint8_t>& bright_key,
                                    const std::vector<uint8_t>& iv,
                                    const std::vector<uint8_t>& encrypted) {
    // AES-CFB decrypt = encrypt (CFB symmetric)
    return aes_cfb_encrypt(bright_key, iv, encrypted);
}

std::vector<uint8_t> aeropause_ps5(
    const std::vector<uint8_t>& ambassador,
    size_t key1off
) {
    if (ambassador.size() < 16 || key1off >= 32) {
        return {};
    }

    std::vector<uint8_t> aeropause(16);

    for (size_t i = 0; i < 16; ++i) {
        const uint8_t k = ps5_keys_1[i * 0x20 + key1off];
        aeropause[i] = static_cast<uint8_t>(
            ambassador[i] + k - 0x2d - i
        );
    }

    return aeropause;
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');

    for (uint8_t byte : bytes) {
        stream << std::setw(2)
               << static_cast<unsigned int>(byte);
    }

    return stream.str();
}

std::vector<uint8_t> encrypt_outer(const std::vector<uint8_t>& bright_key,
                                    const std::vector<uint8_t>& iv,
                                    const std::vector<uint8_t>& plaintext) {
    return aes_cfb_encrypt(bright_key, iv, plaintext);
}

std::vector<uint8_t> build_regist_response(
    const std::vector<uint8_t>& regist_key,
    const std::vector<uint8_t>& rp_key,
    const std::vector<uint8_t>& mac,
    const std::string& nickname,
    bool is_ps5
) {
    const std::string prefix = is_ps5 ? "PS5" : "PS4";

    const std::string response =
        prefix + "-RegistKey:" + bytes_to_hex(regist_key) + "\n" +
        "RP-KeyType:0\n" +
        "RP-Key:" + bytes_to_hex(rp_key) + "\n" +
        prefix + "-Mac:" + bytes_to_hex(mac) + "\n" +
        prefix + "-Nickname:" + nickname + "\n" +
        "\n";

    return std::vector<uint8_t>(response.begin(), response.end());
}