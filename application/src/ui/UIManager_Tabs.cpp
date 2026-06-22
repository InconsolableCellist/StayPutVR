#include "UIManager.hpp"
#include "ImGuiHelpers.hpp"
#include <iostream>
#include <string>
#include <format>
#include <algorithm>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include "stb/stb_image.h" // effigy PNG decode (impl is in stb_image_impl.cpp)
#include <nlohmann/json.hpp>
#include "../../common/Logger.hpp"
#include "../../common/PathUtils.hpp"
#include "../../common/Audio.hpp"
#ifdef _WIN32
#include <shellapi.h> // For ShellExecuteA
#else
#include <cstdlib> // For std::system (xdg-open)
#endif
#include <thread> // For std::this_thread::sleep_for
#include "../../../common/OSCManager.hpp"
#include "../../common/HttpClient.hpp"

// Windows-specific includes for icon handling
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "../resource.h"
#endif

using json = nlohmann::json;

extern std::atomic<bool> g_running;

namespace StayPutVR {

    void UIManager::RenderMainTab() {
        ImGui::Text("StayPutVR Status");
        ImGui::Separator();

        // Splash / What's New recall + auto-close preference.
        if (ImGui::SmallButton("Welcome / About")) {
            if (splash_) splash_->Reshow();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("What's New")) {
            OpenWhatsNew();
        }
        ImGui::SameLine();
        bool auto_close = config_.splash_auto_close;
        if (ImGui::Checkbox("Auto-close splash", &auto_close)) {
            config_.splash_auto_close = auto_close;
            if (splash_) splash_->SetAutoClose(auto_close);
            SaveConfig();
        }
        ImGui::Separator();

        // Connection status panel at the top. Auto-resize height so the
        // not-connected content (message + retry button) always shows in full
        // instead of scrolling inside a fixed 60px box.
        ImGui::BeginChild("ConnectionPanel", ImVec2(0, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        
        bool isConnected = device_manager_ && device_manager_->IsConnected();
        
        if (isConnected) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected to driver");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Not connected to driver");
            ImGui::Text("The driver is not running or could not be reached");
            
            if (ImGui::Button("Retry Connection")) {
                if (device_manager_) {
                    if (device_manager_->ManualReconnect()) {
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Info("Successfully reconnected to driver");
                        }
                    } else {
                        if (StayPutVR::Logger::IsInitialized()) {
                            StayPutVR::Logger::Warning("Failed to reconnect to driver");
                        }
                    }
                }
            }
        }
        
        ImGui::EndChild();

