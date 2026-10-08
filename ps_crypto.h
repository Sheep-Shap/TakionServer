#pragma once
// Криптография регистрации и /sess/ctrl (порт rpcrypt_regist.py и ps_full_server.py).
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps {

using Key16 = std::array<uint8_t, 16>;

constexpr size_t kRegistInnerOffset = 0x1E0;

struct RegistKeys {
    int key0_off = 0;
    int key1_off = 0;
    Key16 ambassador{};
    Key16 bright{};
    Key16 iv{};
    std::vector<uint8_t> inner_encrypted;
};

// payload — тело POST /sess/rgst (не короче kRegistInnerOffset байт).
bool parse_regist_payload(const uint8_t* payload, size_t size, bool is_ps5, uint32_t pin, RegistKeys& out);

// HMAC-SHA256(ambassador || counter_be64)[:16]
Key16 generate_iv(const Key16& ambassador, uint64_t counter, bool is_ps5);

// Ключи для /sess/ctrl (PS5): ambassador и bright из nonce и RP-Key ("morning").
void ctrl_keys_ps5(const Key16& nonce, const Key16& morning, Key16& ambassador, Key16& bright);

// AES-128-CFB (CFB128).
std::vector<uint8_t> aes128_cfb(const Key16& key, const Key16& iv, const uint8_t* data, size_t size, bool encrypt);

std::string base64_encode(const uint8_t* data, size_t size);
std::vector<uint8_t> base64_decode(const std::string& text);
std::string to_hex(const uint8_t* data, size_t size);
bool random_bytes(uint8_t* out, size_t size);

}  // namespace ps
