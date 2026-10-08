// takion_server.cpp
#include "takion_server.h"
#include "takion_proto.h"
#include "crypto.h"
#include "gkcrypt.h"
#include "virtual_pad.h"
#include "audio_streamer.h"
#include "audio_packet.h"
#include "nvenc_encoder.h"
#include "fec_rs.h"

#define NOMINMAX

#include <array>
#include <cstring>
#include <iostream>
#include <random>
#include <deque>
#include <condition_variable>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <string>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/bn.h>

#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>

#pragma comment(lib, "bcrypt.lib")

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Константы видео
static constexpr int VIDEO_WIDTH  = 1920;
static constexpr int VIDEO_HEIGHT = 1080;
static constexpr int VIDEO_FPS = 60;

// Takion AV константы
static constexpr uint8_t VIDEO_CODEC_H265 = 1;
static constexpr uint8_t AUDIO_CODEC_ID = 5;
static constexpr int AUDIO_SAMPLE_RATE = 48000;
static constexpr int AUDIO_CHANNELS = 2;
static constexpr int AUDIO_BITS = 16;
static constexpr int AUDIO_FRAME_SAMPLES = 960;  // 20ms @ 48kHz
static constexpr int AUDIO_FRAME_BYTES =
    AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS * (AUDIO_BITS / 8);

static constexpr size_t TAKION_VIDEO_WIREDATA_OFFSET = 0x15;
static constexpr size_t TAKION_VIDEO_MAC_OFFSET = 0x0A;
static constexpr size_t TAKION_VIDEO_KEYPOS_OFFSET = 0x0E;

static constexpr size_t TAKION_AUDIO_WIREDATA_OFFSET = 0x14;
static constexpr size_t TAKION_AUDIO_MAC_OFFSET = 0x0A;
static constexpr size_t TAKION_AUDIO_KEYPOS_OFFSET = 0x0E;

static constexpr size_t VIDEO_UNIT_DATA_SIZE = 1200;
static constexpr size_t VIDEO_UNIT_HEVC_SIZE = VIDEO_UNIT_DATA_SIZE - 2;

static constexpr uint32_t ARWND = 0x19000;
static constexpr uint16_t OUTBOUND_STREAMS = 0x64;
static constexpr uint16_t INBOUND_STREAMS = 0x64;

// HEVC header
static const uint8_t DEFAULT_HEVC_HEADER[] = {
    // VPS
    0x00,0x00,0x00,0x01,0x40,0x01,0x0C,0x01,0xFF,0xFF,0x01,0x60,0x00,0x00,0x03,0x00,
    0x90,0x00,0x00,0x03,0x00,0x00,0x03,0x00,0x78,0x95,0x94,0x09,
    // SPS
    0x00,0x00,0x00,0x01,0x42,0x01,0x01,0x01,0x60,0x00,0x00,0x03,0x00,0x90,0x00,0x00,
    0x03,0x00,0x00,0x03,0x00,0x78,0xA0,0x02,0x80,0x80,0x2D,0x16,0x59,0x59,0x52,0x93,
    0x0B,0xC0,0x5A,0x02,0x00,0x00,0x03,0x00,0x02,0x00,0x00,0x03,0x00,0x78,0x10,
    // PPS
    0x00,0x00,0x00,0x01,0x44,0x01,0xC0,0x73,0xC1,0x89
};

static std::vector<uint8_t> g_hevc_header(std::begin(DEFAULT_HEVC_HEADER),
                                          std::end(DEFAULT_HEVC_HEADER));

static constexpr uint32_t IDR_REFRESH_EVERY_N_FRAMES = 60;

TakionServer::TakionServer() {
    if (NvencEncoder::available()) {
        std::cout << "[enc] NVIDIA GPU found, using NVENC\n";
        encoder_ = std::make_unique<NvencEncoder>();
    } else {
        std::cout << "[enc] using AMD AMF\n";
        encoder_ = std::make_unique<AMFEncoder>();
    }
    capture_ = std::make_unique<DXGICapture>();
    pad().set_output_callback([this](uint8_t large_motor, uint8_t small_motor, uint8_t, uint8_t, uint8_t) {
        on_pad_output(large_motor, small_motor);
    });
}

TakionServer::~TakionServer() {
    stop();
}

static void write_u16_be(uint8_t* dst, uint16_t value) {
    dst[0] = static_cast<uint8_t>((value >> 8) & 0xff);
    dst[1] = static_cast<uint8_t>(value & 0xff);
}

static void write_u32_be(uint8_t* dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>((value >> 24) & 0xff);
    dst[1] = static_cast<uint8_t>((value >> 16) & 0xff);
    dst[2] = static_cast<uint8_t>((value >> 8) & 0xff);
    dst[3] = static_cast<uint8_t>(value & 0xff);
}

static void sign_control(const GKCrypt& gk, uint8_t* pkt, size_t n, uint32_t keypos) {
    write_u32_be(pkt + 9, keypos);
    uint8_t saved[4];
    memcpy(saved, pkt + 9, 4);
    memset(pkt + 5, 0, 4);
    memset(pkt + 9, 0, 4);
    uint8_t mac[4] = {0, 0, 0, 0};
    gk.gmac(keypos, pkt, n, mac);
    memcpy(pkt + 5, mac, 4);
    memcpy(pkt + 9, saved, 4);
}

static void sign_packet_locked(ClientSession& s, uint8_t* pkt, size_t n) {
    if (!s.gk_out) return;                      // до BANG подписи нет
    uint32_t kp = s.keypos_out;
    s.keypos_out += (uint32_t)(((n - 17) + 15) & ~15u);
    sign_control(*s.gk_out, pkt, n, kp);
}

static void sign_packet(ClientSession& s, uint8_t* pkt, size_t n) {
    std::lock_guard<std::mutex> lk(s.send_mtx);
    sign_packet_locked(s, pkt, n);
}

static uint32_t random_u32() {
    std::random_device rd;
    return (static_cast<uint32_t>(rd()) << 16) ^
           static_cast<uint32_t>(rd());
}

static std::vector<uint8_t> extract_param_sets(const std::vector<uint8_t>& f);

bool TakionServer::start(uint16_t port) {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "Failed to initialize WinSock\n";
        return false;
    }

    udp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_ == INVALID_SOCKET) {
        std::cerr << "Failed to create UDP socket\n";
        return false;
    }

    int sndbuf = 8 * 1024 * 1024;
    setsockopt(udp_socket_, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(udp_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind to port " << port << "\n";
        closesocket(udp_socket_);
        udp_socket_ = INVALID_SOCKET;
        return false;
    }

    if (!encoder_->initialize(VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FPS,
                              (int)net_cfg_.max_bps, (int)(net_cfg_.max_bps * 5 / 4))) {
        std::cerr << "Failed to initialize video encoder\n";
        closesocket(udp_socket_);
        udp_socket_ = INVALID_SOCKET;
        return false;
    }
    
    printf("[cfg] max_bps=%u\n", (unsigned)net_cfg_.max_bps);

    if (!capture_->initialize(0)) {
        std::cerr << "Failed to initialize DXGI capture\n";
        encoder_->shutdown();
        closesocket(udp_socket_);
        udp_socket_ = INVALID_SOCKET;
        return false;
    }

    // Берём VPS/SPS/PPS из первого кадра энкодера (энкодер и захват уже готовы)
    {
        std::vector<uint8_t> nv12, hevc;
        int pitch = 0;
        for (int tries = 0; tries < 100 && hevc.empty(); ++tries) {
            if (capture_->capture_frame(nv12, pitch))
                encoder_->encode_frame(nv12.data(), pitch, hevc);
            else
                Sleep(20);
        }
        std::vector<uint8_t> ps = extract_param_sets(hevc);
        if (!ps.empty()) {
            g_hevc_header = ps;
            std::cout << "[takion] HEVC header from encoder: " << ps.size() << " bytes\n";
        } else {
            std::cerr << "[takion] encoder gave no VPS/SPS/PPS, using DEFAULT_HEVC_HEADER\n";
        }
    }

    running_ = true;
    std::cout << "Takion server started on UDP port " << port << "\n";
    tx_run_ = true;
    tx_thread_ = std::thread([this] { tx_loop(); });
    return true;
}

void TakionServer::stop() {
    tx_run_ = false;
    tx_cv_.notify_all();
    if (tx_thread_.joinable()) tx_thread_.join();
    if (stopped_.exchange(true)) return;          // stop() зовут и вручную, и из деструктора
    pad().set_output_callback(nullptr); Sleep(20);
    running_ = false;

    for (int i = 0; i < 500 && active_video_threads_ > 0; ++i) Sleep(10);   // ждём видеопотоки, до 5 с
    if (active_video_threads_ > 0)
        std::cerr << "[takion] warning: video thread still running at shutdown\n";

    if (capture_) capture_->shutdown();
    if (encoder_) encoder_->shutdown();

    if (udp_socket_ != INVALID_SOCKET) {
        closesocket(udp_socket_);
        udp_socket_ = INVALID_SOCKET;
    }
    WSACleanup();
}

