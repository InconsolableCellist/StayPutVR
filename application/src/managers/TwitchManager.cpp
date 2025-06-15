#include "TwitchManager.hpp"
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace StayPutVR {

    TwitchManager::TwitchManager() 
        : config_(nullptr)
        , connected_(false)
        , chat_connected_(false)
        , eventsub_connected_(false)
        , should_stop_threads_(false)
        , last_chat_message_(std::chrono::steady_clock::now())
        , last_api_call_(std::chrono::steady_clock::now())
        , oauth_server_running_(false)
    {
    }

    TwitchManager::~TwitchManager() {
        Shutdown();
    }

    bool TwitchManager::Initialize(Config* config) {
        if (!config) {
            SetError("Configuration is null");
            return false;
        }

        config_ = config;

        if (Logger::IsInitialized()) {
            Logger::Info("TwitchManager initialized");
        }

        return true;
    }

    void TwitchManager::Shutdown() {
        if (Logger::IsInitialized()) {
            Logger::Info("TwitchManager shutting down");
        }

        DisconnectFromTwitch();
        
        // Stop OAuth server if running
        StopOAuthServer();
        
        // Stop all threads
        should_stop_threads_ = true;
        
        if (eventsub_thread_ && eventsub_thread_->joinable()) {
            eventsub_thread_->join();
        }
        
        if (chat_thread_ && chat_thread_->joinable()) {
            chat_thread_->join();
        }

        eventsub_thread_.reset();
        chat_thread_.reset();
    }

    void TwitchManager::Update() {
        // Check if tokens need refreshing
        if (connected_ && !IsTokenValid()) {
            if (Logger::IsInitialized()) {
                Logger::Info("Access token expired, attempting refresh");
            }
            
            if (!RefreshAccessToken()) {
                SetError("Failed to refresh access token");
                DisconnectFromTwitch();
                return;
            }
            
            // After successful token refresh, try to connect to chat if not already connected
            if (Logger::IsInitialized()) {
                Logger::Info("Token refreshed successfully, attempting delayed chat connection");
            }
            ConnectToChatDelayed();
        }

        // If we have a valid token but chat isn't connected, try to connect
        if (connected_ && IsTokenValid() && !chat_connected_ && config_->twitch_chat_enabled) {
            static auto last_chat_attempt = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_chat_attempt).count();
            
            // Try to connect to chat every 30 seconds if it's not connected
            if (elapsed >= 30) {
                if (Logger::IsInitialized()) {
                    Logger::Info("Attempting periodic chat connection with valid token");
                }
                ConnectToChatDelayed();
                last_chat_attempt = now;
            }
        }

        // Process any pending events or maintenance tasks
        // This is called from the main UI thread, so keep it lightweight
    }

    bool TwitchManager::ConnectToTwitch() {
        if (!ValidateConfiguration()) {
            SetError("Invalid Twitch configuration");
            return false;
        }

        if (connected_) {
            if (Logger::IsInitialized()) {
                Logger::Info("Already connected to Twitch");
            }
            return true;
        }

        if (Logger::IsInitialized()) {
            Logger::Info("Connecting to Twitch...");
        }

        // Step 1: Check if we have stored tokens
        if (!config_->twitch_access_token.empty()) {
            access_token_ = config_->twitch_access_token;
            refresh_token_ = config_->twitch_refresh_token;
            
            if (Logger::IsInitialized()) {
                Logger::Info("Found stored access token, validating...");
            }
            
            // Test the token by making a simple API call
            if (ValidateAccessToken()) {
                if (Logger::IsInitialized()) {
                    Logger::Info("Stored access token is valid");
                }
            } else {
                if (Logger::IsInitialized()) {
                    Logger::Info("Stored access token is invalid, attempting refresh...");
                }
                
                // Try to refresh the token
                if (!RefreshAccessToken()) {
                    if (Logger::IsInitialized()) {
                        Logger::Warning("Token refresh failed, OAuth flow required");
                    }
                    SetError("Access token expired and refresh failed. Please re-authorize the application.");
                    return false;
                }
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Info("No stored access token found, OAuth flow required");
            }
            SetError("No access token available. Please authorize the application first.");
            return false;
        }

        // Step 2: If we get here, we have a valid access token
        connected_ = true;
        
        // Step 3: Set up additional connections based on config
        bool overall_success = true;
        
        // Set up EventSub subscriptions if enabled
        if (config_->twitch_enabled) {
            if (!SetupEventSubscriptions()) {
                if (Logger::IsInitialized()) {
                    Logger::Warning("Failed to set up EventSub subscriptions, but continuing...");
                }
                // Don't fail completely, EventSub might not be critical
            }
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("Successfully connected to Twitch API");
        }

        return true;
    }

    bool TwitchManager::ConnectToChatDelayed() {
        // This method is called after token validation is complete
        if (config_->twitch_chat_enabled && connected_ && !chat_connected_) {
            if (Logger::IsInitialized()) {
                Logger::Info("Starting delayed chat connection with validated token");
            }
            
            if (!ConnectToChat()) {
                if (Logger::IsInitialized()) {
                    Logger::Warning("Failed to connect to chat");
                }
                return false;
            }
        }
        return true;
    }

    void TwitchManager::DisconnectFromTwitch() {
        if (!connected_) {
            return;
        }

        if (Logger::IsInitialized()) {
            Logger::Info("Disconnecting from Twitch");
        }

        // Disconnect from chat
        DisconnectFromChat();
        
        // Stop EventSub
        eventsub_connected_ = false;
        
        // Clear tokens
        access_token_.clear();
        refresh_token_.clear();
        
        connected_ = false;

        if (Logger::IsInitialized()) {
            Logger::Info("Disconnected from Twitch");
        }
    }

    std::string TwitchManager::GenerateOAuthURL() {
        if (!config_) {
            return "";
        }

        // Generate a random state parameter for security
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        
        std::string state;
        for (int i = 0; i < 32; ++i) {
            state += "0123456789abcdef"[dis(gen)];
        }

        // Required scopes for our integration (IRC chat requires legacy chat scopes)
        std::string scopes = "chat:read+chat:edit+user:read:chat+user:write:chat+channel:read:subscriptions+bits:read+channel:read:redemptions";
        
        std::stringstream url;
        url << "https://id.twitch.tv/oauth2/authorize"
            << "?client_id=" << config_->twitch_client_id
            << "&redirect_uri=http://localhost:8080/auth/twitch/callback"
            << "&response_type=code"
            << "&scope=" << scopes
            << "&state=" << state;

        return url.str();
    }

    bool TwitchManager::HandleOAuthCallback(const std::string& code) {
        if (!config_ || code.empty()) {
            SetError("Invalid OAuth callback parameters");
            return false;
        }

        // Exchange authorization code for access token
        // Build form-encoded request body (OAuth requires form encoding, not JSON)
        std::string request_body = 
            "client_id=" + UrlEncode(config_->twitch_client_id) +
            "&client_secret=" + UrlEncode(config_->twitch_client_secret) +
            "&code=" + UrlEncode(code) +
            "&grant_type=authorization_code" +
            "&redirect_uri=" + UrlEncode("http://localhost:8080/auth/twitch/callback");

        std::string response;
        if (!MakeOAuthRequest("https://id.twitch.tv/oauth2/token", "POST", request_body, response)) {
            SetError("Failed to exchange OAuth code for token");
            return false;
        }

        try {
            nlohmann::json response_json = nlohmann::json::parse(response);
            
            if (response_json.contains("access_token")) {
                access_token_ = response_json["access_token"];
                refresh_token_ = response_json.value("refresh_token", "");
                
                int expires_in = response_json.value("expires_in", 3600);
                token_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(expires_in);
                
                // Store tokens in config for persistence
                config_->twitch_access_token = access_token_;
                config_->twitch_refresh_token = refresh_token_;
                
                if (Logger::IsInitialized()) {
                    Logger::Info("Successfully obtained Twitch access token");
                }
                
                return true;
            } else {
                SetError("No access token in OAuth response");
                return false;
            }
        } catch (const std::exception& e) {
            SetError("Failed to parse OAuth response: " + std::string(e.what()));
            return false;
        }
    }

    bool TwitchManager::HandleOAuthCallbackURL(const std::string& callback_url) {
        if (callback_url.empty()) {
            SetError("Empty callback URL");
            return false;
        }

        // Extract the 'code' parameter from the callback URL
        // URL format: http://localhost:8080/auth/twitch/callback?code=XXXXXX&state=XXXXXX
        size_t code_pos = callback_url.find("code=");
        if (code_pos == std::string::npos) {
            SetError("No 'code' parameter found in callback URL");
            return false;
        }

        // Move past "code="
        code_pos += 5;
        
        // Find the end of the code (either & or end of string)
        size_t code_end = callback_url.find('&', code_pos);
        if (code_end == std::string::npos) {
            code_end = callback_url.length();
        }

        std::string code = callback_url.substr(code_pos, code_end - code_pos);
        
        if (code.empty()) {
            SetError("Empty authorization code in callback URL");
            return false;
        }

        if (Logger::IsInitialized()) {
            Logger::Info("Extracted authorization code from callback URL");
        }

        // Use the existing HandleOAuthCallback method
        return HandleOAuthCallback(code);
    }

    bool TwitchManager::RefreshAccessToken() {
        if (!config_ || config_->twitch_refresh_token.empty()) {
            SetError("No refresh token available");
            return false;
        }

        // Build form-encoded request body
        std::string request_body = 
            "client_id=" + UrlEncode(config_->twitch_client_id) +
            "&client_secret=" + UrlEncode(config_->twitch_client_secret) +
            "&refresh_token=" + UrlEncode(config_->twitch_refresh_token) +
            "&grant_type=refresh_token";

        std::string response;
        if (!MakeOAuthRequest("https://id.twitch.tv/oauth2/token", "POST", request_body, response)) {
            SetError("Failed to refresh access token");
            return false;
        }

        try {
            nlohmann::json response_json = nlohmann::json::parse(response);
            
            if (response_json.contains("access_token")) {
                access_token_ = response_json["access_token"];
                refresh_token_ = response_json.value("refresh_token", refresh_token_); // Keep old if not provided
                
                int expires_in = response_json.value("expires_in", 3600);
                token_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(expires_in);
                
                // Update config
                config_->twitch_access_token = access_token_;
                config_->twitch_refresh_token = refresh_token_;
                
                if (Logger::IsInitialized()) {
                    Logger::Info("Successfully refreshed Twitch access token");
                }
                
                return true;
            } else {
                SetError("No access token in refresh response");
                return false;
            }
        } catch (const std::exception& e) {
            SetError("Failed to parse refresh response: " + std::string(e.what()));
            return false;
        }
    }

    bool TwitchManager::SendChatMessage(const std::string& message) {
        if (!connected_ || message.empty()) {
            return false;
        }

        // Rate limiting - Twitch allows 20 messages per 30 seconds for regular users
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_chat_message_).count();
        if (elapsed < 2) { // Minimum 2 seconds between messages
            if (Logger::IsInitialized()) {
                Logger::Debug("Chat message rate limited");
            }
            return false;
        }

        last_chat_message_ = now;

        if (Logger::IsInitialized()) {
            Logger::Info("Sending Twitch chat message: " + message);
        }

        // Use Twitch Chat API instead of IRC for simplicity
        // First, get the broadcaster user ID
        std::string broadcaster_id;
        if (!GetBroadcasterUserId(config_->twitch_channel_name, broadcaster_id)) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to get broadcaster user ID for chat message");
            }
            return false;
        }

        // Prepare the request body
        nlohmann::json request_body;
        request_body["broadcaster_id"] = broadcaster_id;
        request_body["sender_id"] = broadcaster_id; // Bot sends as the broadcaster
        request_body["message"] = message;

        std::string response;
        bool success = MakeAPIRequest("https://api.twitch.tv/helix/chat/messages", "POST", 
                                    request_body.dump(), response);

        if (success) {
            if (Logger::IsInitialized()) {
                Logger::Info("✅ Chat message sent successfully");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("❌ Failed to send chat message");
            }
        }

        return success;
    }

    bool TwitchManager::ConnectToChat() {
        if (!connected_ || chat_connected_) {
            return chat_connected_;
        }

        if (Logger::IsInitialized()) {
            Logger::Info("Connecting to Twitch chat via IRC");
        }

        // Retry logic for token refresh issues
        int max_retries = 2;
        for (int retry = 0; retry < max_retries; retry++) {
            if (retry > 0) {
                if (Logger::IsInitialized()) {
                    Logger::Info("Retrying IRC connection (attempt " + std::to_string(retry + 1) + ")");
                }
                // Wait a bit before retry to allow token refresh
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }

            // Start chat worker thread which will handle IRC connection
            should_stop_threads_ = false;
            chat_thread_ = std::make_unique<std::thread>(&TwitchManager::ChatWorker, this);

            // Wait a moment to see if connection succeeds
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            
            if (chat_connected_) {
                if (Logger::IsInitialized()) {
                    Logger::Info("IRC connection successful");
                }
                return true;
            } else {
                // Connection failed, stop the thread and try again
                should_stop_threads_ = true;
                if (chat_thread_ && chat_thread_->joinable()) {
                    chat_thread_->join();
                }
                chat_thread_.reset();
                
                if (retry < max_retries - 1) {
                    if (Logger::IsInitialized()) {
                        Logger::Warning("IRC connection failed, will retry...");
                    }
                }
            }
        }

        if (Logger::IsInitialized()) {
            Logger::Error("Failed to establish IRC connection after " + std::to_string(max_retries) + " attempts");
        }
        return false;
    }

    void TwitchManager::DisconnectFromChat() {
        if (!chat_connected_) {
            return;
        }

        if (Logger::IsInitialized()) {
            Logger::Info("Disconnecting from Twitch chat");
        }

        chat_connected_ = false;
        
        // Chat worker thread will stop when should_stop_threads_ is set
    }

    bool TwitchManager::SetupEventSubscriptions() {
        if (!connected_) {
            SetError("Not connected to Twitch");
            return false;
        }

        if (Logger::IsInitialized()) {
            Logger::Info("Setting up Twitch EventSub subscriptions");
        }

        bool success = true;

        // Subscribe to bits events if enabled
        if (config_->twitch_bits_enabled) {
            std::string condition = R"({"broadcaster_user_id":")" + config_->twitch_channel_name + R"("})";
            if (!SubscribeToEvent("channel.cheer", condition)) {
                success = false;
            }
        }

        // Subscribe to subscription events if enabled
        if (config_->twitch_subs_enabled) {
            std::string condition = R"({"broadcaster_user_id":")" + config_->twitch_channel_name + R"("})";
            if (!SubscribeToEvent("channel.subscribe", condition)) {
                success = false;
            }
            if (!SubscribeToEvent("channel.subscription.gift", condition)) {
                success = false;
            }
        }

        // Subscribe to donation events if enabled (via channel points or other mechanisms)
        if (config_->twitch_donations_enabled) {
            std::string condition = R"({"broadcaster_user_id":")" + config_->twitch_channel_name + R"("})";
            if (!SubscribeToEvent("channel.channel_points_custom_reward_redemption.add", condition)) {
                success = false;
            }
        }

        if (success) {
            eventsub_connected_ = true;
            
            // Start EventSub worker thread
            should_stop_threads_ = false;
            eventsub_thread_ = std::make_unique<std::thread>(&TwitchManager::EventSubWorker, this);
        }

        return success;
    }

    void TwitchManager::ProcessEventSubMessage(const std::string& message) {
        try {
            nlohmann::json json_message = nlohmann::json::parse(message);
            
            if (!json_message.contains("metadata") || !json_message.contains("payload")) {
                return;
            }

            std::string message_type = json_message["metadata"].value("message_type", "");
            
            if (message_type == "notification") {
                std::string subscription_type = json_message["metadata"].value("subscription_type", "");
                
                TwitchEventData event;
                event.event_type = subscription_type;
                
                if (subscription_type == "channel.cheer" && ParseBitsEvent(message, event)) {
                    ProcessEventSubEvent(event);
                } else if (subscription_type == "channel.subscribe" && ParseSubscriptionEvent(message, event)) {
                    ProcessEventSubEvent(event);
                } else if (subscription_type == "channel.subscription.gift" && ParseSubscriptionEvent(message, event)) {
                    ProcessEventSubEvent(event);
                } else if (subscription_type == "channel.channel_points_custom_reward_redemption.add" && ParseDonationEvent(message, event)) {
                    ProcessEventSubEvent(event);
                }
            }
        } catch (const std::exception& e) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to process EventSub message: " + std::string(e.what()));
            }
        }
    }

    bool TwitchManager::ValidateConfiguration() const {
        if (!config_) {
            return false;
        }

        return !config_->twitch_client_id.empty() && 
               !config_->twitch_client_secret.empty() && 
               !config_->twitch_channel_name.empty();
    }

    std::string TwitchManager::GetConnectionStatus() const {
        if (!connected_) {
            return "Disconnected";
        }

        std::string status = "Connected";
        if (chat_connected_) {
            status += " (Chat)";
        }
        if (eventsub_connected_) {
            status += " (Events)";
        }

        return status;
    }

    void TwitchManager::TestChatMessage() {
        SendChatMessage("StayPutVR integration test - chat working!");
    }

    void TwitchManager::TestDonationEvent(const std::string& username, float amount) {
        TwitchEventData event;
        event.event_type = "test_donation";
        event.username = username;
        event.amount = amount;
        event.message = "Test donation event";

        ProcessEventSubEvent(event);
    }

    void TwitchManager::TestOAuthFlow() {
        if (!ValidateConfiguration()) {
            if (Logger::IsInitialized()) {
                Logger::Error("Invalid Twitch configuration - check client ID and secret");
            }
            return;
        }

        // Step 1: Start the OAuth callback server
        StartOAuthServer();

        // Step 2: Generate OAuth URL
        std::string oauth_url = GenerateOAuthURL();
        if (oauth_url.empty()) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to generate OAuth URL");
            }
            StopOAuthServer();
            return;
        }

        // Step 3: Instructions for user
        if (Logger::IsInitialized()) {
            Logger::Info("=== AUTOMATED TWITCH OAUTH SETUP ===");
            Logger::Info("1. OAuth callback server started on http://localhost:8080");
            Logger::Info("2. Open this URL in your browser:");
            Logger::Info(oauth_url);
            Logger::Info("3. Authorize the application - it will redirect automatically");
            Logger::Info("4. The authorization will be handled automatically!");
            Logger::Info("========================================");
        }

        // For console output as well (in case Logger goes to file)
        printf("\n=== AUTOMATED TWITCH OAUTH SETUP ===\n");
        printf("1. OAuth callback server started on http://localhost:8080\n");
        printf("2. Open this URL in your browser:\n");
        printf("%s\n", oauth_url.c_str());
        printf("3. Authorize the application - it will redirect automatically\n");
        printf("4. The authorization will be handled automatically!\n");
        printf("======================================\n\n");
    }

    void TwitchManager::StartOAuthServer() {
        if (oauth_server_running_) {
            if (Logger::IsInitialized()) {
                Logger::Info("OAuth server already running");
            }
            return;
        }

        oauth_server_running_ = true;
        received_oauth_code_.clear();

        // Start the OAuth callback server in a separate thread
        oauth_server_thread_ = std::make_unique<std::thread>([this]() {
            if (Logger::IsInitialized()) {
                Logger::Info("Starting OAuth callback server on port 8080");
            }

            // Simple HTTP server implementation using WinSock
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                if (Logger::IsInitialized()) {
                    Logger::Error("WSAStartup failed for OAuth server");
                }
                oauth_server_running_ = false;
                return;
            }

            SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
            if (serverSocket == INVALID_SOCKET) {
                if (Logger::IsInitialized()) {
                    Logger::Error("Failed to create OAuth server socket");
                }
                WSACleanup();
                oauth_server_running_ = false;
                return;
            }

            // Set up server address
            sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_addr.s_addr = INADDR_ANY;
            serverAddr.sin_port = htons(8080);

            // Bind and listen
            if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR ||
                listen(serverSocket, 1) == SOCKET_ERROR) {
                if (Logger::IsInitialized()) {
                    Logger::Error("Failed to bind/listen OAuth server socket");
                }
                closesocket(serverSocket);
                WSACleanup();
                oauth_server_running_ = false;
                return;
            }

            if (Logger::IsInitialized()) {
                Logger::Info("OAuth callback server listening on port 8080");
            }

            // Accept one connection
            while (oauth_server_running_) {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(serverSocket, &readfds);

                timeval timeout;
                timeout.tv_sec = 1;  // 1 second timeout
                timeout.tv_usec = 0;

                int result = select(0, &readfds, nullptr, nullptr, &timeout);
                if (result == SOCKET_ERROR || !oauth_server_running_) {
                    break;
                } else if (result == 0) {
                    // Timeout, continue loop to check if we should stop
                    continue;
                }

                SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
                if (clientSocket == INVALID_SOCKET || !oauth_server_running_) {
                    if (clientSocket != INVALID_SOCKET) {
                        closesocket(clientSocket);
                    }
                    break;
                }

                // Read HTTP request
                char buffer[4096];
                int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytesReceived > 0) {
                    buffer[bytesReceived] = '\\0';
                    std::string request(buffer);

                    // Parse for the authorization code
                    size_t codePos = request.find("code=");
                    if (codePos != std::string::npos) {
                        codePos += 5; // Move past "code="
                        size_t codeEnd = request.find('&', codePos);
                        if (codeEnd == std::string::npos) {
                            codeEnd = request.find(' ', codePos); // HTTP request line ends with space
                        }
                        if (codeEnd == std::string::npos) {
                            codeEnd = request.length();
                        }

                        std::string code = request.substr(codePos, codeEnd - codePos);
                        
                        // Store the received code
                        {
                            std::lock_guard<std::mutex> lock(oauth_code_mutex_);
                            received_oauth_code_ = code;
                        }

                        // Send success response
                        std::string response = 
                            "HTTP/1.1 200 OK\\r\\n"
                            "Content-Type: text/html\\r\\n"
                            "Connection: close\\r\\n"
                            "\\r\\n"
                            "<html><body>"
                            "<h1>✅ Authorization Successful!</h1>"
                            "<p>You can close this window and return to StayPutVR.</p>"
                            "<p>The application will automatically connect to Twitch.</p>"
                            "</body></html>";

                        send(clientSocket, response.c_str(), (int)response.length(), 0);

                        if (Logger::IsInitialized()) {
                            Logger::Info("OAuth authorization code received successfully");
                        }

                        // Process the OAuth callback
                        if (HandleOAuthCallback(code)) {
                            if (Logger::IsInitialized()) {
                                Logger::Info("✅ Twitch OAuth setup completed successfully!");
                            }
                            printf("\\n✅ Twitch OAuth setup completed successfully!\\n");
                        } else {
                            if (Logger::IsInitialized()) {
                                Logger::Error("❌ Failed to complete OAuth setup");
                            }
                            printf("\\n❌ Failed to complete OAuth setup\\n");
                        }

                        // Close the client socket and stop the server
                        closesocket(clientSocket);
                        break;
                    } else {
                        // Send error response
                        std::string response = 
                            "HTTP/1.1 400 Bad Request\\r\\n"
                            "Content-Type: text/html\\r\\n"
                            "Connection: close\\r\\n"
                            "\\r\\n"
                            "<html><body>"
                            "<h1>❌ Authorization Failed</h1>"
                            "<p>No authorization code found in the request.</p>"
                            "</body></html>";

                        send(clientSocket, response.c_str(), (int)response.length(), 0);
                    }
                }

                closesocket(clientSocket);
            }

            // Clean shutdown
            shutdown(serverSocket, SD_BOTH);
            closesocket(serverSocket);
            WSACleanup();
            oauth_server_running_ = false;

            if (Logger::IsInitialized()) {
                Logger::Info("OAuth callback server thread finished");
            }
        });
    }

    void TwitchManager::StopOAuthServer() {
        if (!oauth_server_running_) {
            return;
        }

        if (Logger::IsInitialized()) {
            Logger::Info("Stopping OAuth callback server");
        }

        // Signal the server to stop
        oauth_server_running_ = false;
        
        // Force close any listening socket by connecting to it
        // This will unblock the accept() call in the server thread
        try {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
                SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
                if (clientSocket != INVALID_SOCKET) {
                    sockaddr_in serverAddr;
                    serverAddr.sin_family = AF_INET;
                    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
                    serverAddr.sin_port = htons(8080);

                    // Quick connect to unblock the server (ignore result)
                    connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
                    closesocket(clientSocket);
                }
                WSACleanup();
            }
        } catch (...) {
            // Ignore any errors during cleanup
        }
        
        // Wait for the thread to finish (with timeout)
        if (oauth_server_thread_ && oauth_server_thread_->joinable()) {
            // Give the thread up to 2 seconds to finish
            auto start_time = std::chrono::steady_clock::now();
            while (oauth_server_running_ && 
                   std::chrono::steady_clock::now() - start_time < std::chrono::seconds(2)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            oauth_server_thread_->join();
        }
        
        oauth_server_thread_.reset();

        if (Logger::IsInitialized()) {
            Logger::Info("OAuth callback server stopped successfully");
        }
    }

    // Private methods

    void TwitchManager::SetError(const std::string& error) {
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = error;
        
        if (Logger::IsInitialized()) {
            Logger::Error("TwitchManager: " + error);
        }
    }

    bool TwitchManager::IsTokenValid() const {
        if (access_token_.empty()) {
            return false;
        }

        auto now = std::chrono::steady_clock::now();
        return now < token_expiry_;
    }

    bool TwitchManager::ValidateAccessToken() {
        if (access_token_.empty()) {
            return false;
        }

        // Make a simple API call to validate the token
        // We'll call the "Get Users" endpoint to check if our token works
        std::string response;
        bool success = MakeAPIRequest("https://api.twitch.tv/helix/users", "GET", "", response);
        
        if (!success) {
            return false;
        }

        try {
            // Parse the response to make sure it's valid
            nlohmann::json response_json = nlohmann::json::parse(response);
            
            // If we get a "data" array, the token is valid
            if (response_json.contains("data") && response_json["data"].is_array()) {
                if (Logger::IsInitialized()) {
                    Logger::Debug("Access token validation successful");
                }
                
                // Also validate token scopes for IRC chat
                ValidateTokenScopes();
                
                return true;
            } else {
                if (Logger::IsInitialized()) {
                    Logger::Debug("Access token validation failed: invalid response format");
                }
                return false;
            }
        } catch (const std::exception& e) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to parse token validation response: " + std::string(e.what()));
            }
            return false;
        }
    }

    bool TwitchManager::ValidateTokenScopes() {
        if (access_token_.empty()) {
            return false;
        }

        // Make a request to validate token and get scopes
        std::string response;
        
        // Use the OAuth2 validate endpoint to check scopes
        std::map<std::string, std::string> headers;
        headers["Authorization"] = "OAuth " + access_token_;
        
        if (!HttpClient::SendHttpRequest("https://id.twitch.tv/oauth2/validate", "GET", headers, "", response)) {
            if (Logger::IsInitialized()) {
                Logger::Warning("🔧 Failed to validate token scopes");
            }
            return false;
        }

        try {
            nlohmann::json response_json = nlohmann::json::parse(response);
            
            if (response_json.contains("scopes") && response_json["scopes"].is_array()) {
                std::vector<std::string> scopes;
                for (const auto& scope : response_json["scopes"]) {
                    scopes.push_back(scope.get<std::string>());
                }
                
                if (Logger::IsInitialized()) {
                    std::string scope_list;
                    for (const auto& scope : scopes) {
                        if (!scope_list.empty()) scope_list += ", ";
                        scope_list += scope;
                    }
                    Logger::Info("🔧 Token scopes: " + scope_list);
                }
                
                // Check for required IRC chat scopes
                bool has_chat_read = std::find(scopes.begin(), scopes.end(), "chat:read") != scopes.end();
                bool has_chat_edit = std::find(scopes.begin(), scopes.end(), "chat:edit") != scopes.end();
                
                if (!has_chat_read || !has_chat_edit) {
                    if (Logger::IsInitialized()) {
                        Logger::Warning("❌ Token missing required IRC chat scopes (chat:read, chat:edit)");
                        Logger::Warning("❌ Current token was authorized with old scopes - re-authorization needed");
                    }
                    return false;
                } else {
                    if (Logger::IsInitialized()) {
                        Logger::Info("✅ Token has required IRC chat scopes");
                    }
                    return true;
                }
            }
        } catch (const std::exception& e) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to parse scope validation response: " + std::string(e.what()));
            }
        }

        return false;
    }

    void TwitchManager::EventSubWorker() {
        if (Logger::IsInitialized()) {
            Logger::Info("EventSub worker thread started");
        }

        while (!should_stop_threads_ && eventsub_connected_) {
            // TODO: Implement actual EventSub WebSocket handling
            // This would:
            // 1. Maintain WebSocket connection to wss://eventsub.wss.twitch.tv/ws
            // 2. Handle keepalive messages
            // 3. Process incoming event notifications
            // 4. Handle reconnection logic
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (Logger::IsInitialized()) {
            Logger::Info("EventSub worker thread stopped");
        }
    }

    void TwitchManager::ChatWorker() {
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 ChatWorker: Starting IRC connection process");
            Logger::Info("🔧 Bot username: " + (config_->twitch_bot_username.empty() ? 
                                              config_->twitch_channel_name : config_->twitch_bot_username));
            Logger::Info("🔧 Channel: #" + config_->twitch_channel_name);
            Logger::Info("🔧 Access token length: " + std::to_string(access_token_.length()));
        }

        // IRC connection setup
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            if (Logger::IsInitialized()) {
                Logger::Error("❌ WSAStartup failed for IRC connection");
            }
            return;
        }

        SOCKET ircSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (ircSocket == INVALID_SOCKET) {
            if (Logger::IsInitialized()) {
                Logger::Error("❌ Failed to create IRC socket");
            }
            WSACleanup();
            return;
        }

        // Connect to Twitch IRC
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(6667);
        
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Resolving irc.chat.twitch.tv...");
        }
        
        // Resolve irc.chat.twitch.tv
        hostent* host = gethostbyname("irc.chat.twitch.tv");
        if (!host) {
            if (Logger::IsInitialized()) {
                Logger::Error("❌ Failed to resolve irc.chat.twitch.tv");
            }
            closesocket(ircSocket);
            WSACleanup();
            return;
        }
        
        memcpy(&serverAddr.sin_addr, host->h_addr, host->h_length);

        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Connecting to Twitch IRC server...");
        }

        if (connect(ircSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            if (Logger::IsInitialized()) {
                Logger::Error("❌ Failed to connect to Twitch IRC: " + std::to_string(WSAGetLastError()));
            }
            closesocket(ircSocket);
            WSACleanup();
            return;
        }

        if (Logger::IsInitialized()) {
            Logger::Info("✅ Connected to Twitch IRC server");
        }

        // IRC authentication
        std::string bot_username = config_->twitch_bot_username.empty() ? 
                                  config_->twitch_channel_name : config_->twitch_bot_username;
        
        // Make sure we have a valid token before authenticating
        if (access_token_.empty()) {
            if (Logger::IsInitialized()) {
                Logger::Error("❌ No access token available for IRC authentication");
            }
            closesocket(ircSocket);
            WSACleanup();
            return;
        }

        // Validate token freshness before using it for IRC
        if (!IsTokenValid()) {
            if (Logger::IsInitialized()) {
                Logger::Warning("🔧 Token appears expired, but proceeding with IRC auth anyway");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Info("✅ Token is valid for IRC authentication");
            }
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Starting IRC authentication for user: " + bot_username);
            Logger::Info("🔧 Using access token (first 10 chars): " + access_token_.substr(0, 10) + "...");
        }
        
        std::string pass_msg = "PASS oauth:" + access_token_ + "\r\n";
        std::string nick_msg = "NICK " + bot_username + "\r\n";
        std::string join_msg = "JOIN #" + config_->twitch_channel_name + "\r\n";
        std::string caps_msg = "CAP REQ :twitch.tv/tags twitch.tv/commands\r\n";

        // Send IRC commands
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Sending IRC capability request...");
        }
        send(ircSocket, caps_msg.c_str(), (int)caps_msg.length(), 0);
        
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Sending IRC authentication...");
        }
        send(ircSocket, pass_msg.c_str(), (int)pass_msg.length(), 0);
        send(ircSocket, nick_msg.c_str(), (int)nick_msg.length(), 0);
        
        // Wait a moment for authentication to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Check for authentication response
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(ircSocket, &readfds);
        timeval timeout;
        timeout.tv_sec = 5;  // 5 second timeout for auth
        timeout.tv_usec = 0;
        
        char auth_buffer[1024];
        bool auth_successful = false;
        
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Waiting for IRC authentication response...");
        }
        
        while (select(0, &readfds, nullptr, nullptr, &timeout) > 0) {
            int bytesReceived = recv(ircSocket, auth_buffer, sizeof(auth_buffer) - 1, 0);
            if (bytesReceived > 0) {
                auth_buffer[bytesReceived] = '\0';
                std::string auth_response(auth_buffer);
                
                if (Logger::IsInitialized()) {
                    Logger::Info("🔧 IRC Auth Response: " + auth_response);
                }
                
                if (auth_response.find("Login unsuccessful") != std::string::npos) {
                    if (Logger::IsInitialized()) {
                        Logger::Error("❌ IRC authentication failed - token may be invalid or expired");
                    }
                    closesocket(ircSocket);
                    WSACleanup();
                    return;
                } else if (auth_response.find("Welcome") != std::string::npos || 
                          auth_response.find("001") != std::string::npos) {
                    auth_successful = true;
                    if (Logger::IsInitialized()) {
                        Logger::Info("✅ IRC authentication successful!");
                    }
                    break;
                }
            }
            
            // Reset for next select call
            FD_ZERO(&readfds);
            FD_SET(ircSocket, &readfds);
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;
        }
        
        if (!auth_successful) {
            if (Logger::IsInitialized()) {
                Logger::Error("❌ IRC authentication timeout or failed");
            }
            closesocket(ircSocket);
            WSACleanup();
            return;
        }
        
        // Now join the channel
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Joining channel: #" + config_->twitch_channel_name);
        }
        send(ircSocket, join_msg.c_str(), (int)join_msg.length(), 0);

        chat_connected_ = true;
        
        if (Logger::IsInitialized()) {
            Logger::Info("✅ Successfully joined Twitch chat channel: #" + config_->twitch_channel_name);
            Logger::Info("🔧 Starting message reading loop...");
        }

        // Main chat reading loop
        char buffer[4096];
        std::string incomplete_message;
        int message_count = 0;
        
        while (!should_stop_threads_ && chat_connected_) {
            // Use select with timeout to check for messages
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(ircSocket, &readfds);

            timeval timeout;
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;

            int result = select(0, &readfds, nullptr, nullptr, &timeout);
            if (result == SOCKET_ERROR || !chat_connected_) {
                if (Logger::IsInitialized()) {
                    Logger::Error("❌ IRC select error or connection closed");
                }
                break;
            } else if (result == 0) {
                // Timeout, continue loop
                continue;
            }

            // Read data from IRC
            int bytesReceived = recv(ircSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived <= 0) {
                if (Logger::IsInitialized()) {
                    Logger::Info("❌ IRC connection closed (recv returned " + std::to_string(bytesReceived) + ")");
                }
                break;
            }

            buffer[bytesReceived] = '\0';
            std::string received_data = incomplete_message + std::string(buffer);
            incomplete_message.clear();

            if (Logger::IsInitialized()) {
                Logger::Debug("🔧 IRC Raw Data (" + std::to_string(bytesReceived) + " bytes): " + received_data);
            }

            // Process complete IRC messages (split by \r\n)
            size_t pos = 0;
            while ((pos = received_data.find("\r\n")) != std::string::npos) {
                std::string message = received_data.substr(0, pos);
                received_data = received_data.substr(pos + 2);

                if (!message.empty()) {
                    message_count++;
                    if (Logger::IsInitialized()) {
                        Logger::Info("🔧 Processing IRC message #" + std::to_string(message_count) + ": " + message);
                    }
                    ProcessIRCMessage(message);
                }
            }
            
            // Save any incomplete message
            if (!received_data.empty()) {
                incomplete_message = received_data;
            }
        }

        // Cleanup
        chat_connected_ = false;
        closesocket(ircSocket);
        WSACleanup();

        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Chat worker thread stopped (processed " + std::to_string(message_count) + " messages)");
        }
    }

    void TwitchManager::ProcessIRCMessage(const std::string& irc_message) {
        if (Logger::IsInitialized()) {
            Logger::Debug("🔧 ProcessIRCMessage: " + irc_message);
        }

        // Handle PING messages (keep-alive)
        if (irc_message.find("PING") == 0) {
            // Extract the ping token and send PONG
            size_t colonPos = irc_message.find(':');
            if (colonPos != std::string::npos) {
                std::string pongMsg = "PONG " + irc_message.substr(colonPos) + "\r\n";
                // Send PONG back (would need socket reference, for now just log)
                if (Logger::IsInitialized()) {
                    Logger::Info("🔧 Sending PONG response");
                }
            }
            return;
        }

        // Parse PRIVMSG (chat messages)
        // Format with tags: @tags :username!username@username.tmi.twitch.tv PRIVMSG #channel :message
        // Format without tags: :username!username@username.tmi.twitch.tv PRIVMSG #channel :message
        if (irc_message.find("PRIVMSG") != std::string::npos) {
            if (Logger::IsInitialized()) {
                Logger::Info("🔧 Found PRIVMSG in IRC message");
            }
            
            // Find the actual IRC message part (after tags if present)
            std::string message_part = irc_message;
            
            // If message starts with @, skip the tags part
            if (irc_message[0] == '@') {
                size_t space_pos = irc_message.find(' ');
                if (space_pos != std::string::npos) {
                    message_part = irc_message.substr(space_pos + 1);
                    if (Logger::IsInitialized()) {
                        Logger::Debug("🔧 Stripped tags, message part: " + message_part);
                    }
                }
            }
            
            // Extract username from the message part
            size_t exclamPos = message_part.find('!');
            if (exclamPos == std::string::npos || message_part[0] != ':') {
                if (Logger::IsInitialized()) {
                    Logger::Warning("🔧 Invalid PRIVMSG format - missing username in: " + message_part);
                }
                return;
            }
            
            std::string username = message_part.substr(1, exclamPos - 1);
            
            // Extract message content (after the second colon)
            size_t messageStart = message_part.find(" :", message_part.find("PRIVMSG"));
            if (messageStart == std::string::npos) {
                if (Logger::IsInitialized()) {
                    Logger::Warning("🔧 Invalid PRIVMSG format - missing message content in: " + message_part);
                }
                return;
            }
            messageStart += 2; // Skip " :"
            
            std::string message_content = message_part.substr(messageStart);
            
            if (Logger::IsInitialized()) {
                Logger::Info("✅ Chat message from " + username + ": " + message_content);
            }

            // Check if it's a command
            std::string command, args;
            if (ParseChatCommand(message_content, command, args)) {
                if (Logger::IsInitialized()) {
                    Logger::Info("✅ Found chat command: '" + command + "' (args: '" + args + "') from " + username);
                    Logger::Info("🔧 Configured commands - lock: '" + config_->twitch_lock_command + 
                               "', unlock: '" + config_->twitch_unlock_command + 
                               "', status: '" + config_->twitch_status_command + "'");
                }

                // Handle the command
                if (command == config_->twitch_lock_command) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("🔧 Executing LOCK command");
                    }
                    HandleLockCommand(username, args);
                } else if (command == config_->twitch_unlock_command) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("🔧 Executing UNLOCK command");
                    }
                    HandleUnlockCommand(username, args);
                } else if (command == config_->twitch_status_command) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("🔧 Executing STATUS command");
                    }
                    HandleStatusCommand(username, args);
                } else {
                    if (Logger::IsInitialized()) {
                        Logger::Info("🔧 Unknown command: " + command);
                    }
                }

                // Also call the callback if set
                if (chat_command_callback_) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("🔧 Calling chat command callback");
                    }
                    chat_command_callback_(username, command, args);
                } else {
                    if (Logger::IsInitialized()) {
                        Logger::Warning("🔧 No chat command callback set");
                    }
                }
            } else {
                if (Logger::IsInitialized()) {
                    Logger::Debug("🔧 Message is not a command (prefix: '" + config_->twitch_command_prefix + "')");
                }
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Debug("🔧 IRC message is not a PRIVMSG");
            }
        }
    }

    void TwitchManager::HandleLockCommand(const std::string& username, const std::string& args) {
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 HandleLockCommand: Executing lock command from " + username);
        }

        // Send confirmation message to chat
        std::string response = "@" + username + " Locking devices!";
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Sending lock response: " + response);
        }
        SendChatMessage(response);

        // Trigger the actual lock via callback (this will call UIManager's lock logic)
        if (chat_command_callback_) {
            if (Logger::IsInitialized()) {
                Logger::Info("🔧 Calling lock callback");
            }
            chat_command_callback_(username, config_->twitch_lock_command, args);
        } else {
            if (Logger::IsInitialized()) {
                Logger::Warning("🔧 No callback set for lock command");
            }
        }
    }

    void TwitchManager::HandleUnlockCommand(const std::string& username, const std::string& args) {
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 HandleUnlockCommand: Executing unlock command from " + username);
        }

        // Send confirmation message to chat
        std::string response = "@" + username + " Unlocking devices!";
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Sending unlock response: " + response);
        }
        SendChatMessage(response);

        // Trigger the actual unlock via callback
        if (chat_command_callback_) {
            if (Logger::IsInitialized()) {
                Logger::Info("🔧 Calling unlock callback");
            }
            chat_command_callback_(username, config_->twitch_unlock_command, args);
        } else {
            if (Logger::IsInitialized()) {
                Logger::Warning("🔧 No callback set for unlock command");
            }
        }
    }

    void TwitchManager::HandleStatusCommand(const std::string& username, const std::string& args) {
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 HandleStatusCommand: Executing status command from " + username);
        }

        // For now, just send a generic status message
        // In a full implementation, this would check actual device lock states
        std::string response = "@" + username + " StayPutVR is running and monitoring devices!";
        if (Logger::IsInitialized()) {
            Logger::Info("🔧 Sending status response: " + response);
        }
        SendChatMessage(response);

        // Trigger the status callback
        if (chat_command_callback_) {
            if (Logger::IsInitialized()) {
                Logger::Info("🔧 Calling status callback");
            }
            chat_command_callback_(username, config_->twitch_status_command, args);
        } else {
            if (Logger::IsInitialized()) {
                Logger::Warning("🔧 No callback set for status command");
            }
        }
    }

    bool TwitchManager::MakeAPIRequest(const std::string& endpoint, const std::string& method, 
                                      const std::string& body, std::string& response) {
        // Rate limiting for API calls
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_api_call_).count();
        if (elapsed < 100) { // Minimum 100ms between API calls
            std::this_thread::sleep_for(std::chrono::milliseconds(100 - elapsed));
        }
        last_api_call_ = std::chrono::steady_clock::now();

        if (Logger::IsInitialized()) {
            Logger::Debug("Making API request to: " + endpoint);
        }

        // Initialize HttpClient if not already done
        if (!HttpClient::Initialize()) {
            SetError("Failed to initialize HTTP client");
            return false;
        }

        // Set up headers
        std::map<std::string, std::string> headers;
        
        // Add authentication headers if we have an access token
        if (!access_token_.empty()) {
            headers["Authorization"] = "Bearer " + access_token_;
        }
        
        // Add Client-Id header if available
        if (config_ && !config_->twitch_client_id.empty()) {
            headers["Client-Id"] = config_->twitch_client_id;
        }
        
        // Set content type for POST requests
        if (method == "POST" && !body.empty()) {
            // Check if this is a JSON request (for chat messages)
            if (endpoint.find("/chat/messages") != std::string::npos) {
                headers["Content-Type"] = "application/json";
            } else {
                headers["Content-Type"] = "application/x-www-form-urlencoded";
            }
        }

        // Make the HTTP request
        bool success = HttpClient::SendHttpRequest(endpoint, method, headers, body, response);
        
        if (!success) {
            SetError("HTTP request failed to: " + endpoint);
            if (Logger::IsInitialized()) {
                Logger::Error("HTTP request failed. Response: " + response);
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Debug("API request successful. Response length: " + std::to_string(response.length()));
            }
        }

        return success;
    }

    std::string TwitchManager::BuildAuthHeader() const {
        return "Bearer " + access_token_;
    }

    bool TwitchManager::ParseChatCommand(const std::string& message, std::string& command, std::string& args) {
        if (!config_ || message.empty()) {
            return false;
        }

        std::string prefix = config_->twitch_command_prefix;
        if (message.length() <= prefix.length() || message.substr(0, prefix.length()) != prefix) {
            return false;
        }

        std::string command_part = message.substr(prefix.length());
        size_t space_pos = command_part.find(' ');
        
        if (space_pos == std::string::npos) {
            command = command_part;
            args = "";
        } else {
            command = command_part.substr(0, space_pos);
            args = command_part.substr(space_pos + 1);
        }

        return true;
    }

    bool TwitchManager::SubscribeToEvent(const std::string& event_type, const std::string& condition) {
        // TODO: Implement actual EventSub subscription
        // This would make a POST request to https://api.twitch.tv/helix/eventsub/subscriptions
        
        if (Logger::IsInitialized()) {
            Logger::Info("Subscribing to Twitch event: " + event_type);
        }

        return true; // Placeholder
    }

    bool TwitchManager::UnsubscribeFromEvent(const std::string& subscription_id) {
        // TODO: Implement actual EventSub unsubscription
        
        if (Logger::IsInitialized()) {
            Logger::Info("Unsubscribing from Twitch event: " + subscription_id);
        }

        return true; // Placeholder
    }

    bool TwitchManager::ParseDonationEvent(const std::string& json, TwitchEventData& event) {
        try {
            nlohmann::json j = nlohmann::json::parse(json);
            
            if (j.contains("payload") && j["payload"].contains("event")) {
                auto event_data = j["payload"]["event"];
                
                event.username = event_data.value("user_name", "");
                event.message = event_data.value("user_input", "");
                
                // For channel points, we might need to map reward cost to dollar amount
                // This is a simplified approach
                if (event_data.contains("reward") && event_data["reward"].contains("cost")) {
                    int cost = event_data["reward"]["cost"];
                    event.amount = cost / 100.0f; // Convert points to approximate dollar value
                }
                
                return !event.username.empty();
            }
        } catch (const std::exception& e) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to parse donation event: " + std::string(e.what()));
            }
        }
        
        return false;
    }

    bool TwitchManager::ParseBitsEvent(const std::string& json, TwitchEventData& event) {
        try {
            nlohmann::json j = nlohmann::json::parse(json);
            
            if (j.contains("payload") && j["payload"].contains("event")) {
                auto event_data = j["payload"]["event"];
                
                event.username = event_data.value("user_name", "");
                event.bits = event_data.value("bits", 0);
                event.message = event_data.value("message", "");
                
                return !event.username.empty() && event.bits > 0;
            }
        } catch (const std::exception& e) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to parse bits event: " + std::string(e.what()));
            }
        }
        
        return false;
    }

    bool TwitchManager::ParseSubscriptionEvent(const std::string& json, TwitchEventData& event) {
        try {
            nlohmann::json j = nlohmann::json::parse(json);
            
            if (j.contains("payload") && j["payload"].contains("event")) {
                auto event_data = j["payload"]["event"];
                
                event.username = event_data.value("user_name", "");
                event.months = event_data.value("cumulative_months", 1);
                event.is_gift = event_data.value("is_gift", false);
                
                return !event.username.empty();
            }
        } catch (const std::exception& e) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to parse subscription event: " + std::string(e.what()));
            }
        }
        
        return false;
    }

    // Placeholder WebSocket methods - these would need actual WebSocket implementation
    bool TwitchManager::ConnectWebSocket(const std::string& url) {
        // TODO: Implement WebSocket connection
        return true;
    }

    void TwitchManager::DisconnectWebSocket() {
        // TODO: Implement WebSocket disconnection
    }

    bool TwitchManager::SendWebSocketMessage(const std::string& message) {
        // TODO: Implement WebSocket message sending
        return true;
    }

    std::string TwitchManager::ReceiveWebSocketMessage() {
        // TODO: Implement WebSocket message receiving
        return "";
    }

    std::string TwitchManager::UrlEncode(const std::string& value) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;

        for (char c : value) {
            if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } else {
                escaped << '%' << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(c));
            }
        }

        return escaped.str();
    }

    bool TwitchManager::MakeOAuthRequest(const std::string& endpoint, const std::string& method, 
                                      const std::string& body, std::string& response) {
        // Rate limiting for API calls
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_api_call_).count();
        if (elapsed < 100) { // Minimum 100ms between API calls
            std::this_thread::sleep_for(std::chrono::milliseconds(100 - elapsed));
        }
        last_api_call_ = std::chrono::steady_clock::now();

        if (Logger::IsInitialized()) {
            Logger::Debug("Making OAuth request to: " + endpoint);
        }

        // Initialize HttpClient if not already done
        if (!HttpClient::Initialize()) {
            SetError("Failed to initialize HTTP client");
            return false;
        }

        // Set up headers for OAuth request (no Authorization header needed)
        std::map<std::string, std::string> headers;
        
        // Set content type for POST requests
        if (method == "POST" && !body.empty()) {
            headers["Content-Type"] = "application/x-www-form-urlencoded";
        }

        // Make the HTTP request
        bool success = HttpClient::SendHttpRequest(endpoint, method, headers, body, response);
        
        if (!success) {
            SetError("OAuth HTTP request failed to: " + endpoint);
            if (Logger::IsInitialized()) {
                Logger::Error("OAuth HTTP request failed. Response: " + response);
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Debug("OAuth request successful. Response length: " + std::to_string(response.length()));
            }
        }

        return success;
    }

    bool TwitchManager::GetBroadcasterUserId(const std::string& channel_name, std::string& user_id) {
        if (channel_name.empty()) {
            return false;
        }

        // Make API request to get user information
        std::string endpoint = "https://api.twitch.tv/helix/users?login=" + UrlEncode(channel_name);
        std::string response;
        
        if (!MakeAPIRequest(endpoint, "GET", "", response)) {
            return false;
        }

        try {
            nlohmann::json response_json = nlohmann::json::parse(response);
            
            if (response_json.contains("data") && response_json["data"].is_array() && 
                !response_json["data"].empty()) {
                user_id = response_json["data"][0].value("id", "");
                return !user_id.empty();
            }
        } catch (const std::exception& e) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to parse user ID response: " + std::string(e.what()));
            }
        }

        return false;
    }

    void TwitchManager::ProcessEventSubEvent(const TwitchEventData& event) {
        if (Logger::IsInitialized()) {
            Logger::Info("Processing Twitch event: " + event.event_type + " from " + event.username);
        }

        if (event.event_type == "channel.cheer" && bits_callback_) {
            bits_callback_(event.username, event.bits, event.message);
        } else if ((event.event_type == "channel.subscribe" || event.event_type == "channel.subscription.gift") && subscription_callback_) {
            subscription_callback_(event.username, event.months, event.is_gift);
        } else if (event.event_type.find("donation") != std::string::npos && donation_callback_) {
            donation_callback_(event.username, event.amount, event.message);
        }
    }

    void TwitchManager::ProcessChatMessage(const std::string& raw_message) {
        // This method now handles the actual chat message content
        std::string command, args;
        if (ParseChatCommand(raw_message, command, args)) {
            if (chat_command_callback_) {
                // Username is passed separately from IRC parsing
                std::string username = "user"; // This will be set by ProcessIRCMessage
                chat_command_callback_(username, command, args);
            }
        }
    }

} // namespace StayPutVR 