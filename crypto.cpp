// crypto.cpp
#define NOMINMAX
#include "crypto.h"
#include "ps5_auth_keys.h"
#include <windows.h>
#include <bcrypt.h>
#include <iostream>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cctype>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>
#include <openssl/ecdh.h>


#pragma comment(lib, "bcrypt.lib")

// Таблица ps5_keys_0 для вычисления bright
static const uint8_t ps5_keys_0[512] = {
    0x24, 0xd8, 0xc2, 0x69, 0x4c, 0x67, 0x78, 0x71, 0xee, 0x31, 0xbd, 0x2b, 0x83, 0xb2, 0x1d, 0x61,
    0xc9, 0xa7, 0x8e, 0xed, 0x9a, 0xd3, 0x6a, 0x6b, 0x5c, 0xc8, 0x35, 0x79, 0xa7, 0x24, 0xe2, 0x17,
    0x06, 0x60, 0x2e, 0xdf, 0xf4, 0xdb, 0x27, 0x10, 0x55, 0xd9, 0xea, 0x16, 0x4e, 0x90, 0x0c, 0xbf,
    0x40, 0x6f, 0x54, 0xa5, 0x31, 0x70, 0x2d, 0x5d, 0x1e, 0x27, 0xdf, 0x37, 0x40, 0xba, 0x9d, 0x5d,
    0xff, 0xe1, 0x05, 0x70, 0x80, 0xd4, 0xb7, 0xc2, 0x96, 0x7f, 0x2f, 0x42, 0xeb, 0x5a, 0x08, 0xde,
    0xc1, 0xb5, 0x52, 0x15, 0xf6, 0xb5, 0xf2, 0xd9, 0x69, 0xa5, 0xc7, 0xc4, 0x7f, 0x46, 0x64, 0xa4,
    0xfd, 0x46, 0x98, 0xa7, 0xe1, 0x2a, 0x8e, 0x6f, 0xaf, 0x65, 0x42, 0x28, 0xb9, 0xc2, 0x6f, 0x3e,
    0xe3, 0xe4, 0x4e, 0xe4, 0x5b, 0x9d, 0x60, 0x10, 0xb8, 0x5a, 0xb0, 0x7d, 0x04, 0x0c, 0x4c, 0x24,
    0x78, 0xbd, 0xb8, 0xba, 0xdb, 0x8f, 0xe3, 0xa0, 0x75, 0x6d, 0x28, 0xc2, 0x33, 0x5b, 0x32, 0x83,
    0xdd, 0x51, 0xb0, 0xa5, 0x8d, 0x09, 0x66, 0xe4, 0x5c, 0xb8, 0x70, 0x0b, 0xe6, 0x82, 0x14, 0xb6,
    0xd2, 0xb0, 0xc2, 0xe0, 0x55, 0xf3, 0x84, 0xad, 0x9d, 0x3a, 0xf8, 0x77, 0xf5, 0x9d, 0x9a, 0xa9,
    0x7d, 0xf1, 0x45, 0x1b, 0x9b, 0x55, 0x25, 0xd8, 0xc1, 0xff, 0x03, 0xa5, 0x48, 0x0b, 0x1b, 0x19,
    0x0c, 0xbd, 0xe0, 0xcd, 0x48, 0xf3, 0x2c, 0x99, 0x19, 0xd6, 0xb8, 0xbb, 0xd6, 0x35, 0x43, 0x6f,
    0x71, 0xe3, 0xef, 0x3e, 0x97, 0xb8, 0xe9, 0x40, 0xa8, 0x47, 0xe0, 0xe0, 0x01, 0x16, 0x9d, 0xa7,
    0xe5, 0x94, 0x4b, 0x1d, 0xd2, 0x80, 0xa2, 0x7f, 0xf2, 0x98, 0x10, 0x38, 0x0d, 0xb8, 0x56, 0xc3,
    0x7a, 0x4b, 0x4c, 0x85, 0xec, 0x2f, 0x23, 0x89, 0xaf, 0xd5, 0xba, 0x9a, 0xad, 0xb0, 0x61, 0x9c,
    0x51, 0xb4, 0x6d, 0x02, 0x49, 0x26, 0xa4, 0x34, 0x84, 0x20, 0x35, 0x30, 0x23, 0x0a, 0x47, 0x14,
    0x32, 0x1a, 0x96, 0x0e, 0xe8, 0x0f, 0x96, 0x96, 0xd4, 0xba, 0x68, 0x3a, 0x67, 0x15, 0x74, 0xe0,
    0xd6, 0x60, 0x4c, 0x68, 0x50, 0x73, 0x14, 0x2f, 0x11, 0x59, 0xac, 0xc8, 0x32, 0xd1, 0xdb, 0x4c,
    0x8a, 0x94, 0x75, 0x33, 0x61, 0xd1, 0xd4, 0xfd, 0xaa, 0x6a, 0x61, 0x68, 0xd8, 0xae, 0x31, 0x4f,
    0xb8, 0x07, 0x7b, 0x27, 0x0f, 0xf9, 0x0b, 0xb0, 0xc2, 0x64, 0xb3, 0x72, 0xea, 0x8b, 0x87, 0x40,
    0x09, 0xb4, 0x82, 0xb4, 0xad, 0x76, 0xf9, 0x36, 0x05, 0x60, 0x89, 0xc8, 0x20, 0xeb, 0xa5, 0xf1,
    0x51, 0x0b, 0x27, 0xa7, 0xf0, 0x76, 0x84, 0x96, 0xeb, 0xb1, 0x2e, 0xc2, 0x85, 0x28, 0xbc, 0x48,
    0x34, 0xd4, 0x01, 0x8d, 0x5b, 0x25, 0x54, 0xe0, 0xc4, 0x4f, 0xa0, 0xfa, 0x99, 0x8d, 0x6d, 0x7a,
    0x64, 0xb1, 0xa9, 0x5d, 0xa4, 0xf9, 0xf5, 0x22, 0xeb, 0x9a, 0xf4, 0xa8, 0x7a, 0x78, 0x4b, 0x7f,
    0xe2, 0x8b, 0x04, 0x50, 0x43, 0x7d, 0x26, 0x2d, 0x19, 0x98, 0x38, 0x6a, 0x4f, 0x2d, 0x30, 0x15,
    0x2e, 0x4f, 0xcd, 0xb9, 0xce, 0x9e, 0x8d, 0x12, 0xc9, 0xfe, 0x33, 0x8b, 0x84, 0xce, 0x5b, 0x40,
    0xe3, 0x7f, 0x72, 0x6d, 0x6c, 0x8a, 0x6a, 0x9e, 0x54, 0xf1, 0xe3, 0x64, 0x5d, 0x6e, 0x7f, 0xac,
    0x1a, 0xe7, 0xf7, 0xfa, 0x00, 0x22, 0xed, 0x2b, 0x23, 0xfa, 0x58, 0xc5, 0xeb, 0x44, 0x92, 0x5d,
    0xcc, 0xaa, 0x82, 0x9f, 0x23, 0xfb, 0xa6, 0xc9, 0x65, 0x2a, 0xe0, 0x79, 0x12, 0x65, 0x2c, 0x34,
    0xc5, 0x23, 0x16, 0xc9, 0xcc, 0x05, 0x30, 0xf3, 0x96, 0x0b, 0x90, 0x67, 0x1a, 0xa7, 0x69, 0x4c,
    0x3e, 0x43, 0x24, 0x9d, 0x4e, 0x68, 0xbd, 0x8b, 0x75, 0x6e, 0x9d, 0x07, 0x6f, 0x1a, 0x6a, 0xba
};

