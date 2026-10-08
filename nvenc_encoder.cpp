#include "nvenc_encoder.h"
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <iostream>
#include <cstring>
#include "ffnvcodec/nvEncodeAPI.h"

typedef NVENCSTATUS (NVENCAPI* PFN_NvEncCreate)(NV_ENCODE_API_FUNCTION_LIST*);
typedef NVENCSTATUS (NVENCAPI* PFN_NvEncMaxVer)(uint32_t*);

struct NvencEncoder::Impl {
    HMODULE dll = nullptr;
    NV_ENCODE_API_FUNCTION_LIST api{};
    void* enc = nullptr;
    NV_ENC_INPUT_PTR in = nullptr;
    NV_ENC_OUTPUT_PTR out = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    NV_ENC_INITIALIZE_PARAMS init{};
    NV_ENC_CONFIG cfg{};
};

static IDXGIAdapter1* find_nvidia_adapter() {
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f))) return nullptr;
    IDXGIAdapter1* a = nullptr; IDXGIAdapter1* res = nullptr;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
        if (d.VendorId == 0x10DE && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { res = a; break; }
        a->Release();
    }
    f->Release();
    return res;
}

static bool load_api(NvencEncoder::Impl& d) {
    d.dll = LoadLibraryW(L"nvEncodeAPI64.dll");
    if (!d.dll) return false;
    auto get_ver = (PFN_NvEncMaxVer)GetProcAddress(d.dll, "NvEncodeAPIGetMaxSupportedVersion");
    auto create  = (PFN_NvEncCreate)GetProcAddress(d.dll, "NvEncodeAPICreateInstance");
    if (!get_ver || !create) return false;
    uint32_t drv = 0;
    if (get_ver(&drv) != NV_ENC_SUCCESS) return false;
    const uint32_t need = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
    if (drv < need) {
        std::cerr << "[nvenc] driver too old: supports API 0x" << std::hex << drv
                  << ", headers need 0x" << need << std::dec << " (update driver or use older nv-codec-headers)\n";
        return false;
    }
    d.api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    return create(&d.api) == NV_ENC_SUCCESS;
}

NvencEncoder::NvencEncoder() : d_(new Impl) {}
NvencEncoder::~NvencEncoder() { shutdown(); }

bool NvencEncoder::available() {
    HMODULE h = LoadLibraryW(L"nvEncodeAPI64.dll");
    if (!h) return false;
    auto get_ver = (PFN_NvEncMaxVer)GetProcAddress(h, "NvEncodeAPIGetMaxSupportedVersion");
    uint32_t drv = 0;
    const uint32_t need = (NVENCAPI_MAJOR_VERSION << 4) | NVENCAPI_MINOR_VERSION;
    const bool ok = get_ver && get_ver(&drv) == NV_ENC_SUCCESS && drv >= need;
    FreeLibrary(h);
    if (!ok) {
        std::cerr << "[nvenc] NVIDIA driver does not support the NVENC API version required by this build (supports 0x"
                  << std::hex << drv << ", needs 0x" << need << std::dec << ")\n";
        return false;
    }
    IDXGIAdapter1* a = find_nvidia_adapter();
    if (!a) return false;
    a->Release();
    return true;
}

#define NV_CHECK(call, what) do { NVENCSTATUS _s = (call); if (_s != NV_ENC_SUCCESS) { \
    std::cerr << "[nvenc] " what " failed: " << (int)_s << "\n"; shutdown(); return false; } } while (0)

