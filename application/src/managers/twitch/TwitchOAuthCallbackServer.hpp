#pragma once

#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

namespace StayPutVR {

// Lightweight WinSock HTTP server that listens on localhost:8080 for the
// Twitch OAuth redirect. When a valid authorization code arrives, it
// invokes the provided callback, sends a success page to the browser,
// and shuts itself down.
class TwitchOAuthCallbackServer {
public:
    // callback receives the authorization code string; returns true on success.
    using OAuthCodeCallback = std::function<bool(const std::string& code)>;

    TwitchOAuthCallbackServer() = default;
    ~TwitchOAuthCallbackServer();

    // Start listening. Non-blocking — spawns a background thread.
    void Start(OAuthCodeCallback on_code_received);

    // Stop the server and join the background thread (with timeout).
    void Stop();

    bool IsRunning() const { return running_; }

    // Returns the code received from the last successful callback (or empty).
    std::string GetReceivedCode() const;

private:
    void ServerLoop();

    OAuthCodeCallback on_code_received_;
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> thread_;

    mutable std::mutex code_mutex_;
    std::string received_code_;
};

} // namespace StayPutVR
