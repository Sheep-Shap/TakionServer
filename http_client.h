#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>

#pragma comment(lib, "winhttp.lib")

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // GET request
    std::string get(const std::string& url, const std::map<std::string, std::string>& headers = {});
    
    // POST request
    std::string post(const std::string& url, const std::string& body, 
                     const std::map<std::string, std::string>& headers = {});

    // Get last error
    std::string get_last_error() const { return last_error_; }

private:
    HINTERNET h_session_ = nullptr;
    std::string last_error_;
};