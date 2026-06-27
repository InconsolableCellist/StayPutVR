#include "OSCManager.hpp"
#include "Logger.hpp"
#include <sstream>
#include <unordered_set>
#include <mutex>

namespace StayPutVR {

OSCManager& OSCManager::GetInstance() {
    static OSCManager instance;
    return instance;
}

OSCManager::~OSCManager() {
    if (initialized_) {
        Shutdown();
    }
}

bool OSCManager::Initialize(const std::string& address, int send_port, int receive_port,
                            bool use_ephemeral_receive_port) {
    if (initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Already initialized with address " + address_ +
                          " (send port: " + std::to_string(send_port_) +
                          ", receive port: " + std::to_string(receive_port_) + ")");
        }
        return true;
    }

    address_ = address;
    send_port_ = send_port;
    receive_port_ = receive_port;
    actual_receive_port_ = receive_port;

    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: WSAStartup failed with error: " + std::to_string(result));
        }
        return false;
    }

    // Create UDP socket for sending
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Socket creation failed with error: " + std::to_string(WSAGetLastError()));
        }
        WSACleanup();
        return false;
    }

    // Allocate and set up the server address structure for sending
    server_addr_ = new sockaddr_in();
    ZeroMemory(server_addr_, sizeof(sockaddr_in));
    server_addr_->sin_family = AF_INET;
    server_addr_->sin_port = htons(static_cast<u_short>(send_port));
    
    // Convert IPv4 address from text to binary form
    result = inet_pton(AF_INET, address.c_str(), &(server_addr_->sin_addr));
    if (result != 1) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: inet_pton failed with error: " + std::to_string(WSAGetLastError()));
        }
        closesocket(socket_);
        delete server_addr_;
        server_addr_ = nullptr;
        WSACleanup();
        return false;
    }
    
    // Create UDP socket for receiving
    receive_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receive_socket_ == INVALID_SOCKET) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Receive socket creation failed with error: " + std::to_string(WSAGetLastError()));
        }
        closesocket(socket_);
        delete server_addr_;
        server_addr_ = nullptr;
        WSACleanup();
        return false;
    }
    
    // Set up the local address structure for receiving. When OSCQuery is on we
    // bind to port 0 so the OS picks a free ephemeral port; this avoids the
    // crash/conflict when another OSC app already holds the fixed port (9001).
    // VRChat then learns the real port via the OSCQuery/mDNS advertisement.
    sockaddr_in local_addr;
    ZeroMemory(&local_addr, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(static_cast<u_short>(use_ephemeral_receive_port ? 0 : receive_port));

    // Bind the receive socket
    result = bind(receive_socket_, (sockaddr*)&local_addr, sizeof(local_addr));
    if (result == SOCKET_ERROR) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: bind failed with error: " + std::to_string(WSAGetLastError()));
        }
        closesocket(socket_);
        closesocket(receive_socket_);
        delete server_addr_;
        server_addr_ = nullptr;
        WSACleanup();
        return false;
    }

    // Read back the actual bound port (matters when binding ephemeral).
    {
        sockaddr_in bound_addr;
        ZeroMemory(&bound_addr, sizeof(bound_addr));
        socklen_t bound_len = sizeof(bound_addr);
        if (getsockname(receive_socket_, (sockaddr*)&bound_addr, &bound_len) == 0) {
            actual_receive_port_ = ntohs(bound_addr.sin_port);
        } else {
            actual_receive_port_ = receive_port;
        }
    }

    // Start the receive thread
    receive_thread_running_ = true;
    try {
        receive_thread_ = std::thread(&OSCManager::ReceiveThreadFunction, this);

        if (Logger::IsInitialized()) {
            Logger::Info("OSCManager: Receive thread started on port " + std::to_string(actual_receive_port_));
        }
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Failed to start receive thread: " + std::string(e.what()));
        }
        receive_thread_running_ = false;
        closesocket(socket_);
        closesocket(receive_socket_);
        delete server_addr_;
        server_addr_ = nullptr;
        WSACleanup();
        return false;
    }
    
    initialized_ = true;

    if (Logger::IsInitialized()) {
        Logger::Info("OSCManager: Initialized with address " + address +
                    " (send port: " + std::to_string(send_port) +
                    ", receive port: " + std::to_string(actual_receive_port_) +
                    (use_ephemeral_receive_port ? " [ephemeral]" : "") + ")");
    }

    return true;
}

