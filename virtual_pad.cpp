#include "virtual_pad.h"
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <ViGEm/Client.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <functional>
#pragma comment(lib, "setupapi.lib")

static VOID CALLBACK ds4_notify(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large_motor, UCHAR small_motor,
                                DS4_LIGHTBAR_COLOR led, LPVOID user) {
    static_cast<VirtualDS4*>(user)->on_output(large_motor, small_motor, led.Red, led.Green, led.Blue);
}

void VirtualDS4::set_output_callback(OutputCb cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    out_cb_ = std::move(cb);
}

void VirtualDS4::on_output(uint8_t l, uint8_t sm, uint8_t r, uint8_t g, uint8_t b) {
    OutputCb cb;
    { std::lock_guard<std::mutex> lk(mtx_); cb = out_cb_; }
    if (cb) cb(l, sm, r, g, b);
}

static uint8_t axis_to_byte(int16_t v) { return (uint8_t)(((int)v + 32768) >> 8); }

static VOID CALLBACK x360_notify(PVIGEM_CLIENT, PVIGEM_TARGET, UCHAR large, UCHAR small_, UCHAR, LPVOID user) {
    static_cast<VirtualDS4*>(user)->on_output(large, small_, 0, 0, 0);
}

static int16_t byte_to_axis(uint8_t b, bool invert) {
    int v = (int(b) - 128) * 256;
    if (invert) v = -v;                       // у DS4 «вверх» = 0, у Xbox «вверх» = плюс
    return (int16_t)std::max(-32768, std::min(32767, v));
}

void VirtualDS4::flush_x360() {
    XUSB_REPORT r;
    XUSB_REPORT_INIT(&r);
    uint16_t w = 0;
    if (buttons_ & DS4_BUTTON_CROSS)          w |= XUSB_GAMEPAD_A;
    if (buttons_ & DS4_BUTTON_CIRCLE)         w |= XUSB_GAMEPAD_B;
    if (buttons_ & DS4_BUTTON_SQUARE)         w |= XUSB_GAMEPAD_X;
    if (buttons_ & DS4_BUTTON_TRIANGLE)       w |= XUSB_GAMEPAD_Y;
    if (buttons_ & DS4_BUTTON_SHOULDER_LEFT)  w |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (buttons_ & DS4_BUTTON_SHOULDER_RIGHT) w |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    if (buttons_ & DS4_BUTTON_OPTIONS)        w |= XUSB_GAMEPAD_START;
    if (buttons_ & DS4_BUTTON_SHARE)          w |= XUSB_GAMEPAD_BACK;
    if (buttons_ & DS4_BUTTON_THUMB_LEFT)     w |= XUSB_GAMEPAD_LEFT_THUMB;
    if (buttons_ & DS4_BUTTON_THUMB_RIGHT)    w |= XUSB_GAMEPAD_RIGHT_THUMB;
    if (dpad_ & 1) w |= XUSB_GAMEPAD_DPAD_UP;
    if (dpad_ & 2) w |= XUSB_GAMEPAD_DPAD_DOWN;
    if (dpad_ & 4) w |= XUSB_GAMEPAD_DPAD_LEFT;
    if (dpad_ & 8) w |= XUSB_GAMEPAD_DPAD_RIGHT;
    if (special_ & DS4_SPECIAL_BUTTON_PS) w |= XUSB_GAMEPAD_GUIDE;
    r.wButtons = w;
    r.bLeftTrigger = trig_l_;
    r.bRightTrigger = trig_r_;
    r.sThumbLX = byte_to_axis(lx_, false);
    r.sThumbLY = byte_to_axis(ly_, true);
    r.sThumbRX = byte_to_axis(rx_, false);
    r.sThumbRY = byte_to_axis(ry_, true);
    vigem_target_x360_update((PVIGEM_CLIENT)client_, (PVIGEM_TARGET)target_, r);
}