bool NvencEncoder::initialize(int w, int h, int fps, int bitrate_bps, int max_bitrate_bps) {
    Impl& d = *d_;
    if (!load_api(d)) { std::cerr << "[nvenc] NVENC API not available\n"; return false; }

    IDXGIAdapter1* ad = find_nvidia_adapter();
    if (!ad) { std::cerr << "[nvenc] no NVIDIA adapter\n"; return false; }
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                   D3D11_SDK_VERSION, &d.dev, &fl, &d.ctx);
    ad->Release();
    if (FAILED(hr)) { std::cerr << "[nvenc] D3D11CreateDevice failed\n"; return false; }

    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS op{};
    op.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    op.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    op.device = d.dev;
    op.apiVersion = NVENCAPI_VERSION;
    NV_CHECK(d.api.nvEncOpenEncodeSessionEx(&op, &d.enc), "OpenEncodeSessionEx");

    const GUID codec = NV_ENC_CODEC_HEVC_GUID;
    const GUID preset = NV_ENC_PRESET_P3_GUID;
    const NV_ENC_TUNING_INFO tuning = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;

    NV_ENC_PRESET_CONFIG pc{};
    pc.version = NV_ENC_PRESET_CONFIG_VER;
    pc.presetCfg.version = NV_ENC_CONFIG_VER;
    NV_CHECK(d.api.nvEncGetEncodePresetConfigEx(d.enc, codec, preset, tuning, &pc), "GetEncodePresetConfigEx");
    d.cfg = pc.presetCfg;

    d.cfg.profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
    d.cfg.gopLength = NVENC_INFINITE_GOPLENGTH;      // IDR ставим сами
    d.cfg.frameIntervalP = 1;                          // без B-кадров
    d.cfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    d.cfg.rcParams.averageBitRate = (uint32_t)bitrate_bps;
    d.cfg.rcParams.maxBitRate = (uint32_t)bitrate_bps;
    d.cfg.rcParams.vbvBufferSize = (uint32_t)(bitrate_bps / 20);  
    d.cfg.rcParams.vbvInitialDelay = d.cfg.rcParams.vbvBufferSize;
    auto& hc = d.cfg.encodeCodecConfig.hevcConfig;
    hc.maxNumRefFramesInDPB = 1;
    hc.numRefL0 = NV_ENC_NUM_REF_FRAMES_1;
    hc.sliceMode = 0;
    d.cfg.rcParams.enableAQ = 1;
    d.cfg.rcParams.enableLookahead = 0;
    d.cfg.rcParams.zeroReorderDelay = 1;
    d.cfg.encodeCodecConfig.hevcConfig.idrPeriod = NVENC_INFINITE_GOPLENGTH;
    d.cfg.encodeCodecConfig.hevcConfig.outputAUD = 0;
    d.cfg.encodeCodecConfig.hevcConfig.repeatSPSPPS = 1;

    d.init.version = NV_ENC_INITIALIZE_PARAMS_VER;
    d.init.encodeGUID = codec;
    d.init.presetGUID = preset;
    d.init.tuningInfo = tuning;
    d.init.encodeWidth = d.init.darWidth = w;
    d.init.encodeHeight = d.init.darHeight = h;
    d.init.frameRateNum = fps;
    d.init.frameRateDen = 1;
    d.init.enablePTD = 1;
    d.init.enableEncodeAsync = 0;
    d.init.encodeConfig = &d.cfg;
    NV_CHECK(d.api.nvEncInitializeEncoder(d.enc, &d.init), "InitializeEncoder");

    NV_ENC_CREATE_INPUT_BUFFER ci{};
    ci.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
    ci.width = w; ci.height = h;
    ci.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    NV_CHECK(d.api.nvEncCreateInputBuffer(d.enc, &ci), "CreateInputBuffer");
    d.in = ci.inputBuffer;

    NV_ENC_CREATE_BITSTREAM_BUFFER cb{};
    cb.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    NV_CHECK(d.api.nvEncCreateBitstreamBuffer(d.enc, &cb), "CreateBitstreamBuffer");
    d.out = cb.bitstreamBuffer;

    {
        uint8_t buf[1024]; uint32_t sz = 0;
        NV_ENC_SEQUENCE_PARAM_PAYLOAD sp{};
        sp.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
        sp.inBufferSize = sizeof(buf);
        sp.spsppsBuffer = buf;
        sp.outSPSPPSPayloadSize = &sz;
        if (d.api.nvEncGetSequenceParams(d.enc, &sp) == NV_ENC_SUCCESS && sz > 0)
            header_.assign(buf, buf + sz);
    }
    width_ = w; height_ = h; fps_ = fps;
    cur_bitrate_ = bitrate_bps; cur_peak_ = max_bitrate_bps;
    initialized_ = true;
    std::cout << "NVENC HEVC encoder initialized: " << w << "x" << h << "@" << fps << "fps\n";
    return true;
}

void NvencEncoder::shutdown() {
    if (!d_) return;
    Impl& d = *d_;
    if (d.enc) {
        if (d.in)  { d.api.nvEncDestroyInputBuffer(d.enc, d.in); d.in = nullptr; }
        if (d.out) { d.api.nvEncDestroyBitstreamBuffer(d.enc, d.out); d.out = nullptr; }
        d.api.nvEncDestroyEncoder(d.enc);
        d.enc = nullptr;
    }
    if (d.ctx) { d.ctx->Release(); d.ctx = nullptr; }
    if (d.dev) { d.dev->Release(); d.dev = nullptr; }
    if (d.dll) { FreeLibrary(d.dll); d.dll = nullptr; }
    initialized_ = false;
}

static bool has_nal(const std::vector<uint8_t>& f, int t1, int t2 = -1) {
    const size_t n = f.size() < 512 ? f.size() : 512;
    for (size_t i = 0; i + 4 < n; ++i) {
        if (f[i] == 0 && f[i + 1] == 0 && (f[i + 2] == 1 || (f[i + 2] == 0 && f[i + 3] == 1))) {
            const size_t o = i + (f[i + 2] == 1 ? 3 : 4);
            if (o < f.size()) { const int t = (f[o] >> 1) & 0x3f; if (t == t1 || t == t2) return true; }
        }
    }
    return false;
}