void OSCManager::SetSendPort(int send_port) {
    std::lock_guard<std::mutex> lock(send_addr_mutex_);
    send_port_ = send_port;
    if (server_addr_ != nullptr) {
        server_addr_->sin_port = htons(static_cast<u_short>(send_port));
    }
    if (Logger::IsInitialized()) {
        Logger::Info("OSCManager: Send target port updated to " + std::to_string(send_port));
    }
}

void OSCManager::Shutdown() {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Shutdown called but not initialized");
        }
        return;
    }
    
    // Stop the receive thread
    receive_thread_running_ = false;
    
    // Wait for the thread to finish
    if (receive_thread_.joinable()) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Waiting for receive thread to finish...");
        }
        receive_thread_.join();
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Receive thread stopped");
        }
    }

    // Clean up sockets. The send socket and server_addr_ are guarded by
    // send_addr_mutex_ because the OSCQuery browse thread may call SetSendPort();
    // take it here too so teardown can't race a concurrent retarget. (Callers
    // should StopOSCQuery() before Shutdown(), but lock defensively.)
    {
        std::lock_guard<std::mutex> addr_lock(send_addr_mutex_);
        closesocket(socket_);
        delete server_addr_;
        server_addr_ = nullptr;
    }
    closesocket(receive_socket_);

    // Clean up Winsock
    WSACleanup();

    initialized_ = false;

    if (Logger::IsInitialized()) {
        Logger::Info("OSCManager: Shutdown");
        Logger::Debug("OSCManager: OSC client connection closed");
    }
}

void OSCManager::SetConfig(const Config& config) {
    // Update the OSC paths from config
    osc_lock_path_hmd_ = config.osc_lock_path_hmd;
    osc_lock_path_left_hand_ = config.osc_lock_path_left_hand;
    osc_lock_path_right_hand_ = config.osc_lock_path_right_hand;
    osc_lock_path_left_foot_ = config.osc_lock_path_left_foot;
    osc_lock_path_right_foot_ = config.osc_lock_path_right_foot;
    osc_lock_path_hip_ = config.osc_lock_path_hip;
    
    // Update include paths
    osc_include_path_hmd_ = config.osc_include_path_hmd;
    osc_include_path_left_hand_ = config.osc_include_path_left_hand;
    osc_include_path_right_hand_ = config.osc_include_path_right_hand;
    osc_include_path_left_foot_ = config.osc_include_path_left_foot;
    osc_include_path_right_foot_ = config.osc_include_path_right_foot;
    osc_include_path_hip_ = config.osc_include_path_hip;
    
    osc_global_lock_path_ = config.osc_global_lock_path;
    osc_global_unlock_path_ = config.osc_global_unlock_path;
    osc_global_out_of_bounds_path_ = config.osc_global_out_of_bounds_path;
    osc_bite_path_ = config.osc_bite_path;
    osc_shock_path_ = config.osc_shock_path;
    osc_estop_stretch_path_ = config.osc_estop_stretch_path;
    osc_jawopen_path_ = config.osc_jawopen_path;
    osc_collar_toggle_path_ = config.osc_collar_toggle_path;

    if (Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Updated OSC paths from config (jawopen='" +
                      osc_jawopen_path_ + "', collar_toggle='" + osc_collar_toggle_path_ + "')");
    }
}