static const uint8_t HMAC_KEY_PS5[16] = {
    0x46, 0x46, 0x87, 0xB3,
    0x49, 0xCA, 0x8C, 0xE8,
    0x59, 0xC5, 0x27, 0x0F,
    0x5D, 0x7A, 0x69, 0xD6
};

bool ecdh_secp256k1(
    const std::vector<uint8_t>& server_private_key,
    const std::vector<uint8_t>& remote_public_key,
    std::vector<uint8_t>& shared_secret_out
) {
    std::cerr << "[crypto] ecdh_secp256k1: ENTERED, priv_size="
              << server_private_key.size()
              << ", pub_size=" << remote_public_key.size()
              << "\n";

    shared_secret_out.clear();

    if (server_private_key.size() != 32) {
        std::cerr << "[crypto] ecdh_secp256k1: server_private_key size="
                  << server_private_key.size() << " (expected 32)\n";
        return false;
    }

    if (remote_public_key.size() != 65 || remote_public_key[0] != 0x04) {
        std::cerr << "[crypto] ecdh_secp256k1: remote_public_key size="
                  << remote_public_key.size()
                  << ", first_byte=0x" << std::hex
                  << static_cast<int>(remote_public_key.empty() ? 0 : remote_public_key[0])
                  << std::dec << " (expected 65 bytes, starting 0x04)\n";
        return false;
    }

    EC_KEY* ec_key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!ec_key) {
        std::cerr << "[crypto] ecdh_secp256k1: EC_KEY_new_by_curve_name failed\n";
        return false;
    }

    BIGNUM* priv_bn = BN_bin2bn(
        server_private_key.data(),
        static_cast<int>(server_private_key.size()),
        nullptr
    );

    if (!priv_bn) {
        std::cerr << "[crypto] ecdh_secp256k1: BN_bin2bn(private) failed\n";
        EC_KEY_free(ec_key);
        return false;
    }

    if (!EC_KEY_set_private_key(ec_key, priv_bn)) {
        std::cerr << "[crypto] ecdh_secp256k1: EC_KEY_set_private_key failed\n";
        BN_free(priv_bn);
        EC_KEY_free(ec_key);
        return false;
    }

    BN_free(priv_bn);

    const EC_GROUP* group = EC_KEY_get0_group(ec_key);

    EC_POINT* remote_point = EC_POINT_new(group);
    if (!remote_point) {
        std::cerr << "[crypto] ecdh_secp256k1: EC_POINT_new failed\n";
        EC_KEY_free(ec_key);
        return false;
    }

    if (!EC_POINT_oct2point(
            group,
            remote_point,
            remote_public_key.data(),
            remote_public_key.size(),
            nullptr
        )) {
        std::cerr << "[crypto] ecdh_secp256k1: EC_POINT_oct2point failed "
                  << "(remote public key is not a valid point on secp256k1)\n";
        EC_POINT_free(remote_point);
        EC_KEY_free(ec_key);
        return false;
    }

    std::vector<uint8_t> secret(32);

    const int secret_len = ECDH_compute_key(
        secret.data(),
        secret.size(),
        remote_point,
        ec_key,
        nullptr
    );

    EC_POINT_free(remote_point);
    EC_KEY_free(ec_key);

    if (secret_len <= 0) {
        std::cerr << "[crypto] ecdh_secp256k1: ECDH_compute_key failed, returned "
                  << secret_len << "\n";
        return false;
    }

    secret.resize(static_cast<size_t>(secret_len));
    shared_secret_out = secret;

    std::cerr << "[crypto] ecdh_secp256k1: success, secret_len=" << secret_len << "\n";

    return true;
}

