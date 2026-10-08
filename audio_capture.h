#pragma once
// Source of PCM audio for streaming: interleaved stereo int16, 48 kHz.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

// Thread-safe ring of stereo frames. The oldest data is dropped when the ring is full,
// so latency can never grow beyond max_frames.
class PcmRing {
public:
    explicit PcmRing(size_t max_frames) : max_samples_(max_frames * 2) {}

    void push(const int16_t* frames, size_t n_frames) {
        std::lock_guard<std::mutex> lk(m_);
        buf_.insert(buf_.end(), frames, frames + n_frames * 2);
        if (buf_.size() > max_samples_)
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(buf_.size() - max_samples_));
    }

    // Copies up to max_frames frames to dst, returns the number of frames copied.
    size_t pop(int16_t* dst, size_t max_frames) {
        std::lock_guard<std::mutex> lk(m_);
        const size_t n = (std::min)(max_frames, buf_.size() / 2);
        std::copy(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(n * 2), dst);
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(n * 2));
        return n;
    }

    size_t frames() const {
        std::lock_guard<std::mutex> lk(m_);
        return buf_.size() / 2;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(m_);
        buf_.clear();
    }

private:
    mutable std::mutex m_;
    std::vector<int16_t> buf_;
    size_t max_samples_;
};

class PcmCapture {
public:
    virtual ~PcmCapture() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    PcmRing& ring() { return ring_; }

    // Implemented in audio_capture_win.cpp (WASAPI loopback of the default output device).
    static std::unique_ptr<PcmCapture> create();

protected:
    PcmRing ring_{48000 / 1000 * 60};   // 60 ms
};
