#pragma once
// Server settings loaded from server.ini (next to the executable).
#include <array>
#include <cstdint>
#include <string>

struct ServerConfig {
    uint16_t regist_port = 9295;        // UDP discovery + TCP registration/session (FullServer)
    uint16_t takion_port = 9296;        // UDP Takion stream
    uint32_t pin = 12345678;            // 8-digit PIN typed on the phone while registering
    std::string nickname = "My-Fake-PS5";
    std::string rp_key_hex = "00112233445566778899aabbccddeeff";   // 32 hex chars, or "auto"
    std::string session_state_file;     // empty = do not write the legacy JSON bridge file
    std::string controller_type = "ds4";   // "ds4" или "x360"
    bool log_ctrl_messages = false;
    bool adaptive_bitrate = false;      // lower/raise the video bitrate from the client's loss reports
    bool respect_client_bitrate = true;   // не превышать битрейт, заявленный клиентом
    double bitrate_min_mbps = 6.0;      // floor for adaptive bitrate
    double bitrate_max_mbps = 20.0;     // start value and ceiling (the fixed bitrate when adaptive_bitrate = 0)
};

// Loads the config. If the file does not exist it is created with the defaults above.
// A value of rp_key = auto is replaced by a random key which is saved back into the file.
// Returns false and fills err on invalid values or when a generated key cannot be saved.
bool load_server_config(const std::string& path, ServerConfig& cfg, std::string& err);

// Parses exactly 32 hex characters.
bool parse_hex16(const std::string& hex, std::array<uint8_t, 16>& out);

// Directory of the running executable, with a trailing path separator ("" if unknown).
std::string exe_directory();