static void dbg_nals(const std::vector<uint8_t>& f, uint64_t idx) {
    std::cout << "[nvenc] #" << idx << ": " << f.size() << " B, NAL types:";
    int shown = 0;
    for (size_t i = 0; i + 4 < f.size() && shown < 6; ++i) {
        if (f[i] == 0 && f[i + 1] == 0 && (f[i + 2] == 1 || (f[i + 2] == 0 && f[i + 3] == 1))) {
            const size_t o = i + (f[i + 2] == 1 ? 3 : 4);
            if (o < f.size()) { std::cout << ' ' << ((f[o] >> 1) & 0x3f); ++shown; }
            i = o;
        }
    }
    std::cout << "\n";
}

bool NvencEncoder::encode_frame(const void* nv12, int pitch, std::vector<uint8_t>& out) {
    if (!initialized_) return false;
    Impl& d = *d_;

    NV_ENC_LOCK_INPUT_BUFFER li{};
    li.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    li.inputBuffer = d.in;
    if (d.api.nvEncLockInputBuffer(d.enc, &li) != NV_ENC_SUCCESS) return false;
    uint8_t* dst = (uint8_t*)li.bufferDataPtr;
    const int dp = (int)li.pitch;
    const uint8_t* src = (const uint8_t*)nv12;
    for (int y = 0; y < height_; ++y) memcpy(dst + (size_t)y * dp, src + (size_t)y * pitch, width_);
    uint8_t* dst_uv = dst + (size_t)dp * height_;
    const uint8_t* src_uv = src + (size_t)pitch * height_;
    for (int y = 0; y < height_ / 2; ++y) memcpy(dst_uv + (size_t)y * dp, src_uv + (size_t)y * pitch, width_);
    d.api.nvEncUnlockInputBuffer(d.enc, d.in);
    
    NV_ENC_PIC_PARAMS pp{};
    pp.version = NV_ENC_PIC_PARAMS_VER;
    pp.inputBuffer = d.in;
    pp.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
    pp.inputWidth = width_; pp.inputHeight = height_; pp.inputPitch = dp;
    pp.outputBitstream = d.out;
    pp.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    pp.inputTimeStamp = frame_idx_++;

    const bool want_idr = idr_requested_.exchange(false);
    if (want_idr)
        pp.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;

    NVENCSTATUS st = d.api.nvEncEncodePicture(d.enc, &pp);
    if (st == NV_ENC_ERR_NEED_MORE_INPUT) { if (want_idr) idr_requested_ = true; return false; }
    if (st != NV_ENC_SUCCESS) {
        std::cerr << "[nvenc] EncodePicture failed: " << (int)st << "\n";
        if (want_idr) idr_requested_ = true;
        return false;
    }
    
    NV_ENC_LOCK_BITSTREAM lb{};
    lb.version = NV_ENC_LOCK_BITSTREAM_VER;
    lb.outputBitstream = d.out;
    if (d.api.nvEncLockBitstream(d.enc, &lb) != NV_ENC_SUCCESS) return false;
    out.assign((const uint8_t*)lb.bitstreamBufferPtr,
               (const uint8_t*)lb.bitstreamBufferPtr + lb.bitstreamSizeInBytes);
    d.api.nvEncUnlockBitstream(d.enc, d.out);
    if (has_nal(out, 19, 20) && !has_nal(out, 32) && !header_.empty())
        out.insert(out.begin(), header_.begin(), header_.end());
    if (frame_idx_ <= 12) dbg_nals(out, frame_idx_);    
    return !out.empty();
}

bool NvencEncoder::set_bitrate(int bitrate_bps, int max_bitrate_bps) {
    if (!initialized_ || bitrate_bps <= 0) return false;
    if (bitrate_bps == cur_bitrate_ && max_bitrate_bps == cur_peak_) return true;
    Impl& d = *d_;
    d.cfg.rcParams.averageBitRate = (uint32_t)bitrate_bps;
    d.cfg.rcParams.maxBitRate = (uint32_t)bitrate_bps;
    d.cfg.rcParams.vbvBufferSize = (uint32_t)(bitrate_bps / 20);
    d.cfg.rcParams.vbvInitialDelay = d.cfg.rcParams.vbvBufferSize;
    NV_ENC_RECONFIGURE_PARAMS rp{};
    rp.version = NV_ENC_RECONFIGURE_PARAMS_VER;
    rp.reInitEncodeParams = d.init;
    rp.reInitEncodeParams.encodeConfig = &d.cfg;
    if (d.api.nvEncReconfigureEncoder(d.enc, &rp) != NV_ENC_SUCCESS) {
        std::cerr << "[nvenc] set_bitrate failed\n";
        return false;
    }
    cur_bitrate_ = bitrate_bps; cur_peak_ = max_bitrate_bps;
    std::cout << "[nvenc] target=" << bitrate_bps << " peak=" << max_bitrate_bps << "\n";
    return true;
}