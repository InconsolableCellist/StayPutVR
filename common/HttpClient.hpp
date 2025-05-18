#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

namespace StayPutVR {

class HttpClient {
public:
    static bool Initialize();
    static void Shutdown();
    
    // Simple POST request with JSON body and response
    static bool PostJson(
        const std::string& url,
        const nlohmann::json& requestBody,
        std::string& responseText,
        std::function<void(int progress)> progressCallback = nullptr
    );
    
    // Simple synchronous HTTP request with the WinHTTP API
    static bool SendHttpRequest(
        const std::string& url,
        const std::string& method,
        const std::map<std::string, std::string>& headers,
        const std::string& body,
        std::string& responseText,
        std::function<void(int progress)> progressCallback = nullptr
    );
    
private:
    static bool initialized_;
};

// Utility function for PiShock API
bool SendPiShockCommand(
    const std::string& username,
    const std::string& apiKey,
    const std::string& shareCode,
    int operation,           // 0 = shock, 1 = vibrate, 2 = beep
    int intensity,           // 1-100 for shock/vibrate
    int duration,            // 1-15 seconds
    std::string& response
);

} // namespace StayPutVR 