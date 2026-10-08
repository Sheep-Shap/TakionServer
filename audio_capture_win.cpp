// WASAPI loopback capture of the default output device (Windows only).
// Delivers interleaved stereo int16 @ 48 kHz into PcmCapture::ring().
#ifdef _WIN32

#include "audio_capture.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

namespace {

template <class T>
class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    T** put() { reset(); return &p_; }
    void** put_void() { reset(); return reinterpret_cast<void**>(&p_); }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
private:
    T* p_ = nullptr;
};

const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

bool fail(const char* what, HRESULT hr) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "[audio] %s failed: 0x%08lx\n", what, static_cast<unsigned long>(hr));
    std::cerr << buf;
    return false;
}

struct SrcFormat {
    uint32_t rate = 48000;
    uint32_t channels = 2;
    uint32_t bits = 16;
    uint32_t block_align = 4;
    bool is_float = false;
};

int16_t to_i16(float v) {
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return static_cast<int16_t>(v * 32767.0f);
}

float read_sample(const BYTE* p, uint32_t bytes, bool is_float) {
    if (is_float && bytes == 4) { float v; std::memcpy(&v, p, 4); return v; }
    if (bytes == 2) { int16_t v; std::memcpy(&v, p, 2); return static_cast<float>(v) / 32768.0f; }
    if (bytes == 3) {
        int32_t v = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
        if (v & 0x800000) v |= ~0xFFFFFF;
        return static_cast<float>(v) / 8388608.0f;
    }
    if (bytes == 4) { int32_t v; std::memcpy(&v, p, 4); return static_cast<float>(v) / 2147483648.0f; }
    return 0.0f;
}

// Linear resampler: any source rate -> 48 kHz, stereo float in, stereo int16 out.
class Resampler {
public:
    void reset(uint32_t src_rate) {
        step_ = static_cast<double>(src_rate) / 48000.0;
        pos_ = 0.0;
        have_prev_ = false;
    }
    void process(const float* in, size_t frames, std::vector<int16_t>& out) {
        for (size_t i = 0; i < frames; ++i) {
            const float cl = in[2 * i], cr = in[2 * i + 1];
            if (!have_prev_) { pl_ = cl; pr_ = cr; have_prev_ = true; }
            while (pos_ < 1.0) {
                const float t = static_cast<float>(pos_);
                out.push_back(to_i16(pl_ + (cl - pl_) * t));
                out.push_back(to_i16(pr_ + (cr - pr_) * t));
                pos_ += step_;
            }
            pos_ -= 1.0;
            pl_ = cl;
            pr_ = cr;
        }
    }
private:
    double step_ = 1.0, pos_ = 0.0;
    float pl_ = 0.0f, pr_ = 0.0f;
    bool have_prev_ = false;
};

class WasapiLoopback : public PcmCapture {
public:
    ~WasapiLoopback() override { stop(); }

    bool start() override {
        std::promise<bool> ready;
        std::future<bool> result = ready.get_future();
        running_ = true;
        thread_ = std::thread([this, &ready]() { run(ready); });
        const bool ok = result.get();
        if (!ok) {
            running_ = false;
            if (thread_.joinable()) thread_.join();
        }
        return ok;
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

private:
    void close_device() {
        if (client_) client_->Stop();
        capture_.reset();
        client_.reset();
    }

    bool open_device() {
        close_device();

        Com<IMMDeviceEnumerator> enumerator;
        Com<IMMDevice> device;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                      __uuidof(IMMDeviceEnumerator), enumerator.put_void());
        if (FAILED(hr)) return fail("CoCreateInstance(MMDeviceEnumerator)", hr);
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.put());
        if (FAILED(hr)) return fail("GetDefaultAudioEndpoint", hr);

        constexpr REFERENCE_TIME kBuffer = 1000000;   // 100 ms
        const DWORD loop = AUDCLNT_STREAMFLAGS_LOOPBACK;

