#pragma once
// Unified server (port 9295): UDP discovery responder (SRC2/SRC3) and TCP
// POST /sess/rgst, GET /sess/init, GET /sess/ctrl. Port of ps_full_server.py.
#include "ps_crypto.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ps {

class FullServer {
public:
    struct Config {
        uint16_t port = 9295;
        uint32_t pin = 12345678;
        std::string nickname = "My-Fake-PS5";
        // Fixed RP-Key ("morning"): survives server restarts.
        Key16 rp_key = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
        // If not empty, JSON {"nonce": "...", "rp_key": "..."} is written after /sess/init (legacy bridge).
        std::string session_state_path;
        bool log_ctrl_messages = false;   // print every Ctrl message received from the client
    };

    using CtrlCallback = std::function<void(uint16_t type, const std::vector<uint8_t>& payload)>;

    FullServer() = default;
    ~FullServer() { stop(); }
    FullServer(const FullServer&) = delete;
    FullServer& operator=(const FullServer&) = delete;

    bool start(const Config& cfg);
    void stop();

    // Current session state (after GET /sess/init). Returns false if init has not happened yet.
    bool get_session(Key16& nonce, Key16& rp_key) const;
    void set_ctrl_callback(CtrlCallback cb);

private:
    using Handle = std::intptr_t;

    void udp_loop();
    void tcp_loop();
    void handle_client(Handle sock, std::string peer);
    void handle_regist(Handle sock, bool is_ps5, const std::vector<uint8_t>& body);
    void handle_init(Handle sock);
    bool handle_ctrl(Handle sock, const std::string& rp_auth_b64);
    void ctrl_message_loop(Handle sock, const std::string& peer);

    Config cfg_;
    std::atomic<bool> running_{false};
    Handle udp_ = -1;
    Handle tcp_ = -1;
    std::thread udp_thread_;
    std::thread tcp_thread_;
    std::atomic<int> active_clients_{0};
    bool wsa_started_ = false;

    mutable std::mutex state_mtx_;
    bool have_nonce_ = false;
    Key16 nonce_{};

    mutable std::mutex cb_mtx_;
    CtrlCallback ctrl_cb_;
};

}  // namespace ps
