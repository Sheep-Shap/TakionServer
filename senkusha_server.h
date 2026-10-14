// senkusha_server.h - Takion v7 "Senkusha" (RTT / MTU probe) responder, UDP port 9297.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <thread>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <netinet/in.h>
#endif

class SenkushaServer {
public:
    SenkushaServer() = default;
    ~SenkushaServer() { stop(); }
    SenkushaServer(const SenkushaServer&) = delete;
    SenkushaServer& operator=(const SenkushaServer&) = delete;

    bool start(uint16_t port = 9297);
    void stop();

private:
    struct Session {
        uint32_t client_tag = 0;
        uint32_t server_tag = 0;
        uint32_t next_seq = 0;
        uint16_t av_packet_index = 0;
        bool established = false;
        int64_t last_ms = 0;
        std::vector<uint8_t> cookie;
        std::deque<uint32_t> recent_seq;
    };

    void run();
    void on_packet(const uint8_t* d, size_t n, const sockaddr_in& from);
    void handle_init(const uint8_t* d, size_t n, const sockaddr_in& from);
    void handle_cookie(const uint8_t* d, size_t n, const sockaddr_in& from);
    void handle_data(const uint8_t* d, size_t n, const sockaddr_in& from);
    void handle_message(Session& s, const sockaddr_in& from, uint16_t channel,
                        const uint8_t* body, size_t len);

    void send_raw(const sockaddr_in& to, const uint8_t* p, size_t n);
    void send_init_ack(Session& s, const sockaddr_in& to);
    void send_cookie_ack(Session& s, const sockaddr_in& to);
    void send_data_ack(Session& s, const sockaddr_in& to, uint32_t seq);
    void send_data_msg(Session& s, const sockaddr_in& to, uint16_t channel,
                       const std::vector<uint8_t>& body);
    void send_mtu_video(Session& s, const sockaddr_in& to, uint32_t id, uint32_t mtu_req);

    static uint64_t key_of(const sockaddr_in& a);

#ifdef _WIN32
    SOCKET sock_ = INVALID_SOCKET;
#else
    int sock_ = -1;
#endif
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::map<uint64_t, Session> sessions_;
};
