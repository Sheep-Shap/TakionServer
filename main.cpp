#include <iostream>
#include <random>
#include <cstring>
#include <csignal>
#include <thread>
#include <string>
#include <cstdio>
#include <cstdlib>

#include "takion_server.h"
#include "full_server.h"
#include "server_config.h"
#include "virtual_pad.h"
#include <timeapi.h>        // + линковка winmm.lib

static TakionServer* g_server = nullptr;

void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down...\n";
    if (g_server) g_server->request_stop();
}

static void stdin_watch() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "quit") break;
    }
    if (g_server) g_server->request_stop();   // "quit" или закрытый канал
}

static uint32_t random_pin8() {
    std::random_device rd;
    std::uniform_int_distribution<uint32_t> d(10000000, 99999999);
    return d(rd);
}

int main(int argc, char** argv) {
    bool control = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--gui-control") control = true;
    if (control) {
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        std::thread(stdin_watch).detach();
    }
    ServerConfig sc;
    std::string err;
    const std::string cfg_path = exe_directory() + "server.ini";
    if (!load_server_config(cfg_path, sc, err)) {
        std::cerr << "Config error (" << cfg_path << "): " << err << "\n";
        return 1;
    }
    
    bool random_pin = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--random-pin") == 0) random_pin = true;
    if (random_pin) sc.pin = random_pin8();

    std::cout << "Config: " << cfg_path << "\n";
    std::cout << "Registration PIN: " << sc.pin << "\n";

    ps::FullServer::Config cfg;
    cfg.port = sc.regist_port;
    cfg.pin = sc.pin;
    cfg.nickname = sc.nickname;
    parse_hex16(sc.rp_key_hex, cfg.rp_key);
    cfg.session_state_path = sc.session_state_file;
    cfg.log_ctrl_messages = sc.log_ctrl_messages;

    VirtualDS4::set_global_mode(sc.controller_type == "x360" ? PadMode::X360 : PadMode::DS4);

    ps::FullServer full;
    if (!full.start(cfg)) {
        std::cerr << "FullServer: port " << sc.regist_port << " is busy\n";
        return 1;
    }

    timeBeginPeriod(1);
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "Failed to initialize WinSock\n";
        timeEndPeriod(1);
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "Takion Host v1.0\n";
    std::cout << "========================\n\n";

    TakionServer server;
    g_server = &server;

    server.set_session_source([&full](std::vector<uint8_t>& nonce, std::vector<uint8_t>& rp_key) {
        ps::Key16 n, k;
        if (!full.get_session(n, k)) return false;
        nonce.assign(n.begin(), n.end());
        rp_key.assign(k.begin(), k.end());
        return true;
    });

    TakionServer::NetConfig nc;
    nc.adaptive = sc.adaptive_bitrate;
    nc.min_bps = (int64_t)(sc.bitrate_min_mbps * 1e6);
    nc.max_bps = (int64_t)(sc.bitrate_max_mbps * 1e6);
    nc.respect_client = sc.respect_client_bitrate;
    server.set_net_config(nc);

    set_pad_type(sc.controller_type);

    if (!server.start(sc.takion_port)) {
        std::cerr << "Failed to start Takion server\n";
        WSACleanup();
        timeEndPeriod(1);
        return 1;
    }

    std::cout << "Takion server started on UDP port " << sc.takion_port << "\n";
    std::cout << "Waiting for client connections...\n\n";

    server.run();

    server.stop();
    g_server = nullptr;
    WSACleanup();
    full.stop();
    timeEndPeriod(1);

    std::cout << "Server stopped.\n";
    if (control) std::_Exit(0);   // поток stdin висит на чтении, обычный выход мог бы зависнуть
    return 0;
}