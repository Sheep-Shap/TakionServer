// senkusha_server.cpp - Takion v7 "Senkusha" responder (RTT + MTU probe).
//
// The client (chiaki Senkusha) connects to UDP port 9297 and runs:
//   INIT / COOKIE handshake -> TAKIONPROTOCOLREQUEST -> BIG/BANG
//   -> echo on, 10 audio-type AV "pings" (we echo them back verbatim), echo off
//   -> MTU-in test: MtuCommand, we answer with a video AV packet of mtu_req bytes (IP size)
//   -> MTU-out test: ClientMtuCommand (we echo it back), then big audio-type pings (echoed)
//   -> final ClientMtuCommand, DISCONNECT.
// No crypto is used on this connection (enable_crypt = false), GMAC and key pos are zero.

#include "senkusha_server.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

#ifdef _WIN32
  #pragma comment(lib, "ws2_32.lib")
  #define SK_CLOSE closesocket
  #define SK_ERR SOCKET_ERROR
  static int sk_last_error() { return WSAGetLastError(); }
#else
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <unistd.h>
  #include <cerrno>
  #define SK_CLOSE close
  #define SK_ERR (-1)
  static int sk_last_error() { return errno; }
#endif

namespace {

constexpr uint32_t kArwnd = 0x19000;
constexpr uint16_t kStreams = 0x64;
constexpr size_t kHdr = 17;                 // 1 (packet type) + 16 (takion message header)
constexpr size_t kCookieSize = 32;
constexpr size_t kUdpOverhead = 28;         // IPv4 + UDP headers
constexpr size_t kAvBase = 0x12;            // takion v7 AV header
constexpr size_t kAvVideoAdd = 3;
constexpr size_t kMaxDatagram = 2048;

enum : uint8_t {
    CH_DATA = 0x00, CH_INIT = 0x01, CH_INIT_ACK = 0x02, CH_DATA_ACK = 0x03,
    CH_COOKIE = 0x0A, CH_COOKIE_ACK = 0x0B
};

// TakionMessage.PayloadType
enum : uint32_t {
    PT_BIG = 0, PT_BANG = 1, PT_DISCONNECT = 8, PT_SENKUSHA = 12,
    PT_PROTOCOL_REQUEST = 31, PT_PROTOCOL_REQUEST_ACK = 32
};

// SenkushaPayload.Command
enum : uint32_t { SK_ECHO = 0, SK_MTU = 1, SK_BANDWIDTH = 3, SK_CLIENT_MTU = 4 };

void put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
void put32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}
uint16_t get16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
uint32_t get32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

uint32_t rnd32() {
    static std::mt19937 gen{std::random_device{}()};
    return gen();
}

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ---- minimal protobuf writer ----
void pb_varint(std::vector<uint8_t>& o, uint64_t v) {
    while (v >= 0x80) { o.push_back(uint8_t((v & 0x7f) | 0x80)); v >>= 7; }
    o.push_back(uint8_t(v));
}
void pb_field_varint(std::vector<uint8_t>& o, uint32_t field, uint64_t v) {
    pb_varint(o, (uint64_t(field) << 3) | 0);
    pb_varint(o, v);
}
void pb_field_bytes(std::vector<uint8_t>& o, uint32_t field, const std::vector<uint8_t>& v) {
    pb_varint(o, (uint64_t(field) << 3) | 2);
    pb_varint(o, v.size());
    o.insert(o.end(), v.begin(), v.end());
}

// ---- minimal protobuf reader ----
struct PbField {
    uint32_t number = 0;
    uint32_t wire = 0;
    uint64_t varint = 0;
    const uint8_t* data = nullptr;
    size_t len = 0;
};

