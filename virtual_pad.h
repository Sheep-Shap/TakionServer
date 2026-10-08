#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class IVirtualPad {
public:
    using OutputCb = std::function<void(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t)>;
    virtual ~IVirtualPad() = default;
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual void set_sticks(int16_t lx, int16_t ly, int16_t rx, int16_t ry) = 0;
    virtual void set_motion(const float g[3], const float a[3]) = 0;
    virtual void apply_history(const uint8_t* p, size_t n) = 0;
    virtual void flush(char src = '?') = 0;
    virtual void reset() = 0;
    virtual void set_output_callback(OutputCb cb) = 0;
};

enum class PadMode { DS4, X360 };

class VirtualDS4 : public IVirtualPad {
public:
    static void set_global_mode(PadMode m) { s_mode = m; }
    ~VirtualDS4() override { shutdown(); }
    bool init() override;
    void shutdown() override;
    void set_sticks(int16_t lx, int16_t ly, int16_t rx, int16_t ry) override;
    void set_motion(const float g[3], const float a[3]) override;
    void apply_history(const uint8_t* p, size_t n) override;
    void flush(char src = '?') override;
    void reset() override;
    void set_output_callback(OutputCb cb) override;
    void on_output(uint8_t large_motor, uint8_t small_motor, uint8_t r, uint8_t g, uint8_t b);
private:
    static inline PadMode s_mode = PadMode::DS4;
    PadMode mode_ = PadMode::DS4;
    void flush_x360();
    void apply_event(uint8_t code, uint8_t st);
    void set_btn(uint16_t mask, bool on) { if (on) buttons_ |= mask; else buttons_ &= ~mask; }

    struct Touch { bool active = false; int id = -1; uint16_t x = 0, y = 0; uint8_t track = 0; };
    Touch touch_[2];
    std::mutex mtx_;
    OutputCb out_cb_;
    void* client_ = nullptr;
    void* target_ = nullptr;
    uint8_t lx_ = 0x80, ly_ = 0x80, rx_ = 0x80, ry_ = 0x80;
    uint16_t buttons_ = 0;
    int16_t gyro_[3] = {0, 0, 0};
    int16_t accel_[3] = {0, 0, 0};
    uint8_t track_counter_ = 0;
    uint8_t pkt_counter_ = 0;
    uint8_t special_ = 0;
    uint8_t trig_l_ = 0, trig_r_ = 0;
    uint8_t dpad_ = 0;
};

class VirtualX360 : public IVirtualPad {
public:
    ~VirtualX360() override { shutdown(); }
    bool init() override;
    void shutdown() override;
    void set_sticks(int16_t lx, int16_t ly, int16_t rx, int16_t ry) override;
    void set_motion(const float*, const float*) override {}      // у Xbox 360 нет гироскопа
    void apply_history(const uint8_t* p, size_t n) override;
    void flush(char src = '?') override;
    void reset() override;
    void set_output_callback(OutputCb cb) override;
    void on_output(uint8_t large_motor, uint8_t small_motor);
private:
    void apply_event(uint8_t code, uint8_t st);
    void set_btn(uint16_t mask, bool on) { if (on) buttons_ |= mask; else buttons_ &= ~mask; }

    std::mutex mtx_;
    OutputCb out_cb_;
    void* client_ = nullptr;
    void* target_ = nullptr;
    uint16_t buttons_ = 0;
    uint8_t trig_l_ = 0, trig_r_ = 0;
    int16_t lx_ = 0, ly_ = 0, rx_ = 0, ry_ = 0;
};

void set_pad_type(const std::string& type);   // "ds4" или "x360"; вызвать до start()
IVirtualPad& pad();                           // создаёт нужный геймпад при первом обращении