#include "dxgi_capture.h"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <execution>
#include <vector>
#include <cstdint>
#include <windows.h>

DXGICapture::DXGICapture() = default;

DXGICapture::~DXGICapture() {
    shutdown();
}

bool DXGICapture::initialize(int monitor_index) {
    monitor_index_ = monitor_index;
    if (!create_d3d_device()) {
        return false;
    }
    if (!create_duplication(monitor_index)) {
        return false;
    }
    initialized_ = true;
    std::cout << "DXGI capture initialized on monitor " << monitor_index << "\n";
    return true;
}

void DXGICapture::shutdown() {
    if (duplication_) {
        duplication_->ReleaseFrame();
        duplication_->Release();
        duplication_ = nullptr;
    }
    if (output_) {
        output_->Release();
        output_ = nullptr;
    }
    for (int i = 0; i < 2; ++i) {
        if (staging_[i]) {
            staging_[i]->Release();
            staging_[i] = nullptr;
        }
    }
    staging_w_ = staging_h_ = 0;
    wr_ = 0;
    pending_ = false;
    if (d3d_context_) {
        d3d_context_->Release();
        d3d_context_ = nullptr;
    }
    if (d3d_device_) {
        d3d_device_->Release();
        d3d_device_ = nullptr;
    }
    initialized_ = false;
}

bool DXGICapture::create_d3d_device() {
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL feature_level;

    if (D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            feature_levels,
            ARRAYSIZE(feature_levels),
            D3D11_SDK_VERSION,
            &d3d_device_,
            &feature_level,
            &d3d_context_) != S_OK) {
        std::cerr << "Failed to create D3D11 device\n";
        return false;
    }

    // просим планировщик GPU не отдавать копирование на съедение игре
    IDXGIDevice* dxgi_dev = nullptr;
    if (SUCCEEDED(d3d_device_->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_dev))) {
        dxgi_dev->SetGPUThreadPriority(7);
        dxgi_dev->Release();
    }
    return true;
}

bool DXGICapture::create_duplication(int monitor_index) {
    if (output_) {
        output_->Release();
        output_ = nullptr;
    }

    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
        std::cerr << "Failed to create DXGI factory\n";
        return false;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 adapter_desc;
        adapter->GetDesc1(&adapter_desc);

        if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }

        IDXGIOutput* dxgi_output = nullptr;
        for (UINT j = 0; adapter->EnumOutputs(j, &dxgi_output) != DXGI_ERROR_NOT_FOUND; ++j) {
            if (static_cast<int>(j) == monitor_index) {
                IDXGIOutput1* output1 = nullptr;
                if (dxgi_output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1) == S_OK) {
                    if (output1->DuplicateOutput(d3d_device_, &duplication_) == S_OK) {
                        DXGI_OUTPUT_DESC output_desc;
                        dxgi_output->GetDesc(&output_desc);
                        width_  = output_desc.DesktopCoordinates.right  - output_desc.DesktopCoordinates.left;
                        height_ = output_desc.DesktopCoordinates.bottom - output_desc.DesktopCoordinates.top;
                        output_ = dxgi_output;          // оставляем ссылку до shutdown()
                        output1->Release();
                        adapter->Release();
                        factory->Release();
                        return true;
                    }
                    output1->Release();
                }
            }
            dxgi_output->Release();
        }
        adapter->Release();
    }

    factory->Release();
    std::cerr << "Failed to create DXGI duplication for monitor " << monitor_index << "\n";
    return false;
}

bool DXGICapture::ensure_staging(UINT w, UINT h) {
    if (staging_[0] && staging_[1] && staging_w_ == w && staging_h_ == h) return true;

    for (int i = 0; i < 2; ++i) {
        if (staging_[i]) { staging_[i]->Release(); staging_[i] = nullptr; }
    }
    pending_ = false;
    wr_ = 0;

    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    for (int i = 0; i < 2; ++i) {
        if (FAILED(d3d_device_->CreateTexture2D(&d, nullptr, &staging_[i]))) {
            std::cerr << "Failed to create staging texture\n";
            for (int k = 0; k < 2; ++k) {
                if (staging_[k]) { staging_[k]->Release(); staging_[k] = nullptr; }
            }
            return false;
        }
    }
    staging_w_ = w;
    staging_h_ = h;
    return true;
}