static bool hex_decode(
    const std::string& hex,
    std::vector<uint8_t>& output
) {
    output.clear();

    if ((hex.size() % 2) != 0) {
        return false;
    }

    auto hex_value = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }

        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }

        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }

        return -1;
    };

    output.reserve(hex.size() / 2);

    for (size_t i = 0; i < hex.size(); i += 2) {
        const int high = hex_value(hex[i]);
        const int low = hex_value(hex[i + 1]);

        if (high < 0 || low < 0) {
            output.clear();
            return false;
        }

        output.push_back(
            static_cast<uint8_t>((high << 4) | low)
        );
    }

    return true;
}

static bool json_get_string(
    const std::string& json,
    const char* field_name,
    std::string& value
) {
    value.clear();

    const std::string key =
        std::string("\"") + field_name + "\"";

    const size_t key_pos = json.find(key);
    if (key_pos == std::string::npos) {
        return false;
    }

    const size_t colon_pos =
        json.find(':', key_pos + key.size());

    if (colon_pos == std::string::npos) {
        return false;
    }

    const size_t quote_start =
        json.find('"', colon_pos + 1);

    if (quote_start == std::string::npos) {
        return false;
    }

    const size_t quote_end =
        json.find('"', quote_start + 1);

    if (quote_end == std::string::npos) {
        return false;
    }

    value = json.substr(
        quote_start + 1,
        quote_end - quote_start - 1
    );

    return true;
}

