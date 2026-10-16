// discovery_server.cpp
//
// Chiaki-style clients find consoles by broadcasting
//     "SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00030010\n"
// to UDP 9302 (PS5). A console answers with an HTTP-like status line followed by key:value lines
// (host-id, host-type, host-name, host-request-port, device-discovery-protocol-version, system-version).
// We reply the same way, so the PC shows up in the client's list and the client already knows its IP.

#include "discovery_server.h"
#include "host_identity.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  #define DS_CLOSE closesocket
  #define DS_ERR SOCKET_ERROR
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #define DS_CLOSE close
  #define DS_ERR (-1)
#endif

namespace {

bool starts_with(const char* s, size_t n, const char* prefix) {
    const size_t k = std::strlen(prefix);
    return n >= k && std::memcmp(s, prefix, k) == 0;
}

}  // namespace

bool DiscoveryServer::start(uint16_t port, const std::string& host_name, const std::string& rp_key_hex) {
    if (running_) return true;
    host_name_ = host_name.empty() ? "PC-Server" : host_name;
    uint8_t mac[6];
    derive_host_mac(host_name_, rp_key_hex, mac);      // same MAC as in the registration reply
    host_id_ = host_id_from_mac(mac);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (sock_ == INVALID_SOCKET) { WSACleanup(); return false; }
#else
    if (sock_ < 0) return false;
#endif
    int one = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == DS_ERR) {
        std::fprintf(stderr, "[discovery] bind to UDP %u failed\n", unsigned(port));
        DS_CLOSE(sock_);
#ifdef _WIN32
        sock_ = INVALID_SOCKET;
        WSACleanup();
#else
        sock_ = -1;
#endif
        return false;
    }

    running_ = true;
    thread_ = std::thread([this] { run(); });
    std::printf("[discovery] listening on UDP %u as \"%s\" (host-id %s)\n", unsigned(port), host_name_.c_str(), host_id_.c_str());
    return true;
}

void DiscoveryServer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) { DS_CLOSE(sock_); sock_ = INVALID_SOCKET; }
    WSACleanup();
#else
    if (sock_ >= 0) { DS_CLOSE(sock_); sock_ = -1; }
#endif
}

std::string DiscoveryServer::make_reply(const char* req, size_t n) const {
    if (!starts_with(req, n, "SRCH * HTTP/1.1")) return {};
    std::string r;
    r += "HTTP/1.1 200 Ok\n";
    r += "host-id:" + host_id_ + "\n";
    r += "host-type:PS5\n";
    r += "host-name:" + host_name_ + "\n";
    r += "host-request-port:997\n";
    r += "device-discovery-protocol-version:00030010\n";
    r += "system-version:09000000\n";
    r += "\n";
    return r;
}

void DiscoveryServer::run() {
    char buf[1024];
    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock_, &fds);
        timeval tv{0, 200 * 1000};
        if (select(int(sock_) + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;

        sockaddr_in from{};
#ifdef _WIN32
        int fl = sizeof(from);
#else
        socklen_t fl = sizeof(from);
#endif
        const int n = recvfrom(sock_, buf, int(sizeof(buf)), 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) continue;

        const std::string reply = make_reply(buf, size_t(n));
        if (reply.empty()) continue;
        sendto(sock_, reply.data(), int(reply.size()), 0, reinterpret_cast<const sockaddr*>(&from), sizeof(from));

        static int logged = 0;
        if (logged++ < 20) {
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            std::printf("[discovery] search from %s:%u -> replied\n", ip, unsigned(ntohs(from.sin_port)));
        }
    }
}