void OSCManager::ReceiveThreadFunction() {
    if (Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Receive thread started");
    }
    
    // Buffer for incoming data
    std::array<char, MAX_PACKET_SIZE> recv_buffer;
    
    // Set up timeout for recv to allow checking the running flag.
    // Winsock takes a DWORD of milliseconds; POSIX takes a struct timeval.
#ifdef _WIN32
    DWORD timeout = 500; // 500 ms
    setsockopt(receive_socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500 * 1000; // 500 ms
    setsockopt(receive_socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    
    while (receive_thread_running_) {
        // Receive data
        int bytes_received = recv(receive_socket_, recv_buffer.data(), recv_buffer.size(), 0);
        
        if (bytes_received == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT) {
                // Timeout, just continue to check the running flag
                continue;
            }
            else if (error == WSAEMSGSIZE) {
                // Message too large for the buffer
                if (Logger::IsInitialized()) {
                    Logger::Warning("OSCManager: Received an OSC message that exceeds the buffer size (" + 
                                   std::to_string(MAX_PACKET_SIZE) + " bytes). Consider increasing MAX_PACKET_SIZE.");
                }
                // Continue processing despite this error
                continue;
            }
            else {
                if (Logger::IsInitialized()) {
                    Logger::Error("OSCManager: recv failed with error: " + std::to_string(error));
                }
                // Continue running despite errors
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
        }
        
        if (bytes_received > 0) {
            // Record inbound liveness: this is the only signal that VRChat (or
            // any sender) is actually reaching us, since UDP is connectionless.
            last_inbound_ns_.store(
                std::chrono::steady_clock::now().time_since_epoch().count(),
                std::memory_order_relaxed);

            // Process the received OSC message
            ProcessOSCMessage(recv_buffer.data(), bytes_received);
        }
    }
    
    if (Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Receive thread stopped");
    }
}

double OSCManager::SecondsSinceLastInbound() const {
    long long ns = last_inbound_ns_.load(std::memory_order_relaxed);
    if (ns == 0) {
        return -1.0; // nothing received yet
    }
    auto last = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(ns));
    auto elapsed = std::chrono::steady_clock::now() - last;
    return std::chrono::duration<double>(elapsed).count();
}

void OSCManager::ProcessOSCMessage(const char* data, size_t size) {
    try {
        OSCPP::Server::Packet packet(data, size);

        // VRChat batches high-rate parameters (notably face-tracking values like
        // JawOpen) into OSC bundles. Recurse into each contained element; without
        // this the whole bundle is silently dropped and those params never arrive.
        // Each sub-packet's data()/size() points at its true start, so re-parsing
        // correctly re-detects message-vs-bundle (handles nesting too).
        if (packet.isBundle()) {
            OSCPP::Server::Bundle bundle = packet;
            OSCPP::Server::PacketStream packets(bundle.packets());
            while (!packets.atEnd()) {
                OSCPP::Server::Packet p = packets.next();
                ProcessOSCMessage(static_cast<const char*>(p.data()), p.size());
            }
            return;
        }

        if (packet.isMessage()) {
            OSCPP::Server::Message message(packet);
            std::string address = message.address();

            // Diagnostic: log each distinct inbound address exactly once so the
            // log shows precisely what VRChat is sending (and at what path) —
            // e.g. to confirm whether /avatar/parameters/FT/v2/JawOpen arrives.
            // Bounded by the number of unique avatar params, so not spammy.
            {
                static std::mutex seen_mtx;
                static std::unordered_set<std::string> seen_addrs;
                bool is_new;
                {
                    std::lock_guard<std::mutex> lk(seen_mtx);
                    is_new = seen_addrs.insert(address).second;
                }
                if (is_new && Logger::IsInitialized()) {
                    Logger::Debug("OSCManager: first inbound OSC address: " + address);
                }
            }

            // Avatar change: VRChat sends /avatar/change (with the new avatar id
            // as a string argument) when the user switches avatars. The string
            // arg isn't handled by the tag switch below, so dispatch it up front
            // so lock/shock state can be reset. Copy the callback under the lock
            // and invoke it outside the lock (it performs a heavier reset).
            if (address == "/avatar/change") {
                std::function<void()> cb;
                {
                    std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                    cb = avatar_change_callback_;
                }
                if (cb) {
                    if (Logger::IsInitialized()) {
                        Logger::Debug("OSCManager: /avatar/change received");
                    }
                    cb();
                }
                return;
            }

            bool should_log = address.find("SPVR_") != std::string::npos;
            OSCPP::Server::ArgStream args = message.args();
            
            if (!args.atEnd()) {
                bool value_bool = false;
                float float_value = 0.0f;
                char tag = args.tag();
                
                if (tag == 'f') {
                    float_value = args.float32();
                    value_bool = float_value > 0.5f;
                    if (Logger::IsInitialized() && should_log) {
                        Logger::Debug("OSCManager: Received float value: " + std::to_string(float_value) + 
                                    " for address: " + address);
                    }
                }
                else if (tag == 'i') {
                    int32_t value = args.int32();
                    value_bool = value != 0;
                    if (Logger::IsInitialized() && should_log) {
                        Logger::Debug("OSCManager: Received int value: " + std::to_string(value) + 
                                    " for address: " + address);
                    }
                }
                else if (tag == 'T' || tag == 'F') {
                    value_bool = (tag == 'T');
                    if (Logger::IsInitialized() && should_log) {
                        Logger::Debug("OSCManager: Received boolean value: " + std::string(value_bool ? "true" : "false") + 
                                    " for address: " + address);
                    }
                }
                else {
                    if (Logger::IsInitialized() && should_log) {
                        Logger::Warning("OSCManager: Unsupported argument type: " + std::string(1, tag) + 
                                      " for address: " + address);
                    }
                    return;
                }
                
                // Dispatch callbacks under lock to prevent torn reads
                // if a setter is called concurrently from the UI thread.
                std::lock_guard<std::mutex> cb_lock(callback_mutex_);

                // Standard device lock paths
                if (address == osc_lock_path_hmd_ && lock_callback_) {
                    lock_callback_(OSCDeviceType::HMD, value_bool);
                }
                else if (address == osc_lock_path_left_hand_ && lock_callback_) {
                    lock_callback_(OSCDeviceType::ControllerLeft, value_bool);
                }
                else if (address == osc_lock_path_right_hand_ && lock_callback_) {
                    lock_callback_(OSCDeviceType::ControllerRight, value_bool);
                }
                else if (address == osc_lock_path_left_foot_ && lock_callback_) {
                    lock_callback_(OSCDeviceType::FootLeft, value_bool);
                }
                else if (address == osc_lock_path_right_foot_ && lock_callback_) {
                    lock_callback_(OSCDeviceType::FootRight, value_bool);
                }
                else if (address == osc_lock_path_hip_ && lock_callback_) {
                    lock_callback_(OSCDeviceType::Hip, value_bool);
                }
                
                // Global lock/unlock paths
                else if (address == osc_global_lock_path_ && global_lock_callback_ && value_bool) {
                    global_lock_callback_(true);
                }
                else if (address == osc_global_unlock_path_ && global_lock_callback_ && value_bool) {
                    global_lock_callback_(false);
                }
                
                // Global out-of-bounds path
                else if (address == osc_global_out_of_bounds_path_ && global_out_of_bounds_callback_ && value_bool) {
                    global_out_of_bounds_callback_(true);
                }
                
                // Bite path
                else if (address == osc_bite_path_ && bite_callback_ && value_bool) {
                    bite_callback_(true);
                }

                // External shock path (/avatar/parameters/Shock)
                else if (address == osc_shock_path_ && shock_callback_ && value_bool) {
                    shock_callback_(true);
                }
                
                // Emergency stop stretch path
                else if (address == osc_estop_stretch_path_ && estop_stretch_callback_ && tag == 'f') {
                    if (float_value >= 0.5f) {
                        estop_stretch_callback_(float_value);
                    }
                }

                // JawOpen bridge parameter (float 0..1) - SPVR_JawOpen
                else if (address == osc_jawopen_path_ && jawopen_callback_ && tag == 'f') {
                    jawopen_callback_(float_value);
                }

                // Unified collar-mode toggle button (momentary contact). Pass both
                // true and false so the UI can rising-edge detect and advance the mode.
                else if (address == osc_collar_toggle_path_ && collar_toggle_callback_) {
                    collar_toggle_callback_(value_bool);
                }
                
                // Latch_IsPosed paths: direct state change (not toggle)
                else if (address.find("/avatar/parameters/SPVR_") == 0 && address.find("_Latch_IsPosed") != std::string::npos && lock_callback_) {
                    // Extract device type from the path
                    std::string deviceStr = address.substr(25); // Remove "/avatar/parameters/SPVR_" prefix
                    deviceStr = deviceStr.substr(0, deviceStr.find("_Latch_IsPosed")); // Get just the device part
                    
                    // Determine which device type it is
                    OSCDeviceType deviceType;
                    if (deviceStr == "HMD") {
                        deviceType = OSCDeviceType::HMD;
                    } else if (deviceStr == "ControllerLeft") {
                        deviceType = OSCDeviceType::ControllerLeft;
                    } else if (deviceStr == "ControllerRight") {
                        deviceType = OSCDeviceType::ControllerRight;
                    } else if (deviceStr == "FootLeft") {
                        deviceType = OSCDeviceType::FootLeft;
                    } else if (deviceStr == "FootRight") {
                        deviceType = OSCDeviceType::FootRight;
                    } else if (deviceStr == "Hip") {
                        deviceType = OSCDeviceType::Hip;
                    } else {
                        // Unknown device, ignore
                        return;
                    }
                    
                    // Pass the exact boolean state to lock/unlock 
                    lock_callback_(deviceType, value_bool);
                }
                
                // Include paths: toggle behavior
                else if (address.find("/avatar/parameters/SPVR_") == 0 && address.find("_include") != std::string::npos && include_callback_) {
                    // Extract device type from the path
                    std::string deviceStr = address.substr(25); // Remove "/avatar/parameters/SPVR_" prefix
                    deviceStr = deviceStr.substr(0, deviceStr.find("_include")); // Get just the device part
                    
                    // Determine which device type it is
                    OSCDeviceType deviceType;
                    if (deviceStr == "HMD") {
                        deviceType = OSCDeviceType::HMD;
                    } else if (deviceStr == "ControllerLeft") {
                        deviceType = OSCDeviceType::ControllerLeft;
                    } else if (deviceStr == "ControllerRight") {
                        deviceType = OSCDeviceType::ControllerRight;
                    } else if (deviceStr == "FootLeft") {
                        deviceType = OSCDeviceType::FootLeft;
                    } else if (deviceStr == "FootRight") {
                        deviceType = OSCDeviceType::FootRight;
                    } else if (deviceStr == "Hip") {
                        deviceType = OSCDeviceType::Hip;
                    } else {
                        // Unknown device, ignore
                        return;
                    }
                    
                    // If value_bool is true, this should toggle the include state
                    if (value_bool) {
                        include_callback_(deviceType, true); // The callback will handle toggling
                    }
                }
            }
        }
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Error processing OSC message: " + std::string(e.what()));
        }
    }
}