        // 1) Ask the audio engine for exactly our format (it converts).
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client_.put_void());
        if (FAILED(hr)) return fail("Activate(IAudioClient)", hr);

        WAVEFORMATEX want = {};
        want.wFormatTag = WAVE_FORMAT_PCM;
        want.nChannels = 2;
        want.nSamplesPerSec = 48000;
        want.wBitsPerSample = 16;
        want.nBlockAlign = 4;
        want.nAvgBytesPerSec = 48000 * 4;

        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 loop | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                 kBuffer, 0, &want, nullptr);
        if (SUCCEEDED(hr)) {
            fmt_ = SrcFormat();
        } else {
            // 2) Fallback: device mix format; we convert and resample ourselves.
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client_.put_void());
            if (FAILED(hr)) return fail("Activate(IAudioClient) #2", hr);
            WAVEFORMATEX* mix = nullptr;
            hr = client_->GetMixFormat(&mix);
            if (FAILED(hr) || !mix) return fail("GetMixFormat", hr);

            fmt_.rate = mix->nSamplesPerSec;
            fmt_.channels = mix->nChannels;
            fmt_.bits = mix->wBitsPerSample;
            fmt_.block_align = mix->nBlockAlign;
            fmt_.is_float = (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
            if (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22) {
                const WAVEFORMATEXTENSIBLE* ex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix);
                fmt_.is_float = (std::memcmp(&ex->SubFormat, &kSubtypeFloat, sizeof(GUID)) == 0);
            }
            const bool sane = fmt_.channels >= 1 && fmt_.block_align >= fmt_.channels && fmt_.rate > 0;
            if (!sane) { CoTaskMemFree(mix); return fail("mix format check", E_FAIL); }

            hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, loop, kBuffer, 0, mix, nullptr);
            CoTaskMemFree(mix);
            if (FAILED(hr)) return fail("Initialize(mix format)", hr);
            std::cout << "[audio] using device mix format: " << fmt_.rate << " Hz, " << fmt_.channels
                      << " ch, " << fmt_.bits << " bit" << (fmt_.is_float ? " float" : "") << "\n";
        }

        hr = client_->GetService(__uuidof(IAudioCaptureClient), capture_.put_void());
        if (FAILED(hr)) return fail("GetService(IAudioCaptureClient)", hr);

        rs_.reset(fmt_.rate);
        hr = client_->Start();
        if (FAILED(hr)) return fail("IAudioClient::Start", hr);
        return true;
    }

    void handle_buffer(const BYTE* data, UINT32 frames, bool silent) {
        if (frames == 0) return;
        const bool native = fmt_.rate == 48000 && fmt_.channels == 2 && fmt_.bits == 16 && !fmt_.is_float;
        if (native) {
            if (silent) {
                zeros_.assign(static_cast<size_t>(frames) * 2, 0);
                ring_.push(zeros_.data(), frames);
            } else {
                ring_.push(reinterpret_cast<const int16_t*>(data), frames);
            }
            return;
        }

        tmp_.assign(static_cast<size_t>(frames) * 2, 0.0f);
        if (!silent) {
            const uint32_t bps = fmt_.block_align / fmt_.channels;
            for (UINT32 i = 0; i < frames; ++i) {
                const BYTE* f = data + static_cast<size_t>(i) * fmt_.block_align;
                const float l = read_sample(f, bps, fmt_.is_float);
                const float r = fmt_.channels >= 2 ? read_sample(f + bps, bps, fmt_.is_float) : l;
                tmp_[2 * i] = l;
                tmp_[2 * i + 1] = r;
            }
        }
        out_.clear();
        rs_.process(tmp_.data(), frames, out_);
        if (!out_.empty()) ring_.push(out_.data(), out_.size() / 2);
    }

    void run(std::promise<bool>& ready) {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool opened = open_device();
        ready.set_value(opened);          // after this line `ready` must not be touched
        if (!opened) {
            if (SUCCEEDED(co)) CoUninitialize();
            return;
        }

        while (running_) {
            Sleep(5);                     // event mode does not work for loopback, so we poll
            bool broken = false;
            while (running_) {
                UINT32 packet = 0;
                HRESULT hr = capture_->GetNextPacketSize(&packet);
                if (FAILED(hr)) { broken = true; break; }
                if (packet == 0) break;
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                hr = capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
                if (FAILED(hr)) { broken = true; break; }
                handle_buffer(data, frames, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
                capture_->ReleaseBuffer(frames);
            }
            if (broken && running_) {
                std::cerr << "[audio] capture device lost, trying to reopen\n";
                close_device();
                while (running_ && !open_device()) {
                    for (int i = 0; i < 10 && running_; ++i) Sleep(100);
                }
            }
        }
        close_device();
        if (SUCCEEDED(co)) CoUninitialize();
    }

    std::atomic<bool> running_{false};
    std::thread thread_;
    Com<IAudioClient> client_;
    Com<IAudioCaptureClient> capture_;
    SrcFormat fmt_;
    Resampler rs_;
    std::vector<float> tmp_;
    std::vector<int16_t> out_;
    std::vector<int16_t> zeros_;
};

}  // namespace

std::unique_ptr<PcmCapture> PcmCapture::create() {
    return std::make_unique<WasapiLoopback>();
}

#endif  // _WIN32