class PbReader {
public:
    PbReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool next(PbField& f) {
        if (pos_ >= n_) return false;
        uint64_t tag;
        if (!varint(tag)) { bad_ = true; return false; }
        f.number = uint32_t(tag >> 3);
        f.wire = uint32_t(tag & 7);
        f.data = nullptr; f.len = 0; f.varint = 0;
        switch (f.wire) {
        case 0: if (!varint(f.varint)) { bad_ = true; return false; } return true;
        case 1: return skip(8, f);
        case 5: return skip(4, f);
        case 2: {
            uint64_t l;
            if (!varint(l) || l > n_ - pos_) { bad_ = true; return false; }
            f.data = p_ + pos_; f.len = size_t(l); pos_ += size_t(l);
            return true;
        }
        default: bad_ = true; return false;
        }
    }
    bool bad() const { return bad_; }
private:
    bool varint(uint64_t& v) {
        v = 0; int shift = 0;
        while (pos_ < n_ && shift < 64) {
            uint8_t b = p_[pos_++];
            v |= uint64_t(b & 0x7f) << shift;
            if (!(b & 0x80)) return true;
            shift += 7;
        }
        return false;
    }
    bool skip(size_t k, PbField& f) {
        if (k > n_ - pos_) { bad_ = true; return false; }
        f.data = p_ + pos_; f.len = k; pos_ += k; return true;
    }
    const uint8_t* p_; size_t n_; size_t pos_ = 0; bool bad_ = false;
};

struct SenkushaMsg {
    bool has_type = false;
    uint32_t type = 0;
    bool has_payload = false;
    uint32_t command = 0;
    bool echo_state = false;
    // MtuCommand
    bool has_mtu = false;
    uint32_t mtu_id = 0, mtu_req = 0, mtu_num = 1;
    // ClientMtuCommand (kept raw so we can echo it back unchanged)
    bool has_client_mtu = false;
    std::vector<uint8_t> client_mtu_raw;
    uint32_t client_mtu_id = 0, client_mtu_req = 0;
    bool client_mtu_state = false;
};

bool parse_message(const uint8_t* p, size_t n, SenkushaMsg& m) {
    PbReader top(p, n);
    PbField f;
    while (top.next(f)) {
        if (f.number == 1 && f.wire == 0) { m.type = uint32_t(f.varint); m.has_type = true; }
        else if (f.number == 14 && f.wire == 2) {         // senkusha_payload
            m.has_payload = true;
            PbReader sp(f.data, f.len);
            PbField g;
            while (sp.next(g)) {
                if (g.number == 1 && g.wire == 0) m.command = uint32_t(g.varint);
                else if (g.number == 2 && g.wire == 2) {  // echo_command
                    PbReader r(g.data, g.len); PbField h;
                    while (r.next(h)) if (h.number == 1 && h.wire == 0) m.echo_state = h.varint != 0;
                } else if (g.number == 3 && g.wire == 2) { // mtu_command
                    m.has_mtu = true;
                    PbReader r(g.data, g.len); PbField h;
                    while (r.next(h)) {
                        if (h.wire != 0) continue;
                        if (h.number == 1) m.mtu_id = uint32_t(h.varint);
                        else if (h.number == 2) m.mtu_req = uint32_t(h.varint);
                        else if (h.number == 4) m.mtu_num = uint32_t(h.varint);
                    }
                } else if (g.number == 5 && g.wire == 2) { // client_mtu_command
                    m.has_client_mtu = true;
                    m.client_mtu_raw.assign(g.data, g.data + g.len);
                    PbReader r(g.data, g.len); PbField h;
                    while (r.next(h)) {
                        if (h.wire != 0) continue;
                        if (h.number == 1) m.client_mtu_id = uint32_t(h.varint);
                        else if (h.number == 2) m.client_mtu_req = uint32_t(h.varint);
                        else if (h.number == 3) m.client_mtu_state = h.varint != 0;
                    }
                }
            }
        }
    }
    return !top.bad() && m.has_type;
}

}  // namespace

uint64_t SenkushaServer::key_of(const sockaddr_in& a) {
    return (uint64_t(ntohl(a.sin_addr.s_addr)) << 16) | ntohs(a.sin_port);
}