void OSCManager::SendDeviceStatus(OSCDeviceType device, DeviceStatus status) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send device status, OSC is not initialized");
        }
        return;
    }

    std::string path = GetStatusPath(device);
    int statusValue = static_cast<int>(status);

    // Send the device status as an int. This OSC param is written on the local
    // client only and does NOT consume synced parameter space by itself. The 1.4
    // avatar prefab keeps this as a LOCAL animator param and decodes it into 3
    // synced bools (SPVR_<dev>_Status_b0/_b1/_b2) via a parameter-driver layer,
    // which is what actually reduces synced bits (40 -> 15). The pre-1.4 prefab
    // syncs this int directly, so the same message works for both.
    if (SendOSCMessage(path, statusValue) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending status " + std::to_string(statusValue) + " to " + path +
            " (device=" + GetDeviceString(device) + ", status=" + std::to_string(statusValue) + ")");
    }
}

void OSCManager::SendCollarMode(int mode) {
    if (!initialized_) {
        return;
    }
    const std::string path = "/avatar/parameters/SPVR_Collar_Mode";
    if (SendOSCMessage(path, mode) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending collar mode " + std::to_string(mode) + " to " + path);
    }
}

void OSCManager::SendSoundEffect(const std::string& path, int value) {
    if (!initialized_) {
        return;
    }
    SendOSCMessage(path, value);
}

