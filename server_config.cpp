#include "server_config.h"

#include <openssl/rand.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool parse_uint(const std::string& v, unsigned long long max, unsigned long long& out) {
    if (v.empty() || v.size() > 10) return false;
    for (char c : v) if (c < '0' || c > '9') return false;
    out = std::strtoull(v.c_str(), nullptr, 10);
    return out <= max;
}

bool is_true(const std::string& val) {
    const std::string v = lower(val);
    return v == "1" || v == "true" || v == "yes";
}

const char* kDefaultText =
    "# PS Remote server settings. Lines starting with # are comments.\n"
    "regist_port = 9295\n"
    "takion_port = 9296\n"
    "# 8 digits. Type this PIN on the phone when you register it.\n"
    "pin = 12345678\n"
    "nickname = My-PC-Server\n"
    "# 32 hex characters, or auto = generate a random key once and save it here.\n"
    "# Changing the key means the phone has to be registered again.\n"
    "rp_key = 00112233445566778899aabbccddeeff\n"
    "# Leave empty. Old bridge file written after /sess/init (not needed any more).\n"
    "session_state_file =\n"
    "# 1 = print every Ctrl message sent by the client\n"
    "log_ctrl_messages = 0\n"
    "# Video bitrate in Mbit/s. bitrate_max_mbps is the fixed bitrate unless adaptive_bitrate = 1,\n"
    "# in which case it is the start value and the ceiling, and bitrate_min_mbps is the floor.\n"
    "adaptive_bitrate = 0\n"
    "bitrate_min_mbps = 6\n"
    "bitrate_max_mbps = 20\n"
    "# 1 = never exceed the bitrate the client asks for (bwKbpsSent), 0 = always use bitrate_max_mbps.\n"
    "# Emulated gamepad: ds4 (DualShock 4) or x360 (Xbox 360).\n"
    "controller_type = ds4\n"
    "respect_client_bitrate = 1\n";

}  // namespace

bool parse_hex16(const std::string& hex, std::array<uint8_t, 16>& out) {
    if (hex.size() != 32) return false;
    for (size_t i = 0; i < 16; ++i) {
        int v[2];
        for (int k = 0; k < 2; ++k) {
            const char c = hex[i * 2 + k];
            if (c >= '0' && c <= '9') v[k] = c - '0';
            else if (c >= 'a' && c <= 'f') v[k] = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v[k] = c - 'A' + 10;
            else return false;
        }
        out[i] = static_cast<uint8_t>((v[0] << 4) | v[1]);
    }
    return true;
}

std::string exe_directory() {
#ifdef _WIN32
    char buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, static_cast<DWORD>(sizeof(buf)));
    if (n == 0 || n >= sizeof(buf)) return "";
    std::string p(buf, n);
    const size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? "" : p.substr(0, slash + 1);
#else
    return "";
#endif
}

bool load_server_config(const std::string& path, ServerConfig& cfg, std::string& err) {
    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        if (!in) {
            std::ofstream out(path, std::ios::trunc);
            if (out) out << kDefaultText;          // not fatal if the folder is read-only: defaults are used
        }
    }
    {
        std::ifstream in(path);
        std::string line;
        if (in) while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }

    std::string controller_type = "ds4";   // "ds4" или "x360"
    bool rp_auto = false;
    size_t rp_line = static_cast<size_t>(-1);

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (t.empty() || t[0] == '#') continue;
        const size_t eq = t.find('=');
        if (eq == std::string::npos) { err = "line " + std::to_string(i + 1) + ": expected key = value"; return false; }
        const std::string key = lower(trim(t.substr(0, eq)));
        const std::string val = trim(t.substr(eq + 1));
        unsigned long long n = 0;

        if (key == "regist_port") {
            if (!parse_uint(val, 65535, n) || n == 0) { err = "regist_port must be 1..65535"; return false; }
            cfg.regist_port = static_cast<uint16_t>(n);
        } else if (key == "takion_port") {
            if (!parse_uint(val, 65535, n) || n == 0) { err = "takion_port must be 1..65535"; return false; }
            cfg.takion_port = static_cast<uint16_t>(n);
        } else if (key == "pin") {
            if (val.size() != 8 || !parse_uint(val, 99999999ULL, n)) { err = "pin must be exactly 8 digits"; return false; }
            cfg.pin = static_cast<uint32_t>(n);
        } else if (key == "nickname") {
            if (val.empty() || val.size() > 32) { err = "nickname must be 1..32 characters"; return false; }
            cfg.nickname = val;
        } else if (key == "rp_key") {
            rp_line = i;
            if (lower(val) == "auto") rp_auto = true;
            else {
                std::array<uint8_t, 16> tmp{};
                if (!parse_hex16(val, tmp)) { err = "rp_key must be 32 hex characters or auto"; return false; }
                cfg.rp_key_hex = lower(val);
            }
        } else if (key == "session_state_file") {
            cfg.session_state_file = val;
        } else if (key == "log_ctrl_messages") {
            cfg.log_ctrl_messages = is_true(val);
        } else if (key == "controller_type") {
            const std::string v = lower(val);
            cfg.controller_type = (v.find("360") != std::string::npos || v.find("xbox") != std::string::npos)
                                ? "x360" : "ds4";
        } else if (key == "adaptive_bitrate") {
            cfg.adaptive_bitrate = is_true(val);
        } else if (key == "respect_client_bitrate") {
            cfg.respect_client_bitrate = is_true(val);
        } else if (key == "bitrate_min_mbps" || key == "bitrate_max_mbps") {
            char* endp = nullptr;
            const double d = std::strtod(val.c_str(), &endp);
            if (val.empty() || *endp != '\0' || d < 1.0 || d > 200.0) { err = key + " must be a number from 1 to 200"; return false; }
            (key == "bitrate_min_mbps" ? cfg.bitrate_min_mbps : cfg.bitrate_max_mbps) = d;
        }
        // unknown keys are ignored
    }

    if (cfg.bitrate_min_mbps > cfg.bitrate_max_mbps) { err = "bitrate_min_mbps must not exceed bitrate_max_mbps"; return false; }

    if (rp_auto) {
        uint8_t k[16];
        if (RAND_bytes(k, sizeof(k)) != 1) { err = "could not generate a random key"; return false; }
        static const char* hx = "0123456789abcdef";
        std::string hex;
        for (uint8_t b : k) { hex += hx[b >> 4]; hex += hx[b & 15]; }
        lines[rp_line] = "rp_key = " + hex;
        std::ofstream out(path, std::ios::trunc);
        if (!out) { err = "could not save the generated rp_key to " + path; return false; }
        for (const auto& l : lines) out << l << "\n";
        out.close();
        if (!out) { err = "could not save the generated rp_key to " + path; return false; }
        cfg.rp_key_hex = hex;
    }
    return true;
}