void TakionServer::on_congestion(const std::vector<uint8_t>& d) {
    const unsigned received = ((unsigned)d[3] << 8) | d[4];
    const unsigned lost     = ((unsigned)d[5] << 8) | d[6];
    static int shown = 0;
    if (shown < 5) { ++shown; printf("[net] report: received=%u lost=%u\n", received, lost); }

    net_recv_ += received;
    net_lost_ += lost;
    const int64_t t = now_ms();
    if (net_log_ms_ == 0) net_log_ms_ = t;
    if (t - net_log_ms_ >= 5000) {
        const uint64_t total = net_recv_ + net_lost_;
        printf("[net] last 5 s: received %llu, lost %llu (%.1f%%)\n",
               (unsigned long long)net_recv_, (unsigned long long)net_lost_,
               total ? 100.0 * (double)net_lost_ / (double)total : 0.0);
        net_recv_ = net_lost_ = 0;
        net_log_ms_ = t;
    }

    if (net_cfg_.adaptive && bitrate_ctl_.on_report(t, received, lost)) {
        target_bps_ = bitrate_ctl_.bitrate();
        printf("[net] bitrate -> %.1f Mbps (smoothed loss %.1f%%, baseline %.1f%%)\n",
               target_bps_ / 1e6, bitrate_ctl_.smoothed_loss() * 100.0, bitrate_ctl_.baseline() * 100.0);
    }
}

void TakionServer::run() {
    while (running_) {
        char buffer[4096];
        sockaddr_in client_addr = {};
        int client_addr_size = sizeof(client_addr);

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(udp_socket_, &read_fds);

        timeval timeout = {1, 0};
        int select_result = select(0, &read_fds, nullptr, nullptr, &timeout);

        if (select_result > 0 && FD_ISSET(udp_socket_, &read_fds)) {
            int recv_size = recvfrom(
                udp_socket_,
                reinterpret_cast<char*>(buffer),
                sizeof(buffer),
                0,
                reinterpret_cast<sockaddr*>(&client_addr),
                &client_addr_size
            );

            static int64_t last_gc = 0;
            const int64_t t = now_ms();
            if (t - last_gc > 5000) { last_gc = t; gc_sessions(); }

            if (recv_size > 0) {
                std::vector<uint8_t> packet(buffer, buffer + recv_size);
                handle_packet(packet, client_addr);
            }
        }
    }
}

