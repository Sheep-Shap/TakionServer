// discovery_server.h - answers the standard PlayStation "SRCH" discovery broadcast on UDP 9302,
// so Chiaki-based clients list this PC as a console and can register it without typing an IP.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#ifdef _WIN32
  #include <winsock2.h>
#endif

class DiscoveryServer {
public:
    DiscoveryServer() = default;
    ~DiscoveryServer() { stop(); }
    DiscoveryServer(const DiscoveryServer&) = delete;
    DiscoveryServer& operator=(const DiscoveryServer&) = delete;

    // host_name is shown in the client's console list; rp_key_hex (lower-case) makes the host id stable.
    // host-id equals the MAC sent in the registration reply (see host_identity.h).
    bool start(uint16_t port, const std::string& host_name, const std::string& rp_key_hex);
    void stop();

    std::string make_reply(const char* req, size_t n) const;

private:
    void run();

#ifdef _WIN32
    SOCKET sock_ = INVALID_SOCKET;
#else
    int sock_ = -1;
#endif
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::string host_name_;
    std::string host_id_;
};
