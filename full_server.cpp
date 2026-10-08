#include "full_server.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using NativeSock = SOCKET;
  static inline void close_native(NativeSock s) { closesocket(s); }
  static inline NativeSock invalid_native() { return INVALID_SOCKET; }
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using NativeSock = int;
  static inline void close_native(NativeSock s) { ::close(s); }
  static inline NativeSock invalid_native() { return -1; }
#endif

namespace ps {

namespace {

std::mutex g_log_mtx;

void logf(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_log_mtx);
    std::cout << "[full] " << msg << std::endl;
}

NativeSock ns(std::intptr_t h) { return static_cast<NativeSock>(h); }
std::intptr_t hs(NativeSock s) { return static_cast<std::intptr_t>(s); }
bool is_invalid(std::intptr_t h) { return ns(h) == invalid_native(); }

bool wait_readable(NativeSock s, int ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return select(static_cast<int>(s) + 1, &fds, nullptr, nullptr, &tv) > 0;
}

bool send_all(NativeSock s, const uint8_t* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        const int r = send(s, reinterpret_cast<const char*>(data + sent), static_cast<int>(n - sent), 0);
        if (r <= 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

bool send_all(NativeSock s, const std::string& str) {
    return send_all(s, reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

struct Request {
    std::string first_line;
    std::vector<std::pair<std::string, std::string>> headers;   // keys are lower-case
    std::vector<uint8_t> leftover;                                // body bytes already read

    std::string header(const std::string& key) const {
        for (const auto& kv : headers) if (kv.first == key) return kv.second;
        return {};
    }
};

// Reads up to \r\n\r\n. Stops early when running == false.
bool read_request(NativeSock s, const std::atomic<bool>& running, Request& req) {
    std::string raw;
    while (raw.find("\r\n\r\n") == std::string::npos) {
        if (!running) return false;
        if (raw.size() > 65536) return false;
        if (!wait_readable(s, 200)) continue;
        char buf[4096];
        const int r = recv(s, buf, sizeof(buf), 0);
        if (r <= 0) break;
        raw.append(buf, buf + r);
    }
    const size_t end = raw.find("\r\n\r\n");
    const std::string head = raw.substr(0, end == std::string::npos ? raw.size() : end);
    if (end != std::string::npos) {
        const size_t off = end + 4;
        req.leftover.assign(raw.begin() + off, raw.end());
    }
    std::istringstream in(head);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) { req.first_line = line; first = false; continue; }
        const size_t c = line.find(':');
        if (c == std::string::npos) continue;
        req.headers.emplace_back(lower(trim(line.substr(0, c))), trim(line.substr(c + 1)));
    }
    return !req.first_line.empty();
}

bool read_exactly(NativeSock s, const std::atomic<bool>& running, uint8_t* out, size_t n) {
    size_t got = 0;
    while (got < n) {
        if (!running) return false;
        if (!wait_readable(s, 200)) continue;
        const int r = recv(s, reinterpret_cast<char*>(out + got), static_cast<int>(n - got), 0);
        if (r <= 0) return false;
        got += static_cast<size_t>(r);
    }
    return true;
}

std::string build_response_plaintext(const uint8_t regist_key[16], const Key16& rp_key,
                                     const uint8_t mac[6], const std::string& nickname, bool is_ps5) {
    const std::string prefix = is_ps5 ? "PS5" : "PS4";
    std::string s;
    s += prefix + "-RegistKey:" + to_hex(regist_key, 16) + "\r\n";
    s += "RP-KeyType:0\r\n";
    s += "RP-Key:" + to_hex(rp_key.data(), 16) + "\r\n";
    s += prefix + "-Mac:" + to_hex(mac, 6) + "\r\n";
    s += prefix + "-Nickname:" + nickname + "\r\n";
    s += "\r\n";
    return s;
}

}  // namespace

bool FullServer::start(const Config& cfg) {
    if (running_) return true;
    cfg_ = cfg;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        logf("WSAStartup failed");
        return false;
    }
    wsa_started_ = true;
#endif

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(cfg_.port);

    NativeSock u = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    NativeSock t = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (u == invalid_native() || t == invalid_native()) {
        logf("socket() failed");
        if (u != invalid_native()) close_native(u);
        if (t != invalid_native()) close_native(t);
        return false;
    }
#ifndef _WIN32
    int one = 1;
    setsockopt(t, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
#endif
    if (bind(u, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        bind(t, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        listen(t, 16) != 0) {
        logf("bind/listen failed on port " + std::to_string(cfg_.port) +
             " (port in use? is the old RegistrationServer or the Python server still running?)");
        close_native(u);
        close_native(t);
        return false;
    }

    udp_ = hs(u);
    tcp_ = hs(t);
    running_ = true;
    udp_thread_ = std::thread(&FullServer::udp_loop, this);
    tcp_thread_ = std::thread(&FullServer::tcp_loop, this);
    logf("Discovery UDP responder listening on 0.0.0.0:" + std::to_string(cfg_.port));
    logf("Unified server (regist + session/init + ctrl) listening on 0.0.0.0:" + std::to_string(cfg_.port));
    logf("RP-Key = " + to_hex(cfg_.rp_key.data(), 16));
    return true;
}

void FullServer::stop() {
    if (!running_.exchange(false)) return;
    if (udp_thread_.joinable()) udp_thread_.join();
    if (tcp_thread_.joinable()) tcp_thread_.join();
    for (int i = 0; i < 300 && active_clients_ > 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!is_invalid(udp_)) close_native(ns(udp_));
    if (!is_invalid(tcp_)) close_native(ns(tcp_));
    udp_ = tcp_ = hs(invalid_native());
#ifdef _WIN32
    if (wsa_started_) { WSACleanup(); wsa_started_ = false; }
#endif
}

bool FullServer::get_session(Key16& nonce, Key16& rp_key) const {
    std::lock_guard<std::mutex> lk(state_mtx_);
    if (!have_nonce_) return false;
    nonce = nonce_;
    rp_key = cfg_.rp_key;
    return true;
}

void FullServer::set_ctrl_callback(CtrlCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mtx_);
    ctrl_cb_ = std::move(cb);
}

void FullServer::udp_loop() {
    const NativeSock s = ns(udp_);
    while (running_) {
        if (!wait_readable(s, 200)) continue;
        uint8_t buf[512];
        sockaddr_in from;
#ifdef _WIN32
        int fromlen = sizeof(from);
#else
        socklen_t fromlen = sizeof(from);
#endif
        const int r = recvfrom(s, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromlen);
        if (r <= 0) continue;
        size_t n = static_cast<size_t>(r);
        while (n > 0 && buf[n - 1] == 0) --n;
        const std::string text(reinterpret_cast<char*>(buf), n);

        static const char kRes3[] = {'R', 'E', 'S', '3', 0};
        static const char kRes2[] = {'R', 'E', 'S', '2', 0};
        const char* reply = nullptr;
        if (text == "SRC3") reply = kRes3;
        else if (text == "SRC2") reply = kRes2;
        else { logf("Discovery: unknown search packet (" + std::to_string(r) + " bytes)"); continue; }

        sendto(s, reply, 5, 0, reinterpret_cast<sockaddr*>(&from), fromlen);
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        logf(std::string("Discovery: ") + ip + ":" + std::to_string(ntohs(from.sin_port)) + " -> " + text);
    }
}

void FullServer::tcp_loop() {
    const NativeSock s = ns(tcp_);
    while (running_) {
        if (!wait_readable(s, 200)) continue;
        sockaddr_in from;
#ifdef _WIN32
        int fromlen = sizeof(from);
#else
        socklen_t fromlen = sizeof(from);
#endif
        NativeSock c = accept(s, reinterpret_cast<sockaddr*>(&from), &fromlen);
        if (c == invalid_native()) continue;
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        const std::string peer = std::string(ip) + ":" + std::to_string(ntohs(from.sin_port));

        ++active_clients_;
        const Handle h = hs(c);
        std::thread([this, h, peer]() {
            handle_client(h, peer);
            close_native(ns(h));
            --active_clients_;
        }).detach();
    }
}

void FullServer::handle_client(Handle sock, std::string peer) {
    const NativeSock s = ns(sock);
    Request req;
    if (!read_request(s, running_, req)) return;
    logf("Request from " + peer + ": " + req.first_line);
    const std::string fl = lower(req.first_line);
    const bool is_ps5 = fl.find("ps5") != std::string::npos;

    if (req.first_line.find("/sess/rgst") != std::string::npos) {
        size_t content_length = 0;
        try { content_length = static_cast<size_t>(std::stoull(req.header("content-length").empty() ? "0" : req.header("content-length"))); }
        catch (...) { content_length = 0; }
        if (content_length > (1u << 20)) { logf("Regist: request body too large"); return; }
        std::vector<uint8_t> body = req.leftover;
        while (body.size() < content_length) {
            if (!running_) return;
            if (!wait_readable(s, 200)) continue;
            uint8_t buf[4096];
            const int r = recv(s, reinterpret_cast<char*>(buf), sizeof(buf), 0);
            if (r <= 0) break;
            body.insert(body.end(), buf, buf + r);
        }
        if (body.size() >= kRegistInnerOffset) handle_regist(sock, is_ps5, body);
        else logf("Regist: body is shorter than expected");
    } else if (req.first_line.find("/sess/init") != std::string::npos) {
        handle_init(sock);
    } else if (req.first_line.find("/sess/ctrl") != std::string::npos) {
        if (handle_ctrl(sock, req.header("rp-auth"))) ctrl_message_loop(sock, peer);
    } else {
        logf("Unknown path: " + req.first_line);
    }
}

void FullServer::handle_regist(Handle sock, bool is_ps5, const std::vector<uint8_t>& body) {
    RegistKeys k;
    if (!parse_regist_payload(body.data(), body.size(), is_ps5, cfg_.pin, k)) {
        logf("Regist: failed to parse request body");
        return;
    }
    const auto plain_req = aes128_cfb(k.bright, k.iv, k.inner_encrypted.data(), k.inner_encrypted.size(), false);
    logf("Regist: decrypted request:\n" + std::string(plain_req.begin(), plain_req.end()));

    uint8_t regist_key[16], mac[6];
    random_bytes(regist_key, sizeof(regist_key));
    random_bytes(mac, sizeof(mac));
    logf("Regist: RP-Key (fixed) = " + to_hex(cfg_.rp_key.data(), 16));

    const std::string plain = build_response_plaintext(regist_key, cfg_.rp_key, mac, cfg_.nickname, is_ps5);
    const auto enc = aes128_cfb(k.bright, k.iv, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), true);

    const std::string head = "HTTP/1.1 200 Ok\r\nContent-Length: " + std::to_string(enc.size()) + "\r\n\r\n";
    std::vector<uint8_t> resp(head.begin(), head.end());
    resp.insert(resp.end(), enc.begin(), enc.end());
    send_all(ns(sock), resp.data(), resp.size());
    logf("Regist: response sent (" + std::to_string(resp.size()) + " bytes)");
}

void FullServer::handle_init(Handle sock) {
    Key16 nonce{};
    random_bytes(nonce.data(), nonce.size());
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        nonce_ = nonce;
        have_nonce_ = true;
    }
    if (!cfg_.session_state_path.empty()) {
        std::ofstream f(cfg_.session_state_path, std::ios::trunc);
        if (f) f << "{\"nonce\": \"" << to_hex(nonce.data(), 16) << "\", \"rp_key\": \"" << to_hex(cfg_.rp_key.data(), 16) << "\"}";
        else logf("SessionInit: could not write " + cfg_.session_state_path);
    }
    const std::string b64 = base64_encode(nonce.data(), nonce.size());
    logf("SessionInit: generated RP-Nonce = " + b64 + " (raw=" + to_hex(nonce.data(), 16) + ")");
    send_all(ns(sock), "HTTP/1.1 200 Ok\r\nRP-Nonce: " + b64 + "\r\n\r\n");
    logf("SessionInit: response sent");
}

bool FullServer::handle_ctrl(Handle sock, const std::string& rp_auth_b64) {
    Key16 nonce{};
    {
        std::lock_guard<std::mutex> lk(state_mtx_);
        if (!have_nonce_) {
            logf("Ctrl: no saved nonce - GET /sess/init must come before /sess/ctrl");
            return false;
        }
        nonce = nonce_;
    }
    Key16 ambassador{}, bright{};
    ctrl_keys_ps5(nonce, cfg_.rp_key, ambassador, bright);
    logf("Ctrl: ambassador=" + to_hex(ambassador.data(), 16) + " bright=" + to_hex(bright.data(), 16));

    const Key16 iv = generate_iv(ambassador, 0, true);

    if (!rp_auth_b64.empty()) {
        const auto ct = base64_decode(rp_auth_b64);
        const auto dec = aes128_cfb(bright, iv, ct.data(), ct.size(), false);
        logf("Ctrl: RP-Auth decrypted -> " + to_hex(dec.data(), dec.size()) + " (should match RegistKey)");
    }

    uint8_t plain[16] = {2};                       // RP-Server-Type: first byte = 2 (PS5)
    const auto enc = aes128_cfb(bright, iv, plain, sizeof(plain), true);
    const std::string b64 = base64_encode(enc.data(), enc.size());
    send_all(ns(sock), "HTTP/1.1 200 Ok\r\nRP-Server-Type: " + b64 + "\r\n\r\n");
    logf("Ctrl: response sent (RP-Server-Type=" + b64 + ")");
    return true;
}

// After the response the client keeps the connection open and sends binary Ctrl messages:
// 4 bytes size (BE), 2 bytes type (BE), 2 bytes reserved, then the payload.
void FullServer::ctrl_message_loop(Handle sock, const std::string& peer) {
    const NativeSock s = ns(sock);
    for (;;) {
        uint8_t header[8];
        if (!read_exactly(s, running_, header, sizeof(header))) break;
        const uint32_t size = (uint32_t(header[0]) << 24) | (uint32_t(header[1]) << 16) | (uint32_t(header[2]) << 8) | header[3];
        const uint16_t type = static_cast<uint16_t>((header[4] << 8) | header[5]);
        if (size > (16u << 20)) { logf("Ctrl: message too large, closing " + peer); break; }
        std::vector<uint8_t> payload(size);
        if (size && !read_exactly(s, running_, payload.data(), size)) break;
        if (cfg_.log_ctrl_messages) {
            char t[16];
            std::snprintf(t, sizeof(t), "0x%x", type);
            logf(std::string("Ctrl: received message type=") + t + " size=" + std::to_string(size) + " from " + peer);
        }
        CtrlCallback cb;
        { std::lock_guard<std::mutex> lk(cb_mtx_); cb = ctrl_cb_; }
        if (cb) cb(type, payload);
    }
    logf("Ctrl: connection closed " + peer);
}

}  // namespace ps
