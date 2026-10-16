#pragma once
// Run-time encoder options shared by NVENC and AMF. Filled once from server.ini in main().
struct EncoderOptions {
    static inline int  nvenc_preset  = 3;      // 1..7  (NVENC P1..P7)
    static inline int  amf_preset    = 1;      // 0 = speed, 1 = balanced, 2 = quality
    static inline bool intra_refresh = false;  // applies to the active encoder
};
