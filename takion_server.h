// takion_server.h
#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <unordered_map>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include <string>
#include <atomic>
#include <array>
#include <deque>
#include <condition_variable> 
#include <functional>
#include "amf_encoder.h"
#include "dxgi_capture.h"
#include "crypto.h"
#include "gkcrypt.h"
#include "bitrate_controller.h"
#include "video_encoder.h"

struct ClientSession {
    sockaddr_in addr{};
    uint32_t client_tag = 0;
    uint32_t server_tag = 0;
    uint32_t server_seq = 0;
    uint32_t client_seq = 0;
    uint16_t last_hist_seq = 0;  
    uint16_t last_state_seq = 0;

    std::vector<uint8_t> cookie;
    std::unique_ptr<GKCrypt> gkserver;
    std::unique_ptr<GKCrypt> gk_out;   // индекс 3, для пакетов сервер -> клиент
    std::unique_ptr<GKCrypt> gk_in; uint64_t in_keypos_prev = 0;
    std::mutex send_mtx;

    std::vector<uint8_t> reassembly;
    std::vector<uint8_t> ecdh_secret;
    std::vector<uint8_t> remote_ecdh_public_key;
    std::vector<uint8_t> remote_ecdh_signature;
    std::vector<uint8_t> handshake_key;  // 16 bytes
    std::vector<uint8_t> server_private_key;  // 32 bytes
    std::vector<uint8_t> server_public_key;   // 65 bytes
    std::vector<uint8_t> server_signature;    // HMAC(handshakeKey, server_pub)

    std::atomic<bool>    closing{false};
    std::atomic<bool> streaminfo_acked{false};
    std::atomic<int64_t> last_rx_ms{0};
    std::atomic<bool> video_started{false};
    std::atomic<bool> audio_started{false};
    std::atomic<bool> first_idr_sent{false};
    std::atomic<bool> pad_active{false};
    
    int64_t created_ms = 0;
    int64_t client_bw_kbps = 0;

    bool reassembly_active = false;
    uint8_t reassembly_type = 0;

    bool hist_seen = false;
    bool state_seen = false;

    uint64_t keypos_out = 0;

    uint32_t video_packet_index = 1;
    uint32_t video_frame_index = 1;
    uint32_t audio_packet_index = 1;
    uint32_t audio_frame_index = 1;

    uint32_t server_key_pos = 0;

    std::string session_key;
};

class TakionServer {
public:
    struct NetConfig {
        bool adaptive = false;
        int64_t min_bps = 6'000'000;
        int64_t max_bps = 20'000'000;
        bool respect_client = true;
    };
    using SessionSource = std::function<bool(std::vector<uint8_t>& nonce, std::vector<uint8_t>& rp_key)>;

    TakionServer();
    ~TakionServer();

    bool start(uint16_t port = 9296);
    void stop();
    void run();
    void request_stop() { running_ = false; }

    void set_net_config(const NetConfig& c) { net_cfg_ = c; }
    void set_session_source(SessionSource s) { session_source_ = std::move(s); }

private:
    void handle_packet(const std::vector<uint8_t>& data, const sockaddr_in& addr);
    void handle_init(const std::vector<uint8_t>& payload, const sockaddr_in& addr);
    void handle_cookie(const std::vector<uint8_t>& data, const sockaddr_in& addr);
    void handle_data(const std::vector<uint8_t>& data, const sockaddr_in& addr);

    void generate_secp256k1_keypair(ClientSession& session);

    void send_init_ack(const sockaddr_in& addr, ClientSession& session);
    void send_data_ack(const sockaddr_in& addr, ClientSession& session, uint32_t seq_num);
    void handle_big(const sockaddr_in& addr, ClientSession& session, const std::vector<uint8_t>& protobuf);
    void send_bang(const sockaddr_in& addr, ClientSession& session);
    void send_cookie_ack(const sockaddr_in& addr, ClientSession& session);
    void on_congestion(const std::vector<uint8_t>& d);
    uint64_t net_recv_ = 0, net_lost_ = 0;
    int64_t net_log_ms_ = 0;
    int64_t effective_max_bps(const ClientSession& s) const;

    struct TxJob {
        sockaddr_in addr{};
        std::vector<std::vector<uint8_t>> pkts;
        std::chrono::microseconds gap{100};
    };
    std::mutex tx_mtx_;
    std::condition_variable tx_cv_;
    std::deque<TxJob> tx_q_;
    std::thread tx_thread_;
    std::atomic<bool> tx_run_{false};
    void tx_loop();
    void tx_post(TxJob&& j);

    void send_video_frame(const sockaddr_in& addr, ClientSession& session, const std::vector<uint8_t>& frame);
    void video_thread(sockaddr_in addr, ClientSession* s);
    void send_stream_info(const sockaddr_in& addr, ClientSession& session);
    void gc_sessions();

    void send_audio_frame(const sockaddr_in& addr, ClientSession& session,
                          const std::vector<uint8_t>& pcm_frame, uint32_t frame_index);

    void on_pad_output(uint8_t left, uint8_t right);
    void send_rumble_now();
    bool send_data_msg(const sockaddr_in& addr, ClientSession& s, uint8_t data_type,
                       const uint8_t* body, size_t n);
    
    ClientSession* active_s_ = nullptr;
    sockaddr_in active_addr_{};

    SOCKET udp_socket_ = INVALID_SOCKET;
    std::unordered_map<std::string, ClientSession> sessions_;

    std::vector<uint8_t> session_nonce_;
    std::vector<uint8_t> session_rp_key_;
    
    std::unique_ptr<IVideoEncoder> encoder_;
    std::unique_ptr<DXGICapture> capture_;

    std::mutex threads_mtx_;
    std::mutex active_mtx_;
    std::vector<std::thread> video_threads_;   // join в stop()
    std::mutex video_run_mtx_;                  // если ещё не объявлен
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stopped_{false};
    std::atomic<int> active_video_threads_{0};
    std::atomic<uint32_t> send_err_{0};
    std::atomic<int>      last_err_{0};
    std::atomic<bool>     idr_requested_{false};    
    std::atomic<uint16_t> rumble_state_{0};
    std::atomic<int64_t> rumble_sent_ms_{0};

    std::vector<uint8_t> hevc_header_;

    SessionSource session_source_;
    NetConfig net_cfg_;
    BitrateController bitrate_ctl_;
    std::atomic<int64_t> target_bps_{20'000'000};
};