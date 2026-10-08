#pragma once
// Captures system audio, encodes it to Opus (48 kHz, stereo, 10 ms frames, CBR)
// and hands every encoded frame to a callback. One callback call = one Takion audio unit.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

#include "audio_capture.h"

struct OpusEncoder;

class AudioStreamer {
public:
    using PacketCb = std::function<void(const uint8_t* data, size_t size)>;

    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;
    static constexpr int kFrameSamples = 480;       // 10 ms, must match frame_size in the STREAMINFO audio header
    static constexpr int kBitrate = 128000;         // CBR -> 160 bytes per frame (a Takion audio unit is at most 255 bytes)

    AudioStreamer() = default;
    ~AudioStreamer() { stop(); }
    AudioStreamer(const AudioStreamer&) = delete;
    AudioStreamer& operator=(const AudioStreamer&) = delete;

    bool start(PacketCb cb);
    void stop();                                    // joins the thread; the callback is never called after stop() returns

private:
    void run();

    PacketCb cb_;
    std::unique_ptr<PcmCapture> capture_;
    OpusEncoder* enc_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
};
