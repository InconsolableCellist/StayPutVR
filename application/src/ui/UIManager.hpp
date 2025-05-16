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

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "../../../common/DeviceTypes.hpp"
#include "../../../common/Config.hpp"
#include "../../../common/Audio.hpp"
#include "../../../common/Logger.hpp"
#include "../../../common/PathUtils.hpp"
#include "../DeviceManager/DeviceManager.hpp"

namespace StayPutVR {

    // Define tab identifiers
    enum class TabType {
        MAIN,
        DEVICES,
        BOUNDARIES,
        NOTIFICATIONS,
        TIMERS,
        OSC,
        SETTINGS
    };

    struct DevicePosition {
        std::string serial;
        DeviceType type;
        std::string device_name; // Custom name for the device
        float position[3];          // Current position
        float rotation[4];          // Current rotation (quaternion)
        
        bool locked = false;        // Whether the position is locked
        bool include_in_locking = false; // Whether to include this device in global locking
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
        std::string config_dir_ = "configs";
        std::string current_config_file_ = "";
        
        // Global locking settings
        bool global_lock_active_ = false;
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
        void CheckDevicePositionDeviations();
        
        // Timestamp of last played sound for rate limiting
        std::chrono::steady_clock::time_point last_sound_time_ = std::chrono::steady_clock::now();
        
        // DeviceManager reference
        DeviceManager* device_manager_ = nullptr;
    };
} 