// crypto.h
#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>


bool ecdh_secp256k1(
    const std::vector<uint8_t>& server_private_key,
    const std::vector<uint8_t>& remote_public_key,
    std::vector<uint8_t>& shared_secret_out
);


// ECDH key exchange using secp256k1.
class ECDH {
public:
    ECDH();

    const std::vector<uint8_t>& get_public_key() const {
        return public_key_;
    }

    std::vector<uint8_t> compute_shared_secret(
        const std::vector<uint8_t>& remote_public_key
    );

private:
    std::vector<uint8_t> private_key_;
    std::vector<uint8_t> public_key_;
};

// PS5 Bright calculation.
std::vector<uint8_t> compute_bright_ps5(
    uint8_t key_0_off,
    uint32_t pin
);

bool decrypt_launch_spec_ps5(
    const std::string& encoded_launch_spec,
    const std::vector<uint8_t>& nonce,
    const std::vector<uint8_t>& rp_key,
    std::string& out_json
);

bool load_ps_session_state(
    const std::string& filename,
    std::vector<uint8_t>& nonce,
    std::vector<uint8_t>& rp_key
);

// HMAC-SHA256.
std::vector<uint8_t> hmac_sha256(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data
);

// AES-128-CFB.
std::vector<uint8_t> aes_cfb_encrypt(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& data
);

std::vector<uint8_t> aes_cfb_decrypt(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& data
);

// SHA-256.
std::vector<uint8_t> sha256(
    const std::vector<uint8_t>& data
);