void TakionServer::gc_sessions() {
    const int64_t t = now_ms();
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        ClientSession& s = it->second;
        const int64_t last_rx = s.last_rx_ms.load();
        const int64_t last = last_rx > s.created_ms ? last_rx : s.created_ms;
        if (!s.video_started && t - last > 60000) {
            std::cout << "[session] removing idle session " << it->first << "\n";
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void TakionServer::handle_init(
    const std::vector<uint8_t>& payload,
    const sockaddr_in& addr
) {
    if (payload.size() < 33) {
        printf("[takion] INIT dropped: expected >= 33 bytes, got %zu\n",
               payload.size());
        return;
    }

    const uint32_t client_tag =
        (static_cast<uint32_t>(payload[29]) << 24) |
        (static_cast<uint32_t>(payload[30]) << 16) |
        (static_cast<uint32_t>(payload[31]) << 8) |
        static_cast<uint32_t>(payload[32]);

    const std::string session_key =
        std::to_string(ntohs(addr.sin_port));

    ClientSession& session = sessions_[session_key];
    session.client_tag = client_tag;
    if (session.created_ms == 0) session.created_ms = now_ms();

    std::cout << "[takion] INIT received from "
              << inet_ntoa(addr.sin_addr)
              << ":" << ntohs(addr.sin_port)
              << ", client_tag=0x"
              << std::hex << session.client_tag
              << std::dec << "\n";

    send_init_ack(addr, session);
}

static uint64_t extend_keypos(uint64_t& prev, uint32_t low) {
    uint32_t prev_low = (uint32_t)prev, high = (uint32_t)(prev >> 32);
    int32_t diff = (int32_t)(low - prev_low);
    if (diff > 0 && low < prev_low) ++high;
    else if (diff < 0 && low > prev_low) --high;
    return ((uint64_t)high << 32) | low;
}

struct PadState { float gyro[3], accel[3], quat[4]; int16_t lx, ly, rx, ry; };

static bool parse_pad_state(const std::vector<uint8_t>& p, PadState& s) {
    if (p.size() < 0x1c || p[0] != 0xa0) return false;
    auto u16le = [&](size_t o) { return (uint16_t)(p[o] | (p[o + 1] << 8)); };
    auto i16be = [&](size_t o) { return (int16_t)((p[o] << 8) | p[o + 1]); };
    for (int i = 0; i < 3; ++i) {
        s.gyro[i]  = u16le(1 + 2 * i) * (60.0f / 65535.0f) - 30.0f;
        s.accel[i] = u16le(7 + 2 * i) * (10.0f / 65535.0f) - 5.0f;
    }
    s.lx = i16be(0x11); s.ly = i16be(0x13); s.rx = i16be(0x15); s.ry = i16be(0x17);
    return true;
}

void TakionServer::handle_packet(const std::vector<uint8_t>& data, const sockaddr_in& addr) {
    if (data.empty()) return;
    if (data.size() == 15 && data[0] == 0x05) { on_congestion(data); return; }

    if ((data[0] == 0x06 || data[0] == 0x01) && data.size() >= 12 + 4) {   // состояние геймпада / события кнопок
        const std::string session_key = std::to_string(ntohs(addr.sin_port));
        auto it = sessions_.find(session_key);
        ClientSession* s = (it != sessions_.end()) ? &it->second : nullptr;
        if (s && s->gk_in) {
            uint32_t low = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                           ((uint32_t)data[6] << 8)  |  (uint32_t)data[7];
            uint64_t kp = extend_keypos(s->in_keypos_prev, low);
            std::vector<uint8_t> pl(data.begin() + 12, data.end());
            s->gk_in->crypt(kp + 16, pl.data(), pl.size());
            s->in_keypos_prev = kp;
            s->last_rx_ms = now_ms();
            s->pad_active = true;

            const uint16_t seq = ((uint16_t)data[1] << 8) | data[2];
            if (data[0] == 0x01) {
                if (s->hist_seen && (int16_t)(seq - s->last_hist_seq) <= 0) return;   // дубликат или старый пакет
                s->hist_seen = true; s->last_hist_seq = seq;
            } else {
                if (s->state_seen && (int16_t)(seq - s->last_state_seq) <= 0) return;
                s->state_seen = true; s->last_state_seq = seq;
            }
            
            if (data[0] == 0x06) {
                PadState ps;
                if (parse_pad_state(pl, ps)) {
                    pad().set_sticks(ps.lx, ps.ly, ps.rx, ps.ry);
                    pad().set_motion(ps.gyro, ps.accel);
                    pad().flush('S');
                }
            } else {
                pad().apply_history(pl.data(), pl.size());
                pad().flush('H');
            }
        }
        return;
    }

    if (data[0] != 0x00) return;
    if (data.size() < 17) return;

    const uint8_t chunk_type = data[13];
    switch (chunk_type) {
    case 0x01: printf("[takion] INIT chunk detected\n");   handle_init(data, addr);   break;
    case 0x0A: printf("[takion] COOKIE chunk detected\n"); handle_cookie(data, addr); break;
    case 0x00: handle_data(data, addr); break;
    default: break;
    }
}

void TakionServer::handle_cookie(
    const std::vector<uint8_t>& data,
    const sockaddr_in& addr
) {
    constexpr size_t HEADER_SIZE = 17;
    constexpr size_t COOKIE_SIZE = 32;

    if (data.size() != HEADER_SIZE + COOKIE_SIZE) {
        std::cerr << "[takion] COOKIE dropped: invalid size "
                  << data.size() << "\n";
        return;
    }

    const std::string session_key =
        std::to_string(ntohs(addr.sin_port));

    auto it = sessions_.find(session_key);
    if (it == sessions_.end()) {
        std::cerr << "[takion] COOKIE dropped: missing session\n";
        return;
    }

    ClientSession& session = it->second;

    if (session.cookie.size() != COOKIE_SIZE ||
        memcmp(data.data() + HEADER_SIZE,
               session.cookie.data(),
               COOKIE_SIZE) != 0) {
        std::cerr << "[takion] COOKIE dropped: cookie mismatch\n";
        return;
    }

    std::cout << "[takion] COOKIE accepted\n";
    send_cookie_ack(addr, session);
}

void TakionServer::handle_data(
    const std::vector<uint8_t>& data,
    const sockaddr_in& addr
) {
    constexpr size_t HEADER_SIZE = 17;
    constexpr size_t DATA_HEADER_SIZE = 9;

    if (data.size() < HEADER_SIZE + DATA_HEADER_SIZE) {
        std::cerr << "[takion] DATA dropped: packet too short, size="
                  << data.size() << "\n";
        return;
    }

    const uint8_t chunk_flags = data[14];
    const uint16_t wire_payload_size =
        (static_cast<uint16_t>(data[15]) << 8) |
        static_cast<uint16_t>(data[16]);

    if (wire_payload_size < 4) {
        std::cerr << "[takion] DATA dropped: invalid wire payload size\n";
        return;
    }

    const size_t payload_size = wire_payload_size - 4;

    if (HEADER_SIZE + payload_size != data.size()) {
        std::cerr << "[takion] DATA dropped: size mismatch: packet="
                  << data.size()
                  << ", expected=" << HEADER_SIZE + payload_size
                  << "\n";
        return;
    }

    const uint8_t* payload = data.data() + HEADER_SIZE;

    const uint32_t seq_num =
        (static_cast<uint32_t>(payload[0]) << 24) |
        (static_cast<uint32_t>(payload[1]) << 16) |
        (static_cast<uint32_t>(payload[2]) << 8) |
        static_cast<uint32_t>(payload[3]);

    const uint16_t channel =
        (static_cast<uint16_t>(payload[4]) << 8) |
        static_cast<uint16_t>(payload[5]);

    const uint8_t data_type = payload[8] & 0x0f;

    const std::string session_key =
        std::to_string(ntohs(addr.sin_port));

    auto it = sessions_.find(session_key);
    if (it == sessions_.end()) {
        std::cerr << "[takion] DATA dropped: missing session\n";
        return;
    }

    ClientSession& session = it->second;

    session.last_rx_ms = now_ms();

    send_data_ack(addr, session, seq_num);

    const bool is_last = (chunk_flags & 0x01) != 0;
    const bool continuation = session.reassembly_active;          // сборка уже начата
    const size_t fragment_offset = continuation ? 8 : 9;          // у продолжения заголовок 8 байт

    if (payload_size < fragment_offset) return;

    const uint8_t* fragment = payload + fragment_offset;
    const size_t fragment_size = payload_size - fragment_offset;

    if (!continuation) {
        session.reassembly.clear();
        session.reassembly_active = true;
        session.reassembly_type = data_type;
    }

    session.reassembly.insert(session.reassembly.end(), fragment, fragment + fragment_size);

    if (is_last) {
        handle_big(addr, session, session.reassembly);
        session.reassembly.clear();
        session.reassembly_active = false;
        session.reassembly_type = 0;
    }
}

// --- Protobuf helpers ---

static void append_varint(
    std::vector<uint8_t>& out,
    uint64_t value
) {
    while (value >= 0x80) {
        out.push_back(
            static_cast<uint8_t>((value & 0x7f) | 0x80)
        );
        value >>= 7;
    }
    out.push_back(static_cast<uint8_t>(value));
}

static void append_varint_field(
    std::vector<uint8_t>& out,
    uint32_t field_number,
    uint64_t value
) {
    append_varint(out, (field_number << 3) | 0);
    append_varint(out, value);
}

static void append_bytes_field(
    std::vector<uint8_t>& out,
    uint32_t field_number,
    const std::vector<uint8_t>& value
) {
    append_varint(out, (field_number << 3) | 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

static void append_string_field(
    std::vector<uint8_t>& out,
    uint32_t field_number,
    const std::string& value
) {
    append_varint(out, (field_number << 3) | 2);
    append_varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}

static std::vector<uint8_t> build_bang_protobuf(
    uint32_t server_version,
    uint32_t token,
    bool encrypted_key_accepted,
    bool version_accepted,
    const std::string& session_key,
    const std::vector<uint8_t>& ecdh_pub_key,
    const std::vector<uint8_t>& ecdh_sig
) {
    std::vector<uint8_t> bang_payload;

    append_varint_field(bang_payload, 1, server_version);
    append_varint_field(bang_payload, 2, token);
    append_varint_field(
        bang_payload,
        3,
        encrypted_key_accepted ? 1 : 0
    );
    append_varint_field(
        bang_payload,
        4,
        version_accepted ? 1 : 0
    );
    append_string_field(bang_payload, 5, session_key);

    append_bytes_field(bang_payload, 8, ecdh_pub_key);
    append_bytes_field(bang_payload, 9, ecdh_sig);

    std::vector<uint8_t> message;
    append_varint_field(message, 1, 1);               // type = BANG
    append_bytes_field(message, 3, bang_payload);     // поле 3 как length-delimited (0x1a)
    return message;

}

static std::vector<uint8_t> build_streaminfo_protobuf() {
    // STREAMINFO: resolution + hevc_header + audio header
    std::vector<uint8_t> resolution;
    append_varint_field(resolution, 1, VIDEO_WIDTH);
    append_varint_field(resolution, 2, VIDEO_HEIGHT);
    append_bytes_field(resolution, 3, g_hevc_header);

    // AUDIOHEADER (14 байт): channels(1) bits(1) rate(4) frame_size(4) unknown(4)
    std::vector<uint8_t> audio_header(14, 0);
    audio_header[0] = 2;                              // channels
    audio_header[1] = 16;                             // bits
    write_u32_be(audio_header.data() + 2, 48000);     // rate
    write_u32_be(audio_header.data() + 6, 480);       // frame_size (сэмплов на канал)
    write_u32_be(audio_header.data() + 10, 1);        // unknown

    std::vector<uint8_t> stream_info_payload;
    append_bytes_field(stream_info_payload, 1, resolution);
    append_bytes_field(stream_info_payload, 2, audio_header);

    std::vector<uint8_t> message;
    append_varint_field(message, 1, 13);  // type = STREAMINFO
    append_bytes_field(message, 15, stream_info_payload);

    return message;
}

static std::vector<uint8_t> extract_param_sets(const std::vector<uint8_t>& f) {
    std::vector<uint8_t> out;
    auto sc = [&](size_t i) { return i + 3 < f.size() && f[i]==0 && f[i+1]==0 && (f[i+2]==1 || (f[i+2]==0 && f[i+3]==1)); };
    for (size_t i = 0; i + 4 < f.size(); ) {
        if (!sc(i)) { ++i; continue; }
        size_t hdr = (f[i+2] == 1) ? 3 : 4, j = i + hdr;
        size_t k = j; while (k < f.size() && !sc(k)) ++k;
        uint8_t type = (f[j] >> 1) & 0x3f;
        if (type >= 32 && type <= 34) out.insert(out.end(), f.begin() + i, f.begin() + k);
        i = k;
    }
    return out;
}

// --- Protobuf parser для BigPayload ---

struct BigPayload {
    uint64_t client_version = 0;
    std::string session_key;
    std::string launch_spec;
    std::vector<uint8_t> encrypted_key;
    std::vector<uint8_t> ecdh_pub_key;
    std::vector<uint8_t> ecdh_sig;
    bool has_client_version = false;
    bool has_session_key = false;
    bool has_launch_spec = false;
    bool has_encrypted_key = false;
    bool has_ecdh_pub_key = false;
    bool has_ecdh_sig = false;
};

static bool read_varint(
    const std::vector<uint8_t>& buf,
    size_t& pos,
    uint64_t& value
) {
    value = 0;
    int shift = 0;

    while (pos < buf.size() && shift < 64) {
        const uint8_t byte = buf[pos++];
        value |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
        shift += 7;
    }

    return false;
}

static bool read_length_delimited(
    const std::vector<uint8_t>& buf,
    size_t& pos,
    const uint8_t*& value,
    size_t& length
) {
    uint64_t length64 = 0;
    if (!read_varint(buf, pos, length64)) {
        return false;
    }

    if (length64 > buf.size() - pos) {
        return false;
    }

    value = buf.data() + pos;
    length = static_cast<size_t>(length64);
    pos += length;
    return true;
}

struct TakionMessageWrapper {
    uint64_t type = 0;
    bool has_type = false;
    std::vector<uint8_t> big_payload_raw;
    bool has_big_payload = false;
};

static bool parse_takion_message(
    const std::vector<uint8_t>& raw,
    TakionMessageWrapper& out
) {
    size_t pos = 0;

    while (pos < raw.size()) {
        uint64_t tag = 0;
        if (!read_varint(raw, pos, tag)) {
            return false;
        }

        const uint32_t field_number = static_cast<uint32_t>(tag >> 3);
        const uint32_t wire_type = static_cast<uint32_t>(tag & 0x07);

        if (wire_type == 0) {
            uint64_t value = 0;
            if (!read_varint(raw, pos, value)) {
                return false;
            }
            if (field_number == 1) {
                out.type = value;
                out.has_type = true;
            }
        } else if (wire_type == 2) {
            const uint8_t* value = nullptr;
            size_t length = 0;
            if (!read_length_delimited(raw, pos, value, length)) {
                return false;
            }
            if (field_number == 2) {
                out.big_payload_raw.assign(value, value + length);
                out.has_big_payload = true;
            }
            // остальные payload-поля (3, 10, 15, ...) пока игнорируем
        } else {
            return false;
        }
    }

    return true;
}

static bool has_idr(const std::vector<uint8_t>& h) {
    for (size_t i = 0; i + 4 < h.size(); ++i) {
        size_t o = 0;
        if (h[i] == 0 && h[i+1] == 0 && h[i+2] == 1) o = i + 3;
        else if (h[i] == 0 && h[i+1] == 0 && h[i+2] == 0 && h[i+3] == 1) o = i + 4;
        if (o && o < h.size()) {
            uint8_t t = (h[o] >> 1) & 0x3f;
            if (t == 19 || t == 20) return true;
        }
    }
    return false;
}

static bool parse_big_payload(
    const std::vector<uint8_t>& raw,
    BigPayload& out
) {
    size_t pos = 0;

    while (pos < raw.size()) {
        uint64_t tag = 0;
        if (!read_varint(raw, pos, tag)) {
            return false;
        }

        const uint32_t field_number = static_cast<uint32_t>(tag >> 3);
        const uint32_t wire_type = static_cast<uint32_t>(tag & 0x07);

        if (wire_type == 0) {
            uint64_t value = 0;
            if (!read_varint(raw, pos, value)) {
                return false;
            }

            if (field_number == 1) {
                out.client_version = value;
                out.has_client_version = true;
            }
        } else if (wire_type == 2) {
            const uint8_t* value = nullptr;
            size_t length = 0;
            if (!read_length_delimited(raw, pos, value, length)) {
                return false;
            }

            if (field_number == 2) {
                out.session_key.assign(
                    reinterpret_cast<const char*>(value),
                    length
                );
                out.has_session_key = true;
            } else if (field_number == 3) {
                out.launch_spec.assign(
                    reinterpret_cast<const char*>(value),
                    length
                );
                out.has_launch_spec = true;
            } else if (field_number == 4) {
                out.encrypted_key.assign(value, value + length);
                out.has_encrypted_key = true;
            } else if (field_number == 5) {
                out.ecdh_pub_key.assign(value, value + length);
                out.has_ecdh_pub_key = true;
            } else if (field_number == 6) {
                out.ecdh_sig.assign(value, value + length);
                out.has_ecdh_sig = true;
            }
        } else {
            return false;
        }
    }

    return true;
}

// --- Base64 ---

static std::vector<uint8_t> base64_decode(const std::string& encoded) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;

    std::vector<uint8_t> decoded;
    int val = 0, valb = -8;
    for (char c : encoded) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            decoded.push_back((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return decoded;
}

// --- AES-CFB decrypt/encrypt (Windows CNG) ---

static std::vector<uint8_t> aes_cfb128_encrypt(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& plaintext
) {
    if (key.size() != 16 || iv.size() != 16 || plaintext.empty()) {
        return {};
    }

    BCRYPT_ALG_HANDLE h_alg = nullptr;
    BCRYPT_KEY_HANDLE h_key = nullptr;

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &h_alg,
        BCRYPT_AES_ALGORITHM,
        nullptr,
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }

    status = BCryptSetProperty(
        h_alg,
        BCRYPT_CHAINING_MODE,
        (PUCHAR)(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CFB)),
        static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_CFB) + 1) * sizeof(wchar_t)),
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(h_alg, 0);
        return {};
    }

    status = BCryptGenerateSymmetricKey(
        h_alg,
        &h_key,
        nullptr,
        0,
        (PUCHAR)(const_cast<uint8_t*>(key.data())),
        static_cast<ULONG>(key.size()),
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(h_alg, 0);
        return {};
    }

    std::vector<uint8_t> ciphertext(plaintext.size());
    std::vector<uint8_t> iv_copy = iv;
    ULONG cipher_len = 0;

    status = BCryptEncrypt(
        h_key,
        (PUCHAR)(const_cast<uint8_t*>(plaintext.data())),
        static_cast<ULONG>(plaintext.size()),
        nullptr,
        iv_copy.data(),
        static_cast<ULONG>(iv_copy.size()),
        ciphertext.data(),
        static_cast<ULONG>(ciphertext.size()),
        &cipher_len,
        0
    );

    BCryptDestroyKey(h_key);
    BCryptCloseAlgorithmProvider(h_alg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }

    ciphertext.resize(cipher_len);
    return ciphertext;
}