void OSCManager::SendPiShockGroup(int group) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Group, OSC is not initialized");
        }
        return;
    }
    
    std::string path = "/VRCOSC/PiShock/Group";
    
    if (SendOSCMessage(path, group) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Group " + std::to_string(group) + " to " + path + 
            " (int value=" + std::to_string(group) + ")");
    }
}

void OSCManager::SendPiShockDuration(float duration) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Duration, OSC is not initialized");
        }
        return;
    }
    
    // Clamp duration to 0-1 range
    duration = (std::max)(0.0f, (std::min)(duration, 1.0f));
    
    std::string path = "/VRCOSC/PiShock/Duration";
    
    if (SendOSCMessage(path, duration) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Duration " + std::to_string(duration) + " to " + path + 
            " (float value=" + std::to_string(duration) + ", clamped to 0-1 range)");
    }
}

void OSCManager::SendPiShockIntensity(float intensity) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Intensity, OSC is not initialized");
        }
        return;
    }
    
    // Clamp intensity to 0-1 range
    intensity = (std::max)(0.0f, (std::min)(intensity, 1.0f));
    
    std::string path = "/VRCOSC/PiShock/Intensity";
    
    if (SendOSCMessage(path, intensity) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Intensity " + std::to_string(intensity) + " to " + path + 
            " (float value=" + std::to_string(intensity) + ", clamped to 0-1 range)");
    }
}

