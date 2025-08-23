#include "Config.hpp"
#include <fstream>
#include <unordered_set>
#include "../thirdparty/inih/INIReader.h"
#include <filesystem>
#include <sstream>
#include <iostream>
#include "Logger.hpp"
#include <nlohmann/json.hpp>
#include "PathUtils.hpp"

namespace StayPutVR {

Config::Config()
    : log_level("WARNING")
    , osc_address("127.0.0.1")
    , osc_send_port(9000)
    , osc_receive_port(9005)
    , osc_address_bounds("/stayputvr/bounds")
    , osc_address_warning("/stayputvr/warning")
    , osc_address_disable("/stayputvr/disable")
    , osc_enabled(false)
    , chaining_mode(false)
    , osc_lock_path_hmd("/avatar/parameters/SPVR_HMD_Latch_IsPosed")
    , osc_lock_path_left_hand("/avatar/parameters/SPVR_ControllerLeft_Latch_IsPosed")
    , osc_lock_path_right_hand("/avatar/parameters/SPVR_ControllerRight_Latch_IsPosed")
    , osc_lock_path_left_foot("/avatar/parameters/SPVR_FootLeft_Latch_IsPosed")
    , osc_lock_path_right_foot("/avatar/parameters/SPVR_FootRight_Latch_IsPosed")
    , osc_lock_path_hip("/avatar/parameters/SPVR_Hip_Latch_IsPosed")
    , osc_include_path_hmd("/avatar/parameters/SPVR_HMD_include")
    , osc_include_path_left_hand("/avatar/parameters/SPVR_ControllerLeft_include")
    , osc_include_path_right_hand("/avatar/parameters/SPVR_ControllerRight_include")
    , osc_include_path_left_foot("/avatar/parameters/SPVR_FootLeft_include")
    , osc_include_path_right_foot("/avatar/parameters/SPVR_FootRight_include")
    , osc_include_path_hip("/avatar/parameters/SPVR_Hip_include")
    , osc_bite_path("/avatar/parameters/SPVR_Bite")
    , osc_bite_enabled(true)
    , osc_global_lock_path("/avatar/parameters/SPVR_Global_Lock")
    , osc_global_unlock_path("/avatar/parameters/SPVR_Global_Unlock")
    , osc_global_out_of_bounds_path("/avatar/parameters/SPVR_Global_OutOfBounds")
    , osc_global_out_of_bounds_enabled(true)
    , pishock_enabled(false)
    , pishock_group(0)
    , pishock_user_agreement(false)
    , pishock_api_key("")
    , pishock_username("")
    , pishock_share_code("")
    , pishock_warning_beep(false)
    , pishock_warning_shock(false)
    , pishock_warning_vibrate(false)
    , pishock_warning_intensity(0.25f)
    , pishock_warning_duration(0.25f)
    , pishock_disobedience_beep(false)
    , pishock_disobedience_shock(false)
    , pishock_disobedience_vibrate(false)
    , pishock_disobedience_intensity(0.5f)
    , pishock_disobedience_duration(0.5f)
    , warning_threshold(0.1f)
    , bounds_threshold(0.2f)
    , disable_threshold(0.5f)
    , cooldown_enabled(false)
    , cooldown_seconds(5.0f)
    , countdown_enabled(false)
    , countdown_seconds(3.0f)
    , audio_enabled(true)
    , audio_volume(0.8f)
    , warning_audio(true)
    , out_of_bounds_audio(true)
    , lock_audio(true)
    , unlock_audio(true)
    , haptic_enabled(true)
    , haptic_intensity(0.5f)
    , start_with_steamvr(true)
    , minimize_to_tray(false)
    , show_notifications(true)
    , twitch_enabled(false)
    , twitch_user_agreement(false)
    , twitch_client_id("")
    , twitch_client_secret("")
    , twitch_access_token("")
    , twitch_refresh_token("")
    , twitch_channel_name("")
    , twitch_bot_username("")
    , twitch_chat_enabled(false)
    , twitch_command_prefix("!")
    , twitch_lock_command("lock")
    , twitch_unlock_command("unlock")
    , twitch_status_command("status")
    , twitch_bits_enabled(false)
    , twitch_bits_minimum(100)
    , twitch_subs_enabled(false)
    , twitch_donations_enabled(false)
    , twitch_donation_minimum(5.0f)
    , twitch_lock_duration_enabled(false)
    , twitch_lock_base_duration(60.0f)
    , twitch_lock_per_dollar(30.0f)
    , twitch_lock_max_duration(600.0f)
    , twitch_target_all_devices(true)
    , twitch_target_hmd(false)
    , twitch_target_left_hand(false)
    , twitch_target_right_hand(false)
    , twitch_target_left_foot(false)
    , twitch_target_right_foot(false)
    , twitch_target_hip(false)
    , unlock_timer_enabled(false)
    , unlock_timer_duration(300.0f)
    , unlock_timer_show_remaining(true)
    , unlock_timer_audio_warnings(true)
{
}

bool Config::CreateDefaultConfigFile(const std::string& filename) {
    try {
        // Create the default config using the current default values
        return SaveToFile(filename);
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("Exception while creating default config: " + std::string(e.what()));
        }
        return false;
    }
}