bool VirtualDS4::init() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (client_) return true;
    PVIGEM_CLIENT c = vigem_alloc();
    if (!c) return false;
    if (!VIGEM_SUCCESS(vigem_connect(c))) {
        std::cerr << "[pad] ViGEmBus not found (is the driver installed?)\n";
        vigem_free(c);
        return false;
    }
    mode_ = s_mode;
    if (mode_ == PadMode::X360) {
        PVIGEM_TARGET t = vigem_target_x360_alloc();
        if (!VIGEM_SUCCESS(vigem_target_add(c, t))) {
            std::cerr << "[pad] failed to plug in virtual Xbox 360 controller\n";
            vigem_target_free(t); vigem_disconnect(c); vigem_free(c);
            return false;
        }
        vigem_target_x360_register_notification(c, t, x360_notify, this);
        client_ = c; target_ = t;
        std::cout << "[pad] virtual Xbox 360 controller created\n";
        return true;
    }    
    PVIGEM_TARGET t = vigem_target_ds4_alloc();
    if (!VIGEM_SUCCESS(vigem_target_add(c, t))) {
        std::cerr << "[pad] failed to plug in virtual DS4\n";
        vigem_target_ds4_register_notification((PVIGEM_CLIENT)client_, (PVIGEM_TARGET)target_, &ds4_notify, this);
        vigem_target_free(t);
        vigem_disconnect(c);
        vigem_free(c);
        return false;
    }
    client_ = c; target_ = t;
    std::cout << "[pad] virtual DualShock 4 created\n";
    return true;
}

void VirtualDS4::shutdown() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return;
    vigem_target_remove((PVIGEM_CLIENT)client_, (PVIGEM_TARGET)target_);
    vigem_target_free((PVIGEM_TARGET)target_);
    vigem_disconnect((PVIGEM_CLIENT)client_);
    vigem_free((PVIGEM_CLIENT)client_);
    client_ = target_ = nullptr;
}

void VirtualDS4::set_sticks(int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    std::lock_guard<std::mutex> lk(mtx_);
    lx_ = axis_to_byte(lx); ly_ = axis_to_byte(ly);
    rx_ = axis_to_byte(rx); ry_ = axis_to_byte(ry);
}

void VirtualDS4::reset() {
    {
        printf("[pad] reset()\n");
        std::lock_guard<std::mutex> lk(mtx_);
        lx_ = ly_ = rx_ = ry_ = 0x80;
        buttons_ = 0; special_ = 0; trig_l_ = trig_r_ = 0; dpad_ = 0;
    }
    flush();    // flush сам берёт мьютекс, поэтому вызываем после блока
    touch_[0] = touch_[1] = Touch();
    gyro_[0] = gyro_[1] = gyro_[2] = 0; accel_[0] = accel_[1] = accel_[2] = 0;
}

void VirtualDS4::set_motion(const float g[3], const float a[3]) {
    auto cl = [](float v) { return (int16_t)(std::max)(-32767.0f, (std::min)(32767.0f, v)); };
    std::lock_guard<std::mutex> lk(mtx_);
    for (int k = 0; k < 3; ++k) {
        gyro_[k]  = cl(g[k] * 57.29578f * 16.0f);     // рад/с -> °/с -> единицы DS4
        accel_[k] = cl(a[k] * 8192.0f);               // g -> единицы DS4
    }
}

void VirtualDS4::apply_event(uint8_t code, uint8_t st) {
    const bool on = st != 0;
    switch (code) {
    case 0x88: set_btn(DS4_BUTTON_CROSS, on); break;
    case 0x89: set_btn(DS4_BUTTON_CIRCLE, on); break;
    case 0x8a: set_btn(DS4_BUTTON_SQUARE, on); break;
    case 0x8b: set_btn(DS4_BUTTON_TRIANGLE, on); break;
    case 0x80: dpad_ = on ? (dpad_ | 1) : (dpad_ & ~1); break;
    case 0x81: dpad_ = on ? (dpad_ | 2) : (dpad_ & ~2); break;
    case 0x82: dpad_ = on ? (dpad_ | 4) : (dpad_ & ~4); break;
    case 0x83: dpad_ = on ? (dpad_ | 8) : (dpad_ & ~8); break;
    case 0x84: set_btn(DS4_BUTTON_SHOULDER_LEFT, on); break;
    case 0x85: set_btn(DS4_BUTTON_SHOULDER_RIGHT, on); break;
    case 0x86: trig_l_ = st; set_btn(DS4_BUTTON_TRIGGER_LEFT, on); break;
    case 0x87: trig_r_ = st; set_btn(DS4_BUTTON_TRIGGER_RIGHT, on); break;
    case 0x8c: set_btn(DS4_BUTTON_OPTIONS, on); break;
    case 0x8d: set_btn(DS4_BUTTON_SHARE, on); break;
    case 0x8e: special_ = on ? (special_ | DS4_SPECIAL_BUTTON_PS) : (special_ & ~DS4_SPECIAL_BUTTON_PS); break;
    case 0x8f: set_btn(DS4_BUTTON_THUMB_LEFT, on); break;
    case 0x90: set_btn(DS4_BUTTON_THUMB_RIGHT, on); break;
    case 0x91: special_ = on ? (special_ | DS4_SPECIAL_BUTTON_TOUCHPAD) : (special_ & ~DS4_SPECIAL_BUTTON_TOUCHPAD); break;
    default: break;
    }
}

