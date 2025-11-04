#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <array>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "../../../common/DeviceTypes.hpp"
#include "../../../common/Config.hpp"
#include "../../../common/Audio.hpp"
#include "../../../common/Logger.hpp"
#include "../../../common/PathUtils.hpp"
#include "../DeviceManager/DeviceManager.hpp"
#include "../../../common/OSCManager.hpp"
#include "../managers/TwitchManager.hpp"
#include "../managers/PiShockManager.hpp"
#include "../managers/PiShockWebSocketManager.hpp"
#include "../managers/OpenShockManager.hpp"
#include "../managers/ButtplugManager.hpp"

namespace StayPutVR {

    // Define tab identifiers
    enum class TabType {
        MAIN,
        DEVICES,
        BOUNDARIES,
        NOTIFICATIONS,
        TIMERS,
        OSC,
        SETTINGS,
        PISHOCK,
        OPENSHOCK,
        BUTTPLUG,
        TWITCH
    };

    struct DevicePosition {
        std::string serial;
        DeviceType type;
        std::string device_name; // Custom name for the device
        float position[3];          // Current position
        float rotation[4];          // Current rotation (quaternion)
        
        bool locked = false;        // Whether the position is locked
        bool include_in_locking = false; // Whether to include this device in global locking
        DeviceRole role = DeviceRole::None; // Assigned role for the device (HMD, LeftController, etc.)
        float original_position[3]; // Original position when locked
        float original_rotation[4]; // Original rotation when locked
        
        // Offset applied when device is locked
        float position_offset[3] = {0.0f, 0.0f, 0.0f};
        float rotation_offset[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // Identity quaternion
        
        // Time of last position update
        std::chrono::steady_clock::time_point last_update_time = std::chrono::steady_clock::now();
        
        // Previous position for detecting changes
        float previous_position[3] = {0.0f, 0.0f, 0.0f};
        
        // Position deviation from locked position
        float position_deviation = 0.0f;
        bool exceeds_threshold = false;
        bool in_warning_zone = false;
        
        // OpenShock/PiShock device selection - which shock IDs should be used for this device
        std::array<bool, 5> shock_device_enabled = {false, false, false, false, false};
        
        // Buttplug device selection - which vibration IDs should be used for this device
        std::array<bool, 5> vibration_device_enabled = {false, false, false, false, false};
    };

    struct SimpleDevicePosition {
        std::string serial;
        float x;
        float y;
        float z;
    };

    class UIManager {
    public:
        UIManager();
        ~UIManager();

        bool Initialize();
        void Update();
        void Render();
        void Shutdown();
        
        // Update device positions from device manager
        void UpdateDevicePositions(const std::vector<DevicePositionData>& devices);
        
        // Save and load positions
        bool SaveDevicePositions(const std::string& filename);
        bool LoadDevicePositions(const std::string& filename);
        
        // Load and save application configuration
        bool LoadConfig();
        bool SaveConfig();
        
    private:
        // Main window
        GLFWwindow* window_;
        
        // ImGui contexts
        ImGuiContext* imgui_context_;
        
        // Window settings
        int window_width_ = 800;
        int window_height_ = 600;
        std::string window_title_ = "StayPutVR";
        
        // Device data
        std::vector<DevicePosition> device_positions_;
        std::unordered_map<std::string, size_t> device_map_; // Maps serial to index in device_positions_
        
        // Saved configurations directory
        std::string config_dir_ = "config";
        std::string current_config_file_ = "";
        
        // Global locking settings
        bool global_lock_active_ = false;
        bool emergency_stop_active_ = false;
        float position_threshold_ = 0.2f; // Out of bounds threshold in meters
        float warning_threshold_ = 0.1f;  // Warning threshold in meters
        float disable_threshold_ = 0.8f;  // Disable threshold in meters
        
        // Application configuration
        Config config_;
        std::string config_file_ = "config.ini"; // Just the filename, not the full path
        
        // OSC status
        bool osc_enabled_ = false;
        
        // Tab system
        TabType current_tab_ = TabType::MAIN;
        
        // Static callbacks for GLFW
        static void GlfwErrorCallback(int error, const char* description);
        
        // UI elements
        void RenderMainWindow();
        void RenderTabBar();
        
        // Tab content rendering methods
        void RenderMainTab();
        void RenderDevicesTab();
        void RenderBoundariesTab();
        void RenderNotificationsTab();
        void RenderTimersTab();
        void RenderOSCTab();
        void RenderSettingsTab();
        void RenderPiShockTab();
        void RenderOpenShockTab();
        void RenderButtplugTab();
        void RenderTwitchTab();
        