static std::vector<uint8_t> aes_cfb128_decrypt(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& iv,
    const std::vector<uint8_t>& ciphertext
) {
    if (key.size() != 16 || iv.size() != 16 || ciphertext.empty()) {
        return {};
    }

    BCRYPT_ALG_HANDLE h_alg = nullptr;
    BCRYPT_KEY_HANDLE h_key = nullptr;

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &h_alg,
        BCRYPT_AES_ALGORITHM,
        nullptr,
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }

    status = BCryptSetProperty(
        h_alg,
        BCRYPT_CHAINING_MODE,
        (PUCHAR)(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CFB)),
        static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_CFB) + 1) * sizeof(wchar_t)),
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(h_alg, 0);
        return {};
    }

    status = BCryptGenerateSymmetricKey(
        h_alg,
        &h_key,
        nullptr,
        0,
        (PUCHAR)(const_cast<uint8_t*>(key.data())),
        static_cast<ULONG>(key.size()),
        0
    );
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(h_alg, 0);
        return {};
    }

    std::vector<uint8_t> plaintext(ciphertext.size());
    std::vector<uint8_t> iv_copy = iv;
    ULONG plain_len = 0;

    status = BCryptDecrypt(
        h_key,
        (PUCHAR)(const_cast<uint8_t*>(ciphertext.data())),
        static_cast<ULONG>(ciphertext.size()),
        nullptr,
        iv_copy.data(),
        static_cast<ULONG>(iv_copy.size()),
        plaintext.data(),
        static_cast<ULONG>(plaintext.size()),
        &plain_len,
        0
    );

    BCryptDestroyKey(h_key);
    BCryptCloseAlgorithmProvider(h_alg, 0);

    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }

    plaintext.resize(plain_len);
    return plaintext;
}

// --- HMAC-SHA256 (OpenSSL) ---

static std::vector<uint8_t> hmac_sha256(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& data
) {
    unsigned int len = 0;
    std::vector<uint8_t> mac(EVP_MAX_MD_SIZE);

    unsigned char* result = HMAC(
        EVP_sha256(),
        key.data(),
        static_cast<int>(key.size()),
        data.data(),
        data.size(),
        mac.data(),
        &len
    );

    if (!result) {
        return {};
    }

    mac.resize(len);
    return mac;
}

static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), hash.data());
    return hash;
}

static int64_t extract_json_int(const std::string& json, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos) return 0;
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return 0;
    ++p;
    while (p < json.size() && json[p] == ' ') ++p;
    return std::strtoll(json.c_str() + p, nullptr, 10);
}

int64_t TakionServer::effective_max_bps(const ClientSession& s) const {
    int64_t bps = net_cfg_.max_bps;
    if (net_cfg_.respect_client && s.client_bw_kbps > 0)
        bps = (std::min)(bps, s.client_bw_kbps * 1000);
    return (std::max)(bps, (int64_t)2'000'000);
}

// --- Извлечение handshakeKey из JSON ---

static std::string extract_handshake_key_from_json(
    const std::string& json
) {
    const size_t json_start = json.find('{');

    const size_t search_start =
        (json_start == std::string::npos) ? 0 : json_start;

    static constexpr const char* KEY_NAME = "\"handshakeKey\"";

    const size_t key_pos = json.find(KEY_NAME, search_start);
    if (key_pos == std::string::npos) {
        return "";
    }

    const size_t colon_pos =
        json.find(':', key_pos + strlen(KEY_NAME));

    if (colon_pos == std::string::npos) {
        return "";
    }

    const size_t quote_start =
        json.find('"', colon_pos + 1);

    if (quote_start == std::string::npos) {
        return "";
    }

    const size_t quote_end =
        json.find('"', quote_start + 1);

    if (quote_end == std::string::npos) {
        return "";
    }

    return json.substr(
        quote_start + 1,
        quote_end - quote_start - 1
    );
}


// --- BIG / BANG ---

