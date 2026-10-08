#include "gkcrypt.h"
#include <cstring>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

GKCrypt::GKCrypt(uint8_t index, const std::vector<uint8_t>& hs_key,
                 const std::vector<uint8_t>& ecdh_secret) {
    uint8_t d[21] = {1, index, 0};
    memcpy(d + 3, hs_key.data(), 16);
    d[19] = 1; d[20] = 0;
    uint8_t h[32]; unsigned hl = 32;
    HMAC(EVP_sha256(), ecdh_secret.data(), (int)ecdh_secret.size(), d, sizeof(d), h, &hl);
    memcpy(key_, h, 16);
    memcpy(iv_, h + 16, 16);
}

void GKCrypt::counter_add(uint8_t* out, const uint8_t* base, uint64_t v) {
    size_t i = 0;
    do { uint64_t r = base[i] + v; out[i] = (uint8_t)r; v = r >> 8; ++i; } while (i < 16 && v);
    if (i < 16) memcpy(out + i, base + i, 16 - i);
}

void GKCrypt::derive_gmac(uint64_t index, const uint8_t* base, const uint8_t* iv, uint8_t out[16]) {
    uint8_t d[32], md[32];
    memcpy(d, base, 16);
    counter_add(d + 16, iv, index * 44910);
    SHA256(d, 32, md);
    for (int i = 0; i < 16; ++i) out[i] = md[i] ^ md[i + 16];
}

void GKCrypt::gmac_key(uint64_t index, uint8_t out[16]) const {
    uint8_t base[16];
    derive_gmac(0, key_, iv_, base);          // key_gmac_base: индекс 0 от key_base
    if (index == 0) { memcpy(out, base, 16); return; }
    derive_gmac(index, base, iv_, out);       // индекс >= 1 от key_gmac_base
}

bool GKCrypt::gmac(uint64_t keypos, const uint8_t* buf, size_t len, uint8_t out[4]) const {
    uint8_t iv[16], key[16];
    counter_add(iv, iv_, keypos / 16);
    gmac_key(keypos ? (keypos - 1) / 45000 : 0, key);
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int l = 0;
    bool ok = c
        && EVP_EncryptInit_ex(c, EVP_aes_128_gcm(), nullptr, nullptr, nullptr)
        && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 16, nullptr)
        && EVP_EncryptInit_ex(c, nullptr, nullptr, key, iv)
        && EVP_EncryptUpdate(c, nullptr, &l, buf, (int)len)
        && EVP_EncryptFinal_ex(c, nullptr, &l)
        && EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 4, out);
    EVP_CIPHER_CTX_free(c);
    return ok;
}

void GKCrypt::crypt(uint64_t keypos, uint8_t* buf, size_t len) const {
    uint64_t pre = keypos % 16, blk = keypos / 16;
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(c, EVP_aes_128_ecb(), nullptr, key_, nullptr);
    EVP_CIPHER_CTX_set_padding(c, 0);
    for (size_t off = 0; off < len + pre; off += 16, ++blk) {
        uint8_t ctr[16], ks[16]; int l = 0;
        counter_add(ctr, iv_, blk);
        EVP_EncryptUpdate(c, ks, &l, ctr, 16);
        for (size_t j = 0; j < 16; ++j) {
            size_t i = off + j;
            if (i >= pre && i - pre < len) buf[i - pre] ^= ks[j];
        }
    }
    EVP_CIPHER_CTX_free(c);
}