bool DXGICapture::capture_frame(std::vector<uint8_t>& out_nv12_data, int& out_pitch) {
    if (!initialized_) return false;

    if (!duplication_) {
        if (!create_duplication(monitor_index_)) { Sleep(200); return false; }
        std::cerr << "[capture] duplication recreated\n";
    }

    DXGI_OUTDUPL_FRAME_INFO fi;
    IDXGIResource* res = nullptr;
    stage_ = 1;
    HRESULT hr = duplication_->AcquireNextFrame(1, &fi, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
    if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
        std::cerr << "[capture] access lost (0x" << std::hex << hr << std::dec << "), recreating\n";
        duplication_->Release();
        duplication_ = nullptr;
        pending_ = false;
        return false;
    }
    if (FAILED(hr)) {
        std::cerr << "[capture] AcquireNextFrame failed: 0x" << std::hex << hr << std::dec << "\n";
        return false;
    }

    if (fi.LastPresentTime.QuadPart == 0 && have_image_) {   // только курсор: рабочий стол не менялся
        res->Release();
        duplication_->ReleaseFrame();
        return false;                                         // grab() оставит предыдущий кадр
    }

    bool copied = false;
    ID3D11Texture2D* tex = nullptr;
    stage_ = 2;
    if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex))) {
        D3D11_TEXTURE2D_DESC desc;
        tex->GetDesc(&desc);
        if (ensure_staging(desc.Width, desc.Height)) {
            d3d_context_->CopyResource(staging_[wr_], tex);
            copied = true;
        }
        tex->Release();
    }
    res->Release();
    stage_ = 3;
    duplication_->ReleaseFrame();
    stage_ = 4;

    if (!copied) return false;

    // читаем копию предыдущего кадра (GPU с ней уже закончил), текущую оставляем на следующий раз
    const int rd = pending_ ? 1 - wr_ : wr_;
    const bool ok = convert_staging_to_nv12(staging_[rd], out_nv12_data, out_pitch);
    wr_ = 1 - wr_;
    pending_ = true;
    if (ok) have_image_ = true;
    return ok;
}

bool DXGICapture::convert_staging_to_nv12(ID3D11Texture2D* st, std::vector<uint8_t>& out, int& out_pitch) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(d3d_context_->Map(st, 0, D3D11_MAP_READ, 0, &m))) return false;

    const int W = (int)staging_w_, H = (int)staging_h_;
    out_pitch = W;
    out.resize((size_t)W * H * 3 / 2);

    const uint8_t* src = static_cast<const uint8_t*>(m.pData);
    const size_t sp = m.RowPitch;
    uint8_t* yp = out.data();
    uint8_t* uvp = out.data() + (size_t)W * H;

    std::vector<int> rows(H / 2);
    std::iota(rows.begin(), rows.end(), 0);

    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](int j) {
        const uint8_t* s0 = src + (size_t)(2 * j) * sp;
        const uint8_t* s1 = s0 + sp;
        uint8_t* d0 = yp + (size_t)(2 * j) * W;
        uint8_t* d1 = d0 + W;
        uint8_t* uv = uvp + (size_t)j * W;

        for (int x = 0; x < W; x += 2) {
            const uint8_t* a = s0 + x * 4;   // верхний левый пиксель блока 2x2
            const uint8_t* b = a + 4;        // верхний правый
            const uint8_t* c = s1 + x * 4;   // нижний левый
            const uint8_t* d = c + 4;        // нижний правый

            d0[x]     = (uint8_t)(((66 * a[2] + 129 * a[1] + 25 * a[0] + 128) >> 8) + 16);
            d0[x + 1] = (uint8_t)(((66 * b[2] + 129 * b[1] + 25 * b[0] + 128) >> 8) + 16);
            d1[x]     = (uint8_t)(((66 * c[2] + 129 * c[1] + 25 * c[0] + 128) >> 8) + 16);
            d1[x + 1] = (uint8_t)(((66 * d[2] + 129 * d[1] + 25 * d[0] + 128) >> 8) + 16);

            const int bb = a[0], gg = a[1], rr = a[2];   // как и раньше, цвет берём из левого верхнего пикселя
            uv[x]     = (uint8_t)(((-38 * rr - 74 * gg + 112 * bb + 128) >> 8) + 128);
            uv[x + 1] = (uint8_t)(((112 * rr - 94 * gg - 18 * bb + 128) >> 8) + 128);
        }
    });

    d3d_context_->Unmap(st, 0);
    return true;
}