void TakionServer::handle_big(
    const sockaddr_in& addr,
    ClientSession& session,
    const std::vector<uint8_t>& protobuf
) {
    // 1) Сначала разбираем внешний TakionMessage
    TakionMessageWrapper msg;

    if (protobuf.size() >= 2 && protobuf[0] == 0x08) {
        const uint8_t t = protobuf[1];
        if (t == 14) session.streaminfo_acked = true;        
        if (t == 3 || t == 14 || t == 21) return;            // HEARTBEAT, STREAMINFOACK, CONTROLLERCONNECTION
        if (t == 5 || t == 25) { idr_requested_ = true; return; }   // CORRUPTFRAME, IDRREQUEST -> новый IDR
        if (t == 8) { printf("[takion] client disconnect\n"); return; }
    }

    if (!parse_takion_message(protobuf, msg)) {
        std::cerr << "[takion] Failed to parse TakionMessage\n";
        return;
    }

    if (!msg.has_type) {
        std::cerr << "[takion] TakionMessage: missing type\n";
        return;
    }

    std::cout << "[takion] TakionMessage.type=" << msg.type << "\n";

    if (msg.type != 0 /* BIG */) {
        std::cerr << "[takion] Expected BIG (type=0), got type="
                  << msg.type << "\n";
        return;
    }

    if (!msg.has_big_payload) {
        std::cerr << "[takion] TakionMessage: missing big_payload\n";
        return;
    }

    // 2) Теперь разбираем сам BigPayload
    BigPayload big;

    if (!parse_big_payload(msg.big_payload_raw, big)) {
        std::cerr << "[takion] Failed to parse BigPayload\n";
        return;
    }

    std::cout << "[takion] BIG client_version="
              << big.client_version
              << ", session_key=" << big.session_key
              << ", launch_spec_len=" << big.launch_spec.size()
              << ", ecdh_pub_key_len=" << big.ecdh_pub_key.size()
              << ", ecdh_sig_len=" << big.ecdh_sig.size()
              << "\n";

    if (!big.has_client_version) {
        std::cerr << "[takion] BigPayload: missing client_version\n";
        return;
    }

    if (!big.has_session_key) {
        std::cerr << "[takion] BigPayload: missing session_key\n";
        return;
    }

    if (!big.has_launch_spec) {
        std::cerr << "[takion] BigPayload: missing launch_spec\n";
        return;
    }

    session.streaminfo_acked = false;
    session.session_key = big.session_key;
    session.remote_ecdh_public_key = big.ecdh_pub_key;
    session.remote_ecdh_signature = big.ecdh_sig;

    std::vector<uint8_t> session_nonce;
    std::vector<uint8_t> session_rp_key;

    if (!(session_source_ && session_source_(session_nonce, session_rp_key))) {
        std::cerr << "[takion] No PS session state (GET /sess/init has not happened yet)\n";
        return;
    }

    std::string launch_spec_json;

    if (!decrypt_launch_spec_ps5(
            big.launch_spec,
            session_nonce,
            session_rp_key,
            launch_spec_json
        )) {
        std::cerr << "[takion] Failed to decrypt launch_spec\n";
        return;
    }

    session.client_bw_kbps = extract_json_int(launch_spec_json, "bwKbpsSent");
    std::cout << "[takion] client bitrate hint: " << session.client_bw_kbps << " kbps\n";

    std::cout << "[takion] LaunchSpec JSON: "
              << launch_spec_json
              << "\n";

    const std::string handshake_key_b64 =
        extract_handshake_key_from_json(launch_spec_json);

    if (handshake_key_b64.empty()) {
        std::cerr << "[takion] Failed to extract handshakeKey from launch_spec\n";
        return;
    }

    std::cout << "[takion] handshakeKey (base64): " << handshake_key_b64 << "\n";

    session.handshake_key = base64_decode(handshake_key_b64);

    if (session.handshake_key.size() != 16) {
        std::cerr << "[takion] handshakeKey size mismatch: "
                  << session.handshake_key.size() << " (expected 16)\n";
        return;
    }

    std::cout << "[takion] handshakeKey decoded: "
              << session.handshake_key.size() << " bytes\n";

    const std::vector<uint8_t> expected_sig =
        hmac_sha256(session.handshake_key, session.remote_ecdh_public_key);

    if (expected_sig != session.remote_ecdh_signature) {
        std::cerr << "[takion] ECDH signature mismatch\n";
        return;
    }

    std::cout << "[takion] ECDH signature verified\n";

    // Повторный BIG на уже работающей сессии: сначала останавливаем её видеопоток
    if (session.video_started) {
        std::cout << "[takion] repeated BIG on active session: restarting stream\n";
        session.closing = true;
        for (int i = 0; i < 500 && session.video_started; ++i) Sleep(10);
        if (session.video_started) {
            std::cerr << "[takion] old video thread did not stop, ignoring BIG\n";
            return;
        }
    }

    generate_secp256k1_keypair(session);

    std::vector<uint8_t> shared_secret;
    if (!ecdh_secp256k1(
            session.server_private_key,
            session.remote_ecdh_public_key,
            shared_secret)) {
        std::cerr << "[takion] ECDH compute failed\n";
        return;
    }

    session.ecdh_secret = shared_secret;
    std::cout << "[takion] ECDH shared secret: " << shared_secret.size() << " bytes\n";

    session.gkserver = std::make_unique<GKCrypt>(
        3,
        session.handshake_key,
        session.ecdh_secret
    );

    session.server_signature =
        hmac_sha256(session.handshake_key, session.server_public_key);

    std::cout << "[takion] server ecdh_sig computed: "
              << session.server_signature.size() << " bytes\n";

    send_bang(addr, session);
    session.gk_out = std::make_unique<GKCrypt>(3, session.handshake_key, session.ecdh_secret);
    session.gk_in  = std::make_unique<GKCrypt>(2, session.handshake_key, session.ecdh_secret);   // клиент -> сервер
    session.in_keypos_prev = 0;
    session.keypos_out = 0;
    pad().init();
    send_stream_info(addr, session);

    if (!session.video_started) {
        session.last_rx_ms = now_ms();
        session.closing = false;
        // новый поток вытесняет старый: захват и энкодер общие
        for (auto& kv : sessions_)
            if (&kv.second != &session && kv.second.video_started) kv.second.closing = true;
        BitrateController::Params bp;
        bp.min_bps = net_cfg_.min_bps;
        bp.max_bps = effective_max_bps(session);
        bitrate_ctl_ = BitrateController(bp);      // новая сессия начинает с потолка
        target_bps_ = effective_max_bps(session);    
        session.video_started = true;
        sockaddr_in a = addr;
        ClientSession* sp = &session;
        ++active_video_threads_;
        std::thread([this, a, sp]() {
            video_thread(a, sp);
            --active_video_threads_;
        }).detach();
    }
}

void TakionServer::send_bang(
    const sockaddr_in& addr,
    ClientSession& session
) {
    static constexpr uint8_t CONTROL = 0x00;
    static constexpr uint8_t CHUNK_DATA = 0x00;

    if (session.server_public_key.empty() ||
        session.server_public_key.size() != 65) {
        std::cerr << "[takion] Cannot send BANG: no public key\n";
        return;
    }

    std::vector<uint8_t> bang_protobuf = build_bang_protobuf(
        12,
        0,
        true,
        true,
        session.session_key,
        session.server_public_key,
        session.server_signature
    );

    static constexpr size_t DATA_HEADER_SIZE = 9;  // seq4 + chan2 + zero2 + type_b1

    const size_t packet_size = 17 + 9 + bang_protobuf.size();     // 212
    std::vector<uint8_t> packet(packet_size, 0);

    packet[0] = 0x00;
    write_u32_be(packet.data() + 1, session.client_tag);
    packet[13] = 0x00;                                            // тип чанка DATA
    packet[14] = 0x01;                                            // chunk_flags = 1  (это и есть type_b)
    write_u16_be(packet.data() + 15, (uint16_t)(packet_size - 13)); // 199

    uint8_t* p = packet.data() + 17;
    write_u32_be(p + 0, session.server_seq);   // БЕЗ += 1: первый seq = initial_seq_num из INIT_ACK
    session.server_seq += 1;                   // инкремент после отправки
    write_u16_be(p + 4, 1);                    // channel
    write_u16_be(p + 6, 0);
    p[8] = 0x00;                               // тип данных = PROTOBUF (0), не 1
    memcpy(p + 9, bang_protobuf.data(), bang_protobuf.size());

    const int sent = sendto(
        udp_socket_,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr)
    );
}

