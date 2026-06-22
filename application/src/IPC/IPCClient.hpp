#pragma once
#include <string>
#include <vector>
#include <functional>
#ifdef _WIN32
#include <Windows.h>
#else
// The IPC client talks to the SteamVR driver over a Windows named pipe. There
// is no driver on the Linux development build, so provide placeholder types and
// stub the implementation (always reports disconnected, surfaces no devices).
typedef void* HANDLE;
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE (nullptr)
#endif
#endif
#include <thread>
#include <mutex>
#include <atomic>
#include "../../../common/DeviceTypes.hpp"

namespace StayPutVR {
    class IPCClient {
    public:
        IPCClient();
        ~IPCClient();
        
        bool Connect();
        void Disconnect();
        bool IsConnected() const;
        void ProcessMessages();
        void SendCommand(const std::string& command, const std::string& params);
        
        // Callback for device updates
        using DeviceUpdateCallback = std::function<void(const std::vector<DevicePositionData>&)>;
        void SetDeviceUpdateCallback(DeviceUpdateCallback callback);
        
    private:
        static constexpr const char* PIPE_NAME = "\\\\.\\pipe\\StayPutVR";
        
        HANDLE pipe_handle_ = INVALID_HANDLE_VALUE;
        std::atomic<bool> connected_ = false;
        std::atomic<bool> running_ = false;
        std::thread reader_thread_;
        std::mutex mutex_;
        DeviceUpdateCallback device_update_callback_;
        
        void ReaderThread();
        bool ReadMessage(std::vector<uint8_t>& buffer);
        bool WriteMessage(const std::vector<uint8_t>& buffer);
        void ProcessDeviceUpdateMessage(const std::vector<uint8_t>& buffer);
    };
}
