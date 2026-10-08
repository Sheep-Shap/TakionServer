#pragma once

#include <vector>
#include <cstdint>
#include <atomic>
#include "video_encoder.h"

// Forward declare AMF interfaces
namespace amf {
class AMFContext;
class AMFComponent;
class AMFBuffer;
class AMFSurface;
}

class AMFEncoder : public IVideoEncoder {
public:
    AMFEncoder();
    ~AMFEncoder() override;

    bool initialize(int width, int height, int fps, int bitrate_bps, int max_bitrate_bps) override;
    void shutdown() override;

    // Submit frame (NV12 from DXGI capture)
    bool encode_frame(const void* nv12_data, int pitch,
                      std::vector<uint8_t>& out_hevc_frame) override;

    // Force IDR on next frame
    void force_idr() override;

    bool set_bitrate(int bitrate_bps, int max_bitrate_bps) override;

    bool is_initialized() const override { return initialized_; }

private:
    int cur_bitrate_ = 0;
    int cur_peak_ = 0;
    bool create_context();
    bool create_encoder(int width, int height, int fps, int bitrate_bps, int max_bitrate_bps);

    amf::AMFContext* context_ = nullptr;
    amf::AMFComponent* encoder_ = nullptr;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> idr_requested_{false};

    int width_ = 0;
    int height_ = 0;
    int fps_ = 0;
};