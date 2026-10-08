#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

class GKCrypt {
public:
    GKCrypt(uint8_t index, const std::vector<uint8_t>& hs_key,
            const std::vector<uint8_t>& ecdh_secret);
    bool gmac(uint64_t keypos, const uint8_t* buf, size_t len, uint8_t out[4]) const;
    void crypt(uint64_t keypos, uint8_t* buf, size_t len) const;
private:
    uint8_t key_[16];
    uint8_t iv_[16];
    static void derive_gmac(uint64_t index, const uint8_t* base, const uint8_t* iv, uint8_t out[16]);
    static void counter_add(uint8_t* out, const uint8_t* base, uint64_t v);
    void gmac_key(uint64_t index, uint8_t out[16]) const;
};