void TakionServer::send_stream_info(
    const sockaddr_in& addr,
    ClientSession& session
) {
    if (!session.gk_out) {
        std::cerr << "[takion] Cannot send STREAMINFO: no gk_out\n";
        return;
    }

    std::vector<uint8_t> pb = build_streaminfo_protobuf();

    const size_t packet_size = 17 + 9 + pb.size();
    std::vector<uint8_t> packet(packet_size, 0);

    packet[0] = 0x00;                                   // CONTROL
    write_u32_be(packet.data() + 1, session.client_tag);
    packet[13] = 0x00;                                  // чанк DATA
    packet[14] = 0x01;                                  // chunk_flags = 1
    write_u16_be(packet.data() + 15, (uint16_t)(packet_size - 13));

    uint8_t* p = packet.data() + 17;
    write_u32_be(p + 0, session.server_seq);            // текущий seq, без += 1 до записи
    session.server_seq += 1;
    write_u16_be(p + 4, 1);                             // channel
    write_u16_be(p + 6, 0);
    p[8] = 0x00;                                        // тип данных = PROTOBUF
    memcpy(p + 9, pb.data(), pb.size());

    sign_packet(session, packet.data(), packet.size()); // GMAC + keypos, после сборки

    const int sent = sendto(
        udp_socket_,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr)
    );

    if (sent == SOCKET_ERROR) {
        std::cerr << "[takion] STREAMINFO sendto failed: " << WSAGetLastError() << "\n";
        return;
    }
    std::cout << "[takion] STREAMINFO sent: bytes=" << sent << "\n";
}

