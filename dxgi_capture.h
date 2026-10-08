#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <atomic>
#include <cstdint>
#include <vector>

class DXGICapture {
public:
    DXGICapture();
    ~DXGICapture();

    bool initialize(int monitor_index = 0);
    void shutdown();

    // Capture frame and convert to NV12
    bool capture_frame(std::vector<uint8_t>& out_nv12_data, int& out_pitch);

    bool is_initialized() const { return initialized_; }
    bool have_image_ = false;

    std::atomic<int> stage_{0};

private:
    bool create_d3d_device();
    bool create_duplication(int monitor_index);
    bool ensure_staging(UINT w, UINT h);
    bool convert_staging_to_nv12(ID3D11Texture2D* st, std::vector<uint8_t>& out_nv12, int& out_pitch);

    ID3D11Device* d3d_device_ = nullptr;
    ID3D11DeviceContext* d3d_context_ = nullptr;

    // две staging-текстуры: копируем в одну, читаем предыдущую (без блокировки на GPU)
    ID3D11Texture2D* staging_[2] = {nullptr, nullptr};
    int  wr_ = 0;
    bool pending_ = false;
    UINT staging_w_ = 0, staging_h_ = 0;

    IDXGIOutputDuplication* duplication_ = nullptr;
    IDXGIOutput* output_ = nullptr;

    std::atomic<bool> initialized_{false};
    int monitor_index_ = 0;
    int width_ = 0;
    int height_ = 0;
};