static int base64_char_value(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }

    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }

    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }

    if (c == '+') {
        return 62;
    }

    if (c == '/') {
        return 63;
    }

    return -1;
}

static std::vector<uint8_t> base64_decode(const std::string& input) {
    std::vector<uint8_t> output;

    if (input.empty()) {
        return output;
    }

    int val = 0;
    int valb = -8;

    for (char c : input) {
        if (c == '=') {
            break;
        }

        const int v = base64_char_value(c);
        if (v < 0) {
            continue;
        }

        val = (val << 6) + v;
        valb += 6;

        if (valb >= 0) {
            output.push_back(
                static_cast<uint8_t>((val >> valb) & 0xFF)
            );
            valb -= 8;
        }
    }

    return output;
}

static void auth_crypt_keys_ps5(
    const std::vector<uint8_t>& nonce,
    const std::vector<uint8_t>& morning,
    std::vector<uint8_t>& ambassador,
    std::vector<uint8_t>& bright
) {
    ambassador.clear();
    bright.clear();

    if (nonce.size() != 16 || morning.size() != 16) {
        return;
    }

    const size_t key_a_offset =
        static_cast<size_t>(nonce[0] >> 3) * 0x70;

    const size_t key_b_offset =
        static_cast<size_t>(nonce[7] >> 3) * 0x70;

    if (key_a_offset + 16 > sizeof(keys_a_ps5) ||
        key_b_offset + 16 > sizeof(keys_b_ps5)) {
        return;
    }

    const uint8_t* key_a = keys_a_ps5 + key_a_offset;
    const uint8_t* key_b = keys_b_ps5 + key_b_offset;

    ambassador.resize(16);
    bright.resize(16);

    for (size_t i = 0; i < 16; ++i) {
        const uint8_t a =
            static_cast<uint8_t>(
                nonce[i] - 0x2d - static_cast<uint8_t>(i)
            );

        ambassador[i] =
            static_cast<uint8_t>(a ^ key_a[i]);

        uint8_t b =
            static_cast<uint8_t>(
                morning[i] + 0x18 + static_cast<uint8_t>(i)
            );

        b ^= nonce[i];
        b ^= key_b[i];

        bright[i] = b;
    }
}