void VirtualDS4::apply_history(const uint8_t* p, size_t n) {
    struct Ev { uint8_t code, st; };
    struct TEv { int id; bool down; uint16_t x, y; };
    std::vector<Ev> evs;
    std::vector<TEv> tevs;                                // от новых к старым
    size_t i = 0;
    while (i < n) {
        if (p[i] == 0x80 && i + 1 < n) {
            const uint8_t code = p[i + 1];
            const bool one = (code >= 0x8c && code <= 0x91) || (code >= 0xac && code <= 0xb1);
            if (one) {
                const bool pressed = code >= 0xac;
                evs.push_back({ (uint8_t)((pressed ? code - 0xac : code - 0x8c) + 0x8c), (uint8_t)(pressed ? 1 : 0) });
                i += 2;
            } else {
                if (i + 2 >= n) break;
                evs.push_back({ code, p[i + 2] });
                i += 3;
            }
        } else if (p[i] == 0xd0 || p[i] == 0xc0) {
            if (i + 5 > n) break;
            tevs.push_back({ p[i + 1] & 0x7f, p[i] == 0xd0,
                             (uint16_t)((p[i + 2] << 4) | (p[i + 3] >> 4)),
                             (uint16_t)(((p[i + 3] & 0x0f) << 8) | p[i + 4]) });
            i += 5;
        } else {
            break;
        }
    }

    std::lock_guard<std::mutex> lk(mtx_);
    for (size_t k = evs.size(); k-- > 0; )
        apply_event(evs[k].code, evs[k].st);

    std::vector<int> seen;                                // берём только самое новое событие каждого пальца
    for (const TEv& t : tevs) {
        if (std::find(seen.begin(), seen.end(), t.id) != seen.end()) continue;
        seen.push_back(t.id);
        int slot = -1;
        for (int k = 0; k < 2; ++k) if (touch_[k].active && touch_[k].id == t.id) slot = k;
        if (t.down) {
            if (slot < 0) {
                for (int k = 0; k < 2; ++k) if (!touch_[k].active) { slot = k; break; }
                if (slot < 0) continue;
                touch_[slot].active = true;
                touch_[slot].id = t.id;
                touch_[slot].track = (track_counter_++) & 0x7f;
            }
            touch_[slot].x = std::min<uint16_t>(t.x, 1919);
            touch_[slot].y = std::min<uint16_t>(t.y, 942);
        } else if (slot >= 0) {
            touch_[slot].active = false;
        }
    }
}

