#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <mutex>

#include "../../../common/Config.hpp"
#include "../../../common/Logger.hpp"
#include "../../../common/HttpClient.hpp"

namespace StayPutVR {

    // Forward declarations
    class UIManager;

    // Callback types for Twitch events
    using TwitchDonationCallback = std::function<void(const std::string& username, float amount, const std::string& message)>;
    using TwitchBitsCallback = std::function<void(const std::string& username, int bits, const std::string& message)>;
    using TwitchSubscriptionCallback = std::function<void(const std::string& username, int months, bool is_gift)>;
    using TwitchChatCommandCallback = std::function<void(const std::string& username, const std::string& command, const std::string& args)>;

    struct TwitchEventData {
        std::string event_type;
        std::string username;
        float amount = 0.0f;
        int bits = 0;
        int months = 0;
        bool is_gift = false;
        std::string message;
        std::string command;
        std::string args;
    };

    class TwitchManager {
    public:
        TwitchManager();
        ~TwitchManager();

        // Initialization and cleanup
        bool Initialize(Config* config);
        void Shutdown();
        void Update(); // Called from main update loop

        // Connection management
        bool ConnectToTwitch();
        void DisconnectFromTwitch();
        bool IsConnected() const { return connected_; }
        bool ConnectToChatDelayed();
        
        // OAuth authentication
        std::string GenerateOAuthURL();
        bool HandleOAuthCallback(const std::string& code);
        bool HandleOAuthCallbackURL(const std::string& callback_url);
        bool RefreshAccessToken();
        
        // Chat functionality
        bool SendChatMessage(const std::string& message);
        bool ConnectToChat();
        void DisconnectFromChat();
        
        // EventSub functionality
        bool SetupEventSubscriptions();
        void ProcessEventSubMessage(const std::string& message);
        
        // Event callbacks
        void SetDonationCallback(TwitchDonationCallback callback) { donation_callback_ = callback; }
        void SetBitsCallback(TwitchBitsCallback callback) { bits_callback_ = callback; }
        void SetSubscriptionCallback(TwitchSubscriptionCallback callback) { subscription_callback_ = callback; }
        void SetChatCommandCallback(TwitchChatCommandCallback callback) { chat_command_callback_ = callback; }
        
        // Utility functions
        bool ValidateConfiguration() const;
        std::string GetConnectionStatus() const;
        std::string GetLastError() const { return last_error_; }
        
        // Test functions
        void TestChatMessage();
        void TestDonationEvent(const std::string& username, float amount);
        void TestOAuthFlow();
        void StartOAuthServer();
        void StopOAuthServer();
        
        // Chat command parsing
        bool ParseChatCommand(const std::string& message, std::string& command, std::string& args);
        void ProcessIRCMessage(const std::string& irc_message);
        void HandleLockCommand(const std::string& username, const std::string& args);
        void HandleUnlockCommand(const std::string& username, const std::string& args);
        void HandleStatusCommand(const std::string& username, const std::string& args);
        
    private:
        // Configuration
        Config* config_;
        
        // Connection state
        std::atomic<bool> connected_;
        std::atomic<bool> chat_connected_;
        std::atomic<bool> eventsub_connected_;
        
        // Authentication tokens
        std::string access_token_;
        std::string refresh_token_;
        std::chrono::steady_clock::time_point token_expiry_;
        
        // WebSocket connections
        std::unique_ptr<std::thread> eventsub_thread_;
        std::unique_ptr<std::thread> chat_thread_;
        std::atomic<bool> should_stop_threads_;
        
        // Event callbacks
        TwitchDonationCallback donation_callback_;
        TwitchBitsCallback bits_callback_;
        TwitchSubscriptionCallback subscription_callback_;
        TwitchChatCommandCallback chat_command_callback_;
        
        // Error handling
        std::string last_error_;
        std::mutex error_mutex_;
        
        // Rate limiting
        std::chrono::steady_clock::time_point last_chat_message_;
        std::chrono::steady_clock::time_point last_api_call_;
        
        // WebSocket functionality (placeholder implementations)
        bool ConnectWebSocket(const std::string& url);
        void DisconnectWebSocket();
        bool SendWebSocketMessage(const std::string& message);
        std::string ReceiveWebSocketMessage();
        
        // OAuth callback server
        std::atomic<bool> oauth_server_running_;
        std::unique_ptr<std::thread> oauth_server_thread_;
        std::string received_oauth_code_;
        std::mutex oauth_code_mutex_;
        
        // Internal methods
        void SetError(const std::string& error);
        bool IsTokenValid() const;
        bool ValidateAccessToken();
        bool ValidateTokenScopes();
        void EventSubWorker();
        void ChatWorker();
        void ProcessChatMessage(const std::string& raw_message);
        void ProcessEventSubEvent(const TwitchEventData& event);
        
        // HTTP helpers
        bool MakeAPIRequest(const std::string& endpoint, const std::string& method, 
                           const std::string& body, std::string& response);
        bool MakeOAuthRequest(const std::string& endpoint, const std::string& method, 
                             const std::string& body, std::string& response);
        std::string BuildAuthHeader() const;
        std::string UrlEncode(const std::string& value);
        bool GetBroadcasterUserId(const std::string& channel_name, std::string& user_id);
        
        // EventSub subscription management
        bool SubscribeToEvent(const std::string& event_type, const std::string& condition);
        bool UnsubscribeFromEvent(const std::string& subscription_id);
        
        // JSON parsing helpers
        bool ParseDonationEvent(const std::string& json, TwitchEventData& event);
        bool ParseBitsEvent(const std::string& json, TwitchEventData& event);
        bool ParseSubscriptionEvent(const std::string& json, TwitchEventData& event);
    };

} // namespace StayPutVR 