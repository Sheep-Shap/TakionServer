#pragma once
#include "video_encoder.h"
#include <atomic>
#include <memory>

class NvencEncoder : public IVideoEncoder {
public:
    NvencEncoder();
    ~NvencEncoder() override;
    bool initialize(int width, int height, int fps, int bitrate_bps, int max_bitrate_bps) override;
    void shutdown() override;
    bool encode_frame(const void* nv12_data, int pitch, std::vector<uint8_t>& out) override;
    void force_idr() override { idr_requested_ = true; }
    bool set_bitrate(int bitrate_bps, int max_bitrate_bps) override;
    bool is_initialized() const override { return initialized_; }
    static bool available();   // есть nvEncodeAPI64.dll и видеокарта NVIDIA
    struct Impl;
private:
    std::unique_ptr<Impl> d_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> idr_requested_{false};
    std::vector<uint8_t> header_;
    int width_ = 0, height_ = 0, fps_ = 0, cur_bitrate_ = 0, cur_peak_ = 0;
    uint64_t frame_idx_ = 0;
};