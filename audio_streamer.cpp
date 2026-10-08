#include "audio_streamer.h"

#if __has_include(<opus/opus.h>)
#include <opus/opus.h>
#else
#include <opus.h>
#endif

#include <chrono>
#include <iostream>
#include <vector>

bool AudioStreamer::start(PacketCb cb) {
    if (running_) return true;
    cb_ = std::move(cb);

    int err = 0;
    enc_ = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (!enc_ || err != OPUS_OK) {
        std::cerr << "[audio] opus_encoder_create failed: " << err << "\n";
        enc_ = nullptr;
        return false;
    }
    opus_encoder_ctl(enc_, OPUS_SET_BITRATE(kBitrate));
    opus_encoder_ctl(enc_, OPUS_SET_VBR(0));
    opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(5));

    capture_ = PcmCapture::create();
    if (!capture_ || !capture_->start()) {
        std::cerr << "[audio] capture start failed\n";
        capture_.reset();
        opus_encoder_destroy(enc_);
        enc_ = nullptr;
        return false;
    }

    running_ = true;
    thread_ = std::thread(&AudioStreamer::run, this);
    std::cout << "[audio] streaming started: Opus " << kSampleRate << " Hz, " << kChannels
              << " ch, " << kFrameSamples << " samples/frame, " << kBitrate / 1000 << " kbit/s CBR\n";
    return true;
}

void AudioStreamer::stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) thread_.join();
        return;
    }
    if (thread_.joinable()) thread_.join();
    if (capture_) { capture_->stop(); capture_.reset(); }
    if (enc_) { opus_encoder_destroy(enc_); enc_ = nullptr; }
}

void AudioStreamer::run() {
    using clk = std::chrono::steady_clock;
    const auto kTick = std::chrono::microseconds(1000000LL * kFrameSamples / kSampleRate);   // 10 ms
    constexpr size_t kPrime = kFrameSamples + kFrameSamples / 2;                                // 15 ms jitter margin

    std::vector<int16_t> pcm(static_cast<size_t>(kFrameSamples) * kChannels);
    uint8_t out[400];
    bool primed = false;
    auto next = clk::now();

    while (running_) {
        PcmRing& ring = capture_->ring();
        size_t got = 0;

        // Wait for a small reserve before starting to consume, so that capture jitter
        // does not cut frames. When the ring runs dry (nothing is playing), send silence.
        if (!primed && ring.frames() >= kPrime) primed = true;
        if (primed) {
            got = ring.pop(pcm.data(), kFrameSamples);
            if (got == 0) primed = false;
        }
        if (got < static_cast<size_t>(kFrameSamples))
            std::fill(pcm.begin() + static_cast<std::ptrdiff_t>(got * kChannels), pcm.end(), int16_t(0));

        const int n = opus_encode(enc_, pcm.data(), kFrameSamples, out, 255);
        if (n > 0 && cb_) cb_(out, static_cast<size_t>(n));

        next += kTick;
        const auto now = clk::now();
        if (next + std::chrono::milliseconds(100) < now) next = now;    // resync after a long stall
        std::this_thread::sleep_until(next);
    }
}