        // OSC quick controls so users don't need to open Settings > OSC just to
        // start listening. Mirrors the enable/disable button on the OSC sub-tab.
        ImGui::BeginChild("OSCQuickPanel", ImVec2(0, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
        ImGui::Text("OSC");
        ImGui::SameLine();
        if (osc_enabled_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Disable OSC", ImVec2(130, 0))) {
                DisconnectOSC();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Active - listening for OSC");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("Enable OSC", ImVec2(130, 0))) {
                HandleOSCConnection();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Inactive");
        }

        // Avatar trigger toggles (intensity/duration live in Integrations > OSC Triggers).
        if (ImGui::Checkbox("Bite trigger", &config_.osc_bite_enabled)) {
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Shock trigger", &config_.osc_shock_enabled)) {
            SaveConfig();
        }
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Fire a shock from avatar parameters. Tune intensity/duration in "
            "Integrations > OSC Triggers; change parameter paths in Settings > OSC.");
        ImGui::EndChild();

        // Emergency Stop Status Panel
        if (emergency_stop_active_) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.8f, 0.2f, 0.2f, 0.3f)); // Red background
            ImGui::BeginChild("EmergencyStopPanel", ImVec2(0, 80), true);
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f)); // Red text
            ImGui::Text("⚠️ EMERGENCY STOP MODE ACTIVE ⚠️");
            ImGui::PopStyleColor();
            
            ImGui::Text("All locking and disobedience actions are disabled.");
            ImGui::Text("All devices have been unlocked and cannot be re-locked.");
            
            ImGui::Spacing();
            
            if (ImGui::Button("Reset Emergency Stop", ImVec2(200, 30))) {
                ResetEmergencyStop();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Click to resume normal operation");
            
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        
        // Layout with two columns
        ImGui::Columns(2, "MainTabColumns", false);
        
        // Status panel on the left column
        ImGui::BeginChild("StatusPanel", ImVec2(0, 200), true);
        ImGui::Text("Current Status:");
        ImGui::Separator();
        
        // Display active device count
        int active_devices = 0;
        int locked_devices = 0;
        int warning_devices = 0;
        int out_of_bounds_devices = 0;
        
        for (const auto& device : device_positions_) {
            active_devices++;
            if (device.locked || (device.include_in_locking && global_lock_active_)) {
                locked_devices++;
                
                // Calculate position deviation to check for warning/out of bounds
                float deviation = 0.0f;
                for (int i = 0; i < 3; i++) {
                    float diff = device.position[i] - device.original_position[i];
                    deviation += diff * diff;
                }
                deviation = std::sqrt(deviation);
                
                // Use the actual thresholds
                if (deviation > position_threshold_) {
                    out_of_bounds_devices++;
                } else if (deviation > warning_threshold_) {
                    warning_devices++;
                }
            }
        }
        
        ImGui::Text("Active Devices: %d", active_devices);
        ImGui::Text("Locked Devices: %d", locked_devices);
        
        if (global_lock_active_) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Global Lock Active");
        }
        
        // Count individually locked devices
        int individually_locked = 0;
        for (const auto& device : device_positions_) {
            if (device.locked) {
                individually_locked++;
            }
        }
        
        if (individually_locked > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), 
                "%d device(s) individually locked", individually_locked);
        }
        
        // Show any warnings
        if (warning_devices > 0) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
                "%d device(s) in warning zone", warning_devices);
        }
        
        if (out_of_bounds_devices > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                "%d device(s) out of bounds", out_of_bounds_devices);
        }
        
        if (locked_devices > 0 && warning_devices == 0 && out_of_bounds_devices == 0) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "All devices within safe zone");
        }
        
        ImGui::EndChild();
        
        ImGui::NextColumn();
        
        // Boundary Configuration (moved from Boundaries tab)
        ImGui::Text("Boundary Thresholds:");
        ImGui::Separator();
        
        bool changed = false;
        
        // Warning distance
        ImGui::Text("Safe Zone Radius:");
        if (ImGui::SliderFloat("##WarningDistance", &warning_threshold_, 0.01f, 0.5f, "%.2f m")) {
            config_.warning_threshold = warning_threshold_;
            changed = true;
        }
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Distance at which warning feedback begins");
        
        // Out of bounds distance
        ImGui::Text("Warning Zone Radius:");
        if (ImGui::SliderFloat("##OutOfBoundsDistance", &position_threshold_, 0.05f, 1.0f, "%.2f m")) {
            config_.bounds_threshold = position_threshold_;
            changed = true;
        }
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Distance at which the device is considered out of bounds");
        
        // Disable threshold
        ImGui::Text("Disable Distance:");
        if (ImGui::SliderFloat("##DisableThreshold", &disable_threshold_, 0.2f, 2.0f, "%.2f m")) {
            config_.disable_threshold = disable_threshold_;
            changed = true;
        }
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Distance at which locking is automatically disabled (safety feature)");
        
        // Save changes if needed
        if (changed) {
            SaveConfig();
        }
        
        ImGui::Columns(1);
        
        // Device visualization with all boundary rings
        ImGui::BeginChild("DeviceVisualization", ImVec2(0, 300), true);
        ImGui::Text("Device Positions:");
        ImGui::Separator();
        
        RenderZoneMap();
        
        ImGui::EndChild();
        
        // Add the lock button at the bottom of the Main tab
        ImGui::Separator();
        ImGui::Text("Global Lock Controls:");
        ImGui::TextWrapped("This affects all devices marked as 'Will Lock' in the Devices tab. Individual devices can be locked/unlocked in the Devices tab.");

        if (!global_lock_active_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f)); // Green
            if (ImGui::Button("Lock All Included Devices", ImVec2(250, 40))) {
                ActivateGlobalLock(true);
            }
            ImGui::PopStyleColor();
            
            // Prompt to select devices if none are selected
            int devices_to_lock = 0;
            for (const auto& device : device_positions_) {
                if (device.include_in_locking) {
                    devices_to_lock++;
                }
            }
            
            if (devices_to_lock == 0) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Select devices to lock in the Devices tab first");
            } else {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 0.7f, 0.0f, 1.0f), "%d device(s) will be locked", devices_to_lock);
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)); // Red
            if (ImGui::Button("Unlock All Included Devices", ImVec2(250, 40))) {
                ActivateGlobalLock(false);
            }
            ImGui::PopStyleColor();
            
            // Count locked devices
            int locked_devices = 0;
            for (const auto& device : device_positions_) {
                if (device.include_in_locking) {
                    locked_devices++;
                }
            }
            
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%d device(s) will be unlocked", locked_devices);
        }
    }

    void UIManager::RenderNotificationsTab() {
        ImGui::Text("Notification Settings");
        ImGui::Separator();
        
        bool changed = false;
        
        // Audio notification settings
        ImGui::Text("Audio Notifications");
        ImGui::Separator();
        
        bool enable_audio = config_.audio.enabled;
        if (ImGui::Checkbox("Enable Audio Notifications", &enable_audio)) {
            config_.audio.enabled = enable_audio;
            changed = true;
        }
        
        // Audio volume
        float audio_volume = config_.audio.volume;
        if (ImGui::SliderFloat("Audio Volume", &audio_volume, 0.0f, 1.0f, "%.1f")) {
            config_.audio.volume = audio_volume;
            changed = true;
        }
        
        // Audio cue types
        bool warning_audio = config_.audio.warning;
        if (ImGui::Checkbox("Warning Sound", &warning_audio)) {
            config_.audio.warning = warning_audio;
            changed = true;
        }
        
        bool out_of_bounds_audio = config_.audio.out_of_bounds;
        if (ImGui::Checkbox("Out of Bounds Sound", &out_of_bounds_audio)) {
            config_.audio.out_of_bounds = out_of_bounds_audio;
            changed = true;
        }
        
        bool lock_audio = config_.audio.lock;
        if (ImGui::Checkbox("Lock Sound", &lock_audio)) {
            config_.audio.lock = lock_audio;
            changed = true;
        }
        
        bool unlock_audio = config_.audio.unlock;
        if (ImGui::Checkbox("Unlock Sound", &unlock_audio)) {
            config_.audio.unlock = unlock_audio;
            changed = true;
        }
        
        // Test buttons for sound effects
        ImGui::Separator();
        ImGui::Text("Test Audio:");
        
        // First row of buttons
        if (ImGui::Button("Test Warning Sound")) {
            AudioManager::PlayWarningSound(config_.audio.volume);
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Disobedience Sound")) {
            std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\disobedience.wav";
            if (std::filesystem::exists(filePath)) {
                AudioManager::PlaySound("disobedience.wav", config_.audio.volume);
            } else {
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Warning("disobedience.wav not found, please add it to the resources folder");
                }
                // Show a message in the UI that the file is missing
                ImGui::OpenPopup("Disobedience Sound Missing");
            }
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Success Sound")) {
            std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\success.wav";
            if (std::filesystem::exists(filePath)) {
                AudioManager::PlaySound("success.wav", config_.audio.volume);
            } else {
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Warning("success.wav not found, please add it to the resources folder");
                }
                // Show a message in the UI that the file is missing
                ImGui::OpenPopup("Success Sound Missing");
            }
        }
        
        // Second row of buttons
        if (ImGui::Button("Test Lock Sound")) {
            AudioManager::PlayLockSound(config_.audio.volume);
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Unlock Sound")) {
            AudioManager::PlayUnlockSound(config_.audio.volume);
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Countdown Sound")) {
            std::string filePath = StayPutVR::GetAppDataPath() + "\\resources\\countdown.wav";
            if (std::filesystem::exists(filePath)) {
                AudioManager::PlaySound("countdown.wav", config_.audio.volume);
            } else {
                if (StayPutVR::Logger::IsInitialized()) {
                    StayPutVR::Logger::Warning("countdown.wav not found, please add it to the resources folder");
                }
                // Show a message in the UI that the file is missing
                ImGui::OpenPopup("Countdown Sound Missing");
            }
        }
        
        // Add all popup blocks after the buttons
        if (ImGui::BeginPopup("Disobedience Sound Missing")) {
            ImGui::Text("disobedience.wav not found in resources folder");
            ImGui::Text("Please add the file to:");
            ImGui::Text("%s\\resources\\", StayPutVR::GetAppDataPath().c_str());
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopup("Success Sound Missing")) {
            ImGui::Text("success.wav not found in resources folder");
            ImGui::Text("Please add the file to:");
            ImGui::Text("%s\\resources\\", StayPutVR::GetAppDataPath().c_str());
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopup("Countdown Sound Missing")) {
            ImGui::Text("countdown.wav not found in resources folder");
            ImGui::Text("Please add the file to:");
            ImGui::Text("%s\\resources\\", StayPutVR::GetAppDataPath().c_str());
            ImGui::EndPopup();
        }
        
        // Save changes if anything was modified
        if (changed) {
            SaveConfig();
        }
    }

    void UIManager::RenderTimersTab() {
        ImGui::Text("Timer Settings");
        ImGui::Separator();
        
        bool changed = false;
        
        // Cooldown timeout
        ImGui::Text("Cooldown Timer");
        ImGui::Separator();
        
        bool cooldown_enabled = config_.cooldown_enabled;
        if (ImGui::Checkbox("Enable Cooldown After Unlock", &cooldown_enabled)) {
            config_.cooldown_enabled = cooldown_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("After unlocking, disables being locked again for this amount of time");
        
        float cooldown_seconds = config_.cooldown_seconds;
        if (ImGui::SliderFloat("Cooldown Duration", &cooldown_seconds, 1.0f, 60.0f, "%.1f seconds")) {
            config_.cooldown_seconds = cooldown_seconds;
            changed = true;
        }
        
        // Countdown timer
        ImGui::Text("Countdown Timer");
        ImGui::Separator();
        
        bool countdown_enabled = config_.countdown_enabled;
        if (ImGui::Checkbox("Enable Countdown Before Lock", &countdown_enabled)) {
            config_.countdown_enabled = countdown_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("When enabled, a 3-second countdown sound will play before locking devices");
        
        ImGui::Text("Countdown plays a 3-second countdown sound before locking devices.");
        
        ImGui::Spacing();
        ImGui::Separator();
        
        // Unlock Timer
        ImGui::Text("Unlock Timer");
        ImGui::Separator();
        
        bool unlock_timer_enabled = config_.unlock_timer_enabled;
        if (ImGui::Checkbox("Enable Automatic Unlock Timer", &unlock_timer_enabled)) {
            config_.unlock_timer_enabled = unlock_timer_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Automatically unlock devices after a specified duration (useful for Twitch integrations)");
        
        ImGui::BeginDisabled(!config_.unlock_timer_enabled);
        
        float unlock_timer_minutes = config_.unlock_timer_duration / 60.0f;
        if (ImGui::SliderFloat("Unlock Timer Duration", &unlock_timer_minutes, 1.0f, 60.0f, "%.1f minutes")) {
            config_.unlock_timer_duration = unlock_timer_minutes * 60.0f;
            changed = true;
        }
        
        bool unlock_timer_show_remaining = config_.unlock_timer_show_remaining;
        if (ImGui::Checkbox("Show Remaining Time", &unlock_timer_show_remaining)) {
            config_.unlock_timer_show_remaining = unlock_timer_show_remaining;
            changed = true;
        }
        
        bool unlock_timer_audio_warnings = config_.unlock_timer_audio_warnings;
        if (ImGui::Checkbox("Audio Warnings", &unlock_timer_audio_warnings)) {
            config_.unlock_timer_audio_warnings = unlock_timer_audio_warnings;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Play audio warnings at 60s, 30s, and 10s remaining");
        
        // Show current unlock timer status if active
        if (twitch_unlock_timer_active_) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Unlock Timer Active!");
            ImGui::Text("Time remaining: %.1f seconds", twitch_unlock_timer_remaining_);
            
            if (ImGui::Button("Cancel Unlock Timer")) {
                twitch_unlock_timer_active_ = false;
                twitch_unlock_timer_remaining_ = 0.0f;
            }
        }
        
        ImGui::EndDisabled();
        
        ImGui::Spacing();
        ImGui::Separator();
        
        ImGui::Text("Shock Cooldown Timer");
        ImGui::Separator();
        
        bool shock_cooldown_enabled = config_.shock_cooldown_enabled;
        if (ImGui::Checkbox("Enable Shock Cooldown", &shock_cooldown_enabled)) {
            config_.shock_cooldown_enabled = shock_cooldown_enabled;
            changed = true;
        }
        
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Prevents shocks from being sent within the cooldown window for both PiShock and OpenShock");
        
        ImGui::BeginDisabled(!config_.shock_cooldown_enabled);
        
        float shock_cooldown_seconds = config_.shock_cooldown_seconds;
        if (ImGui::SliderFloat("Shock Cooldown Duration", &shock_cooldown_seconds, 1.0f, 60.0f, "%.0f seconds")) {
            config_.shock_cooldown_seconds = shock_cooldown_seconds;
            changed = true;
        }
        
        ImGui::Text("Shocks will be blocked if sent within %.0f seconds of the last shock.", config_.shock_cooldown_seconds);
        
        ImGui::EndDisabled();
        
        // Save changes if anything was modified
        if (changed) {
            SaveConfig();
        }
    }

    void UIManager::RenderSettingsTab() {
        bool changed = false;

        // ---- First-class: UI font size ----
        ImGui::Text("UI font size:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderFloat("##FontScale", &config_.ui_font_scale, 0.70f, 1.60f, "%.2fx")) {
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##font")) {
            config_.ui_font_scale = 1.0f;
            changed = true;
        }

        // ---- First-class: logging ----
        {
            const char* log_levels[] = { "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL" };
            static int current_log_level = 0;
            for (int i = 0; i < IM_ARRAYSIZE(log_levels); i++) {
                if (config_.log_level == log_levels[i]) { current_log_level = i; break; }
            }
            ImGui::Text("Log level:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160);
            if (ImGui::Combo("##LogLevel", &current_log_level, log_levels, IM_ARRAYSIZE(log_levels))) {
                config_.log_level = log_levels[current_log_level];
                StayPutVR::Logger::SetLogLevel(StayPutVR::Logger::StringToLogLevel(config_.log_level));
                Logger::Info("Log level changed to: " + config_.log_level);
                changed = true;
            }
            ImGui::SameLine();
            ImGuiHelpers::HelpTooltip("DEBUG: most verbose. INFO: informational and above. "
                "WARNING: warnings + errors (default). ERROR: errors only. CRITICAL: most severe only.");
        }

        // ---- First-class: About ----
        ImGui::Text("StayPutVR 1.4");
        ImGui::SameLine();
        ImGui::TextDisabled("(c) 2026 Foxipso");
        ImGui::SameLine();
        if (ImGui::SmallButton("foxipso.com")) {
#ifdef _WIN32
            ShellExecuteA(NULL, "open", "https://foxipso.com", NULL, NULL, SW_SHOWDEFAULT);
#else
            (void)std::system("xdg-open 'https://foxipso.com' >/dev/null 2>&1 &");
#endif
        }

        ImGui::Separator();

        // ---- Sub-tabs ----
        if (ImGui::BeginTabBar("SettingsSubTabs")) {
            if (ImGui::BeginTabItem("OSC")) {
                RenderOSCTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Timers")) {
                RenderTimersTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Notifications")) {
                RenderNotificationsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Folders")) {
                if (ImGui::CollapsingHeader("Configuration Profiles")) {
                    RenderConfigControls();
                }
                if (ImGui::CollapsingHeader("Data Folders", ImGuiTreeNodeFlags_DefaultOpen)) {
                    std::string appDataPath = GetAppDataPath();
                    std::string logPath = appDataPath + "/logs";
                    std::string configPath = appDataPath + "/config";
                    std::string resourcesPath = appDataPath + "/resources";
                    ImGui::Text("Settings Path: %s", configPath.c_str());
                    ImGui::Text("Log Path: %s", logPath.c_str());
                    ImGui::Text("Resources Path: %s", resourcesPath.c_str());
                    if (ImGui::Button("Open Settings Folder")) {
#ifdef _WIN32
                        ShellExecuteA(NULL, "open", configPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#else
                        (void)std::system(("xdg-open '" + configPath + "' >/dev/null 2>&1 &").c_str());
#endif
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Open Log Folder")) {
#ifdef _WIN32
                        ShellExecuteA(NULL, "open", logPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#else
                        (void)std::system(("xdg-open '" + logPath + "' >/dev/null 2>&1 &").c_str());
#endif
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Open Resources Folder")) {
#ifdef _WIN32
                        ShellExecuteA(NULL, "open", resourcesPath.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#else
                        (void)std::system(("xdg-open '" + resourcesPath + "' >/dev/null 2>&1 &").c_str());
#endif
                    }
                }
                if (ImGui::CollapsingHeader("Audio Resources")) {
                    ImGui::TextWrapped("To use custom sounds, place these in the Resources folder:");
                    ImGui::BulletText("warning.wav - device in warning zone");
                    ImGui::BulletText("disobedience.wav - device exceeds bounds");
                    ImGui::BulletText("success.wav - device returns to safe zone");
                    ImGui::BulletText("lock.wav / unlock.wav - on lock / unlock");
                    ImGui::BulletText("countdown.wav - during countdown timer");
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (changed) {
            SaveConfig();
        }
    }

    void UIManager::RenderConfigControls() {
        // Check for existing config files
        std::vector<std::string> config_files;
        
        // Ensure the config directory exists
        std::filesystem::create_directories(config_dir_);
        
        for (const auto& entry : std::filesystem::directory_iterator(config_dir_)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                config_files.push_back(entry.path().stem().string());
            }
        }
        
        ImGui::PushID("ConfigSection");

        // Load configuration
        if (!config_files.empty()) {
            static int selected_config = -1;
            if (selected_config >= (int)config_files.size()) {
                selected_config = -1;
            }
            
            ImGui::Text("Load Configuration:");
            
            if (ImGui::BeginListBox("##ConfigList")) {
                for (int i = 0; i < config_files.size(); i++) {
                    const bool is_selected = (selected_config == i);
                    if (ImGui::Selectable(config_files[i].c_str(), is_selected)) {
                        selected_config = i;
                    }
                    
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndListBox();
            }
            
            if (selected_config >= 0) {
                if (ImGui::Button("Load Selected")) {
                    if (LoadDevicePositions(config_files[selected_config])) {
                        ImGui::OpenPopup("LoadSuccess");
                    } else {
                        ImGui::OpenPopup("LoadFailed");
                    }
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Delete Selected")) {
                    ImGui::OpenPopup("ConfirmDelete");
                }
                
                // Confirm delete popup
                if (ImGui::BeginPopupModal("ConfirmDelete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Are you sure you want to delete this configuration?");
                    ImGui::Text("%s", config_files[selected_config].c_str());
                    ImGui::Separator();
                    
                    if (ImGui::Button("Yes", ImVec2(120, 0))) {
                        std::string filepath = config_dir_ + "\\" + config_files[selected_config] + ".json";
                        try {
                            std::filesystem::remove(filepath);
                            selected_config = -1;
                        } catch (const std::exception& e) {
                            // Handle error
                            if (StayPutVR::Logger::IsInitialized()) {
                                StayPutVR::Logger::Error("UIManager: Failed to delete config file: " + std::string(e.what()));
                            }
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("No", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
        } else {
            ImGui::Text("No saved configurations found.");
        }
        
        // Reset all devices
        if (ImGui::Button("Reset All Devices")) {
            ResetAllDevices();
        }
        
        // Success/Failure popups
        if (ImGui::BeginPopupModal("SaveSuccess", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Configuration saved successfully!");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopupModal("SaveFailed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Failed to save configuration!");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopupModal("LoadSuccess", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Configuration loaded successfully!");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        if (ImGui::BeginPopupModal("LoadFailed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Failed to load configuration!");
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        ImGui::PopID();
        ImGui::Separator();
    }

} // namespace StayPutVR
