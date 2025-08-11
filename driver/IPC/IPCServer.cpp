#include "IPCServer.hpp"
#include "../../common/Logger.hpp"
#include <iostream>

namespace StayPutVR {

    IPCServer::IPCServer() : pipe_handle_(INVALID_HANDLE_VALUE), connected_(false), running_(false), initialized_(false), writer_busy_(false), consecutive_failures_(0) {
        Logger::Info("IPCServer: Constructor called");
        last_connection_log_ = std::chrono::steady_clock::now() - LOG_THROTTLE_INTERVAL; // Allow immediate first log
        last_failure_log_ = std::chrono::steady_clock::now() - LOG_THROTTLE_INTERVAL;
        circuit_breaker_timeout_ = std::chrono::steady_clock::now();
    }

    IPCServer::~IPCServer() {
        Shutdown();
    }
    
    bool IPCServer::Initialize() {
        Logger::Info("IPCServer: Initializing");
        
        if (initialized_) {
            Logger::Warning("IPCServer: Already initialized");
            return true;
        }
        
        // Create the pipe with FILE_FLAG_OVERLAPPED for async operations
        if (!CreatePipe()) {
            return false;
        }
        
        // Start the threads
        running_ = true;
        listener_thread_ = std::thread(&IPCServer::ListenerThread, this);
        writer_thread_ = std::thread(&IPCServer::WriterThread, this);
        
        initialized_ = true;
        Logger::Info("IPCServer: Initialized successfully");
        return true;
    }
    
