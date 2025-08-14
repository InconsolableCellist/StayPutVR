#include "IPCClient.hpp"
#include "../../../common/Logger.hpp"
#include <iostream>
#include <Windows.h>
#include <future>
#include <chrono>

namespace StayPutVR {

    IPCClient::IPCClient() : pipe_handle_(INVALID_HANDLE_VALUE), connected_(false), running_(false) {
        Logger::Info("IPCClient: Constructor called");
    }

    IPCClient::~IPCClient() {
        Disconnect();
    }

    bool IPCClient::Connect() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        Logger::Info("IPCClient: Connecting to pipe");
        
        if (connected_) {
            Logger::Warning("IPCClient: Already connected");
            return true;
        }
        
        // Ensure we're in a clean state before connecting
        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_handle_);
            pipe_handle_ = INVALID_HANDLE_VALUE;
        }
        
        // Ensure reader thread is stopped
        if (reader_thread_.joinable()) {
            running_ = false;
            reader_thread_.join();
        }
        
        // Try to connect to the named pipe
        HANDLE pipe_handle = CreateFileA(
            PIPE_NAME,                      // Pipe name
            GENERIC_READ | GENERIC_WRITE,   // Read/write access
            0,                              // No sharing
            NULL,                           // Default security attributes
            OPEN_EXISTING,                  // Open existing pipe
            0,                              // Default attributes
            NULL                            // No template file
        );
        
        if (pipe_handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error == ERROR_PIPE_BUSY) {
                Logger::Warning("IPCClient: Pipe is busy, waiting...");
                
                // Wait for the pipe to become available (increased timeout for driver startup)
                if (!WaitNamedPipeA(PIPE_NAME, 15000)) { // 15 second timeout
                    Logger::Warning("IPCClient: Timed out waiting for pipe (driver may not be loaded)");
                    return false;
                }
                
                // Try to connect again
                pipe_handle = CreateFileA(
                    PIPE_NAME,
                    GENERIC_READ | GENERIC_WRITE,
                    0,
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL
                );
                
                if (pipe_handle == INVALID_HANDLE_VALUE) {
                    Logger::Error("IPCClient: Failed to connect to pipe: " + std::to_string(GetLastError()));
                    return false;
                }
            } else {
                Logger::Error("IPCClient: Failed to connect to pipe: " + std::to_string(error));
                return false;
            }
        }
        
        // Set the pipe to message-read mode
        DWORD dwMode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(pipe_handle, &dwMode, NULL, NULL)) {
            Logger::Error("IPCClient: Failed to set pipe mode: " + std::to_string(GetLastError()));
            CloseHandle(pipe_handle);
            return false;
        }
        
        // Store the pipe handle
        pipe_handle_ = pipe_handle;
        connected_ = true;
        
        // Start the reader thread
        running_ = true;
        reader_thread_ = std::thread(&IPCClient::ReaderThread, this);
        
        Logger::Info("IPCClient: Connected successfully");
        return true;
    }

    void IPCClient::Disconnect() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        Logger::Info("IPCClient: Disconnecting");
        
        // Check if we're already disconnected
        if (!connected_ && pipe_handle_ == INVALID_HANDLE_VALUE && !reader_thread_.joinable()) {
            Logger::Info("IPCClient: Already disconnected");
            return;
        }
        
        // Signal thread to exit first
        running_ = false;
        
        // Use CancelIoEx to cancel any pending operations
        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            if (!CancelIoEx(pipe_handle_, NULL)) {
                DWORD error = GetLastError();
                if (error != ERROR_NOT_FOUND) { // Not an error if no I/O is pending
                    Logger::Warning("IPCClient: CancelIoEx failed: " + std::to_string(error));
                }
            }
        }
        
        // Wait for thread to exit with a timeout
        if (reader_thread_.joinable()) {
            // First try a timed join
            auto timeout = std::chrono::milliseconds(500);
            auto start = std::chrono::steady_clock::now();
            
            Logger::Info("IPCClient: Waiting for reader thread to exit (timeout: 500ms)");
            
            std::future<void> future = std::async(std::launch::async, [&] {
                if (reader_thread_.joinable()) {
                    reader_thread_.join();
                }
            });
            
            // Wait with timeout
            if (future.wait_for(timeout) == std::future_status::timeout) {
                Logger::Warning("IPCClient: Reader thread did not exit within timeout, forcing disconnection");
                // Continue with cleanup anyway
            }
            else {
                Logger::Info("IPCClient: Reader thread exited gracefully");
            }
        }
        
        // Close pipe handle 
        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_handle_);
            pipe_handle_ = INVALID_HANDLE_VALUE;
        }
        
        connected_ = false;
        Logger::Info("IPCClient: Disconnected");
    }

    bool IPCClient::IsConnected() const {
        return connected_;
    }

    void IPCClient::ProcessMessages() {
        // This is handled by the reader thread
    }

    void IPCClient::SendCommand(const std::string& command, const std::string& params) {
        if (!connected_ || pipe_handle_ == INVALID_HANDLE_VALUE) {
            return;
        }
        
        try {
            // Serialize command data
            std::vector<uint8_t> buffer;
            
            // Message type: 2 = command
            uint8_t msgType = 2;
            buffer.push_back(msgType);
            
            // Command string length
            uint32_t cmdLen = static_cast<uint32_t>(command.size());
            buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&cmdLen),
                         reinterpret_cast<uint8_t*>(&cmdLen) + sizeof(cmdLen));
            
            // Command string
            buffer.insert(buffer.end(), command.begin(), command.end());
            
            // Params string length
            uint32_t paramsLen = static_cast<uint32_t>(params.size());
            buffer.insert(buffer.end(), reinterpret_cast<uint8_t*>(&paramsLen),
                         reinterpret_cast<uint8_t*>(&paramsLen) + sizeof(paramsLen));
            
            // Params string
            buffer.insert(buffer.end(), params.begin(), params.end());
            
            // Send the message
            WriteMessage(buffer);
        }
        catch (const std::exception& e) {
            Logger::Error("IPCClient: Exception in SendCommand: " + std::string(e.what()));
        }
    }

    void IPCClient::SetDeviceUpdateCallback(DeviceUpdateCallback callback) {
        device_update_callback_ = callback;
    }

    void IPCClient::ReaderThread() {
        Logger::Info("IPCClient: Reader thread started");
        
        while (running_ && connected_) {
            try {
                std::vector<uint8_t> buffer;
                if (!ReadMessage(buffer)) {
                    // Connection lost
                    connected_ = false;
                    break;
                }
                
                // Process the message
                if (!buffer.empty()) {
                    uint8_t msgType = buffer[0];
                    
                    switch (msgType) {
                        case 1: // Device update
                            ProcessDeviceUpdateMessage(buffer);
                            break;
                        default:
                            Logger::Warning("IPCClient: Unknown message type: " + std::to_string(msgType));
                            break;
                    }
                }
            }
            catch (const std::exception& e) {
                Logger::Error("IPCClient: Exception in reader thread: " + std::string(e.what()));
                connected_ = false;
                break;
            }
        }
        
        Logger::Info("IPCClient: Reader thread exiting");
    }

    bool IPCClient::ReadMessage(std::vector<uint8_t>& buffer) {
        if (pipe_handle_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        // Read message size
        uint32_t messageSize = 0;
        DWORD bytesRead = 0;
        BOOL result = ReadFile(
            pipe_handle_,
            &messageSize,
            sizeof(messageSize),
            &bytesRead,
            NULL
        );
        
        if (!result || bytesRead != sizeof(messageSize)) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                // Server disconnected
                Logger::Info("IPCClient: Server disconnected");
            } else {
                Logger::Error("IPCClient: ReadFile failed: " + std::to_string(error));
            }
            return false;
        }
        
        // Allocate buffer for message
        buffer.resize(messageSize);
        
        // Read message data
        result = ReadFile(
            pipe_handle_,
            buffer.data(),
            messageSize,
            &bytesRead,
            NULL
        );
        
        if (!result || bytesRead != messageSize) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                // Server disconnected
                Logger::Info("IPCClient: Server disconnected");
            } else {
                Logger::Error("IPCClient: ReadFile failed: " + std::to_string(error));
            }
            return false;
        }
        
        return true;
    }

    bool IPCClient::WriteMessage(const std::vector<uint8_t>& buffer) {
        if (!connected_ || pipe_handle_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Write message size
        uint32_t messageSize = static_cast<uint32_t>(buffer.size());
        DWORD bytesWritten = 0;
        BOOL result = WriteFile(
            pipe_handle_,
            &messageSize,
            sizeof(messageSize),
            &bytesWritten,
            NULL
        );
        
        if (!result || bytesWritten != sizeof(messageSize)) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                // Server disconnected
                Logger::Info("IPCClient: Server disconnected");
                connected_ = false;
            } else {
                Logger::Error("IPCClient: WriteFile failed: " + std::to_string(error));
            }
            return false;
        }
        
        // Write message data
        result = WriteFile(
            pipe_handle_,
            buffer.data(),
            messageSize,
            &bytesWritten,
            NULL
        );
        
        if (!result || bytesWritten != messageSize) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) {
                // Server disconnected
                Logger::Info("IPCClient: Server disconnected");
                connected_ = false;
            } else {
                Logger::Error("IPCClient: WriteFile failed: " + std::to_string(error));
            }
            return false;
        }
        
        // Flush the pipe
        FlushFileBuffers(pipe_handle_);
        
        return true;
    }

    void IPCClient::ProcessDeviceUpdateMessage(const std::vector<uint8_t>& buffer) {
        if (!device_update_callback_ || buffer.size() < 5) { // 1 byte type + 4 bytes count
            return;
        }
        
        try {
            // Skip message type
            size_t offset = 1;
            
            // Read number of devices
            uint32_t numDevices;
            memcpy(&numDevices, buffer.data() + offset, sizeof(numDevices));
            offset += sizeof(numDevices);
            
            // Parse device data
            std::vector<DevicePositionData> devices;
            devices.reserve(numDevices);
            
            for (uint32_t i = 0; i < numDevices; i++) {
                if (offset + sizeof(uint32_t) > buffer.size()) {
                    Logger::Error("IPCClient: Buffer too small for device data");
                    break;
                }
                
                // Read serial length
                uint32_t serialLen;
                memcpy(&serialLen, buffer.data() + offset, sizeof(serialLen));
                offset += sizeof(serialLen);
                
                if (offset + serialLen > buffer.size()) {
                    Logger::Error("IPCClient: Buffer too small for serial string");
                    break;
                }
                
                // Read serial string
                std::string serial(reinterpret_cast<const char*>(buffer.data() + offset), serialLen);
                offset += serialLen;
                
                if (offset + 1 > buffer.size()) {
                    Logger::Error("IPCClient: Buffer too small for device type");
                    break;
                }
                
                // Read device type
                uint8_t deviceType = buffer[offset++];
                
                if (offset + sizeof(float) * 3 > buffer.size()) {
                    Logger::Error("IPCClient: Buffer too small for position data");
                    break;
                }
                
                // Read position
                float position[3];
                memcpy(position, buffer.data() + offset, sizeof(float) * 3);
                offset += sizeof(float) * 3;
                
                if (offset + sizeof(float) * 4 > buffer.size()) {
                    Logger::Error("IPCClient: Buffer too small for rotation data");
                    break;
                }
                
                // Read rotation
                float rotation[4];
                memcpy(rotation, buffer.data() + offset, sizeof(float) * 4);
                offset += sizeof(float) * 4;
                
                if (offset + 1 > buffer.size()) {
                    Logger::Error("IPCClient: Buffer too small for connected flag");
                    break;
                }
                
                // Read connected flag
                bool connected = buffer[offset++] != 0;
                
                // Create device position data
                DevicePositionData device;
                device.serial = serial;
                device.type = static_cast<DeviceType>(deviceType);
                memcpy(device.position, position, sizeof(float) * 3);
                memcpy(device.rotation, rotation, sizeof(float) * 4);
                device.connected = connected;
                
                devices.push_back(device);
            }
            
            // Call the callback with the device data
            device_update_callback_(devices);
        }
        catch (const std::exception& e) {
            Logger::Error("IPCClient: Exception in ProcessDeviceUpdateMessage: " + std::string(e.what()));
        }
    }
}