bool SenkushaServer::start(uint16_t port) {
    if (running_) return true;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (sock_ == INVALID_SOCKET) return false;
#else
    if (sock_ < 0) return false;
#endif

    // Real consoles drop oversized packets instead of fragmenting them; do the same
    // so the MTU-in test measures something meaningful.
#ifdef _WIN32
    DWORD df = 1;
    setsockopt(sock_, IPPROTO_IP, IP_DONTFRAGMENT, reinterpret_cast<const char*>(&df), sizeof(df));
#elif defined(IP_MTU_DISCOVER)
    int df = IP_PMTUDISC_DO;
    setsockopt(sock_, IPPROTO_IP, IP_MTU_DISCOVER, &df, sizeof(df));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SK_ERR) {
        std::fprintf(stderr, "[senkusha] bind to UDP %u failed (err %d)\n", unsigned(port), sk_last_error());
        SK_CLOSE(sock_);
#ifdef _WIN32
        sock_ = INVALID_SOCKET;
#else
        sock_ = -1;
#endif
        return false;
    }

    running_ = true;
    thread_ = std::thread([this] { run(); });
    std::printf("[senkusha] responder listening on UDP %u\n", unsigned(port));
    return true;
}

void SenkushaServer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
#ifdef _WIN32
    if (sock_ != INVALID_SOCKET) { SK_CLOSE(sock_); sock_ = INVALID_SOCKET; }
#else
    if (sock_ >= 0) { SK_CLOSE(sock_); sock_ = -1; }
#endif
    sessions_.clear();
}

void SenkushaServer::run() {
    std::vector<uint8_t> buf(kMaxDatagram);
    int64_t last_gc = now_ms();
    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock_, &fds);
        timeval tv{0, 200 * 1000};
        const int r = select(int(sock_) + 1, &fds, nullptr, nullptr, &tv);
        if (r > 0 && FD_ISSET(sock_, &fds)) {
            sockaddr_in from{};
#ifdef _WIN32
            int fl = sizeof(from);
#else
            socklen_t fl = sizeof(from);
#endif
            const int n = recvfrom(sock_, reinterpret_cast<char*>(buf.data()), int(buf.size()), 0,
                                   reinterpret_cast<sockaddr*>(&from), &fl);
            if (n > 0) on_packet(buf.data(), size_t(n), from);
        }
        const int64_t t = now_ms();
        if (t - last_gc > 5000) {
            last_gc = t;
            for (auto it = sessions_.begin(); it != sessions_.end();) {
                if (t - it->second.last_ms > 60000) it = sessions_.erase(it); else ++it;
            }
        }
    }
}

void SenkushaServer::on_packet(const uint8_t* d, size_t n, const sockaddr_in& from) {
    if (n == 0) return;
    const uint8_t base = d[0] & 0x0f;

    // Audio-type AV packet = ping. Echo it back unchanged (that is the "pong").
    if (base == 3) {
        auto it = sessions_.find(key_of(from));
        if (it == sessions_.end() || !it->second.established || n < kAvBase + 8) return;
        it->second.last_ms = now_ms();
        send_raw(from, d, n);
        return;
    }

    if (base != 0 || n < kHdr) return;               // congestion / feedback packets etc. are ignored
    switch (d[13]) {
    case CH_INIT:   handle_init(d, n, from);   break;
    case CH_COOKIE: handle_cookie(d, n, from); break;
    case CH_DATA:   handle_data(d, n, from);   break;
    default: break;
    }
}

void SenkushaServer::handle_init(const uint8_t* d, size_t n, const sockaddr_in& from) {
    if (n < kHdr + 16) return;
    const uint32_t client_tag = get32(d + kHdr);     // INIT payload: tag, a_rwnd, streams, initial seq
    if (client_tag == 0) return;

    Session& s = sessions_[key_of(from)];
    if (s.client_tag != client_tag || s.server_tag == 0) {
        s = Session{};
        s.client_tag = client_tag;
        s.server_tag = rnd32() | 1;
        s.next_seq = s.server_tag;                   // client expects the first data seq == server tag
        s.cookie.resize(kCookieSize);
        for (auto& b : s.cookie) b = uint8_t(rnd32());
    }
    s.last_ms = now_ms();
    std::printf("[senkusha] INIT from %s:%u tag=0x%x\n", inet_ntoa(from.sin_addr),
                unsigned(ntohs(from.sin_port)), client_tag);
    send_init_ack(s, from);
}