    bool IPCServer::InitializeIfNeeded() {
        if (initialized_) {
            return true; // Already initialized
        }
        
        // Only initialize when first needed
        Logger::Info("IPCServer: Starting lazy initialization");
        return Initialize();
    }
    

    
    void IPCServer::Shutdown() {
        Logger::Info("IPCServer: Shutting down");
        
        // Signal threads to exit
        running_ = false;
        
        // Notify writer thread to wake up if it's sleeping
        {
            std::lock_guard<std::mutex> lock(mutex_);
            write_cv_.notify_all();
        }
        
        // Close pipe handle
        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_handle_);
            pipe_handle_ = INVALID_HANDLE_VALUE;
        }
        
        // Wait for threads to exit
        if (listener_thread_.joinable()) {
            listener_thread_.join();
        }
        
        if (writer_thread_.joinable()) {
            writer_thread_.join();
        }
        
        connected_ = false;
        initialized_ = false;
        Logger::Info("IPCServer: Shut down");
    }
    
    bool IPCServer::IsConnected() const {
        return connected_;
    }
    
    void IPCServer::LogConnectionFailure() {
        auto now = std::chrono::steady_clock::now();
        if (now - last_failure_log_ > LOG_THROTTLE_INTERVAL) {
            Logger::Info("IPCServer: No client application connected (StayPutVR companion app not running)");
            last_failure_log_ = now;
        }
    }
    
    void IPCServer::LogConnectionSuccess() {
        auto now = std::chrono::steady_clock::now();
        if (now - last_connection_log_ > LOG_THROTTLE_INTERVAL) {
            Logger::Info("IPCServer: Client application connected successfully");
            last_connection_log_ = now;
        }
        // Reset circuit breaker on successful connection
        consecutive_failures_ = 0;
    }
    
    bool IPCServer::IsCircuitBreakerOpen() const {
        auto now = std::chrono::steady_clock::now();
        return (consecutive_failures_ >= MAX_CONSECUTIVE_FAILURES) && 
               (now < circuit_breaker_timeout_);
    }
    
    void IPCServer::SendDeviceUpdates(const std::vector<DevicePositionData>& devices) {
        if (devices.empty()) {
            return;
        }
        
        // Fast exit if not initialized or connected - no blocking operations
        if (!initialized_ || !connected_) {
            return; // Silently fail - don't log in high-frequency VR loop
        }
        
        // Circuit breaker: if we've had too many consecutive failures, temporarily stop trying
        if (IsCircuitBreakerOpen()) {
            return; // Silently fail during circuit breaker timeout
        }
        
        try {
            // Create message buffer
            std::vector<uint8_t> buffer;
            
            // Message type: 1 = device update
            uint8_t msgType = static_cast<uint8_t>(MessageType::DEVICE_UPDATE);
            buffer.push_back(msgType);
            
            // Number of devices
            uint32_t deviceCount = static_cast<uint32_t>(devices.size());
            buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&deviceCount),
                         reinterpret_cast<uint8_t*>(&deviceCount) + sizeof(deviceCount));
            
            // Device data
            for (const auto& device : devices) {
                // Serial length and string
                uint32_t serialLen = static_cast<uint32_t>(device.serial.size());
                buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&serialLen),
                             reinterpret_cast<uint8_t*>(&serialLen) + sizeof(serialLen));
                buffer.insert(buffer.end(), device.serial.begin(), device.serial.end());
                
                // Device type
                uint8_t deviceType = static_cast<uint8_t>(device.type);
                buffer.push_back(deviceType);
                
                // Position (3 floats)
                buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(device.position),
                             reinterpret_cast<const uint8_t*>(device.position) + 3 * sizeof(float));
                
                // Rotation (4 floats)  
                buffer.insert(buffer.end(), reinterpret_cast<const uint8_t*>(device.rotation),
                             reinterpret_cast<const uint8_t*>(device.rotation) + 4 * sizeof(float));
                
                // Connected flag
                uint8_t connectedFlag = device.connected ? 1 : 0;
                buffer.push_back(connectedFlag);
            }
            
            // Send the message
            WriteMessageAsync(buffer);
        }
        catch (const std::exception& e) {
            Logger::Error("IPCServer: Exception in SendDeviceUpdates: " + std::string(e.what()));
        }
    }
    
    void IPCServer::ProcessIncomingMessages() {
        // Only process messages if connected
        if (!connected_ || pipe_handle_ == INVALID_HANDLE_VALUE) {
            return;
        }
        
        try {
            std::vector<uint8_t> buffer;
            // Non-blocking read will return quickly if no messages
            if (ReadMessage(buffer)) {
                // Process the message if we got one
                if (!buffer.empty()) {
                    uint8_t msgTypeRaw = buffer[0];
                    MessageType msgType = static_cast<MessageType>(msgTypeRaw);
                    
                    // Process message silently to avoid logging in VR frame loop
                    
                    // Handle the message based on type
                    switch (msgType) {
                        case MessageType::COMMAND:
                            // Process command (not implemented yet)
                            break;
                        default:
                            // Unknown message type - silently ignore in VR loop
                            break;
                    }
                }
            }
        }
        catch (const std::exception& e) {
            Logger::Error("IPCServer: Exception in ProcessIncomingMessages: " + std::string(e.what()));
        }
        
        // This method is designed to be called frequently from VR RunFrame
        // so it must be non-blocking and fast - no loops or retries here
    }
    
    void IPCServer::ListenerThread() {
        Logger::Info("IPCServer: Listener thread started");
        
        while (running_) {
            // Wait for a client connection
            if (!WaitForConnection()) {
                // Failed to accept connection, use throttled logging instead of spamming
                LogConnectionFailure();
                
                // Wait longer between retries when no client is available (normal case)
                std::this_thread::sleep_for(std::chrono::seconds(10)); // Increased to 10 seconds when no client
                continue;
            }
            
            Logger::Info("IPCServer: Client connected");
            LogConnectionSuccess();
            connected_ = true;
            
            // Process messages from the client (if any)
            // In a one-way model, this mainly maintains the connection
            int consecutive_read_failures = 0;
            
            while (running_ && connected_) {
                try {
                    // Brief sleep to prevent busy-waiting
                    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Increased from 5ms to 50ms
                    
                    Logger::Debug("IPCServer: Checking for incoming messages");
                    std::vector<uint8_t> buffer;
                    
                    // Try to read a message - this now uses non-blocking I/O
                    bool readResult = ReadMessage(buffer);
                    
                    if (!readResult) {
                        // Non-blocking read didn't find any messages - this is normal
                        // in a primarily one-way communication setup
                        consecutive_read_failures++;
                        
                        // Only log or disconnect if we have persistent failures
                        // that indicate a real connection problem
                        if (consecutive_read_failures > 200) { // Increased threshold from 100 to 200
                            // Use throttled logging instead of warning on every failure
                            auto now = std::chrono::steady_clock::now();
                            if (now - last_failure_log_ > std::chrono::minutes(5)) { // Log every 5 minutes instead of every minute
                                Logger::Debug("IPCServer: Multiple read failures, checking pipe status (normal for one-way communication)");
                                last_failure_log_ = now;
                            }
                            
                            // Check if the pipe is still valid
                            if (pipe_handle_ == INVALID_HANDLE_VALUE) {
                                Logger::Warning("IPCServer: Pipe handle became invalid");
                                connected_ = false;
                                break;
                            }
                            
                            // Reset the counter but keep the connection
                            consecutive_read_failures = 0;
                        }
                        
                        continue;
                    }
                    
                    // Reset failure counter on successful read
                    consecutive_read_failures = 0;
                    
                    // Process the message
                    if (!buffer.empty()) {
                        uint8_t msgTypeRaw = buffer[0];
                        MessageType msgType = static_cast<MessageType>(msgTypeRaw);
                        
                        Logger::Debug("IPCServer: Received message type: " + std::to_string(static_cast<int>(msgType)));
                        
                        // Handle the message based on type
                        switch (msgType) {
                            case MessageType::COMMAND:
                                Logger::Debug("IPCServer: Processing command message");
                                // Process command (not implemented yet)
                                break;
                            default:
                                Logger::Warning("IPCServer: Unknown message type: " + std::to_string(msgTypeRaw));
                                break;
                        }
                    }
                }
                catch (const std::exception& e) {
                    Logger::Error("IPCServer: Exception in listener thread: " + std::string(e.what()));
                    connected_ = false;
                    break;
                }
                catch (...) {
                    Logger::Error("IPCServer: Unknown exception in listener thread");
                    connected_ = false;
                    break;
                }
            }
            
            // Disconnect and create a new pipe
            Logger::Info("IPCServer: Client disconnected, cleaning up connection");
            if (pipe_handle_ != INVALID_HANDLE_VALUE) {
                CloseHandle(pipe_handle_);
                pipe_handle_ = INVALID_HANDLE_VALUE;
            }
            
            // Create a new pipe for the next connection
            if (running_) {
                Logger::Info("IPCServer: Creating new pipe for next connection");
                if (!CreatePipe()) {
                    // Failed to create pipe, use throttled logging and longer retry
                    Logger::Error("IPCServer: Failed to create new pipe, retrying in 10 seconds");
                    std::this_thread::sleep_for(std::chrono::seconds(10)); // Increased from 1 second to 10 seconds
                }
            }
        }
        
        Logger::Info("IPCServer: Listener thread exiting");
    }
    
    bool IPCServer::CreatePipe() {
        Logger::Info("IPCServer: Creating pipe");
        
        // First check if the pipe already exists and try to close it
        HANDLE existing_pipe = CreateFileA(
            PIPE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL
        );
        
        if (existing_pipe != INVALID_HANDLE_VALUE) {
            Logger::Warning("IPCServer: Pipe already exists, attempting to close it");
            CloseHandle(existing_pipe);
            // Wait a bit before trying to create a new one
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        // Create the named pipe with explicit security attributes to allow access
        SECURITY_ATTRIBUTES sa;
        SECURITY_DESCRIPTOR sd;
        
        // Initialize security descriptor
        if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
            DWORD error = GetLastError();
            Logger::Error("IPCServer: InitializeSecurityDescriptor failed: " + std::to_string(error));
            return false;
        }
        
        // Set DACL to NULL for unrestricted access
        if (!SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE)) {
            DWORD error = GetLastError();
            Logger::Error("IPCServer: SetSecurityDescriptorDacl failed: " + std::to_string(error));
            return false;
        }
        
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;
        
        // Create the named pipe with the security attributes and FILE_FLAG_OVERLAPPED for async I/O
        HANDLE pipe_handle = CreateNamedPipeA(
            PIPE_NAME,                      // Pipe name
            PIPE_ACCESS_DUPLEX |            // Read/write access
            FILE_FLAG_OVERLAPPED,           // Overlapped mode for async operations
            PIPE_TYPE_MESSAGE |             // Message type pipe
            PIPE_READMODE_MESSAGE |         // Message-read mode
            PIPE_WAIT,                      // Blocking mode
            1,                              // Max instances
            4096,                           // Output buffer size
            4096,                           // Input buffer size
            0,                              // Default time-out
            &sa                             // Security attributes
        );
        
        if (pipe_handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            Logger::Error("IPCServer: Failed to create pipe: " + std::to_string(error));
            
            // Provide more detailed error information
            switch (error) {
                case ERROR_ACCESS_DENIED:
                    Logger::Error("IPCServer: Access denied - check permissions or if another instance is running");
                    break;
                case ERROR_PIPE_BUSY:
                    Logger::Error("IPCServer: Pipe is busy - another process may be using it");
                    break;
                case ERROR_NOT_ENOUGH_MEMORY:
                    Logger::Error("IPCServer: Not enough memory to create pipe");
                    break;
                default:
                    Logger::Error("IPCServer: Unknown error creating pipe");
                    break;
            }
            return false;
        }
        
        pipe_handle_ = pipe_handle;
        Logger::Info("IPCServer: Pipe created successfully");
        return true;
    }
    
    bool IPCServer::WaitForConnection() {
        Logger::Info("IPCServer: Waiting for client connection");
        
        if (pipe_handle_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        // Connect the pipe with overlapped I/O
        OVERLAPPED connectOverlapped = {0};
        connectOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (connectOverlapped.hEvent == NULL) {
            DWORD error = GetLastError();
            Logger::Error("IPCServer: Failed to create event for overlapped connection: " + std::to_string(error));
            return false;
        }
        
        // Wait for a client to connect with overlap
        BOOL result = ConnectNamedPipe(pipe_handle_, &connectOverlapped);
        DWORD error = GetLastError();
        
        if (result) {
            // Immediate connection success (unusual)
            CloseHandle(connectOverlapped.hEvent);
            return true;
        }
        
        // Check error code
        if (error == ERROR_IO_PENDING) {
            // Connection is pending, wait for result
            Logger::Debug("IPCServer: Connection pending, waiting for client");
            
            // Use a longer timeout here since this is just waiting for initial connection  
            DWORD waitResult = WaitForSingleObject(connectOverlapped.hEvent, 2000); // 2 second timeout
            
            if (waitResult == WAIT_OBJECT_0) {
                // Connection completed
                DWORD unused;
                if (GetOverlappedResult(pipe_handle_, &connectOverlapped, &unused, FALSE)) {
                    CloseHandle(connectOverlapped.hEvent);
                    return true;
                }
                
                error = GetLastError();
                Logger::Error("IPCServer: GetOverlappedResult failed for connection: " + std::to_string(error));
                CloseHandle(connectOverlapped.hEvent);
                return false;
            }
            else {
                // Wait failed or timed out
                Logger::Warning("IPCServer: Connection wait failed or timed out");
                CancelIo(pipe_handle_);
                CloseHandle(connectOverlapped.hEvent);
                return false;
            }
        }
        else if (error == ERROR_PIPE_CONNECTED) {
            // Client already connected
            CloseHandle(connectOverlapped.hEvent);
            return true;
        }
        else {
            // Connection failed
            CloseHandle(connectOverlapped.hEvent);
            
            // Handle specific error codes gracefully
            if (error == 232) { // ERROR_NO_DATA - no client connected yet, this is normal
                // Don't log as error - this is expected when server starts before client
                return false;
            } else {
                Logger::Error("IPCServer: ConnectNamedPipe failed: " + std::to_string(error));
                return false;
            }
        }
    }
    
    bool IPCServer::ReadMessage(std::vector<uint8_t>& buffer) {
        if (pipe_handle_ == INVALID_HANDLE_VALUE) {
            Logger::Error("IPCServer: ReadMessage called with invalid pipe handle");
            return false;
        }
        
        // Create an OVERLAPPED structure for async reading
        OVERLAPPED overlapped = {0};
        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (overlapped.hEvent == NULL) {
            DWORD error = GetLastError();
            Logger::Error("IPCServer: Failed to create event for overlapped read I/O: " + std::to_string(error));
            return false;
        }
        
        // Read message size with a very short timeout
        uint32_t messageSize = 0;
        DWORD bytesRead = 0;
        
        Logger::Debug("IPCServer: Reading message size (non-blocking)");
        BOOL result = ReadFile(
            pipe_handle_,
            &messageSize,
            sizeof(messageSize),
            &bytesRead,
            &overlapped
        );
        
        if (!result) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // Operation is pending, wait for a very short time to avoid blocking VR performance
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1); // 1ms timeout
                
                if (waitResult == WAIT_TIMEOUT) {
                    // No data available yet, not an error in a one-way communication model
                    CancelIo(pipe_handle_);
                    CloseHandle(overlapped.hEvent);
                    return false; // Return false but don't log as error or disconnect
                }
                else if (waitResult != WAIT_OBJECT_0) {
                    DWORD waitError = GetLastError();
                    CloseHandle(overlapped.hEvent);
                    Logger::Error("IPCServer: Wait failed for message size read: " + std::to_string(waitError));
                    return false;
                }
                
                // Get the results
                if (!GetOverlappedResult(pipe_handle_, &overlapped, &bytesRead, FALSE)) {
                    error = GetLastError();
                    CloseHandle(overlapped.hEvent);
                    
                    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                        Logger::Info("IPCServer: Client disconnected during overlapped read");
                    } else {
                        Logger::Error("IPCServer: GetOverlappedResult failed for message size: " + std::to_string(error));
                    }
                    return false;
                }
            }
            else if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                // Client disconnected
                Logger::Info("IPCServer: Client disconnected while reading message size");
                CloseHandle(overlapped.hEvent);
                return false;
            }
            else {
                Logger::Error("IPCServer: ReadFile failed for message size: Error " + std::to_string(error) + 
                             ", bytes read: " + std::to_string(bytesRead) + "/" + std::to_string(sizeof(messageSize)));
                CloseHandle(overlapped.hEvent);
                return false;
            }
        }
        
        CloseHandle(overlapped.hEvent);
        
        if (bytesRead != sizeof(messageSize)) {
            Logger::Error("IPCServer: Incomplete read for message size: " + 
                         std::to_string(bytesRead) + "/" + std::to_string(sizeof(messageSize)));
            return false;
        }
        
        // Validate message size to prevent buffer issues
        if (messageSize == 0) {
            Logger::Warning("IPCServer: Received message with zero size");
            return false;
        }
        
        if (messageSize > 1024 * 1024) {  // 1 MB limit
            Logger::Error("IPCServer: Message size too large: " + std::to_string(messageSize) + " bytes");
            return false;
        }
        
        Logger::Debug("IPCServer: Message size: " + std::to_string(messageSize) + " bytes");
        
        // Allocate buffer for message
        try {
            buffer.resize(messageSize);
        } catch (const std::exception& e) {
            Logger::Error("IPCServer: Failed to allocate buffer for message: " + std::string(e.what()));
            return false;
        }
        
        // Create a new overlapped structure for the data read
        OVERLAPPED dataOverlapped = {0};
        dataOverlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (dataOverlapped.hEvent == NULL) {
            DWORD error = GetLastError();
            Logger::Error("IPCServer: Failed to create event for overlapped data read: " + std::to_string(error));
            return false;
        }
        
        // Read message data with the same short timeout
        Logger::Debug("IPCServer: Reading message data");
        result = ReadFile(
            pipe_handle_,
            buffer.data(),
            messageSize,
            &bytesRead,
            &dataOverlapped
        );
        
        if (!result) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                // Operation is pending, wait for a very short time
                DWORD waitResult = WaitForSingleObject(dataOverlapped.hEvent, 1); // 1ms timeout
                
                if (waitResult == WAIT_TIMEOUT) {
                    // No data available yet, not an error in a one-way communication model
                    CancelIo(pipe_handle_);
                    CloseHandle(dataOverlapped.hEvent);
                    return false; // Return false but don't log as error or disconnect
                }
                else if (waitResult != WAIT_OBJECT_0) {
                    DWORD waitError = GetLastError();
                    CloseHandle(dataOverlapped.hEvent);
                    Logger::Error("IPCServer: Wait failed for data read: " + std::to_string(waitError));
                    return false;
                }
                
                // Get the results
                if (!GetOverlappedResult(pipe_handle_, &dataOverlapped, &bytesRead, FALSE)) {
                    error = GetLastError();
                    CloseHandle(dataOverlapped.hEvent);
                    
                    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                        Logger::Info("IPCServer: Client disconnected during overlapped data read");
                    } else {
                        Logger::Error("IPCServer: GetOverlappedResult failed for data: " + std::to_string(error));
                    }
                    return false;
                }
            }
            else if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                // Client disconnected
                Logger::Info("IPCServer: Client disconnected while reading message data");
                CloseHandle(dataOverlapped.hEvent);
                return false;
            }
            else {
                Logger::Error("IPCServer: ReadFile failed for message data: Error " + std::to_string(error) + 
                             ", bytes read: " + std::to_string(bytesRead) + "/" + std::to_string(messageSize));
                CloseHandle(dataOverlapped.hEvent);
                return false;
            }
        }
        
        CloseHandle(dataOverlapped.hEvent);
        
        if (bytesRead != messageSize) {
            Logger::Error("IPCServer: Incomplete read for message data: " + 
                         std::to_string(bytesRead) + "/" + std::to_string(messageSize));
            return false;
        }
        
        Logger::Debug("IPCServer: Successfully read message with " + std::to_string(bytesRead) + " bytes");
        return true;
    }
    
    bool IPCServer::WriteMessage(const std::vector<uint8_t>& buffer) {
        // Forward to the async version for improved performance
        return WriteMessageAsync(buffer);
    }
    
    void IPCServer::WriterThread() {
        Logger::Info("IPCServer: Writer thread started");
        
        while (running_) {
            std::shared_ptr<MessageData> msg_data;
            
            // Wait for a message to process
            {
                std::unique_lock<std::mutex> lock(mutex_);
                write_cv_.wait(lock, [this]() { 
                    return !write_queue_.empty() || !running_; 
                });
                
                // Check if we're shutting down
                if (!running_) {
                    break;
                }
                
                // Get the next message
                if (!write_queue_.empty()) {
                    msg_data = write_queue_.front();
                    write_queue_.pop();
                    writer_busy_ = true;
                }
            }
            
            // Process the message if we have one
            if (msg_data) {
                bool success = PerformAsyncWrite(msg_data);
                
                // Mark as processed
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    writer_busy_ = false;
                    msg_data->processed = true;
                }
                
                if (!success && connected_) {
                    consecutive_failures_++;
                    if (consecutive_failures_ >= MAX_CONSECUTIVE_FAILURES) {
                        Logger::Warning("IPCServer: Too many consecutive failures, opening circuit breaker");
                        circuit_breaker_timeout_ = std::chrono::steady_clock::now() + CIRCUIT_BREAKER_TIMEOUT;
                    }
                    Logger::Error("IPCServer: Failed to write message, disconnecting client");
                    connected_ = false;
                } else if (success) {
                    // Reset failure count on successful write
                    consecutive_failures_ = 0;
                }
                
                // Process the next message immediately if there is one
                if (!write_queue_.empty()) {
                    write_cv_.notify_one();
                }
            }
        }
        
        Logger::Info("IPCServer: Writer thread exiting");
    }
    
    bool IPCServer::WriteMessageAsync(const std::vector<uint8_t>& buffer) {
        try {
            // Validate connection and handle
            if (!connected_ || pipe_handle_ == INVALID_HANDLE_VALUE) {
                Logger::Warning("IPCServer: WriteMessageAsync called with invalid pipe handle or disconnected client");
                return false;
            }
            
            // Validate buffer
            if (buffer.empty()) {
                Logger::Warning("IPCServer: WriteMessageAsync called with empty buffer");
                return false;
            }
            
            // Create a copy of the buffer to queue
            auto msg_data = std::make_shared<MessageData>();
            msg_data->buffer = buffer;
            msg_data->processed = false;
            
            // Queue the message
            {
                std::lock_guard<std::mutex> lock(mutex_);
                write_queue_.push(msg_data);
                write_cv_.notify_one();
            }
            
            Logger::Debug("IPCServer: Message queued for async write, queue size: " + 
                         std::to_string(write_queue_.size()) + ", buffer size: " + 
                         std::to_string(buffer.size()));
            
            return true;
        }
        catch (const std::exception& e) {
            Logger::Error("IPCServer: Exception in WriteMessageAsync: " + std::string(e.what()));
            return false;
        }
        catch (...) {
            Logger::Error("IPCServer: Unknown exception in WriteMessageAsync");
            return false;
        }
    }
    
    bool IPCServer::PerformAsyncWrite(std::shared_ptr<MessageData> msg_data) {
        if (!connected_ || pipe_handle_ == INVALID_HANDLE_VALUE) {
            Logger::Warning("IPCServer: PerformAsyncWrite called with invalid pipe handle or disconnected client");
            return false;
        }
        
        if (msg_data->buffer.empty()) {
            Logger::Warning("IPCServer: PerformAsyncWrite called with empty buffer");
            return false;
        }
        
        try {
            // Create an OVERLAPPED structure for async operations
            OVERLAPPED overlapped = {0};
            overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            if (overlapped.hEvent == NULL) {
                DWORD error = GetLastError();
                Logger::Error("IPCServer: Failed to create event for overlapped I/O: " + std::to_string(error));
                return false;
            }
            
            // Write message size asynchronously
            uint32_t messageSize = static_cast<uint32_t>(msg_data->buffer.size());
            DWORD bytesWritten = 0;
            
            Logger::Debug("IPCServer: Writing message header with size: " + std::to_string(messageSize) + " bytes");
            
            BOOL result = WriteFile(
                pipe_handle_,
                &messageSize,
                sizeof(messageSize),
                &bytesWritten,
                &overlapped
            );
            
            if (!result) {
                DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    // Handle error
                    CloseHandle(overlapped.hEvent);
                    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                        Logger::Info("IPCServer: Client disconnected while writing message size");
                        connected_ = false;
                    } else {
                        Logger::Error("IPCServer: WriteFile failed for message size: Error " + std::to_string(error));
                    }
                    return false;
                }
                
                // Wait for the operation to complete with a timeout
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000); // 1000ms timeout
                if (waitResult != WAIT_OBJECT_0) {
                    CloseHandle(overlapped.hEvent);
                    if (waitResult == WAIT_TIMEOUT) {
                        Logger::Error("IPCServer: Timeout waiting for WriteFile to complete for message size");
                    } else {
                        Logger::Error("IPCServer: WaitForSingleObject failed: " + std::to_string(GetLastError()));
                    }
                    return false;
                }
                
                // Get the results
                if (!GetOverlappedResult(pipe_handle_, &overlapped, &bytesWritten, FALSE)) {
                    DWORD error = GetLastError();
                    CloseHandle(overlapped.hEvent);
                    Logger::Error("IPCServer: GetOverlappedResult failed for message size: " + std::to_string(error));
                    return false;
                }
            }
            
            ResetEvent(overlapped.hEvent);
            
            if (bytesWritten != sizeof(messageSize)) {
                CloseHandle(overlapped.hEvent);
                Logger::Error("IPCServer: Incomplete write for message size: " + 
                             std::to_string(bytesWritten) + "/" + std::to_string(sizeof(messageSize)));
                return false;
            }
            
            // Write message data asynchronously
            Logger::Debug("IPCServer: Writing message data");
            
            result = WriteFile(
                pipe_handle_,
                msg_data->buffer.data(),
                messageSize,
                &bytesWritten,
                &overlapped
            );
            
            if (!result) {
                DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    // Handle error
                    CloseHandle(overlapped.hEvent);
                    if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                        Logger::Info("IPCServer: Client disconnected while writing message data");
                        connected_ = false;
                    } else {
                        Logger::Error("IPCServer: WriteFile failed for message data: Error " + std::to_string(error));
                    }
                    return false;
                }
                
                // Wait for the operation to complete with a timeout
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 1000); // 1000ms timeout
                if (waitResult != WAIT_OBJECT_0) {
                    CloseHandle(overlapped.hEvent);
                    if (waitResult == WAIT_TIMEOUT) {
                        Logger::Error("IPCServer: Timeout waiting for WriteFile to complete for message data");
                    } else {
                        Logger::Error("IPCServer: WaitForSingleObject failed: " + std::to_string(GetLastError()));
                    }
                    return false;
                }
                
                // Get the results
                if (!GetOverlappedResult(pipe_handle_, &overlapped, &bytesWritten, FALSE)) {
                    DWORD error = GetLastError();
                    CloseHandle(overlapped.hEvent);
                    Logger::Error("IPCServer: GetOverlappedResult failed for message data: " + std::to_string(error));
                    return false;
                }
            }
            
            CloseHandle(overlapped.hEvent);
            
            if (bytesWritten != messageSize) {
                Logger::Error("IPCServer: Incomplete write for message data: " + 
                             std::to_string(bytesWritten) + "/" + std::to_string(messageSize));
                return false;
            }
            
            Logger::Debug("IPCServer: Async write completed successfully");
            return true;
        }
        catch (const std::exception& e) {
            Logger::Error("IPCServer: Exception in PerformAsyncWrite: " + std::string(e.what()));
            return false;
        }
        catch (...) {
            Logger::Error("IPCServer: Unknown exception in PerformAsyncWrite");
            return false;
        }
    }
}