static std::vector<uint8_t> auth_iv_ps5(
    const std::vector<uint8_t>& ambassador,
    uint64_t counter
) {
    if (ambassador.size() != 16) {
        return {};
    }

    static const uint8_t HMAC_KEY_PS5[16] = {
        0x46, 0x46, 0x87, 0xB3,
        0x49, 0xCA, 0x8C, 0xE8,
        0x59, 0xC5, 0x27, 0x0F,
        0x5D, 0x7A, 0x69, 0xD6
    };

    std::vector<uint8_t> key(
        std::begin(HMAC_KEY_PS5),
        std::end(HMAC_KEY_PS5)
    );

    std::vector<uint8_t> payload = ambassador;
    payload.resize(24);

    for (int i = 0; i < 8; ++i) {
        payload[16 + i] = static_cast<uint8_t>(
            (counter >> ((7 - i) * 8)) & 0xff
        );
    }

    // <-- Отладочный вывод ВНУТРИ auth_iv_ps5, после объявления key и payload:
    std::cerr << "[crypto] auth_iv: key.size()=" << key.size()
              << ", payload.size()=" << payload.size()
              << "\n";

    std::vector<uint8_t> full_hmac =
        hmac_sha256(key, payload);

    if (full_hmac.size() < 16) {
        return {};
    }

    return std::vector<uint8_t>(
        full_hmac.begin(),
        full_hmac.begin() + 16
    );
}

bool decrypt_launch_spec_ps5(
    const std::string& encoded_launch_spec,
    const std::vector<uint8_t>& nonce,
    const std::vector<uint8_t>& rp_key,
    std::string& out_json
) {
    out_json.clear();

    if (nonce.size() != 16 || rp_key.size() != 16) {
        std::cerr << "[crypto] launchspec: nonce/rp_key must be 16 bytes\n";
        return false;
    }

    std::cerr << "[crypto] keys_a_ps5 first16=";
    for (size_t i = 0; i < 16; ++i) {
        fprintf(stderr, "%02x", keys_a_ps5[i]);
    }
    fprintf(stderr, "\n");
    
    const std::vector<uint8_t> encrypted =
        base64_decode(encoded_launch_spec);

    if (encrypted.empty()) {
        std::cerr << "[crypto] launchspec: base64 decode failed or is empty\n";
        return false;
    }

    std::vector<uint8_t> ambassador;
    std::vector<uint8_t> bright;

    std::cerr << "[crypto] decrypt_launch_spec: nonce.size()=" << nonce.size()
              << ", rp_key.size()=" << rp_key.size() << "\n";
    
    auth_crypt_keys_ps5(
        nonce,
        rp_key,
        ambassador,
        bright
    );

    if (ambassador.size() != 16 || bright.size() != 16) {
        std::cerr << "[crypto] launchspec: auth_crypt_keys failed\n";
        return false;
    }

    const std::vector<uint8_t> iv =
        auth_iv_ps5(ambassador, 0);

    if (iv.size() != 16) {
        std::cerr << "[crypto] launchspec: auth_iv failed\n";
        return false;
    }

    std::cerr << "[crypto] ambassador=";
    for (auto b : ambassador) { char buf[3]; snprintf(buf, sizeof(buf), "%02x", b); std::cerr << buf; }
    std::cerr << "\n[crypto] bright=";
    for (auto b : bright) { char buf[3]; snprintf(buf, sizeof(buf), "%02x", b); std::cerr << buf; }
    std::cerr << "\n[crypto] iv=";
    for (auto b : iv) { char buf[3]; snprintf(buf, sizeof(buf), "%02x", b); std::cerr << buf; }
    std::cerr << "\n";

    std::cerr << "[crypto] launch_spec base64 length="
              << encoded_launch_spec.size()
              << "\n";
    std::cerr << "[crypto] launch_spec base64 (full)="
              << encoded_launch_spec
              << "\n";

    // AES-CFB encrypt of zero bytes gives the same keystream that the
    // client used before XORing with the JSON.
    const std::vector<uint8_t> zeros(encrypted.size(), 0);

    const std::vector<uint8_t> keystream =
        aes_cfb_encrypt(bright, iv, zeros);

    if (keystream.size() != encrypted.size()) {
        std::cerr << "[crypto] launchspec: keystream generation failed: "
                  << keystream.size()
                  << " != "
                  << encrypted.size()
                  << "\n";
        return false;
    }

    std::vector<uint8_t> plain(encrypted.size());

    for (size_t i = 0; i < encrypted.size(); ++i) {
        plain[i] = static_cast<uint8_t>(
            encrypted[i] ^ keystream[i]
        );
    }

    const auto nul = std::find(plain.begin(), plain.end(), 0);

    out_json.assign(
        plain.begin(),
        nul
    );

    // Безусловная проверка проблемного диапазона (тот, что вызвал сбой раньше)
    static constexpr size_t kFixedDebugOffset = 2352;

    std::cerr << "[crypto] keys_b_ps5[" << kFixedDebugOffset
              << ".." << (kFixedDebugOffset + 16) << "] (C++, fixed)=";

    for (size_t i = kFixedDebugOffset;
         i < kFixedDebugOffset + 16 && i < sizeof(keys_b_ps5);
         ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", keys_b_ps5[i]);
        std::cerr << buf;
    }

    std::cerr << "\n";

    // Плюс текущий динамический диапазон, как и раньше
    const size_t debug_key_b_offset =
        static_cast<size_t>(nonce[7] >> 3) * 0x70;

    std::cerr << "[crypto] keys_b_ps5[" << debug_key_b_offset
              << ".." << (debug_key_b_offset + 16) << "] (C++, dynamic)=";

    for (size_t i = debug_key_b_offset;
         i < debug_key_b_offset + 16 && i < sizeof(keys_b_ps5);
         ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", keys_b_ps5[i]);
        std::cerr << buf;
    }

    std::cerr << "\n";

    std::cerr << "[crypto] encrypted.size()=" << encrypted.size() << "\n";
    std::cerr << "[crypto] encrypted first16=";
    for (size_t i = 0; i < std::min(size_t(16), encrypted.size()); ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", encrypted[i]);
        std::cerr << buf;
    }
    std::cerr << "\n";

    std::cerr << "[crypto] launchspec decrypted first32=";
    const size_t debug_len = (plain.size() < size_t(32)) ? plain.size() : size_t(32);

    for (size_t i = 0; i < debug_len; ++i) {
        fprintf(stderr, "%02x", plain[i]);
    }
    fprintf(stderr, "\n");

    if (out_json.empty() || out_json.front() != '{') {
        std::cerr << "[crypto] launchspec: decrypted data is not JSON\n";
        return false;
    }

    return true;
}

