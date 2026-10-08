#include "http_client.h"
#include <iostream>
#include <sstream>

HttpClient::HttpClient() {
    h_session_ = WinHttpOpen(L"PS Takion Server/1.0", 
                             WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                             WINHTTP_NO_PROXY_NAME, 
                             WINHTTP_NO_PROXY_BYPASS, 
                             0);
    
    if (!h_session_) {
        last_error_ = "Failed to open WinHTTP session";
    }
}

HttpClient::~HttpClient() {
    if (h_session_) {
        WinHttpCloseHandle(h_session_);
    }
}

std::string HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) {
    if (!h_session_) {
        return "";
    }
    
    // Parse URL
    URL_COMPONENTS url_comp = {};
    url_comp.dwStructSize = sizeof(url_comp);
    url_comp.dwSchemeLength = -1;
    url_comp.dwHostNameLength = -1;
    url_comp.dwUrlPathLength = -1;
    
    std::wstring wide_url(url.begin(), url.end());
    
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &url_comp)) {
        last_error_ = "Failed to parse URL";
        return "";
    }
    
    // Connect to host
    std::wstring host(url_comp.lpszHostName, url_comp.dwHostNameLength);
    HINTERNET h_connect = WinHttpConnect(h_session_, host.c_str(), 
                                         url_comp.nPort, 0);
    
    if (!h_connect) {
        last_error_ = "Failed to connect to host";
        return "";
    }
    
    // Open request
    std::wstring path(url_comp.lpszUrlPath, url_comp.dwUrlPathLength);
    HINTERNET h_request = WinHttpOpenRequest(h_connect, L"GET", path.c_str(), 
                                              nullptr, WINHTTP_NO_REFERER, 
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                              WINHTTP_FLAG_SECURE);
    
    if (!h_request) {
        WinHttpCloseHandle(h_connect);
        last_error_ = "Failed to open request";
        return "";
    }
    
    // Send request
    if (!WinHttpSendRequest(h_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                            nullptr, 0, 0, 0)) {
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        last_error_ = "Failed to send request";
        return "";
    }
    
    // Receive response
    if (!WinHttpReceiveResponse(h_request, nullptr)) {
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        last_error_ = "Failed to receive response";
        return "";
    }
    
    // Read response body
    std::string response;
    DWORD bytes_read = 0;
    
    while (true) {
        if (!WinHttpQueryDataAvailable(h_request, &bytes_read)) {
            break;
        }
        
        if (bytes_read == 0) {
            break;
        }
        
        std::vector<char> buffer(bytes_read + 1);
        if (WinHttpReadData(h_request, buffer.data(), bytes_read, &bytes_read)) {
            response.append(buffer.data(), bytes_read);
        } else {
            break;
        }
    }
    
    WinHttpCloseHandle(h_request);
    WinHttpCloseHandle(h_connect);
    
    return response;
}

std::string HttpClient::post(const std::string& url, const std::string& body, 
                              const std::map<std::string, std::string>& headers) {
    if (!h_session_) {
        return "";
    }
    
    // Parse URL
    URL_COMPONENTS url_comp = {};
    url_comp.dwStructSize = sizeof(url_comp);
    url_comp.dwSchemeLength = -1;
    url_comp.dwHostNameLength = -1;
    url_comp.dwUrlPathLength = -1;
    
    std::wstring wide_url(url.begin(), url.end());
    
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &url_comp)) {
        last_error_ = "Failed to parse URL";
        return "";
    }
    
    // Connect to host
    std::wstring host(url_comp.lpszHostName, url_comp.dwHostNameLength);
    HINTERNET h_connect = WinHttpConnect(h_session_, host.c_str(), 
                                         url_comp.nPort, 0);
    
    if (!h_connect) {
        last_error_ = "Failed to connect to host";
        return "";
    }
    
    // Open request
    std::wstring path(url_comp.lpszUrlPath, url_comp.dwUrlPathLength);
    HINTERNET h_request = WinHttpOpenRequest(h_connect, L"POST", path.c_str(), 
                                              nullptr, WINHTTP_NO_REFERER, 
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, 
                                              WINHTTP_FLAG_SECURE);
    
    if (!h_request) {
        WinHttpCloseHandle(h_connect);
        last_error_ = "Failed to open request";
        return "";
    }
    
    // Build headers
    std::wstring additional_headers;
    for (const auto& [key, value] : headers) {
        std::wstring wkey(key.begin(), key.end());
        std::wstring wvalue(value.begin(), value.end());
        additional_headers += wkey + L": " + wvalue + L"\r\n";
    }
    
    // Send request
    std::wstring wide_body(body.begin(), body.end());
    if (!WinHttpSendRequest(h_request, additional_headers.c_str(), -1, 
                            (LPVOID)wide_body.c_str(), (DWORD)wide_body.size() * sizeof(wchar_t), 
                            (DWORD)wide_body.size(), 0)) {
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        last_error_ = "Failed to send request";
        return "";
    }
    
    // Receive response
    if (!WinHttpReceiveResponse(h_request, nullptr)) {
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        last_error_ = "Failed to receive response";
        return "";
    }
    
    // Read response body
    std::string response;
    DWORD bytes_read = 0;
    
    while (true) {
        if (!WinHttpQueryDataAvailable(h_request, &bytes_read)) {
            break;
        }
        
        if (bytes_read == 0) {
            break;
        }
        
        std::vector<char> buffer(bytes_read + 1);
        if (WinHttpReadData(h_request, buffer.data(), bytes_read, &bytes_read)) {
            response.append(buffer.data(), bytes_read);
        } else {
            break;
        }
    }
    
    WinHttpCloseHandle(h_request);
    WinHttpCloseHandle(h_connect);
    
    return response;
}