void VirtualDS4::flush(char src) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return;
    if (mode_ == PadMode::X360) { flush_x360(); return; }
    DS4_REPORT r;
    DS4_REPORT_INIT(&r);
    r.bThumbLX = lx_; r.bThumbLY = ly_; r.bThumbRX = rx_; r.bThumbRY = ry_;
    r.wButtons = buttons_;
    const bool u = dpad_ & 1, d = dpad_ & 2, l = dpad_ & 4, rt = dpad_ & 8;
    DS4_DPAD_DIRECTIONS hat = DS4_BUTTON_DPAD_NONE;
    if (u && rt) hat = DS4_BUTTON_DPAD_NORTHEAST;
    else if (d && rt) hat = DS4_BUTTON_DPAD_SOUTHEAST;
    else if (d && l) hat = DS4_BUTTON_DPAD_SOUTHWEST;
    else if (u && l) hat = DS4_BUTTON_DPAD_NORTHWEST;
    else if (u) hat = DS4_BUTTON_DPAD_NORTH;
    else if (rt) hat = DS4_BUTTON_DPAD_EAST;
    else if (d) hat = DS4_BUTTON_DPAD_SOUTH;
    else if (l) hat = DS4_BUTTON_DPAD_WEST;
    DS4_SET_DPAD(&r, hat);
    r.bSpecial = special_;
    r.bTriggerL = trig_l_; r.bTriggerR = trig_r_;
    static uint16_t lb = 0xffff; static uint8_t lsp = 0xff, a = 0xff, b = 0xff, c = 0xff, d2 = 0xff;

    DS4_REPORT_EX ex;
    memset(&ex, 0, sizeof(ex));
    ex.Report.bThumbLX = r.bThumbLX; ex.Report.bThumbLY = r.bThumbLY;
    ex.Report.bThumbRX = r.bThumbRX; ex.Report.bThumbRY = r.bThumbRY;
    ex.Report.wButtons = r.wButtons;
    ex.Report.bSpecial = r.bSpecial;
    ex.Report.bTriggerL = r.bTriggerL; ex.Report.bTriggerR = r.bTriggerR;
    ex.Report.bBatteryLvlSpecial = 0x1b;                 // предположение: питание от USB, полный заряд

    auto enc = [](uint8_t& up, uint8_t* d, const Touch& t) {
        up = t.active ? t.track : (uint8_t)(0x80 | t.track);
        d[0] = t.x & 0xff;
        d[1] = ((t.x >> 8) & 0x0f) | ((t.y & 0x0f) << 4);
        d[2] = t.y >> 4;
    };
    ex.Report.bTouchPacketsN = 1;
    ex.Report.sCurrentTouch.bPacketCounter = ++pkt_counter_;
    enc(ex.Report.sCurrentTouch.bIsUpTrackingNum1, ex.Report.sCurrentTouch.bTouchData1, touch_[0]);
    enc(ex.Report.sCurrentTouch.bIsUpTrackingNum2, ex.Report.sCurrentTouch.bTouchData2, touch_[1]);
    ex.Report.wGyroX = gyro_[0];  ex.Report.wGyroY = gyro_[1];  ex.Report.wGyroZ = gyro_[2];
    ex.Report.wAccelX = accel_[0]; ex.Report.wAccelY = accel_[1]; ex.Report.wAccelZ = accel_[2];
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    ex.Report.wTimestamp = (uint16_t)((us * 3 / 16) & 0xffff);   // единицы по 5,33 мкс
    vigem_target_ds4_update_ex((PVIGEM_CLIENT)client_, (PVIGEM_TARGET)target_, ex);
}

// ---------- Xbox 360 ----------

void VirtualX360::set_output_callback(OutputCb cb) {
    std::lock_guard<std::mutex> lk(mtx_);
    out_cb_ = std::move(cb);
}

void VirtualX360::on_output(uint8_t large_motor, uint8_t small_motor) {
    OutputCb cb;
    { std::lock_guard<std::mutex> lk(mtx_); cb = out_cb_; }
    if (cb) cb(large_motor, small_motor, 0, 0, 0);
}

bool VirtualX360::init() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (client_) return true;
    PVIGEM_CLIENT c = vigem_alloc();
    if (!c) return false;
    if (!VIGEM_SUCCESS(vigem_connect(c))) {
        std::cerr << "[pad] ViGEmBus not found (is the driver installed?)\n";
        vigem_free(c);
        return false;
    }
    PVIGEM_TARGET t = vigem_target_x360_alloc();
    if (!VIGEM_SUCCESS(vigem_target_add(c, t))) {
        std::cerr << "[pad] failed to plug in virtual Xbox 360\n";
        vigem_target_free(t);
        vigem_disconnect(c);
        vigem_free(c);
        return false;
    }
    vigem_target_x360_register_notification(c, t, &x360_notify, this);
    client_ = c; target_ = t;
    std::cout << "[pad] virtual Xbox 360 created\n";
    return true;
}

void VirtualX360::shutdown() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return;
    vigem_target_x360_unregister_notification((PVIGEM_TARGET)target_);
    vigem_target_remove((PVIGEM_CLIENT)client_, (PVIGEM_TARGET)target_);
    vigem_target_free((PVIGEM_TARGET)target_);
    vigem_disconnect((PVIGEM_CLIENT)client_);
    vigem_free((PVIGEM_CLIENT)client_);
    client_ = target_ = nullptr;
}

void VirtualX360::set_sticks(int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    auto inv = [](int16_t v) -> int16_t { return v == -32768 ? 32767 : (int16_t)-v; };  // у XInput вверх = плюс
    std::lock_guard<std::mutex> lk(mtx_);
    lx_ = lx; ly_ = inv(ly); rx_ = rx; ry_ = inv(ry);
}