// Вычисление bright для PS5 из key_0_off и pin
std::vector<uint8_t> compute_bright_ps5(uint8_t key_0_off, uint32_t pin) {
    std::vector<uint8_t> bright(16);
    
    for (size_t i = 0; i < 16; i++) {
        bright[i] = ps5_keys_0[i * 32 + key_0_off];
    }
    
    bright[12] ^= (pin >> 24) & 0xff;
    bright[13] ^= (pin >> 16) & 0xff;
    bright[14] ^= (pin >> 8) & 0xff;
    bright[15] ^= pin & 0xff;
    
    return bright;
}

// HMAC-SHA256
static std::vector<uint8_t> hmac_sha256(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data
) {
    if (key.empty()) {
        return {};
    }

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    NTSTATUS status;
    DWORD cbData = 0;
    DWORD cbHashObject = 0;
    std::vector<uint8_t> hashObject;
    std::vector<uint8_t> digest(32);

    status = BCryptOpenAlgorithmProvider(
        &hAlg,
        BCRYPT_SHA256_ALGORITHM,
        NULL,
        BCRYPT_ALG_HANDLE_HMAC_FLAG
    );

    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }

    status = BCryptGetProperty(
        hAlg,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&cbHashObject),
        sizeof(cbHashObject),
        &cbData,
        0
    );

    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    hashObject.resize(cbHashObject);

    // <-- ВСТАВЬТЕ ОТЛАДОЧНЫЙ ВЫВОД СЮДА, внутри функции:

    status = BCryptCreateHash(
        hAlg,
        &hHash,
        hashObject.data(),
        cbHashObject,
        const_cast<PUCHAR>(key.data()),
        static_cast<ULONG>(key.size()),
        0
    );

    if (!BCRYPT_SUCCESS(status)) {
        std::cerr << "[crypto] BCryptCreateHash failed: 0x"
                  << std::hex << status << std::dec
                  << "\n";
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    
    // Hash data
    status = BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    
    // Finish hash
    ULONG hash_len = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len, sizeof(hash_len), &hash_len, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    
    std::vector<uint8_t> hash(hash_len);
    status = BCryptFinishHash(hHash, hash.data(), (ULONG)hash.size(), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    
    return hash;
}

// AES-CFB encrypt
std::vector<uint8_t> aes_cfb_encrypt(const std::vector<uint8_t>& key,
                                      const std::vector<uint8_t>& iv,
                                      const std::vector<uint8_t>& data) {
    if (key.size() != 16 || iv.size() != 16 || data.empty()) {
        return {};
    }

    BCRYPT_ALG_HANDLE h_alg = nullptr;
    BCRYPT_KEY_HANDLE h_key = nullptr;

    NTSTATUS status = BCryptOpenAlgorithmProvider(&h_alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) return {};

    status = BCryptSetProperty(h_alg, BCRYPT_CHAINING_MODE,
                               (PUCHAR)(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)),
                               static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_ECB) + 1) * sizeof(wchar_t)), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(h_alg, 0);
        return {};
    }

    status = BCryptGenerateSymmetricKey(h_alg, &h_key, nullptr, 0,
                                        (PUCHAR)key.data(), static_cast<ULONG>(key.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyKey(h_key);
        BCryptCloseAlgorithmProvider(h_alg, 0);
        return {};
    }

    std::vector<uint8_t> ciphertext(data.size());
    uint8_t block[16];
    memcpy(block, iv.data(), 16);

    for (size_t offset = 0; offset < data.size(); offset += 16) {
        // Шифруем блок
        uint8_t encrypted_block[16];
        ULONG len;
        status = BCryptEncrypt(h_key, block, 16, nullptr, nullptr, 0, encrypted_block, 16, &len, 0);
        if (!BCRYPT_SUCCESS(status)) {
            BCryptDestroyKey(h_key);
            BCryptCloseAlgorithmProvider(h_alg, 0);
            return {};
        }

        // XOR с plaintext (побайтово, в пределах блока)
        for (size_t i = 0; i < 16 && (offset + i) < data.size(); ++i) {
            ciphertext[offset + i] = data[offset + i] ^ encrypted_block[i];
        }

        // Обратная связь: следующий блок = текущий ciphertext (полный блок)
        if (offset + 16 <= data.size()) {
            memcpy(block, ciphertext.data() + offset, 16);
        } else {
            // Последний неполный блок
            memset(block, 0, 16);
            memcpy(block, ciphertext.data() + offset, data.size() - offset);
        }
    }

    BCryptDestroyKey(h_key);
    BCryptCloseAlgorithmProvider(h_alg, 0);

    return ciphertext;  // <-- Вот здесь return, и ничего после!
}


