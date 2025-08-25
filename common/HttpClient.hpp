#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <thread>
#include <queue>
#include <atomic>
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
    
    // Start the worker thread for async requests
    static void StartWorkerThread();
    
    // Stop the worker thread
    static void StopWorkerThread();
    
    // Add an async request to the queue
    static void QueueAsyncRequest(std::function<void()> request);
    
private:
    static bool initialized_;
    static std::thread worker_thread_;
    static std::atomic<bool> worker_running_;
    static std::queue<std::function<void()>> request_queue_;
    static std::mutex queue_mutex_;
    
    // Worker thread function
    static void WorkerThreadFunction();
};

// Synchronous utility function for PiShock API
bool SendPiShockCommand(
    const std::string& username,
    const std::string& apiKey,
    const std::string& shareCode,
    int operation,           // 0 = shock, 1 = vibrate, 2 = beep
    int intensity,           // 1-100 for shock/vibrate
    int duration,            // 1-15 seconds
    std::string& response
);

// Asynchronous utility function for PiShock API
void SendPiShockCommandAsync(
    const std::string& username,
    const std::string& apiKey,
    const std::string& shareCode,
    int operation,           // 0 = shock, 1 = vibrate, 2 = beep
    int intensity,           // 1-100 for shock/vibrate
    int duration,            // 1-15 seconds
    std::function<void(bool success, const std::string& response)> callback = nullptr
);

// Synchronous utility function for OpenShock API
bool SendOpenShockCommand(
    const std::string& serverUrl,
    const std::string& apiToken,
    const std::string& deviceId,
    int operation,           // 0 = shock, 1 = vibrate, 2 = sound
    int intensity,           // 1-100 for shock/vibrate
    int duration,            // Duration in milliseconds
    std::string& response
);

// Asynchronous utility function for OpenShock API
void SendOpenShockCommandAsync(
    const std::string& serverUrl,
    const std::string& apiToken,
    const std::string& deviceId,
    int operation,           // 0 = shock, 1 = vibrate, 2 = sound
    int intensity,           // 1-100 for shock/vibrate
    int duration,            // Duration in milliseconds
    std::function<void(bool success, const std::string& response)> callback = nullptr
);

} // namespace StayPutVR 