void VirtualX360::reset() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        buttons_ = 0; trig_l_ = trig_r_ = 0;
        lx_ = ly_ = rx_ = ry_ = 0;
    }
    flush();
}

void VirtualX360::apply_event(uint8_t code, uint8_t st) {
    const bool on = st != 0;
    switch (code) {
    case 0x88: set_btn(XUSB_GAMEPAD_A, on); break;               // Cross
    case 0x89: set_btn(XUSB_GAMEPAD_B, on); break;               // Circle
    case 0x8a: set_btn(XUSB_GAMEPAD_X, on); break;               // Square
    case 0x8b: set_btn(XUSB_GAMEPAD_Y, on); break;               // Triangle
    case 0x80: set_btn(XUSB_GAMEPAD_DPAD_UP, on); break;
    case 0x81: set_btn(XUSB_GAMEPAD_DPAD_DOWN, on); break;
    case 0x82: set_btn(XUSB_GAMEPAD_DPAD_LEFT, on); break;
    case 0x83: set_btn(XUSB_GAMEPAD_DPAD_RIGHT, on); break;
    case 0x84: set_btn(XUSB_GAMEPAD_LEFT_SHOULDER, on); break;   // L1
    case 0x85: set_btn(XUSB_GAMEPAD_RIGHT_SHOULDER, on); break;  // R1
    case 0x86: trig_l_ = st; break;                              // L2 (аналоговый)
    case 0x87: trig_r_ = st; break;                              // R2
    case 0x8c: set_btn(XUSB_GAMEPAD_START, on); break;           // Options
    case 0x8d: set_btn(XUSB_GAMEPAD_BACK, on); break;            // Share/Create
    case 0x8e: set_btn(XUSB_GAMEPAD_GUIDE, on); break;           // PS
    case 0x8f: set_btn(XUSB_GAMEPAD_LEFT_THUMB, on); break;      // L3
    case 0x90: set_btn(XUSB_GAMEPAD_RIGHT_THUMB, on); break;     // R3
    default: break;                                              // тачпад не используется
    }
}

void VirtualX360::apply_history(const uint8_t* p, size_t n) {
    struct Ev { uint8_t code, st; };
    std::vector<Ev> evs;
    size_t i = 0;
    while (i < n) {
        if (p[i] == 0x80 && i + 1 < n) {
            const uint8_t code = p[i + 1];
            const bool one = (code >= 0x8c && code <= 0x91) || (code >= 0xac && code <= 0xb1);
            if (one) {
                const bool pressed = code >= 0xac;
                evs.push_back({ (uint8_t)((pressed ? code - 0xac : code - 0x8c) + 0x8c), (uint8_t)(pressed ? 1 : 0) });
                i += 2;
            } else {
                if (i + 2 >= n) break;
                evs.push_back({ code, p[i + 2] });
                i += 3;
            }
        } else if (p[i] == 0xd0 || p[i] == 0xc0) {
            if (i + 5 > n) break;
            i += 5;                                              // касания тачпада пропускаем
        } else {
            break;
        }
    }
    std::lock_guard<std::mutex> lk(mtx_);
    for (size_t k = evs.size(); k-- > 0; )
        apply_event(evs[k].code, evs[k].st);
}

void VirtualX360::flush(char) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!client_) return;
    XUSB_REPORT r;
    XUSB_REPORT_INIT(&r);
    r.wButtons = buttons_;
    r.bLeftTrigger = trig_l_; r.bRightTrigger = trig_r_;
    r.sThumbLX = lx_; r.sThumbLY = ly_; r.sThumbRX = rx_; r.sThumbRY = ry_;
    vigem_target_x360_update((PVIGEM_CLIENT)client_, (PVIGEM_TARGET)target_, r);
}

// ---------- выбор типа ----------
static std::string g_pad_type = "ds4";
static std::unique_ptr<IVirtualPad> g_pad_inst;

void set_pad_type(const std::string& type) { g_pad_type = (type == "x360") ? "x360" : "ds4"; }

IVirtualPad& pad() {
    static std::mutex m;
    std::lock_guard<std::mutex> lk(m);
    if (!g_pad_inst) {
        if (g_pad_type == "x360") g_pad_inst.reset(new VirtualX360);
        else                      g_pad_inst.reset(new VirtualDS4);
    }
    return *g_pad_inst;
}