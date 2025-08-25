#include "HttpClient.hpp"
#include "Logger.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <sstream>
#include <string>
#include <algorithm>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>

#pragma comment(lib, "winhttp.lib")

namespace StayPutVR {

bool HttpClient::initialized_ = false;
std::thread HttpClient::worker_thread_;
std::atomic<bool> HttpClient::worker_running_(false);
std::queue<std::function<void()>> HttpClient::request_queue_;
std::mutex HttpClient::queue_mutex_;

bool HttpClient::Initialize() {
    if (initialized_) {
        return true;
    }
    
    initialized_ = true;
    
    // Start the worker thread for async requests
    StartWorkerThread();
    
    return true;
}

void HttpClient::Shutdown() {
    if (!initialized_) {
        return;
    }
    
    // Stop the worker thread
    StopWorkerThread();
    
    initialized_ = false;
}

void HttpClient::StartWorkerThread() {
    if (worker_running_) {
        return; // Thread already running
    }
    
    // Set the flag
    worker_running_ = true;
    
    // Start the worker thread
    try {
        worker_thread_ = std::thread(WorkerThreadFunction);
        
        if (Logger::IsInitialized()) {
            Logger::Info("HttpClient: Worker thread started for async requests");
        }
    }
    catch (const std::exception& e) {
        worker_running_ = false;
        if (Logger::IsInitialized()) {
            Logger::Error("HttpClient: Failed to start worker thread: " + std::string(e.what()));
        }
    }
}

void HttpClient::StopWorkerThread() {
    if (!worker_running_) {
        return; // Thread not running
    }
    
    // Set the flag to stop
    worker_running_ = false;
    
    // Wait for the thread to finish
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    // Clear the queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!request_queue_.empty()) {
            request_queue_.pop();
        }
    }
    
    if (Logger::IsInitialized()) {
        Logger::Info("HttpClient: Worker thread stopped");
    }
}

void HttpClient::WorkerThreadFunction() {
    if (Logger::IsInitialized()) {
        Logger::Debug("HttpClient: Worker thread started");
    }
    
    while (worker_running_) {
        std::function<void()> request = nullptr;
        
        // Get a request from the queue
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (!request_queue_.empty()) {
                request = request_queue_.front();
                request_queue_.pop();
            }
        }
        
        // Process the request
        if (request) {
            try {
                request();
            }
            catch (const std::exception& e) {
                if (Logger::IsInitialized()) {
                    Logger::Error("HttpClient: Error in async request: " + std::string(e.what()));
                }
            }
            catch (...) {
                if (Logger::IsInitialized()) {
                    Logger::Error("HttpClient: Unknown error in async request");
                }
            }
        }
        else {
            // No requests, sleep for a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    if (Logger::IsInitialized()) {
        Logger::Debug("HttpClient: Worker thread stopped");
    }
}

void HttpClient::QueueAsyncRequest(std::function<void()> request) {
    if (!worker_running_) {
        // Worker not running, start it
        StartWorkerThread();
    }
    
    // Add the request to the queue
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        request_queue_.push(request);
    }
}

bool HttpClient::PostJson(
    const std::string& url,
    const nlohmann::json& requestBody,
    std::string& responseText,
    std::function<void(int progress)> progressCallback) {
    
    // Serialize JSON
    std::string body = requestBody.dump();
    
    // Set headers
    std::map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    
    // Send request
    return SendHttpRequest(url, "POST", headers, body, responseText, progressCallback);
}

// Convert a wide character string to a UTF-8 string
std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    
    // Calculate the required buffer size
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    
    // Create the output string of the required size
    std::string strTo(size_needed, 0);
    
    // Perform the conversion
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    
    return strTo;
}

// Convert a UTF-8 string to a wide character string
std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    
    // Calculate the required buffer size
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    
    // Create the output string of the required size
    std::wstring wstrTo(size_needed, 0);
    
    // Perform the conversion
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    
    return wstrTo;
}