void SenkushaServer::handle_cookie(const uint8_t* d, size_t n, const sockaddr_in& from) {
    auto it = sessions_.find(key_of(from));
    if (it == sessions_.end() || n != kHdr + kCookieSize) return;
    Session& s = it->second;
    if (s.cookie.size() != kCookieSize || std::memcmp(d + kHdr, s.cookie.data(), kCookieSize) != 0) return;
    s.last_ms = now_ms();
    s.established = true;
    send_cookie_ack(s, from);
}

void SenkushaServer::handle_data(const uint8_t* d, size_t n, const sockaddr_in& from) {
    auto it = sessions_.find(key_of(from));
    if (it == sessions_.end() || !it->second.established) return;
    Session& s = it->second;

    const uint8_t flags = d[14];
    const uint16_t wire = get16(d + 15);
    if (wire < 4 || kHdr + (wire - 4) != n || n < kHdr + 9) return;

    const uint8_t* p = d + kHdr;
    const uint32_t seq = get32(p);
    const uint16_t channel = get16(p + 4);
    const uint8_t data_type = p[8] & 0x0f;

    s.last_ms = now_ms();
    send_data_ack(s, from, seq);

    for (uint32_t r : s.recent_seq) if (r == seq) return;     // retransmission, already handled
    s.recent_seq.push_back(seq);
    if (s.recent_seq.size() > 64) s.recent_seq.pop_front();

    if (data_type != 0 || !(flags & 0x01)) return;            // only single-fragment protobuf messages
    handle_message(s, from, channel, p + 9, n - kHdr - 9);
}

void SenkushaServer::handle_message(Session& s, const sockaddr_in& from, uint16_t channel,
                                    const uint8_t* body, size_t len) {
    SenkushaMsg m;
    if (!parse_message(body, len, m)) {
        std::printf("[senkusha] could not parse message (%zu bytes)\n", len);
        return;
    }

    switch (m.type) {
    case PT_PROTOCOL_REQUEST: {
        std::vector<uint8_t> inner, msg;
        pb_field_varint(inner, 1, 9);                          // takion_protocol_version
        pb_field_varint(msg, 1, PT_PROTOCOL_REQUEST_ACK);
        pb_field_bytes(msg, 32, inner);
        send_data_msg(s, from, channel, msg);
        std::printf("[senkusha] protocol request acked\n");
        break;
    }
    case PT_BIG: {
        std::vector<uint8_t> bang, msg;
        pb_field_varint(bang, 1, 9);                           // server_version
        pb_field_varint(bang, 2, 0);                           // token
        pb_field_varint(bang, 3, 1);                           // encrypted_key_accepted
        pb_field_varint(bang, 4, 1);                           // version_accepted
        pb_field_bytes(bang, 5, {});                           // session_key (required, empty)
        pb_field_varint(msg, 1, PT_BANG);
        pb_field_bytes(msg, 3, bang);
        send_data_msg(s, from, channel, msg);
        std::printf("[senkusha] BIG -> BANG\n");
        break;
    }
    case PT_SENKUSHA: {
        if (!m.has_payload) break;
        if (m.command == SK_ECHO) {
            std::printf("[senkusha] echo %s\n", m.echo_state ? "on" : "off");   // data ack already sent
        } else if (m.command == SK_MTU && m.has_mtu) {
            const uint32_t num = m.mtu_num == 0 ? 1 : (m.mtu_num > 8 ? 8 : m.mtu_num);
            for (uint32_t i = 0; i < num; ++i) send_mtu_video(s, from, m.mtu_id, m.mtu_req);
        } else if (m.command == SK_CLIENT_MTU && m.has_client_mtu) {
            std::vector<uint8_t> payload, msg;
            pb_field_varint(payload, 1, SK_CLIENT_MTU);
            pb_field_bytes(payload, 5, m.client_mtu_raw);      // echo the command back unchanged
            pb_field_varint(msg, 1, PT_SENKUSHA);
            pb_field_bytes(msg, 14, payload);
            send_data_msg(s, from, channel, msg);
            std::printf("[senkusha] client MTU command id=%u mtu=%u state=%d\n",
                        m.client_mtu_id, m.client_mtu_req, m.client_mtu_state ? 1 : 0);
        }
        break;
    }
    case PT_DISCONNECT:
        std::printf("[senkusha] client disconnected\n");
        sessions_.erase(key_of(from));
        break;
    default:
        break;
    }
}