bool TakionServer::send_data_msg(const sockaddr_in& addr, ClientSession& s, uint8_t data_type,
                                 const uint8_t* body, size_t n) {
    const size_t packet_size = 17 + 9 + n;
    std::vector<uint8_t> packet(packet_size, 0);
    packet[0] = 0x00;                                   // CONTROL
    write_u32_be(packet.data() + 1, s.client_tag);
    packet[13] = 0x00;                                  // чанк DATA
    packet[14] = 0x01;                                  // chunk_flags = 1
    write_u16_be(packet.data() + 15, (uint16_t)(packet_size - 13));
    uint8_t* p = packet.data() + 17;
    write_u16_be(p + 4, 1);                             // channel
    write_u16_be(p + 6, 0);
    p[8] = data_type;
    if (n) memcpy(p + 9, body, n);

    std::lock_guard<std::mutex> lk(s.send_mtx);         // seq, keypos и отправка в одном порядке
    if (!s.gk_out) return false;
    write_u32_be(p + 0, s.server_seq++);
    sign_packet_locked(s, packet.data(), packet.size());
    const int sent = sendto(udp_socket_, reinterpret_cast<const char*>(packet.data()),
                            (int)packet.size(), 0,
                            reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (sent == SOCKET_ERROR) { ++send_err_; last_err_ = WSAGetLastError(); return false; }
    return true;
}

void TakionServer::send_rumble_now() {
    std::lock_guard<std::mutex> lk(active_mtx_);
    if (!active_s_ || !active_s_->gk_out) { printf("[rumble] no active session, dropped\n"); return; }
    const uint16_t st = rumble_state_;
    const uint8_t body[3] = {0, (uint8_t)(st >> 8), (uint8_t)st};
    const bool ok = send_data_msg(active_addr_, *active_s_, 7, body, sizeof(body));
    printf("[rumble] sent to client: left=%u right=%u ok=%d\n", body[1], body[2], ok ? 1 : 0);
    if (ok) rumble_sent_ms_ = now_ms();
}

void TakionServer::on_pad_output(uint8_t left, uint8_t right) {
    printf("[rumble] from game: left=%u right=%u\n", left, right);
    rumble_state_ = (uint16_t)((left << 8) | right);
    send_rumble_now();
}

static inline void pace_until(const std::chrono::steady_clock::time_point& t) {
    using namespace std::chrono;
    for (;;) {
        const auto d = t - steady_clock::now();
        if (d <= microseconds(0)) return;
        if (d > microseconds(2500)) std::this_thread::sleep_for(d - microseconds(2000));
        else _mm_pause();
    }
}

void TakionServer::tx_post(TxJob&& j) {
    {
        std::lock_guard<std::mutex> lk(tx_mtx_);
        if (tx_q_.size() >= 4) { tx_q_.clear(); idr_requested_ = true; }  // отстали: просим свежий IDR
        tx_q_.push_back(std::move(j));
    }
    tx_cv_.notify_one();
}

void TakionServer::tx_loop() {
    using clk = std::chrono::steady_clock;
    while (tx_run_) {
        TxJob j;
        {
            std::unique_lock<std::mutex> lk(tx_mtx_);
            tx_cv_.wait(lk, [&] { return !tx_run_ || !tx_q_.empty(); });
            if (!tx_run_) break;
            j = std::move(tx_q_.front());
            tx_q_.pop_front();
        }
        auto next = clk::now();
        for (auto& pkt : j.pkts) {
            pace_until(next);
            const int r = sendto(udp_socket_, reinterpret_cast<const char*>(pkt.data()),
                                 (int)pkt.size(), 0,
                                 reinterpret_cast<const sockaddr*>(&j.addr), sizeof(j.addr));
            if (r != (int)pkt.size()) { ++send_err_; last_err_ = WSAGetLastError(); }
            next = (std::max)(next, clk::now()) + j.gap;
        }
    }
}

// добавьте в ClientSession:  std::mutex send_mtx;  (включите <mutex>)

void TakionServer::send_video_frame(const sockaddr_in& addr, ClientSession& s,
                                    const std::vector<uint8_t>& frame)
{
    if (!s.gk_out || frame.empty()) return;

    const size_t kChunk = 1198;
    const size_t kUnit  = 1200;                       // 2 байта padding + 1198 данных
    const size_t k = (frame.size() + kChunk - 1) / kChunk;
    const uint16_t frame_index = (uint16_t)s.video_frame_index++;
    unsigned m = fecrs::units_for(k);
    m = (std::max)(m, (unsigned)((k * 3 + 9) / 10));
    if (k + m > 255) m = (k < 255) ? 255 - k : 0;
    const size_t total = k + m;

    const size_t kMinData = 64;                       // минимум данных в пакете, включая поле padding
    auto tail_extra = [&](size_t n) -> size_t {
        return (2 + n < kMinData) ? (kMinData - (2 + n)) : 0;
    };

    // Чётность считается по исходным юнитам, добитым нулями до kUnit
    std::vector<uint8_t> parity;
    if (m) {
        std::vector<uint8_t> blk(k * kUnit, 0);
        for (size_t u = 0; u < k; ++u) {
            const size_t off = u * kChunk;
            const size_t n = (std::min)(kChunk, frame.size() - off);
            const size_t extra = (u == k - 1) ? tail_extra(n) : 0;
            const uint16_t padding = (u == k - 1) ? (uint16_t)(kChunk - n) : 0;   // без "- extra"
            write_u16_be(&blk[u * kUnit], padding);
            memcpy(&blk[u * kUnit + 2], frame.data() + off, n);
        }
        parity = fecrs::encode(blk.data(), kUnit, (unsigned)k, m);
    }

    // Растягиваем кадр примерно на 30% времени кадра (с учётом парити-пакетов)
    const auto frame_budget = std::chrono::microseconds(1000000 / VIDEO_FPS * 6 / 10);
    const auto gap = (std::max)(std::chrono::microseconds(60),
        std::chrono::duration_cast<std::chrono::microseconds>(frame_budget / (long long)total));

    static const bool kFecTestDrop = false;           // true только для проверки FEC

    TxJob job;
    job.addr = addr;
    job.gap = gap;
    job.pkts.reserve(total);

    for (size_t u = 0; u < total; ++u) {
        const bool is_parity = (u >= k);

        size_t n = 0;
        uint16_t padding = 0;
        if (!is_parity) {
            const size_t off = u * kChunk;
            n = (std::min)(kChunk, frame.size() - off);
            padding = (u == k - 1) ? (uint16_t)(kChunk - n) : 0;
        }
        const size_t extra_len = (!is_parity && u == k - 1) ? tail_extra(n) : 0;
        const size_t data_len = is_parity ? kUnit : (2 + n + extra_len);

        std::vector<uint8_t> pkt(21 + data_len, 0);
        pkt[0] = 0x02;
        write_u16_be(&pkt[1], (uint16_t)s.video_packet_index++);
        write_u16_be(&pkt[3], frame_index);
        // индекс юнита | (всего юнитов - 1) | число FEC-юнитов
        write_u32_be(&pkt[5], ((uint32_t)u << 21) | ((uint32_t)(total - 1) << 10) | (uint32_t)m);
        pkt[9] = 0x01;

        if (is_parity) {
            memcpy(&pkt[21], &parity[(u - k) * kUnit], kUnit);
        } else {
            write_u16_be(&pkt[21], padding);
            memcpy(&pkt[23], frame.data() + u * kChunk, n);
        }

        {
            std::lock_guard<std::mutex> lk(s.send_mtx);
            const uint64_t kp = s.keypos_out;
            s.keypos_out += (uint64_t)(((data_len + 15) & ~(size_t)15) + 16);
            s.gk_out->crypt(kp + 16, &pkt[21], data_len);
            write_u32_be(&pkt[14], (uint32_t)kp);
            uint8_t mac[4] = {0};
            s.gk_out->gmac(kp, pkt.data(), pkt.size(), mac);
            memcpy(&pkt[10], mac, 4);
        }

        if (kFecTestDrop && !is_parity && u == 1 && (frame_index % 10) == 0)
            continue;                                  // имитация потери юнита

        if (!is_parity && pkt.size() < 64)
            printf("[video] small packet %zu bytes frame %u unit %zu/%zu\n", pkt.size(), (unsigned)frame_index, u, k);

        job.pkts.push_back(std::move(pkt));
    }
    tx_post(std::move(job));
}

static std::vector<uint8_t> load_hex_file(const char* path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> out;
    int hi = -1;
    for (char c : s) {
        int v = (c >= '0' && c <= '9') ? c - '0'
              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
              : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (v < 0) continue;
        if (hi < 0) hi = v; else { out.push_back((uint8_t)((hi << 4) | v)); hi = -1; }
    }
    return out;
}

void TakionServer::video_thread(sockaddr_in addr, ClientSession* s) {
    std::lock_guard<std::mutex> run_lk(video_run_mtx_);
    if (!running_ || s->closing) { s->video_started = false; return; }
    int64_t applied_bps = effective_max_bps(*s);
    encoder_->set_bitrate((int)applied_bps, (int)(applied_bps * 5 / 4));   // после прошлой сессии кодировщик мог остаться на пониженном
    { std::lock_guard<std::mutex> lk(active_mtx_); active_s_ = s; active_addr_ = addr; }
    using clk = std::chrono::steady_clock;
    std::vector<uint8_t> nv12;
    int pitch = 0;
    bool have = false;

    std::mutex qm; std::condition_variable qcv;
    std::deque<std::vector<uint8_t>> q; bool qstop = false;

    std::thread sender([&] {
        for (;;) {
            std::vector<uint8_t> f;
            {
                std::unique_lock<std::mutex> lk(qm);
                qcv.wait(lk, [&] { return qstop || !q.empty(); });
                if (q.empty()) return;
                f = std::move(q.front()); q.pop_front();
            }
            send_video_frame(addr, *s, f);   // у вас там `s` — указатель или ссылка, подставьте как есть
        }
    });

    auto enqueue = [&](std::vector<uint8_t>&& f) {
        std::lock_guard<std::mutex> lk(qm);
        if (q.size() >= 4) {          // отправка не успевает: цепочка P-кадров уже сломана
            q.clear();
            idr_requested_ = true;     // следующий кадр должен быть IDR
        }
        q.push_back(std::move(f));
        qcv.notify_one();
    };

    auto grab = [&]() {
        std::vector<uint8_t> f; int p = 0;
        if (capture_->capture_frame(f, p)) { nv12.swap(f); pitch = p; have = true; }
    };
    
    double enc_ms = 0, send_ms = 0; size_t sent_bytes = 0;
    int idr_cnt = 0;
    
    bool got_idr = false;                    // ДО лямбды, не внутри
    
    auto encode_send = [&]() -> bool {
        if (!have) return false;
        std::vector<uint8_t> h;
        auto a = clk::now();
        bool ok = encoder_->encode_frame(nv12.data(), pitch, h) && !h.empty();
        auto b = clk::now();
        if (!ok) return false;

        if (!got_idr) {                      // новая вставка
            if (!has_idr(h)) { encoder_->force_idr(); return false; }
            got_idr = true;
        }

        idr_cnt += has_idr(h) ? 1 : 0;
        const size_t sz = h.size();
        enqueue(std::move(h));
        auto c = clk::now();
        enc_ms  = std::chrono::duration<double, std::milli>(b - a).count();
        send_ms = std::chrono::duration<double, std::milli>(c - b).count();
        sent_bytes = sz;
        return true;
    };                                       // обязательно ';' после '}'

    // 1) один кадр сразу
    encoder_->force_idr();
    for (int i = 0; i < 100 && running_ && s->gk_out; ++i) {
        grab();
        if (encode_send()) break;
        Sleep(20);
    }

    // 2) пока декодер открывается, ничего не шлём
    for (int i = 0; i < 150 && running_ && !s->closing && !s->streaminfo_acked; ++i) Sleep(10);

    // 3) чистый IDR и обычный цикл
    encoder_->force_idr();
    uint32_t n = 0;
    const auto kFrame = std::chrono::microseconds(1000000 / VIDEO_FPS);
    auto next = clk::now();
    auto last_idr = clk::now();

    auto t_report = clk::now();
    size_t max_frame = 0;
    double t_cap = 0, t_enc = 0, t_send = 0;
    int frames = 0, late = 0, white_frames = 0; size_t bytes = 0;
    int idr_req = 0, idr_per = 0;
    idr_cnt = 0;

    AudioStreamer audio;
    if (!audio.start([this, addr, s](const uint8_t* d, size_t n) {
            if (s->closing || !s->gk_out) return;
            send_audio_frame(addr, *s, std::vector<uint8_t>(d, d + n), 0);
        })) {
        std::cerr << "[audio] disabled (capture or encoder could not start)\n";
    }

    while (running_ && s->gk_out && !s->closing) {
        auto t0 = clk::now();
        grab();
        auto t1 = clk::now();

        // яркость захваченного кадра (выборка)
        if (have && pitch > 0) {
            uint64_t sum = 0; size_t cnt = 0;
            const size_t ysz = (size_t)pitch * 1080;
            for (size_t i = 0; i < ysz && i < nv12.size(); i += 4099) { sum += nv12[i]; ++cnt; }
            if (cnt && double(sum) / cnt > 215) ++white_frames;
        }

        if (idr_requested_.exchange(false) && clk::now() - last_idr > std::chrono::milliseconds(2000)) {
            encoder_->force_idr();
            last_idr = clk::now();
        }

        if (now_ms() - s->last_rx_ms > 10000) {          // 10 с без пакетов от клиента
            std::cout << "[session] client timeout\n";
            s->closing = true;
            break;
        }

        const uint32_t period = (n < 2 * VIDEO_FPS) ? VIDEO_FPS / 2 : VIDEO_FPS * 10;
        if (n && n % period == 0) { encoder_->force_idr(); last_idr = clk::now(); ++idr_per; }

        if (encode_send()) {
            ++n; ++frames;
            t_cap  += std::chrono::duration<double, std::milli>(t1 - t0).count();
            t_enc  += enc_ms;
            t_send += send_ms;
            bytes  += sent_bytes;
            if (sent_bytes > max_frame) max_frame = sent_bytes;
        }

        next += kFrame;
        auto now = clk::now();
        if (next < now) { next = now; ++late; }
        std::this_thread::sleep_until(next);

        if (rumble_state_ != 0 && now_ms() - rumble_sent_ms_ > 250) send_rumble_now();

        if (clk::now() - t_report >= std::chrono::seconds(5)) {
            const double secs = std::chrono::duration<double>(clk::now() - t_report).count();
            if (frames > 0) {
                printf("[video] %.1f fps, %.1f Mbit/s, avg frame %.1f KB, max %.1f KB, IDR %d (per %d, req %d), "
                       "capture %.2f ms, encode %.2f ms, send %.2f ms, late %d, blank %d\n",
                        frames / secs, (double)bytes * 8.0 / secs / 1e6,
                       (double)bytes / 1024.0 / frames, (double)max_frame / 1024.0, idr_cnt, idr_per, idr_req,
                       t_cap / frames, t_enc / frames, t_send / frames, late, white_frames);
            }
            t_report = clk::now();
            frames = 0; bytes = 0; max_frame = 0; idr_cnt = 0;
            t_cap = t_enc = t_send = 0; late = 0; white_frames = 0;
        }

        // сторожевой таймер: нет пакетов геймпада > 3 с — отпустить всё (один раз)
        if (s->pad_active && now_ms() - s->last_rx_ms > 3000) {
            pad().reset();
            s->pad_active = false;
        }
     
    }
    
    { std::lock_guard<std::mutex> lk(qm); qstop = true; }
    qcv.notify_all();
    sender.join();
    
    audio.stop();
    { std::lock_guard<std::mutex> lk(active_mtx_); if (active_s_ == s) active_s_ = nullptr; }
    pad().reset();
    s->video_started = false;   // позволяет переподключиться к той же сессии
}

void TakionServer::send_audio_frame(
    const sockaddr_in& addr,
    ClientSession& s,
    const std::vector<uint8_t>& opus_frame,
    uint32_t /*unused*/
) {
    if (!s.gk_out || opus_frame.empty() || opus_frame.size() > 255) return;

    const size_t n = opus_frame.size();
    std::vector<uint8_t> pkt(kAudioHeaderSize + n, 0);
    fill_audio_header(pkt.data(), (uint16_t)(s.audio_packet_index++),
                      (uint16_t)(s.audio_frame_index++), (uint8_t)n);
    memcpy(&pkt[kAudioHeaderSize], opus_frame.data(), n);

    {
        std::lock_guard<std::mutex> lk(s.send_mtx);
        const uint64_t kp = s.keypos_out;
        s.keypos_out += (uint64_t)(((n + 15) & ~(size_t)15) + 16);
        s.gk_out->crypt(kp + 16, &pkt[kAudioHeaderSize], n);
        write_u32_be(&pkt[14], (uint32_t)kp);
        uint8_t mac[4] = {0};
        s.gk_out->gmac(kp, pkt.data(), pkt.size(), mac);
        memcpy(&pkt[10], mac, 4);
    }
    
    int r = sendto(udp_socket_, reinterpret_cast<const char*>(pkt.data()), (int)pkt.size(), 0,
                   reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (r != (int)pkt.size()) { ++send_err_; last_err_ = WSAGetLastError(); }
}

void TakionServer::generate_secp256k1_keypair(ClientSession& session) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx) {
        std::cerr << "[takion] Failed to create EVP_PKEY_CTX\n";
        return;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        std::cerr << "[takion] Failed to init keygen\n";
        EVP_PKEY_CTX_free(ctx);
        return;
    }

    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_secp256k1) <= 0) {
        std::cerr << "[takion] Failed to set curve secp256k1\n";
        EVP_PKEY_CTX_free(ctx);
        return;
    }

    EVP_PKEY* pkey = nullptr;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0 || !pkey) {
        std::cerr << "[takion] Failed to generate keypair\n";
        EVP_PKEY_CTX_free(ctx);
        return;
    }

    EVP_PKEY_CTX_free(ctx);

    EC_KEY* ec_key = EVP_PKEY_get1_EC_KEY(pkey);
    if (!ec_key) {
        std::cerr << "[takion] Failed to get EC_KEY\n";
        EVP_PKEY_free(pkey);
        return;
    }

    const BIGNUM* priv_bn = EC_KEY_get0_private_key(ec_key);
    if (!priv_bn) {
        std::cerr << "[takion] Failed to get private key\n";
        EC_KEY_free(ec_key);
        EVP_PKEY_free(pkey);
        return;
    }

    session.server_private_key.resize(32);
    BN_bn2binpad(priv_bn, session.server_private_key.data(), 32);

    session.server_public_key.resize(65);
    size_t pub_len = EC_POINT_point2oct(
        EC_KEY_get0_group(ec_key),
        EC_KEY_get0_public_key(ec_key),
        POINT_CONVERSION_UNCOMPRESSED,
        session.server_public_key.data(),
        65,
        nullptr
    );

    if (pub_len != 65) {
        std::cerr << "[takion] Public key size mismatch: " << pub_len << "\n";
        session.server_public_key.clear();
    }

    EC_KEY_free(ec_key);
    EVP_PKEY_free(pkey);

    std::cout << "[takion] secp256k1 keypair generated\n";
    std::cout << "[takion] Public key (first 8): ";
    for (int i = 0; i < 8 && i < static_cast<int>(session.server_public_key.size()); i++) {
        printf("%02x", session.server_public_key[i]);
    }
    printf("\n");
}

