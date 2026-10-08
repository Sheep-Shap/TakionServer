#pragma once
// Adaptive bitrate controller driven by the client's congestion reports
// (received / lost packet counters, one report about every 200 ms).
// Pure logic, no I/O: feed it reports, apply the returned bitrate to the encoder.
//
// Random loss that does not depend on the bitrate (e.g. Wi-Fi) must not push the bitrate to the floor,
// so every decision is made on the loss ABOVE a baseline. The baseline is the lowest 5-second average loss
// seen during the last minute, but never more than max_baseline.
#include <algorithm>
#include <cstddef>
#include <cstdint>

class BitrateController {
public:
    struct Params {
        int64_t min_bps = 6'000'000;
        int64_t max_bps = 20'000'000;
        double high_excess = 0.08;      // one report with this much loss above the baseline: immediate big cut
        double mid_excess = 0.03;       // smoothed loss above baseline + this: gentle cut
        double low_excess = 0.01;       // smoothed loss below baseline + this: allowed to probe upwards
        double max_baseline = 0.06;     // cap for the baseline estimate
        double cut_hard = 0.70;
        double cut_soft = 0.90;
        double raise = 1.08;
        int64_t cut_cooldown_ms = 1000;
        int64_t raise_after_ms = 3000;  // quiet time since the last change before probing upwards
        int min_samples = 10;           // ignore reports with fewer packets than this
    };

    BitrateController() : BitrateController(Params()) {}
    explicit BitrateController(const Params& p) : p_(p), bps_(p.max_bps) {
        for (int i = 0; i < kBuckets; ++i) { bucket_lost_[i] = 0; bucket_total_[i] = 0; }
    }

    int64_t bitrate() const { return bps_; }
    double smoothed_loss() const { return ema_; }
    double baseline() const { return (std::min)(baseline_raw(), p_.max_baseline); }

    // Returns true when the bitrate changed (read it with bitrate()).
    bool on_report(int64_t now_ms, unsigned received, unsigned lost) {
        const unsigned total = received + lost;
        if (total < static_cast<unsigned>(p_.min_samples)) return false;
        const double loss = static_cast<double>(lost) / static_cast<double>(total);
        ema_ = have_ema_ ? 0.7 * ema_ + 0.3 * loss : loss;
        have_ema_ = true;
        track_baseline(now_ms, received, lost);

        const double base = baseline();
        const int64_t before = bps_;
        if (loss - base >= p_.high_excess && now_ms - last_cut_ms_ >= p_.cut_cooldown_ms / 2) {
            bps_ = static_cast<int64_t>(bps_ * p_.cut_hard);
            last_cut_ms_ = last_change_ms_ = now_ms;
        } else if (ema_ - base >= p_.mid_excess && now_ms - last_cut_ms_ >= p_.cut_cooldown_ms) {
            bps_ = static_cast<int64_t>(bps_ * p_.cut_soft);
            last_cut_ms_ = last_change_ms_ = now_ms;
        } else if (ema_ - base < p_.low_excess && now_ms - last_change_ms_ >= p_.raise_after_ms) {
            bps_ = static_cast<int64_t>(bps_ * p_.raise);
            last_change_ms_ = now_ms;
        }
        bps_ = (std::max)(p_.min_bps, (std::min)(p_.max_bps, bps_));
        return bps_ != before;
    }

private:
    static constexpr int kBuckets = 12;                 // 12 x 5 s = the last minute
    static constexpr int64_t kBucketMs = 5000;

    // Per-5-second packet counters; the baseline is the lowest average loss of any
    // bucket with enough packets in the last minute (an average is much less noisy than a minimum of a moving average).
    void track_baseline(int64_t now_ms, unsigned received, unsigned lost) {
        const int64_t slot = now_ms / kBucketMs;
        if (slot != cur_slot_) {
            for (int64_t s = cur_slot_ + 1; s <= slot && s - cur_slot_ <= kBuckets; ++s) {
                bucket_lost_[static_cast<size_t>(s % kBuckets)] = 0;
                bucket_total_[static_cast<size_t>(s % kBuckets)] = 0;
            }
            cur_slot_ = slot;
        }
        bucket_lost_[static_cast<size_t>(slot % kBuckets)] += lost;
        bucket_total_[static_cast<size_t>(slot % kBuckets)] += received + lost;
    }

    double baseline_raw() const {
        double m = 1.0;
        for (int i = 0; i < kBuckets; ++i) {
            if (bucket_total_[i] < 100) continue;
            m = (std::min)(m, static_cast<double>(bucket_lost_[i]) / static_cast<double>(bucket_total_[i]));
        }
        return m >= 1.0 ? 0.0 : m;
    }

    Params p_;
    int64_t bps_;
    double ema_ = 0.0;
    bool have_ema_ = false;
    int64_t last_cut_ms_ = -1000000;
    int64_t last_change_ms_ = 0;
    int64_t cur_slot_ = -1;
    uint64_t bucket_lost_[kBuckets];
    uint64_t bucket_total_[kBuckets];
};