void OSCManager::SendPiShockShock(bool enabled) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Shock, OSC is not initialized");
        }
        return;
    }
    
    std::string path = "/VRCOSC/PiShock/Shock";
    
    if (SendOSCMessage(path, enabled) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Shock " + std::string(enabled ? "true" : "false") + " to " + path + 
            " (bool value=" + std::string(enabled ? "true" : "false") + ")");
    }
}

void OSCManager::SendPiShockVibrate(bool enabled) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Vibrate, OSC is not initialized");
        }
        return;
    }
    
    std::string path = "/VRCOSC/PiShock/Vibrate";
    
    if (SendOSCMessage(path, enabled) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Vibrate " + std::string(enabled ? "true" : "false") + " to " + path + 
            " (bool value=" + std::string(enabled ? "true" : "false") + ")");
    }
}

void OSCManager::SendPiShockBeep(bool enabled) {
    if (!initialized_) {
        if (Logger::IsInitialized()) {
            Logger::Debug("OSCManager: Cannot send PiShock Beep, OSC is not initialized");
        }
        return;
    }
    
    std::string path = "/VRCOSC/PiShock/Beep";
    
    if (SendOSCMessage(path, enabled) && Logger::IsInitialized()) {
        Logger::Debug("OSCManager: Sending PiShock Beep " + std::string(enabled ? "true" : "false") + " to " + path + 
            " (bool value=" + std::string(enabled ? "true" : "false") + ")");
    }
}

