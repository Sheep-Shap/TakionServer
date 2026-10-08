#pragma once
#include <cstdint>
#include <vector>

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;
    virtual bool initialize(int width, int height, int fps, int bitrate_bps, int max_bitrate_bps) = 0;
    virtual void shutdown() = 0;
    virtual bool encode_frame(const void* nv12_data, int pitch, std::vector<uint8_t>& out) = 0;
    virtual void force_idr() = 0;
    virtual bool set_bitrate(int bitrate_bps, int max_bitrate_bps) = 0;
    virtual bool is_initialized() const = 0;
};