void TakionServer::send_data_ack(
    const sockaddr_in& addr,
    ClientSession& session,
    uint32_t seq_num
) {
    static constexpr uint8_t CONTROL = 0x00;
    static constexpr uint8_t CHUNK_DATA_ACK = 0x03;

    static constexpr size_t ACK_PAYLOAD_SIZE = 12;
    static constexpr size_t PACKET_SIZE = 17 + ACK_PAYLOAD_SIZE;

    std::array<uint8_t, PACKET_SIZE> packet{};

    packet[0] = CONTROL;
    write_u32_be(packet.data() + 1, session.client_tag);
    memset(packet.data() + 5, 0, 4);
    write_u32_be(packet.data() + 9, 0);

    packet[13] = CHUNK_DATA_ACK;
    packet[14] = 0x00;

    write_u16_be(
        packet.data() + 15,
        static_cast<uint16_t>(ACK_PAYLOAD_SIZE + 4)
    );

    uint8_t* ack = packet.data() + 17;
    write_u32_be(ack + 0, seq_num);
    write_u32_be(ack + 4, ARWND);
    write_u16_be(ack + 8, 0);
    write_u16_be(ack + 10, 0);

    sign_packet(session, packet.data(), packet.size());
    const int sent = sendto(
        udp_socket_,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr)
    );

    if (sent == SOCKET_ERROR) {
        std::cerr << "[takion] DATA_ACK sendto failed: "
                  << WSAGetLastError() << "\n";
        return;
    }

}

void TakionServer::send_init_ack(
    const sockaddr_in& addr,
    ClientSession& session
) {
    static constexpr size_t MESSAGE_HEADER_SIZE = 16;
    static constexpr size_t COOKIE_SIZE = 32;
    static constexpr size_t INIT_ACK_PAYLOAD_SIZE = 16 + COOKIE_SIZE;
    static constexpr size_t PACKET_SIZE =
        1 + MESSAGE_HEADER_SIZE + INIT_ACK_PAYLOAD_SIZE;

    if (session.client_tag == 0) {
        std::cerr << "[takion] cannot send INIT_ACK: client_tag is zero\n";
        return;
    }

    if (session.server_tag == 0) {
        session.server_tag = random_u32();
        if (session.server_tag == 0) {
            session.server_tag = 1;
        }
    }

    if (session.server_seq == 0) {
        session.server_seq = random_u32();
        if (session.server_seq == 0) {
            session.server_seq = 1;
        }
    }

    session.cookie.resize(COOKIE_SIZE);
    for (size_t i = 0; i < COOKIE_SIZE; ++i) {
        session.cookie[i] =
            static_cast<uint8_t>(random_u32() & 0xff);
    }

    std::array<uint8_t, PACKET_SIZE> packet{};
    packet[0] = 0x00;

    write_u32_be(packet.data() + 1, session.client_tag);
    memset(packet.data() + 5, 0, 4);
    write_u32_be(packet.data() + 9, 0);

    packet[13] = 0x02;  // INIT_ACK
    packet[14] = 0x00;

    write_u16_be(
        packet.data() + 15,
        static_cast<uint16_t>(INIT_ACK_PAYLOAD_SIZE + 4)
    );

    uint8_t* payload = packet.data() + 17;

    write_u32_be(payload + 0, session.server_tag);
    write_u32_be(payload + 4, ARWND);
    write_u16_be(payload + 8, OUTBOUND_STREAMS);
    write_u16_be(payload + 10, INBOUND_STREAMS);
    write_u32_be(payload + 12, session.server_seq);

    memcpy(payload + 16, session.cookie.data(), COOKIE_SIZE);

    const int sent = sendto(
        udp_socket_,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr)
    );

    if (sent == SOCKET_ERROR) {
        std::cerr << "[takion] INIT_ACK sendto failed: "
                  << WSAGetLastError() << "\n";
        return;
    }

    std::cout << "[takion] INIT_ACK sent: bytes=" << sent
              << " client_tag=0x" << std::hex << session.client_tag
              << " server_tag=0x" << session.server_tag
              << " server_seq=0x" << session.server_seq
              << std::dec << "\n";
}

void TakionServer::send_cookie_ack(
    const sockaddr_in& addr,
    ClientSession& session
) {
    static constexpr uint8_t CONTROL = 0x00;
    static constexpr uint8_t CHUNK_COOKIE_ACK = 0x0B;
    static constexpr size_t PACKET_SIZE = 17;

    if (session.client_tag == 0) {
        std::cerr << "[takion] cannot send COOKIE_ACK: client_tag is zero\n";
        return;
    }

    std::array<uint8_t, PACKET_SIZE> packet{};

    packet[0] = CONTROL;
    write_u32_be(packet.data() + 1, session.client_tag);
    memset(packet.data() + 5, 0, 4);
    write_u32_be(packet.data() + 9, 0);

    packet[13] = CHUNK_COOKIE_ACK;
    packet[14] = 0x00;

    write_u16_be(packet.data() + 15, 4);

    const int sent = sendto(
        udp_socket_,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&addr),
        sizeof(addr)
    );

    if (sent == SOCKET_ERROR) {
        std::cerr << "[takion] COOKIE_ACK sendto failed: "
                  << WSAGetLastError() << "\n";
        return;
    }

    std::cout << "[takion] COOKIE_ACK sent: bytes="
              << sent << "\n";
}