bool HttpClient::SendHttpRequest(
    const std::string& url,
    const std::string& method,
    const std::map<std::string, std::string>& headers,
    const std::string& body,
    std::string& responseText,
    std::function<void(int progress)> progressCallback) {
    
    // Parse URL
    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    
    // Set required fields
    urlComp.dwSchemeLength = (DWORD)-1;
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;
    
    std::wstring wideUrl = Utf8ToWide(url);
    if (!WinHttpCrackUrl(wideUrl.c_str(), (DWORD)wideUrl.length(), 0, &urlComp)) {
        DWORD error = GetLastError();
        Logger::Error("Failed to parse URL: " + url + " Error: " + std::to_string(error));
        return false;
    }
    
    // Extract URL components
    std::wstring scheme(urlComp.lpszScheme, urlComp.dwSchemeLength);
    std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.dwExtraInfoLength > 0) {
        urlPath += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    
    // Open session
    HINTERNET hSession = WinHttpOpen(
        L"StayPutVR HTTP Client/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    
    if (!hSession) {
        DWORD error = GetLastError();
        Logger::Error("Failed to open HTTP session. Error: " + std::to_string(error));
        return false;
    }
    
    // Connect to server
    HINTERNET hConnect = WinHttpConnect(
        hSession,
        hostName.c_str(),
        urlComp.nPort,
        0
    );
    
    if (!hConnect) {
        DWORD error = GetLastError();
        Logger::Error("Failed to connect to server: " + WideToUtf8(hostName) + " Error: " + std::to_string(error));
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Create request
    DWORD flags = (scheme == L"https") ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        Utf8ToWide(method).c_str(),
        urlPath.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    
    if (!hRequest) {
        DWORD error = GetLastError();
        Logger::Error("Failed to open request. Error: " + std::to_string(error));
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Add headers
    for (const auto& header : headers) {
        std::wstring headerLine = Utf8ToWide(header.first + ": " + header.second);
        if (!WinHttpAddRequestHeaders(
            hRequest,
            headerLine.c_str(),
            (DWORD)headerLine.length(),
            WINHTTP_ADDREQ_FLAG_ADD)) {
            
            DWORD error = GetLastError();
            Logger::Warning("Failed to add header: " + header.first + " Error: " + std::to_string(error));
            // Continue anyway
        }
    }
    
    // Send request
    BOOL result = WinHttpSendRequest(
        hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        (LPVOID)body.c_str(),
        (DWORD)body.length(),
        (DWORD)body.length(),
        0
    );
    
    if (!result) {
        DWORD error = GetLastError();
        Logger::Error("Failed to send request. Error: " + std::to_string(error));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Receive response
    result = WinHttpReceiveResponse(hRequest, NULL);
    if (!result) {
        DWORD error = GetLastError();
        Logger::Error("Failed to receive response. Error: " + std::to_string(error));
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Get response status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX
    );
    
    // Read response data
    responseText.clear();
    DWORD bytesAvailable = 0;
    DWORD bytesRead = 0;
    char buffer[4096];
    
    do {
        bytesAvailable = 0;
        result = WinHttpQueryDataAvailable(hRequest, &bytesAvailable);
        
        if (!result || bytesAvailable == 0) {
            break;
        }
        
        ZeroMemory(buffer, sizeof(buffer));
        if (bytesAvailable > sizeof(buffer)) {
            bytesAvailable = sizeof(buffer);
        }
        
        result = WinHttpReadData(hRequest, buffer, bytesAvailable, &bytesRead);
        
        if (!result || bytesRead == 0) {
            break;
        }
        
        responseText.append(buffer, bytesRead);
        
        // Call progress callback if provided
        if (progressCallback) {
            progressCallback(0); // We don't have total size, so just indicate progress
        }
    } while (bytesAvailable > 0);
    
    // Clean up
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Check status code
    if (statusCode >= 200 && statusCode < 300) {
        return true;
    } else {
        Logger::Error("HTTP request failed with status code: " + std::to_string(statusCode) + 
                     " Response: " + responseText);
        return false;
    }
}

bool SendPiShockCommand(
    const std::string& username,
    const std::string& apiKey,
    const std::string& shareCode,
    int operation,
    int intensity,
    int duration,
    std::string& response) {
    
    if (!HttpClient::Initialize()) {
        Logger::Error("Failed to initialize HTTP client for PiShock");
        return false;
    }
    
    nlohmann::json requestBody;
    requestBody["Username"] = username;
    requestBody["Apikey"] = apiKey;
    requestBody["Code"] = shareCode;
    requestBody["Name"] = "StayPutVR";
    requestBody["Op"] = operation;
    
    // For shock and vibrate, also include intensity
    if (operation == 0 || operation == 1) {
        // Clamp intensity between 1 and 100
        intensity = (std::max)(1, (std::min)(100, intensity));
        requestBody["Intensity"] = std::to_string(intensity);
    }
    
    // Duration is required for all operations
    // Clamp duration between 1 and 15 seconds
    duration = (std::max)(1, (std::min)(15, duration));
    requestBody["Duration"] = std::to_string(duration);
    
    Logger::Info("Sending PiShock command. Operation: " + std::to_string(operation) + 
                 ", Intensity: " + std::to_string(intensity) + 
                 ", Duration: " + std::to_string(duration));
    
    bool success = HttpClient::PostJson(
        "https://do.pishock.com/api/apioperate",
        requestBody,
        response
    );
    
    if (success) {
        Logger::Info("PiShock command succeeded: " + response);
    } else {
        Logger::Error("PiShock command failed: " + response);
    }
    
    return success;
}

void SendPiShockCommandAsync(
    const std::string& username,
    const std::string& apiKey,
    const std::string& shareCode,
    int operation,
    int intensity,
    int duration,
    std::function<void(bool success, const std::string& response)> callback) {
    
    if (!HttpClient::Initialize()) {
        Logger::Error("Failed to initialize HTTP client for PiShock");
        if (callback) {
            callback(false, "Failed to initialize HTTP client");
        }
        return;
    }
    
    // Create a lambda that will make the request on a background thread
    auto request = [username, apiKey, shareCode, operation, intensity, duration, callback]() {
        std::string response;
        nlohmann::json requestBody;
        requestBody["Username"] = username;
        requestBody["Apikey"] = apiKey;
        requestBody["Code"] = shareCode;
        requestBody["Name"] = "StayPutVR";
        requestBody["Op"] = operation;
        
        // For shock and vibrate, also include intensity
        int clampedIntensity = intensity;
        if (operation == 0 || operation == 1) {
            // Clamp intensity between 1 and 100
            clampedIntensity = (std::max)(1, (std::min)(100, clampedIntensity));
            requestBody["Intensity"] = std::to_string(clampedIntensity);
        }
        
        // Duration is required for all operations
        // Clamp duration between 1 and 15 seconds
        int clampedDuration = (std::max)(1, (std::min)(15, duration));
        requestBody["Duration"] = std::to_string(clampedDuration);
        
        Logger::Info("Sending async PiShock command. Operation: " + std::to_string(operation) + 
                    ", Intensity: " + std::to_string(clampedIntensity) + 
                    ", Duration: " + std::to_string(clampedDuration));
        
        bool success = HttpClient::PostJson(
            "https://do.pishock.com/api/apioperate",
            requestBody,
            response
        );
        
        if (success) {
            Logger::Info("Async PiShock command succeeded: " + response);
        } else {
            Logger::Error("Async PiShock command failed: " + response);
        }
        
        // Call the callback if provided
        if (callback) {
            callback(success, response);
        }
    };
    
    // Add the request to the async queue
    HttpClient::QueueAsyncRequest(request);
}

bool SendOpenShockCommand(
    const std::string& serverUrl,
    const std::string& apiToken,
    const std::string& deviceId,
    int operation,
    int intensity,
    int duration,
    std::string& response) {
    
    if (!HttpClient::Initialize()) {
        Logger::Error("Failed to initialize HTTP client for OpenShock");
        return false;
    }
    
    // Create the JSON payload for OpenShock API
    nlohmann::json requestBody = nlohmann::json::array();
    
    nlohmann::json control_data;
    control_data["id"] = deviceId;
    
    // Convert operation integer to string type
    std::string type_string;
    switch (operation) {
        case 0: type_string = "Shock"; break;
        case 1: type_string = "Vibrate"; break;
        case 2: type_string = "Sound"; break;
        default: type_string = "Stop"; break;
    }
    control_data["type"] = type_string;
    
    // For sound, intensity is required but not meaningful, use 1 as minimum
    if (type_string == "Sound" && intensity == 0) {
        control_data["intensity"] = 1;
    } else {
        control_data["intensity"] = intensity;
    }
    
    control_data["duration"] = duration;
    control_data["exclusive"] = true;  // Stop other commands when this one starts
    
    requestBody.push_back(control_data);
    
    Logger::Info("Sending OpenShock command. Operation: " + std::to_string(operation) + 
                 ", Intensity: " + std::to_string(intensity) + 
                 ", Duration: " + std::to_string(duration) + "ms");
    
    // Prepare headers for OpenShock API
    std::map<std::string, std::string> headers;
    headers["Open-Shock-Token"] = apiToken;
    headers["User-Agent"] = "StayPutVR/1.1";
    headers["Content-Type"] = "application/json";
    
            bool success = HttpClient::SendHttpRequest(
            serverUrl + "/1/shockers/control",
            "POST",
            headers,
            requestBody.dump(),
            response
        );
    
    if (success) {
        Logger::Info("OpenShock command succeeded: " + response);
    } else {
        Logger::Error("OpenShock command failed: " + response);
    }
    
    return success;
}

void SendOpenShockCommandAsync(
    const std::string& serverUrl,
    const std::string& apiToken,
    const std::string& deviceId,
    int operation,
    int intensity,
    int duration,
    std::function<void(bool success, const std::string& response)> callback) {
    
    if (!HttpClient::Initialize()) {
        Logger::Error("Failed to initialize HTTP client for OpenShock");
        if (callback) {
            callback(false, "Failed to initialize HTTP client");
        }
        return;
    }
    
    // Create a lambda that will make the request on a background thread
    auto request = [serverUrl, apiToken, deviceId, operation, intensity, duration, callback]() {
        std::string response;
        
        // Create the JSON payload for OpenShock API
        nlohmann::json requestBody = nlohmann::json::array();
        
        nlohmann::json control_data;
        control_data["id"] = deviceId;
        
        // Convert operation integer to string type
        std::string type_string;
        switch (operation) {
            case 0: type_string = "Shock"; break;
            case 1: type_string = "Vibrate"; break;
            case 2: type_string = "Sound"; break;
            default: type_string = "Stop"; break;
        }
        control_data["type"] = type_string;
        
        // For sound, intensity is required but not meaningful, use 1 as minimum
        if (type_string == "Sound" && intensity == 0) {
            control_data["intensity"] = 1;
        } else {
            control_data["intensity"] = intensity;
        }
        
        control_data["duration"] = duration;
        control_data["exclusive"] = true;  // Stop other commands when this one starts
        
        requestBody.push_back(control_data);
        
        Logger::Info("Sending async OpenShock command. Operation: " + std::to_string(operation) + 
                    ", Intensity: " + std::to_string(intensity) + 
                    ", Duration: " + std::to_string(duration) + "ms");
        
        // Prepare headers for OpenShock API
        std::map<std::string, std::string> headers;
        headers["Open-Shock-Token"] = apiToken;
        headers["User-Agent"] = "StayPutVR/1.1";
        headers["Content-Type"] = "application/json";
        
        bool success = HttpClient::SendHttpRequest(
            serverUrl + "/1/shockers/control",
            "POST",
            headers,
            requestBody.dump(),
            response
        );
        
        if (success) {
            Logger::Info("OpenShock async command succeeded: " + response);
        } else {
            Logger::Error("OpenShock async command failed: " + response);
        }
        
        // Call the callback if provided
        if (callback) {
            callback(success, response);
        }
    };
    
    // Add the request to the async queue
    HttpClient::QueueAsyncRequest(request);
}

} // namespace StayPutVR 