bool Config::LoadFromFile(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to open config file: " + filename);
            }
            return false;
        }

        nlohmann::json j;
        file >> j;

        // OSC settings
        osc_enabled = j.value("osc_enabled", false);
        osc_address = j.value("osc_address", "127.0.0.1");
        
        // Check if we're loading from an old config that had a single osc_port
        if (j.contains("osc_port")) {
            int old_port = j.value("osc_port", 9000);
            osc_send_port = old_port;
            osc_receive_port = 9005; // Default receive port for older configs
        } else {
            osc_send_port = j.value("osc_send_port", 9000);
            osc_receive_port = j.value("osc_receive_port", 9005);
        }
        
        chaining_mode = j.value("chaining_mode", false);
        
        // Load OSC lock paths
        osc_lock_path_hmd = j.value("osc_lock_path_hmd", "/avatar/parameters/SPVR_HMD_Latch_IsPosed");
        osc_lock_path_left_hand = j.value("osc_lock_path_left_hand", "/avatar/parameters/SPVR_ControllerLeft_Latch_IsPosed");
        osc_lock_path_right_hand = j.value("osc_lock_path_right_hand", "/avatar/parameters/SPVR_ControllerRight_Latch_IsPosed");
        osc_lock_path_left_foot = j.value("osc_lock_path_left_foot", "/avatar/parameters/SPVR_FootLeft_Latch_IsPosed");
        osc_lock_path_right_foot = j.value("osc_lock_path_right_foot", "/avatar/parameters/SPVR_FootRight_Latch_IsPosed");
        osc_lock_path_hip = j.value("osc_lock_path_hip", "/avatar/parameters/SPVR_Hip_Latch_IsPosed");
        
        // Load OSC include paths
        osc_include_path_hmd = j.value("osc_include_path_hmd", "/avatar/parameters/SPVR_HMD_include");
        osc_include_path_left_hand = j.value("osc_include_path_left_hand", "/avatar/parameters/SPVR_ControllerLeft_include");
        osc_include_path_right_hand = j.value("osc_include_path_right_hand", "/avatar/parameters/SPVR_ControllerRight_include");
        osc_include_path_left_foot = j.value("osc_include_path_left_foot", "/avatar/parameters/SPVR_FootLeft_include");
        osc_include_path_right_foot = j.value("osc_include_path_right_foot", "/avatar/parameters/SPVR_FootRight_include");
        osc_include_path_hip = j.value("osc_include_path_hip", "/avatar/parameters/SPVR_Hip_include");
        
        // Load global lock/unlock paths
        osc_global_lock_path = j.value("osc_global_lock_path", "/avatar/parameters/SPVR_Global_Lock");
        osc_global_unlock_path = j.value("osc_global_unlock_path", "/avatar/parameters/SPVR_Global_Unlock");
        osc_global_out_of_bounds_path = j.value("osc_global_out_of_bounds_path", "/avatar/parameters/SPVR_Global_OutOfBounds");
        osc_global_out_of_bounds_enabled = j.value("osc_global_out_of_bounds_enabled", true);
        osc_bite_path = j.value("osc_bite_path", "/avatar/parameters/SPVR_Bite");
        osc_bite_enabled = j.value("osc_bite_enabled", true);

        // PiShock settings
        pishock_enabled = j.value("pishock_enabled", false);
        pishock_group = j.value("pishock_group", 0);
        pishock_user_agreement = j.value("pishock_user_agreement", false);
        
        // PiShock API settings
        pishock_api_key = j.value("pishock_api_key", "");
        pishock_username = j.value("pishock_username", "");
        pishock_share_code = j.value("pishock_share_code", "");
        
        // Warning Zone PiShock Settings
        pishock_warning_beep = j.value("pishock_warning_beep", false);
        pishock_warning_shock = j.value("pishock_warning_shock", false);
        pishock_warning_vibrate = j.value("pishock_warning_vibrate", false);
        pishock_warning_intensity = j.value("pishock_warning_intensity", 0.25f);
        pishock_warning_duration = j.value("pishock_warning_duration", 0.25f);
        
        // Disobedience (Out of Bounds) PiShock Settings
        pishock_disobedience_beep = j.value("pishock_disobedience_beep", false);
        pishock_disobedience_shock = j.value("pishock_disobedience_shock", false);
        pishock_disobedience_vibrate = j.value("pishock_disobedience_vibrate", false);
        pishock_disobedience_intensity = j.value("pishock_disobedience_intensity", 0.5f);
        pishock_disobedience_duration = j.value("pishock_disobedience_duration", 0.5f);

        // Twitch Integration Settings
        twitch_enabled = j.value("twitch_enabled", false);
        twitch_user_agreement = j.value("twitch_user_agreement", false);
        
        // Twitch API Authentication
        twitch_client_id = j.value("twitch_client_id", "");
        twitch_client_secret = j.value("twitch_client_secret", "");
        twitch_access_token = j.value("twitch_access_token", "");
        twitch_refresh_token = j.value("twitch_refresh_token", "");
        twitch_channel_name = j.value("twitch_channel_name", "");
        twitch_bot_username = j.value("twitch_bot_username", "");
        
        // Twitch Chat Bot Settings
        twitch_chat_enabled = j.value("twitch_chat_enabled", false);
        twitch_command_prefix = j.value("twitch_command_prefix", "!");
        twitch_lock_command = j.value("twitch_lock_command", "lock");
        twitch_unlock_command = j.value("twitch_unlock_command", "unlock");
        twitch_status_command = j.value("twitch_status_command", "status");
        
        // Twitch Donation Trigger Settings
        twitch_bits_enabled = j.value("twitch_bits_enabled", false);
        twitch_bits_minimum = j.value("twitch_bits_minimum", 100);
        twitch_subs_enabled = j.value("twitch_subs_enabled", false);
        twitch_donations_enabled = j.value("twitch_donations_enabled", false);
        twitch_donation_minimum = j.value("twitch_donation_minimum", 5.0f);
        
        // Twitch Lock Duration Settings
        twitch_lock_duration_enabled = j.value("twitch_lock_duration_enabled", false);
        twitch_lock_base_duration = j.value("twitch_lock_base_duration", 60.0f);
        twitch_lock_per_dollar = j.value("twitch_lock_per_dollar", 30.0f);
        twitch_lock_max_duration = j.value("twitch_lock_max_duration", 600.0f);
        
        // Twitch Device Targeting
        twitch_target_all_devices = j.value("twitch_target_all_devices", true);
        twitch_target_hmd = j.value("twitch_target_hmd", false);
        twitch_target_left_hand = j.value("twitch_target_left_hand", false);
        twitch_target_right_hand = j.value("twitch_target_right_hand", false);
        twitch_target_left_foot = j.value("twitch_target_left_foot", false);
        twitch_target_right_foot = j.value("twitch_target_right_foot", false);
        twitch_target_hip = j.value("twitch_target_hip", false);
        
        // Unlock Timer Settings
        unlock_timer_enabled = j.value("unlock_timer_enabled", false);
        unlock_timer_duration = j.value("unlock_timer_duration", 300.0f);
        unlock_timer_show_remaining = j.value("unlock_timer_show_remaining", true);
        unlock_timer_audio_warnings = j.value("unlock_timer_audio_warnings", true);

        // Load logging settings
        log_level = j.value("log_level", "WARNING");
        
        // Load boundary settings
        warning_threshold = j.value("warning_threshold", 0.1f);
        bounds_threshold = j.value("bounds_threshold", 0.2f);
        disable_threshold = j.value("disable_threshold", 0.5f);
        
        // Load timer settings
        cooldown_enabled = j.value("cooldown_enabled", false);
        cooldown_seconds = j.value("cooldown_seconds", 5.0f);
        countdown_enabled = j.value("countdown_enabled", false);
        countdown_seconds = j.value("countdown_seconds", 3.0f);
        
        // Load notification settings
        audio_enabled = j.value("audio_enabled", true);
        audio_volume = j.value("audio_volume", 0.8f);
        warning_audio = j.value("warning_audio", true);
        out_of_bounds_audio = j.value("out_of_bounds_audio", true);
        lock_audio = j.value("lock_audio", true);
        unlock_audio = j.value("unlock_audio", true);
        haptic_enabled = j.value("haptic_enabled", true);
        haptic_intensity = j.value("haptic_intensity", 0.5f);
        
        // Load application settings
        start_with_steamvr = j.value("start_with_steamvr", true);
        minimize_to_tray = j.value("minimize_to_tray", false);
        show_notifications = j.value("show_notifications", true);
        
        // Clear existing device data
        device_names.clear();
        device_settings.clear();
        device_roles.clear();
        
        // Load device names, settings, and roles from new format (direct properties)
        if (j.contains("device_names") && j["device_names"].is_object()) {
            for (auto& [serial, name] : j["device_names"].items()) {
                device_names[serial] = name.get<std::string>();
                if (Logger::IsInitialized()) {
                    Logger::Debug("Loaded device name from direct property: " + serial + " -> " + name.get<std::string>());
                }
            }
        }
        
        if (j.contains("device_settings") && j["device_settings"].is_object()) {
            for (auto& [serial, include] : j["device_settings"].items()) {
                device_settings[serial] = include.get<bool>();
                if (Logger::IsInitialized()) {
                    Logger::Debug("Loaded device setting from direct property: " + serial + " -> " + 
                                 (include.get<bool>() ? "true" : "false"));
                }
            }
        }
        
        if (j.contains("device_roles") && j["device_roles"].is_object()) {
            for (auto& [serial, role] : j["device_roles"].items()) {
                device_roles[serial] = role.get<int>();
                if (Logger::IsInitialized()) {
                    Logger::Info("Loaded device role from direct property: " + serial + " -> role value: " + 
                                std::to_string(role.get<int>()));
                }
            }
        }
        
        // Also check the old format (devices array) for backward compatibility
        // This will add any devices that weren't already loaded from the direct properties
        const nlohmann::json& devices = j.value("devices", nlohmann::json::array());
        for (const auto& device : devices) {
            if (!device.contains("serial")) continue;
            
            std::string serial = device.value("serial", "Unknown Device");
            
            // Load device name if present and not already loaded
            if (device.contains("name") && device_names.find(serial) == device_names.end()) {
                std::string name = device.value("name", "Unknown Device");
                device_names[serial] = name;
                if (Logger::IsInitialized()) {
                    Logger::Debug("Loaded device name from devices array: " + serial + " -> " + name);
                }
            }
            
            // Load include_in_locking if present and not already loaded
            if (device.contains("include_in_locking") && device_settings.find(serial) == device_settings.end()) {
                bool include_in_locking = device.value("include_in_locking", false);
                device_settings[serial] = include_in_locking;
                if (Logger::IsInitialized()) {
                    Logger::Debug("Loaded device setting from devices array: " + serial + " -> " + 
                                 (include_in_locking ? "true" : "false"));
                }
            }
            
            // Load device role if present and not already loaded
            if (device.contains("role") && device_roles.find(serial) == device_roles.end()) {
                int role_value = device.value("role", 0);
                device_roles[serial] = role_value;
                if (Logger::IsInitialized()) {
                    Logger::Info("Loaded device role from devices array: " + serial + " -> role value: " + 
                                std::to_string(role_value));
                }
            }
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("Loaded config file: " + filename);
            Logger::Debug("Loaded " + std::to_string(device_roles.size()) + " device roles, " + 
                         std::to_string(device_settings.size()) + " device settings, and " + 
                         std::to_string(device_names.size()) + " device names");
        }
        return true;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("Error loading config: " + std::string(e.what()));
        }
        return false;
    }
}

