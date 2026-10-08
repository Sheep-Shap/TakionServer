#include "amf_encoder.h"
#include <iostream>
#include <cstring>
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <thread>

// AMF includes (актуальная версия SDK)
#include "core/Factory.h"
#include "core/Context.h"
#include "core/Data.h"
#include "core/Buffer.h"
#include "core/Surface.h"
#include "core/Plane.h"
#include "components/VideoEncoderHEVC.h"
#include "components/VideoConverter.h"

using namespace amf;

// Global AMF factory and DLL handle
static AMFFactory* g_amf_factory = nullptr;
static HMODULE g_amf_dll = nullptr;

// Helper to load AMF DLL and get factory
static bool InitAMF() {
    if (g_amf_factory) {
        return true; // Already initialized
    }
    
    // Load AMF runtime DLL
    g_amf_dll = LoadLibraryW(L"amfrt64.dll");
    if (!g_amf_dll) {
        std::cerr << "Failed to load amfrt64.dll\n";
        return false;
    }
    
    // Get AMFInit function
    AMFInit_Fn init_fn = (AMFInit_Fn)GetProcAddress(g_amf_dll, "AMFInit");
    if (!init_fn) {
        std::cerr << "Failed to get AMFInit function\n";
        FreeLibrary(g_amf_dll);
        g_amf_dll = nullptr;
        return false;
    }
    
    // Initialize factory
    if (init_fn(AMF_FULL_VERSION, &g_amf_factory) != AMF_OK) {
        std::cerr << "Failed to initialize AMF factory\n";
        FreeLibrary(g_amf_dll);
        g_amf_dll = nullptr;
        return false;
    }
    
    return true;
}

static void TerminateAMF() {
    if (g_amf_factory) {
        g_amf_factory = nullptr;
    }
    if (g_amf_dll) {
        FreeLibrary(g_amf_dll);
        g_amf_dll = nullptr;
    }
}

AMFEncoder::AMFEncoder() {
    if (!InitAMF()) {
        std::cerr << "AMF initialization failed\n";
    }
}

AMFEncoder::~AMFEncoder() {
    shutdown();
    TerminateAMF();
}

bool AMFEncoder::initialize(int width, int height, int fps, int bitrate_bps, int max_bitrate_bps) {
    if (!g_amf_factory) {
        std::cerr << "AMF factory not initialized\n";
        return false;
    }
    
    if (!create_context()) {
        return false;
    }
    
    if (!create_encoder(width, height, fps, bitrate_bps, max_bitrate_bps)) {
        return false;
    }
    
    width_ = width;
    height_ = height;
    fps_ = fps;
    cur_bitrate_ = bitrate_bps; cur_peak_ = max_bitrate_bps;
    initialized_ = true;
    
    std::cout << "AMF HEVC encoder initialized: " << width << "x" << height << "@" << fps << "fps\n";
    return true;
}

void AMFEncoder::shutdown() {
    if (encoder_) {
        encoder_->Terminate();
        encoder_->Release();
        encoder_ = nullptr;
    }
    
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    
    initialized_ = false;
}

bool AMFEncoder::create_context() {
    if (g_amf_factory->CreateContext(&context_) != AMF_OK) {
        std::cerr << "Failed to create AMF context\n";
        return false;
    }
    
    // Initialize for DX11 (we'll use DXGI capture)
    if (context_->InitDX11(nullptr) != AMF_OK) {
        std::cerr << "Failed to initialize DX11 context\n";
        return false;
    }
    
    return true;
}

static void set_prop(amf::AMFComponent* e, const wchar_t* name, amf::AMFVariantStruct v, const char* label) {
    const AMF_RESULT r = e->SetProperty(name, v);
    if (r != AMF_OK) std::cerr << "[amf] SetProperty " << label << " failed: " << (int)r << "\n";
}

bool AMFEncoder::create_encoder(int width, int height, int fps, int bitrate_bps, int max_bitrate_bps) {
    if (g_amf_factory->CreateComponent(context_, AMFVideoEncoder_HEVC, &encoder_) != AMF_OK) {
        std::cerr << "Failed to create HEVC encoder component\n";
        return false;
    }

    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_ENFORCE_HRD, false);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_PRE_ANALYSIS_ENABLE, false);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_ENABLE_VBAQ, true);                  // лучше на детальных сценах
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, bitrate_bps);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, max_bitrate_bps);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_VBV_BUFFER_SIZE, bitrate_bps / 20);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, AMFConstructRate(fps, 1));
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, 3600);                      // IDR ставим сами
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_NUM_GOPS_PER_IDR, 1);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSERT_AUD, false);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE, AMF_VIDEO_ENCODER_HEVC_PROFILE_MAIN);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1);
    encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, AMF_COLOR_BIT_DEPTH_8);

    AMF_RESULT r = encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSERT_AUD, false);
    if (r != AMF_OK) std::cerr << "INSERT_AUD not accepted: " << (int)r << "\n";

    if (encoder_->Init(AMF_SURFACE_NV12, width, height) != AMF_OK) {
        std::cerr << "Failed to initialize HEVC encoder\n";
        return false;
    }
    
    amf_int64 tb = 0, pb = 0, lvl = 0, tier = 0, rc = 0, vbv = 0;
    encoder_->GetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, &tb);
    encoder_->GetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, &pb);
    encoder_->GetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, &lvl);
    encoder_->GetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, &tier);
    encoder_->GetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, &rc);
    encoder_->GetProperty(AMF_VIDEO_ENCODER_HEVC_VBV_BUFFER_SIZE, &vbv);
    std::cout << "[amf] applied: target=" << tb << " peak=" << pb << " level=" << lvl
              << " tier=" << tier << " rc=" << rc << " vbv=" << vbv << "\n";

    return true;
}