bool OSCManager::SendOSCMessage(const std::string& path, int value) {
    try {
        // Create OSC packet with int32 message
        OSCPP::Client::Packet packet(buffer_.data(), buffer_.size());
        packet.openMessage(path.c_str(), 1)
              .int32(value)
              .closeMessage();

        // Send the packet. Hold send_addr_mutex_ so a concurrent SetSendPort()
        // (from the OSCQuery browse thread) can't tear server_addr_->sin_port.
        std::lock_guard<std::mutex> lock(send_addr_mutex_);
        int result = sendto(socket_,
                     static_cast<const char*>(packet.data()),
                     static_cast<int>(packet.size()),
                     0,
                     reinterpret_cast<struct sockaddr*>(server_addr_),
                     sizeof(sockaddr_in));
                     
        if (result == SOCKET_ERROR) {
            if (Logger::IsInitialized()) {
                Logger::Error("OSCManager: Failed to send OSC message, error: " + std::to_string(WSAGetLastError()));
            }
            return false;
        }
        
        return true;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Exception when sending OSC message: " + std::string(e.what()));
        }
        return false;
    }
}

bool OSCManager::SendOSCMessage(const std::string& path, float value) {
    try {
        // Create OSC packet with float message
        OSCPP::Client::Packet packet(buffer_.data(), buffer_.size());
        packet.openMessage(path.c_str(), 1)
              .float32(value)
              .closeMessage();

        // Send the packet. Hold send_addr_mutex_ so a concurrent SetSendPort()
        // (from the OSCQuery browse thread) can't tear server_addr_->sin_port.
        std::lock_guard<std::mutex> lock(send_addr_mutex_);
        int result = sendto(socket_,
                     static_cast<const char*>(packet.data()),
                     static_cast<int>(packet.size()),
                     0,
                     reinterpret_cast<struct sockaddr*>(server_addr_),
                     sizeof(sockaddr_in));
                     
        if (result == SOCKET_ERROR) {
            if (Logger::IsInitialized()) {
                Logger::Error("OSCManager: Failed to send OSC message, error: " + std::to_string(WSAGetLastError()));
            }
            return false;
        }
        
        return true;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("OSCManager: Exception when sending OSC message: " + std::string(e.what()));
        }
        return false;
    }
}

bool OSCManager::SendOSCMessage(const std::string& path, bool value) {
    // OSC doesn't have a native bool type, so we send as int (1 for true, 0 for false)
    return SendOSCMessage(path, value ? 1 : 0);
}

std::string OSCManager::GetDeviceString(OSCDeviceType device) const {
    switch (device) {
        case OSCDeviceType::HMD: return "HMD";
        case OSCDeviceType::ControllerLeft: return "ControllerLeft";
        case OSCDeviceType::ControllerRight: return "ControllerRight";
        case OSCDeviceType::FootLeft: return "FootLeft";
        case OSCDeviceType::FootRight: return "FootRight";
        case OSCDeviceType::Hip: return "Hip";
        case OSCDeviceType::Jaw: return "Jaw";
        case OSCDeviceType::Mic: return "Mic";
        default: return "Unknown";
    }
}

std::string OSCManager::GetStatusPath(OSCDeviceType device) const {
    return "/avatar/parameters/SPVR_" + GetDeviceString(device) + "_Status";
}

std::string OSCManager::GetLockPath(OSCDeviceType device) const {
    return "/avatar/parameters/SPVR_" + GetDeviceString(device) + "_Latch_IsPosed";
}

std::string OSCManager::GetIncludePath(OSCDeviceType device) const {
    return "/avatar/parameters/SPVR_" + GetDeviceString(device) + "_include";
}

std::string OSCManager::GetRoleString(DeviceRole role) const {
    switch (role) {
        case DeviceRole::HMD: return "HMD";
        case DeviceRole::LeftController: return "LeftController";
        case DeviceRole::RightController: return "RightController";
        case DeviceRole::Hip: return "Hip";
        case DeviceRole::LeftFoot: return "LeftFoot";
        case DeviceRole::RightFoot: return "RightFoot";
        default: return "None";
    }
}

} // namespace StayPutVR 