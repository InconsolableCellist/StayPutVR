#include "HttpClient.hpp"
#include "Logger.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <sstream>
#include <string>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

namespace StayPutVR {

bool HttpClient::initialized_ = false;

bool HttpClient::Initialize() {
    if (initialized_) {
        return true;
    }
    
    initialized_ = true;
    return true;
}

void HttpClient::Shutdown() {
    if (!initialized_) {
        return;
    }
    
    initialized_ = false;
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

} // namespace StayPutVR 