bool Config::SaveToFile(const std::string& filename) const {
    try {
        nlohmann::json j;

        // OSC settings
        j["osc_enabled"] = osc_enabled;
        j["osc_address"] = osc_address;
        j["osc_send_port"] = osc_send_port;
        j["osc_receive_port"] = osc_receive_port;
        j["chaining_mode"] = chaining_mode;
        
        // OSC device lock paths
        j["osc_lock_path_hmd"] = osc_lock_path_hmd;
        j["osc_lock_path_left_hand"] = osc_lock_path_left_hand;
        j["osc_lock_path_right_hand"] = osc_lock_path_right_hand;
        j["osc_lock_path_left_foot"] = osc_lock_path_left_foot;
        j["osc_lock_path_right_foot"] = osc_lock_path_right_foot;
        j["osc_lock_path_hip"] = osc_lock_path_hip;
        
        // OSC device include paths
        j["osc_include_path_hmd"] = osc_include_path_hmd;
        j["osc_include_path_left_hand"] = osc_include_path_left_hand;
        j["osc_include_path_right_hand"] = osc_include_path_right_hand;
        j["osc_include_path_left_foot"] = osc_include_path_left_foot;
        j["osc_include_path_right_foot"] = osc_include_path_right_foot;
        j["osc_include_path_hip"] = osc_include_path_hip;
        
        // Global lock/unlock paths
        j["osc_global_lock_path"] = osc_global_lock_path;
        j["osc_global_unlock_path"] = osc_global_unlock_path;
        j["osc_global_out_of_bounds_path"] = osc_global_out_of_bounds_path;
        j["osc_global_out_of_bounds_enabled"] = osc_global_out_of_bounds_enabled;
        j["osc_bite_path"] = osc_bite_path;
        j["osc_bite_enabled"] = osc_bite_enabled;

        // PiShock settings
        j["pishock_enabled"] = pishock_enabled;
        j["pishock_group"] = pishock_group;
        j["pishock_user_agreement"] = pishock_user_agreement;
        
        // PiShock API settings
        j["pishock_api_key"] = pishock_api_key;
        j["pishock_username"] = pishock_username;
        j["pishock_share_code"] = pishock_share_code;
        
        // Warning Zone PiShock Settings
        j["pishock_warning_beep"] = pishock_warning_beep;
        j["pishock_warning_shock"] = pishock_warning_shock;
        j["pishock_warning_vibrate"] = pishock_warning_vibrate;
        j["pishock_warning_intensity"] = pishock_warning_intensity;
        j["pishock_warning_duration"] = pishock_warning_duration;
        
        // Disobedience (Out of Bounds) PiShock Settings
        j["pishock_disobedience_beep"] = pishock_disobedience_beep;
        j["pishock_disobedience_shock"] = pishock_disobedience_shock;
        j["pishock_disobedience_vibrate"] = pishock_disobedience_vibrate;
        j["pishock_disobedience_intensity"] = pishock_disobedience_intensity;
        j["pishock_disobedience_duration"] = pishock_disobedience_duration;

        // Twitch Integration Settings
        j["twitch_enabled"] = twitch_enabled;
        j["twitch_user_agreement"] = twitch_user_agreement;
        
        // Twitch API Authentication
        j["twitch_client_id"] = twitch_client_id;
        j["twitch_client_secret"] = twitch_client_secret;
        j["twitch_access_token"] = twitch_access_token;
        j["twitch_refresh_token"] = twitch_refresh_token;
        j["twitch_channel_name"] = twitch_channel_name;
        j["twitch_bot_username"] = twitch_bot_username;
        
        // Twitch Chat Bot Settings
        j["twitch_chat_enabled"] = twitch_chat_enabled;
        j["twitch_command_prefix"] = twitch_command_prefix;
        j["twitch_lock_command"] = twitch_lock_command;
        j["twitch_unlock_command"] = twitch_unlock_command;
        j["twitch_status_command"] = twitch_status_command;
        
        // Twitch Donation Trigger Settings
        j["twitch_bits_enabled"] = twitch_bits_enabled;
        j["twitch_bits_minimum"] = twitch_bits_minimum;
        j["twitch_subs_enabled"] = twitch_subs_enabled;
        j["twitch_donations_enabled"] = twitch_donations_enabled;
        j["twitch_donation_minimum"] = twitch_donation_minimum;
        
        // Twitch Lock Duration Settings
        j["twitch_lock_duration_enabled"] = twitch_lock_duration_enabled;
        j["twitch_lock_base_duration"] = twitch_lock_base_duration;
        j["twitch_lock_per_dollar"] = twitch_lock_per_dollar;
        j["twitch_lock_max_duration"] = twitch_lock_max_duration;
        
        // Twitch Device Targeting
        j["twitch_target_all_devices"] = twitch_target_all_devices;
        j["twitch_target_hmd"] = twitch_target_hmd;
        j["twitch_target_left_hand"] = twitch_target_left_hand;
        j["twitch_target_right_hand"] = twitch_target_right_hand;
        j["twitch_target_left_foot"] = twitch_target_left_foot;
        j["twitch_target_right_foot"] = twitch_target_right_foot;
        j["twitch_target_hip"] = twitch_target_hip;
        
        // Unlock Timer Settings
        j["unlock_timer_enabled"] = unlock_timer_enabled;
        j["unlock_timer_duration"] = unlock_timer_duration;
        j["unlock_timer_show_remaining"] = unlock_timer_show_remaining;
        j["unlock_timer_audio_warnings"] = unlock_timer_audio_warnings;

        // Logging settings
        j["log_level"] = log_level;
        
        // Boundary settings
        j["warning_threshold"] = warning_threshold;
        j["bounds_threshold"] = bounds_threshold;
        j["disable_threshold"] = disable_threshold;
        
        // Timer settings
        j["cooldown_enabled"] = cooldown_enabled;
        j["cooldown_seconds"] = cooldown_seconds;
        j["countdown_enabled"] = countdown_enabled;
        j["countdown_seconds"] = countdown_seconds;
        
        // Notification settings
        j["audio_enabled"] = audio_enabled;
        j["audio_volume"] = audio_volume;
        j["warning_audio"] = warning_audio;
        j["out_of_bounds_audio"] = out_of_bounds_audio;
        j["lock_audio"] = lock_audio;
        j["unlock_audio"] = unlock_audio;
        j["haptic_enabled"] = haptic_enabled;
        j["haptic_intensity"] = haptic_intensity;
        
        // Application settings
        j["start_with_steamvr"] = start_with_steamvr;
        j["minimize_to_tray"] = minimize_to_tray;
        j["show_notifications"] = show_notifications;
        
        // Save device names and settings directly at the root level
        // Create JSON objects for device_roles and device_settings
        nlohmann::json device_roles_json = nlohmann::json::object();
        nlohmann::json device_settings_json = nlohmann::json::object();
        nlohmann::json device_names_json = nlohmann::json::object();
        
        // Populate device roles
        for (const auto& [serial, role] : device_roles) {
            device_roles_json[serial] = role;
        }
        j["device_roles"] = device_roles_json;
        
        // Populate device settings (include_in_locking)
        for (const auto& [serial, include] : device_settings) {
            device_settings_json[serial] = include;
        }
        j["device_settings"] = device_settings_json;
        
        // Populate device names
        for (const auto& [serial, name] : device_names) {
            device_names_json[serial] = name;
        }
        j["device_names"] = device_names_json;
        
        // Populate the devices array for backward compatibility
        nlohmann::json devices = nlohmann::json::array();
        // Create a set of all serials across all three maps
        std::unordered_set<std::string> all_serials;
        for (const auto& [serial, _] : device_names) all_serials.insert(serial);
        for (const auto& [serial, _] : device_settings) all_serials.insert(serial);
        for (const auto& [serial, _] : device_roles) all_serials.insert(serial);
        
        // Create device objects
        for (const auto& serial : all_serials) {
            nlohmann::json device;
            device["serial"] = serial;
            
            // Add name if available
            auto name_it = device_names.find(serial);
            if (name_it != device_names.end()) {
                device["name"] = name_it->second;
            } else {
                device["name"] = "Unknown Device";
            }
            
            // Add include_in_locking if available
            auto setting_it = device_settings.find(serial);
            if (setting_it != device_settings.end()) {
                device["include_in_locking"] = setting_it->second;
            } else {
                device["include_in_locking"] = false;
            }
            
            // Add role if available
            auto role_it = device_roles.find(serial);
            if (role_it != device_roles.end()) {
                device["role"] = role_it->second;
            }
            
            devices.push_back(device);
        }
        j["devices"] = devices;
        
        // Write the JSON to file
        std::ofstream file(filename);
        if (!file.is_open()) {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to open config file for writing: " + filename);
            }
            return false;
        }

        file << j.dump(4);
        
        if (Logger::IsInitialized()) {
            Logger::Info("Saved config file: " + filename);
            Logger::Debug("Saved " + std::to_string(device_roles.size()) + " device roles, " + 
                         std::to_string(device_settings.size()) + " device settings, and " + 
                         std::to_string(device_names.size()) + " device names");
        }
        return true;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("Error saving config: " + std::string(e.what()));
        }
        return false;
    }
}

} // namespace StayPutVR 