        // Original UI elements (to be migrated to tabs)
        void RenderDeviceList();
        void RenderConfigControls();
        void RenderLockControls();
        
        // Flag for window closing
        std::atomic<bool>* running_ptr_;
        
        // Configuration management
        void UpdateConfigFromUI();
        void UpdateUIFromConfig();
        
        // Device position handling
        void LockDevicePosition(const std::string& serial, bool lock);
        void ResetAllDevices();
        void ApplyLockedPositions();
        void ActivateGlobalLock(bool activate);
        void ActivateGlobalLockInternal(bool activate);
        void CheckDevicePositionDeviations();
        
        // Timestamp of last played sound for rate limiting
        std::chrono::steady_clock::time_point last_sound_time_ = std::chrono::steady_clock::now();
        
        DeviceManager* device_manager_ = nullptr;
        
        std::unique_ptr<TwitchManager> twitch_manager_;
        
        std::unique_ptr<PiShockManager> pishock_manager_;
        std::unique_ptr<PiShockWebSocketManager> pishock_ws_manager_;
        
        std::unique_ptr<OpenShockManager> openshock_manager_;
        
        std::unique_ptr<ButtplugManager> buttplug_manager_;
        
        // Countdown timer variables
        bool countdown_active_ = false;
        float countdown_remaining_ = 0.0f;
        std::chrono::steady_clock::time_point countdown_last_beep_;
        
        // OSC callbacks
        void OnDeviceLocked(OSCDeviceType device, bool locked);
        void OnDeviceIncluded(OSCDeviceType device, bool include);
        void TriggerGlobalOutOfBoundsActions();
        void TriggerBiteActions();
        void ResetEmergencyStop();
        
        // Helper functions
        void UpdateDeviceStatus(OSCDeviceType device, DeviceStatus status);
        void HandleOSCConnection();
        void DisconnectOSC();
        void VerifyOSCCallbacks();
        
        // Helper function to map DeviceType to OSCDeviceType
        OSCDeviceType MapToOSCDeviceType(DeviceType type);
        
        // Helper function to convert OSCDeviceType to string
        std::string GetOSCDeviceString(OSCDeviceType device) const;
        
        // Helper function to convert DeviceRole to OSCDeviceType
        OSCDeviceType DeviceRoleToOSCDeviceType(DeviceRole role) const;
        
        void InitializePiShockManager();
        void InitializePiShockWebSocketManager();
        void ShutdownPiShockManager();
        void TriggerPiShockDisobedience(const std::string& device_serial);
        void TriggerPiShockWarning(const std::string& device_serial);
        bool CanTriggerPiShock() const;
        
        void InitializeOpenShockManager();
        void ShutdownOpenShockManager();
        
        void InitializeButtplugManager();
        void ShutdownButtplugManager();
        
        // Twitch helper functions
        void InitializeTwitchManager();
        void ShutdownTwitchManager();
        void ProcessTwitchUnlockTimer();
        
        // Global out-of-bounds timer helper
        void ProcessGlobalOutOfBoundsTimer();
        void ProcessBiteTimer();
        
        // Twitch unlock timer variables
        bool twitch_unlock_timer_active_ = false;
        float twitch_unlock_timer_remaining_ = 0.0f;
        std::chrono::steady_clock::time_point twitch_unlock_timer_start_;
        
        // Global out-of-bounds timer variables
        bool global_out_of_bounds_timer_active_ = false;
        std::chrono::steady_clock::time_point global_out_of_bounds_timer_start_;
        static constexpr float GLOBAL_OUT_OF_BOUNDS_DURATION = 1.0f; // Duration in seconds
        
        // Bite timer variables
        bool bite_timer_active_ = false;
        std::chrono::steady_clock::time_point bite_timer_start_;
        static constexpr float BITE_DURATION = 3.0f; // Duration in seconds
        
        // Twitch donation callbacks
            void OnTwitchDonation(const std::string& username, float amount, const std::string& message);
    void OnTwitchBits(const std::string& username, int bits, const std::string& message);
    void OnTwitchSubscription(const std::string& username, int months, bool is_gift);
    void OnTwitchChatCommand(const std::string& username, const std::string& command, const std::string& args);
        
        // Last time OSC lock was toggled (for debouncing)
        std::chrono::steady_clock::time_point last_osc_toggle_time_;
    };
} 