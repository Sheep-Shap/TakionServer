#include "ps_crypto.h"
#include "ps_keys.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cstring>

namespace ps {

namespace {

const uint8_t kHmacKeyPs5[16] = {0x46, 0x46, 0x87, 0xb3, 0x49, 0xca, 0x8c, 0xe8,
                                 0x59, 0xc5, 0x27, 0x0f, 0x5d, 0x7a, 0x69, 0xd6};
const uint8_t kHmacKeyPs4[16] = {0x20, 0xd6, 0x6f, 0x59, 0x04, 0xea, 0x7c, 0x14,
                                 0xe5, 0x57, 0xff, 0xc5, 0x2e, 0x48, 0x8a, 0xc8};

// wurzelbert из ПРЯМОЙ функции chiaki_rpcrypt_aeropause (-0x2d для PS5 даёт 0xD3 по модулю 256)
constexpr uint8_t kWurzelbertFwdPs5 = static_cast<uint8_t>(-0x2d);
constexpr uint8_t kWurzelbertFwdPs4 = 0x29;

}  // namespace

Key16 generate_iv(const Key16& ambassador, uint64_t counter, bool is_ps5) {
    uint8_t buf[24];
    std::memcpy(buf, ambassador.data(), 16);
    for (int i = 0; i < 8; ++i) buf[16 + i] = static_cast<uint8_t>(counter >> (56 - 8 * i));
    const uint8_t* key = is_ps5 ? kHmacKeyPs5 : kHmacKeyPs4;
    uint8_t digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key, 16, buf, sizeof(buf), digest, &len);
    Key16 iv{};
    std::memcpy(iv.data(), digest, 16);
    return iv;
}

bool parse_regist_payload(const uint8_t* p, size_t size, bool is_ps5, uint32_t pin, RegistKeys& out) {
    if (!p || size < kRegistInnerOffset) return false;

    out.key0_off = p[0x18D] & 0x1F;
    out.key1_off = p[0] >> 3;

    uint8_t aeropause[16];
    std::memcpy(aeropause, p + 0x191, 8);
    std::memcpy(aeropause + 8, p + 0xC7, 8);

    const uint8_t* keys0 = is_ps5 ? PS5_KEYS_0 : PS4_KEYS_0;
    const uint8_t* keys1 = is_ps5 ? PS5_KEYS_1 : PS4_KEYS_1;
    const uint8_t w = is_ps5 ? kWurzelbertFwdPs5 : kWurzelbertFwdPs4;

    for (int i = 0; i < 16; ++i) {
        const uint8_t k = keys1[i * 0x20 + out.key1_off];
        out.ambassador[i] = static_cast<uint8_t>((static_cast<uint8_t>(aeropause[i] - w - i)) ^ k);
        out.bright[i] = keys0[i * 0x20 + out.key0_off];
    }
    out.bright[0xC] ^= static_cast<uint8_t>(pin >> 24);
    out.bright[0xD] ^= static_cast<uint8_t>(pin >> 16);
    out.bright[0xE] ^= static_cast<uint8_t>(pin >> 8);
    out.bright[0xF] ^= static_cast<uint8_t>(pin);

    out.iv = generate_iv(out.ambassador, 0, is_ps5);
    out.inner_encrypted.assign(p + kRegistInnerOffset, p + size);
    return true;
}

void ctrl_keys_ps5(const Key16& nonce, const Key16& morning, Key16& ambassador, Key16& bright) {
    const uint8_t* key_a = KEYS_A_PS5 + (nonce[0] >> 3) * 0x70;
    const uint8_t* key_b = KEYS_B_PS5 + (nonce[7] >> 3) * 0x70;
    for (int i = 0; i < 16; ++i) {
        ambassador[i] = static_cast<uint8_t>(static_cast<uint8_t>(nonce[i] - 0x2d - i) ^ key_a[i]);
        bright[i] = static_cast<uint8_t>(static_cast<uint8_t>(morning[i] + 0x18 + i) ^ nonce[i] ^ key_b[i]);
    }
}

std::vector<uint8_t> aes128_cfb(const Key16& key, const Key16& iv, const uint8_t* data, size_t size, bool encrypt) {
    std::vector<uint8_t> out(size + 16);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};
    int len = 0, total = 0;
    bool ok = EVP_CipherInit_ex(ctx, EVP_aes_128_cfb128(), nullptr, key.data(), iv.data(), encrypt ? 1 : 0) == 1;
    if (ok && size > 0) {
        ok = EVP_CipherUpdate(ctx, out.data(), &len, data, static_cast<int>(size)) == 1;
        total = len;
    }
    if (ok) {
        ok = EVP_CipherFinal_ex(ctx, out.data() + total, &len) == 1;
        total += len;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return {};
    out.resize(static_cast<size_t>(total));
    return out;
}

std::string base64_encode(const uint8_t* d, size_t n) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s;
    s.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = (d[i] << 16) | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        s += tbl[(v >> 18) & 63];
        s += tbl[(v >> 12) & 63];
        s += (i + 1 < n) ? tbl[(v >> 6) & 63] : '=';
        s += (i + 2 < n) ? tbl[v & 63] : '=';
    }
    return s;
}

std::vector<uint8_t> base64_decode(const std::string& text) {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

std::string to_hex(const uint8_t* d, size_t n) {
    static const char* h = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += h[d[i] >> 4];
        s += h[d[i] & 15];
    }
    return s;
}

bool random_bytes(uint8_t* out, size_t size) {
    return RAND_bytes(out, static_cast<int>(size)) == 1;
}

}  // namespace ps