void SenkushaServer::send_raw(const sockaddr_in& to, const uint8_t* p, size_t n) {
    const int r = sendto(sock_, reinterpret_cast<const char*>(p), int(n), 0,
                         reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    if (r == SK_ERR) {
        static int64_t last_log = 0;
        const int64_t t = now_ms();
        if (t - last_log > 2000) {                             // oversize packets are expected during MTU probing
            last_log = t;
            std::printf("[senkusha] sendto %zu bytes failed (err %d)\n", n, sk_last_error());
        }
    }
}

void SenkushaServer::send_init_ack(Session& s, const sockaddr_in& to) {
    constexpr size_t payload = 16 + kCookieSize;
    std::vector<uint8_t> pkt(kHdr + payload, 0);
    pkt[0] = 0x00;
    put32(&pkt[1], s.client_tag);
    pkt[13] = CH_INIT_ACK;
    put16(&pkt[15], uint16_t(payload + 4));
    uint8_t* p = &pkt[kHdr];
    put32(p + 0, s.server_tag);
    put32(p + 4, kArwnd);
    put16(p + 8, kStreams);
    put16(p + 10, kStreams);
    put32(p + 12, s.server_tag);                               // initial seq == tag
    std::memcpy(p + 16, s.cookie.data(), kCookieSize);
    send_raw(to, pkt.data(), pkt.size());
}

void SenkushaServer::send_cookie_ack(Session& s, const sockaddr_in& to) {
    uint8_t pkt[kHdr] = {0};
    put32(pkt + 1, s.client_tag);
    pkt[13] = CH_COOKIE_ACK;
    put16(pkt + 15, 4);
    send_raw(to, pkt, sizeof(pkt));
}

void SenkushaServer::send_data_ack(Session& s, const sockaddr_in& to, uint32_t seq) {
    uint8_t pkt[kHdr + 12] = {0};
    put32(pkt + 1, s.client_tag);
    pkt[13] = CH_DATA_ACK;
    put16(pkt + 15, 12 + 4);
    put32(pkt + kHdr + 0, seq);
    put32(pkt + kHdr + 4, kArwnd);
    send_raw(to, pkt, sizeof(pkt));
}

void SenkushaServer::send_data_msg(Session& s, const sockaddr_in& to, uint16_t channel,
                                   const std::vector<uint8_t>& body) {
    std::vector<uint8_t> pkt(kHdr + 9 + body.size(), 0);
    put32(&pkt[1], s.client_tag);
    pkt[13] = CH_DATA;
    pkt[14] = 0x01;                                            // last fragment
    put16(&pkt[15], uint16_t(pkt.size() - 13));
    uint8_t* p = &pkt[kHdr];
    put32(p + 0, s.next_seq++);
    put16(p + 4, channel);
    put16(p + 6, 0);
    p[8] = 0x00;                                               // protobuf
    if (!body.empty()) std::memcpy(p + 9, body.data(), body.size());
    send_raw(to, pkt.data(), pkt.size());
}

// Video AV packet whose total IP size equals mtu_req (UDP payload = mtu_req - 28).
void SenkushaServer::send_mtu_video(Session& s, const sockaddr_in& to, uint32_t id, uint32_t mtu_req) {
    size_t total = mtu_req > kUdpOverhead ? size_t(mtu_req) - kUdpOverhead : 0;
    const size_t min_total = kAvBase + kAvVideoAdd + 8;
    if (total < min_total) total = min_total;
    if (total > 1472) total = 1472;

    std::vector<uint8_t> pkt(total, 0);
    pkt[0] = 0x02;                                             // video
    put16(&pkt[1], s.av_packet_index++);                       // packet index (must stay sequential)
    put16(&pkt[3], uint16_t(id));                              // frame index == MTU request id
    put32(&pkt[5], 0);                                         // fec 0, 1 unit in frame, unit index 0
    pkt[9] = 0xff;                                             // codec
    static const char pad[] = {'C', 'H', 'I', 'A', 'K', 'I'};
    for (size_t i = kAvBase + kAvVideoAdd; i < total; ++i) pkt[i] = uint8_t(pad[i % sizeof(pad)]);
    send_raw(to, pkt.data(), pkt.size());
}