// AES-CFB encrypt
// AES-CFB decrypt
std::vector<uint8_t> aes_cfb_decrypt(const std::vector<uint8_t>& key,
                                      const std::vector<uint8_t>& iv,
                                      const std::vector<uint8_t>& data) {
    BCRYPT_ALG_HANDLE h_alg = nullptr;
    BCRYPT_KEY_HANDLE h_key = nullptr;

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &h_alg,
        BCRYPT_AES_ALGORITHM,
        nullptr,
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        std::cerr << "BCryptOpenAlgorithmProvider failed: 0x"
                  << std::hex << status << std::dec << "\n";
        return {};
    }

    status = BCryptSetProperty(
        h_alg,
        BCRYPT_CHAINING_MODE,
        (PUCHAR)(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CFB)),
        static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_CFB) + 1) * sizeof(wchar_t)),
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(h_alg, 0);
        return {};
    }

    // NTSTATUS BCryptGenerateSymmetricKey(
    //   BCRYPT_ALG_HANDLE  hAlgorithm,
    //   BCRYPT_KEY_HANDLE  *phKey,
    //   PUCHAR             pbKeyObject,
    //   ULONG              cbKeyObject,
    //   PUCHAR             pbSecret,
    //   ULONG              cbSecret,
    //   ULONG              dwFlags
    // );
    status = BCryptGenerateSymmetricKey(
        h_alg,
        &h_key,
        nullptr,
        0,
        (PUCHAR)(const_cast<uint8_t*>(key.data())),
        static_cast<ULONG>(key.size()),
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(h_alg, 0);
        return {};
    }

    std::vector<uint8_t> plain(data.size());
    std::vector<uint8_t> iv_copy = iv;
    ULONG plain_len = 0;

    // NTSTATUS BCryptDecrypt(
    //   BCRYPT_KEY_HANDLE hKey,
    //   PUCHAR            pbInput,
    //   ULONG             cbInput,
    //   void              *pPaddingInfo,
    //   PUCHAR            pbIV,
    //   ULONG             cbIV,
    //   PUCHAR            pbOutput,
    //   ULONG             cbOutput,
    //   ULONG             *pcbResult,
    //   ULONG             dwFlags
    // );
    status = BCryptDecrypt(
        h_key,
        (PUCHAR)(const_cast<uint8_t*>(data.data())),
        static_cast<ULONG>(data.size()),
        nullptr,
        iv_copy.data(),
        static_cast<ULONG>(iv_copy.size()),
        plain.data(),
        static_cast<ULONG>(plain.size()),
        &plain_len,
        0
    );

    BCryptDestroyKey(h_key);
    BCryptCloseAlgorithmProvider(h_alg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        std::cerr << "BCryptDecrypt failed: 0x"
                  << std::hex << status << std::dec << "\n";
        return {};
    }

    plain.resize(plain_len);
    return plain;
}

