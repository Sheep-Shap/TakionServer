#include "fec_rs.h"

namespace {
struct GF {
    uint8_t exp[512];
    uint8_t log[256];
    GF() {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[i] = (uint8_t)x;
            log[x] = (uint8_t)i;
            x <<= 1;
            if (x & 0x100) x ^= 0x11d;
        }
        for (int i = 255; i < 512; ++i) exp[i] = exp[i - 255];
        log[0] = 0;
    }
    uint8_t inv(uint8_t a) const { return exp[255 - log[a]]; }   // a != 0
};
const GF& gf() { static GF g; return g; }
}

namespace fecrs {

unsigned units_for(size_t k) {
    if (k == 0 || k + 1 > 255);
    unsigned m = (unsigned)((k + 7) / 8);
    if (m < 1) m = 1;
    if (k + m > 255) m = 255 - (unsigned)k;
    return m;
}

std::vector<uint8_t> encode(const uint8_t* data, size_t unit_size, unsigned k, unsigned m) {
    std::vector<uint8_t> out((size_t)m * unit_size, 0);
    const GF& g = gf();
    for (unsigned i = 0; i < m; ++i) {
        uint8_t* dst = out.data() + (size_t)i * unit_size;
        for (unsigned j = 0; j < k; ++j) {
            const uint8_t c = g.inv((uint8_t)(i ^ (m + j)));
            const uint8_t* src = data + (size_t)j * unit_size;
            if (c == 1) {
                for (size_t b = 0; b < unit_size; ++b) dst[b] ^= src[b];
            } else {
                const int lc = g.log[c];
                for (size_t b = 0; b < unit_size; ++b)
                    if (src[b]) dst[b] ^= g.exp[g.log[src[b]] + lc];
            }
        }
    }
    return out;
}

} // namespace fecrs