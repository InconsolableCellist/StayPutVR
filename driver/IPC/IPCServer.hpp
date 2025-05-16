#pragma once
#include <string>
#include <vector>
#include <functional>
#include <Windows.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include "../../common/DeviceTypes.hpp"

namespace StayPutVR {
    class IPCServer {
    public:
        // Message types for IPC communication
        enum class MessageType : uint8_t {
            UNKNOWN = 0,
            DEVICE_UPDATE = 1,
            COMMAND = 2
        };

        IPCServer();
        ~IPCServer();
        
        bool Initialize();
        void Shutdown();
        bool IsConnected() const;
        void SendDeviceUpdates(const std::vector<DevicePositionData>& devices);
        void ProcessIncomingMessages();
        
    private:
        static constexpr const char* PIPE_NAME = "\\\\.\\pipe\\StayPutVR";
        
        HANDLE pipe_handle_ = INVALID_HANDLE_VALUE;
        std::atomic<bool> connected_ = false;
        std::atomic<bool> running_ = false;
        std::thread listener_thread_;
        std::thread writer_thread_;
        std::mutex mutex_;
        std::condition_variable write_cv_;
        bool writer_busy_ = false;
        
        // Structure to hold message data for async write queue
        struct MessageData {
            std::vector<uint8_t> buffer;
            bool processed = false;
        };
        
        std::queue<std::shared_ptr<MessageData>> write_queue_;
        
        void ListenerThread();
        void WriterThread();
        bool CreatePipe();
        bool WaitForConnection();
        bool ReadMessage(std::vector<uint8_t>& buffer);
        bool WriteMessage(const std::vector<uint8_t>& buffer);
        bool WriteMessageAsync(const std::vector<uint8_t>& buffer);
        bool PerformAsyncWrite(std::shared_ptr<MessageData> msg_data);
    };
}