// SHA256 hash
std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS status;
    
    // Open SHA256 algorithm
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        std::cerr << "BCryptOpenAlgorithmProvider failed: " << std::hex << status << "\n";
        return {};
    }
    
    // Create hash
    ULONG hash_obj_len = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hash_obj_len, sizeof(hash_obj_len), &hash_obj_len, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    
    std::vector<uint8_t> hash_obj(hash_obj_len);
    
    status = BCryptCreateHash(hAlg, &hHash, hash_obj.data(), (ULONG)hash_obj.size(), nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    
    // Hash data
    status = BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    
    // Finish hash
    ULONG hash_len = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len, sizeof(hash_len), &hash_len, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    
    std::vector<uint8_t> hash(hash_len);
    status = BCryptFinishHash(hHash, hash.data(), (ULONG)hash.size(), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }
    
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    
    return hash;
}


// ECDH implementation (placeholder - needs proper SECP256K1)
ECDH::ECDH() {
    // Generate random private key (placeholder)
    private_key_.resize(32);
    for (int i = 0; i < 32; ++i) {
        private_key_[i] = (uint8_t)(i + 1);
    }
    
    // Compute public key (placeholder - needs proper EC point multiplication)
    public_key_.resize(65);
    public_key_[0] = 0x04; // Uncompressed point
    memcpy(public_key_.data() + 1, private_key_.data(), 32);
    memset(public_key_.data() + 33, 0, 32);
}

std::vector<uint8_t> ECDH::compute_shared_secret(const std::vector<uint8_t>& remote_public_key) {
    // Placeholder - needs proper ECDH on SECP256K1
    return std::vector<uint8_t>(remote_public_key.begin(), remote_public_key.begin() + 32);
}