#pragma once
#include <string>
#include <vector>
#include <functional>
#include <Windows.h>
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
