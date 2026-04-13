#include "TwitchOAuthCallbackServer.hpp"
#include "../../../../common/Logger.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <chrono>

namespace StayPutVR {

TwitchOAuthCallbackServer::~TwitchOAuthCallbackServer() {
    Stop();
}

void TwitchOAuthCallbackServer::Start(OAuthCodeCallback on_code_received) {
    if (running_) {
        Logger::Info("OAuth server already running");
        return;
    }

    on_code_received_ = std::move(on_code_received);
    running_ = true;
    {
        std::lock_guard<std::mutex> lk(code_mutex_);
        received_code_.clear();
    }

    thread_ = std::make_unique<std::thread>(&TwitchOAuthCallbackServer::ServerLoop, this);
}

void TwitchOAuthCallbackServer::Stop() {
    if (!running_) return;

    Logger::Info("Stopping OAuth callback server");
    running_ = false;

    // Poke the listening socket to unblock accept()
    try {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
            if (s != INVALID_SOCKET) {
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                addr.sin_port = htons(8080);
                connect(s, (sockaddr*)&addr, sizeof(addr));
                closesocket(s);
            }
            WSACleanup();
        }
    } catch (...) {}

    if (thread_ && thread_->joinable()) {
        auto t0 = std::chrono::steady_clock::now();
        while (running_ &&
               std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        thread_->join();
    }
    thread_.reset();

    Logger::Info("OAuth callback server stopped successfully");
}

std::string TwitchOAuthCallbackServer::GetReceivedCode() const {
    std::lock_guard<std::mutex> lk(code_mutex_);
    return received_code_;
}

void TwitchOAuthCallbackServer::ServerLoop() {
    Logger::Info("Starting OAuth callback server on port 8080");

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Logger::Error("WSAStartup failed for OAuth server");
        running_ = false;
        return;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        Logger::Error("Failed to create OAuth server socket");
        WSACleanup();
        running_ = false;
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR ||
        listen(serverSocket, 1) == SOCKET_ERROR) {
        Logger::Error("Failed to bind/listen OAuth server socket");
        closesocket(serverSocket);
        WSACleanup();
        running_ = false;
        return;
    }

    Logger::Info("OAuth callback server listening on port 8080");

    while (running_) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverSocket, &readfds);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int result = select(0, &readfds, nullptr, nullptr, &timeout);
        if (result == SOCKET_ERROR || !running_) break;
        if (result == 0) continue;

        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET || !running_) {
            if (clientSocket != INVALID_SOCKET) closesocket(clientSocket);
            break;
        }

        char buffer[4096];
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            std::string request(buffer);

            size_t codePos = request.find("code=");
            if (codePos != std::string::npos) {
                codePos += 5;
                size_t codeEnd = request.find('&', codePos);
                if (codeEnd == std::string::npos)
                    codeEnd = request.find(' ', codePos);
                if (codeEnd == std::string::npos)
                    codeEnd = request.length();

                std::string code = request.substr(codePos, codeEnd - codePos);

                {
                    std::lock_guard<std::mutex> lk(code_mutex_);
                    received_code_ = code;
                }

                // Send success page
                std::string response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "<html><body>"
                    "<h1>Authorization Successful!</h1>"
                    "<p>You can close this window and return to StayPutVR.</p>"
                    "<p>The application will automatically connect to Twitch.</p>"
                    "</body></html>";

                send(clientSocket, response.c_str(), (int)response.length(), 0);
                Logger::Info("OAuth authorization code received successfully");

                // Invoke callback
                if (on_code_received_) {
                    if (on_code_received_(code)) {
                        Logger::Info("Twitch OAuth setup completed successfully!");
                    } else {
                        Logger::Error("Failed to complete OAuth setup");
                    }
                }

                closesocket(clientSocket);
                break;
            } else {
                std::string response =
                    "HTTP/1.1 400 Bad Request\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "<html><body>"
                    "<h1>Authorization Failed</h1>"
                    "<p>No authorization code found in the request.</p>"
                    "</body></html>";

                send(clientSocket, response.c_str(), (int)response.length(), 0);
            }
        }

        closesocket(clientSocket);
    }

    shutdown(serverSocket, SD_BOTH);
    closesocket(serverSocket);
    WSACleanup();
    running_ = false;

    Logger::Info("OAuth callback server thread finished");
}

} // namespace StayPutVR