bool AMFEncoder::set_bitrate(int bitrate_bps, int max_bitrate_bps) {
    if (!initialized_ || !encoder_ || bitrate_bps <= 0) return false;
    if (bitrate_bps == cur_bitrate_ && max_bitrate_bps == cur_peak_) return true;
    AMF_RESULT r = encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, (amf_int64)bitrate_bps);
    if (r == AMF_OK && max_bitrate_bps > 0)
        r = encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, (amf_int64)max_bitrate_bps);
    if (r == AMF_OK)
        r = encoder_->SetProperty(AMF_VIDEO_ENCODER_HEVC_VBV_BUFFER_SIZE, (amf_int64)(bitrate_bps / 8));
    if (r != AMF_OK) {
        std::cerr << "set_bitrate failed: " << (int)r << "\n";
        return false;
    }
    cur_bitrate_ = bitrate_bps;
    if (max_bitrate_bps > 0) cur_peak_ = max_bitrate_bps;
    std::cout << "[amf] target=" << bitrate_bps << " peak=" << cur_peak_ << "\n";
    return true;
}

void AMFEncoder::force_idr() {
    idr_requested_ = true;
}

static void strip_aud(std::vector<uint8_t>& h) {
    const size_t n = h.size();
    auto find_sc = [&](size_t from, size_t& sc_len) -> size_t {
        for (size_t k = from; k + 2 < n; ++k) {
            if (h[k] == 0 && h[k+1] == 0) {
                if (h[k+2] == 1) { sc_len = 3; return k; }
                if (k + 3 < n && h[k+2] == 0 && h[k+3] == 1) { sc_len = 4; return k; }
            }
        }
        sc_len = 0;
        return n;
    };

    std::vector<uint8_t> out;
    out.reserve(n);
    size_t sc_len = 0;
    size_t pos = find_sc(0, sc_len);
    while (pos < n) {
        const size_t nal_start = pos + sc_len;
        size_t next_len = 0;
        const size_t next = find_sc(nal_start, next_len);
        const uint8_t type = (nal_start < n) ? ((h[nal_start] >> 1) & 0x3f) : 0;
        if (type != 35) out.insert(out.end(), h.begin() + pos, h.begin() + next);
        pos = next;
        sc_len = next_len;
    }
    if (!out.empty()) h.swap(out);
}

static unsigned long long g_in = 0, g_out = 0;

bool AMFEncoder::encode_frame(const void* nv12_data, int pitch, 
                               std::vector<uint8_t>& out_hevc_frame) {
    if (!initialized_ || !encoder_ || !context_) {
        return false;
    }
    
    // Allocate input surface (5 аргументов, без pitch)
    AMFSurface* input_surface = nullptr;
    if (context_->AllocSurface(AMF_MEMORY_HOST, AMF_SURFACE_NV12, width_, height_, &input_surface) != AMF_OK) {
        std::cerr << "Failed to allocate input surface\n";
        return false;
    }
    
    // Copy NV12 data to surface planes
    const uint8_t* src = static_cast<const uint8_t*>(nv12_data);
    
    // Y plane
    AMFPlane* y_plane = input_surface->GetPlane(AMF_PLANE_Y);
    uint8_t* dst_y = static_cast<uint8_t*>(y_plane->GetNative());
    int pitch_y = y_plane->GetHPitch();
    for (int y = 0; y < height_; ++y) {
        memcpy(dst_y + y * pitch_y, src + y * pitch, width_);
    }
    
    // UV plane
    AMFPlane* uv_plane = input_surface->GetPlane(AMF_PLANE_UV);
    uint8_t* dst_uv = static_cast<uint8_t*>(uv_plane->GetNative());
    int pitch_uv = uv_plane->GetHPitch();
    const uint8_t* src_uv = src + pitch * height_;
    for (int y = 0; y < height_ / 2; ++y) {
        memcpy(dst_uv + y * pitch_uv, src_uv + y * pitch, width_);
    }
    
    // Set IDR if requested
    if (idr_requested_) {
        input_surface->SetProperty(AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE,
                                   (amf_int64)AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_IDR);
        input_surface->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSERT_HEADER, (amf_bool)true);
        idr_requested_ = false;
    }

    AMF_RESULT sr = encoder_->SubmitInput(input_surface);
    input_surface->Release();
    if (sr != AMF_OK && sr != AMF_INPUT_FULL) {
        std::cerr << "SubmitInput failed: " << (int)sr << "\n";
        return false;
    }

    if (sr == AMF_OK) ++g_in;

    AMFData* output_data = nullptr;
    AMF_RESULT qr = encoder_->QueryOutput(&output_data);
    if (qr == AMF_REPEAT || (qr == AMF_OK && !output_data)) return false;   // кадр ещё кодируется, заберём на следующем тике
    if (qr != AMF_OK) {
        std::cerr << "QueryOutput failed: " << (int)qr << "\n";
        return false;
    }

    AMFBuffer* output_buffer = nullptr;
    if (output_data->QueryInterface(AMFBuffer::IID(), (void**)&output_buffer) == AMF_OK && output_buffer) {
        out_hevc_frame.resize(output_buffer->GetSize());
        memcpy(out_hevc_frame.data(), output_buffer->GetNative(), output_buffer->GetSize());
        output_buffer->Release();
    }
    output_data->Release();
    strip_aud(out_hevc_frame);
    if (!out_hevc_frame.empty()) ++g_out;
    if (g_in % 300 == 0)
        std::printf("[amf] in=%llu out=%llu lag=%lld\n", g_in, g_out, (long long)g_in - (long long)g_out);
    return !out_hevc_frame.empty();
}