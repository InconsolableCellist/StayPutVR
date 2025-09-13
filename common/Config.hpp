#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <array>
#include <nlohmann/json.hpp>

namespace StayPutVR {

class Config {
public:
    Config();
    ~Config() = default;

    // These methods expect just the filename (e.g., "config.ini"), not a full path.
    // The path will be constructed internally using GetAppDataPath() + "\\config\\" + filename
    bool LoadFromFile(const std::string& filename);
    bool SaveToFile(const std::string& filename) const;
    bool CreateDefaultConfigFile(const std::string& filename);

    // Logging Settings
    std::string log_level;

    // OSC Settings
    bool osc_enabled = false;
    std::string osc_address = "127.0.0.1";
    int osc_send_port = 9000;
    int osc_receive_port = 9001;
    bool chaining_mode = false;
    std::string osc_address_bounds;
    std::string osc_address_warning;
    std::string osc_address_disable;
    
    // OSC Device Lock Paths
    std::string osc_lock_path_hmd = "/avatar/parameters/SPVR_HMD_Latch_IsPosed";
    std::string osc_lock_path_left_hand = "/avatar/parameters/SPVR_ControllerLeft_Latch_IsPosed";
    std::string osc_lock_path_right_hand = "/avatar/parameters/SPVR_ControllerRight_Latch_IsPosed";
    std::string osc_lock_path_left_foot = "/avatar/parameters/SPVR_FootLeft_Latch_IsPosed";
    std::string osc_lock_path_right_foot = "/avatar/parameters/SPVR_FootRight_Latch_IsPosed";
    std::string osc_lock_path_hip = "/avatar/parameters/SPVR_Hip_Latch_IsPosed";
    
    // OSC Device Include Paths
    std::string osc_include_path_hmd = "/avatar/parameters/SPVR_HMD_include";
    std::string osc_include_path_left_hand = "/avatar/parameters/SPVR_ControllerLeft_include";
    std::string osc_include_path_right_hand = "/avatar/parameters/SPVR_ControllerRight_include";
    std::string osc_include_path_left_foot = "/avatar/parameters/SPVR_FootLeft_include";
    std::string osc_include_path_right_foot = "/avatar/parameters/SPVR_FootRight_include";
    std::string osc_include_path_hip = "/avatar/parameters/SPVR_Hip_include";

    std::string osc_bite_path = "/avatar/parameters/SPVR_Bite";
    bool osc_bite_enabled = true;
    
    // Global lock/unlock paths
    std::string osc_global_lock_path = "/avatar/parameters/SPVR_Global_Lock";
    std::string osc_global_unlock_path = "/avatar/parameters/SPVR_Global_Unlock";
    
    // Global out-of-bounds path and setting
    std::string osc_global_out_of_bounds_path = "/avatar/parameters/SPVR_Global_OutOfBounds";
    bool osc_global_out_of_bounds_enabled = true;
    
    // PiShock Settings via VRCOSC
    bool pishock_enabled = false;
    int pishock_group = 0;
    bool pishock_user_agreement = false;
    
    // PiShock Direct API Settings
    std::string pishock_api_key;
    std::string pishock_username;
    std::string pishock_share_code;
    
    // Warning Zone PiShock Settings
    bool pishock_warning_beep = false;
    bool pishock_warning_shock = false;
    bool pishock_warning_vibrate = false;
    float pishock_warning_intensity = 0.25f;
    float pishock_warning_duration = 0.25f;
    
    // Disobedience (Out of Bounds) PiShock Settings
    bool pishock_disobedience_beep = false;
    bool pishock_disobedience_shock = false;
    bool pishock_disobedience_vibrate = false;
    float pishock_disobedience_intensity = 0.5f;
    float pishock_disobedience_duration = 0.5f;

    // OpenShock Settings
    bool openshock_enabled = false;
    bool openshock_user_agreement = false;
    
    // OpenShock API Settings
    std::string openshock_api_token;
    std::array<std::string, 5> openshock_device_ids; // Support up to 5 device IDs
    std::string openshock_server_url = "https://api.openshock.app"; 
    
    // Warning Zone OpenShock Settings
    int openshock_warning_action = 0; // 0=none, 1=shock, 2=vibrate
    float openshock_warning_intensity = 0.25f;
    float openshock_warning_duration = 0.25f;
    
    // Disobedience (Out of Bounds) OpenShock Settings
    int openshock_disobedience_action = 0; // 0=none, 1=shock, 2=vibrate
    float openshock_disobedience_intensity = 0.1f;
    float openshock_disobedience_duration = 0.05f;

    // Twitch Integration Settings
    bool twitch_enabled = false;
    bool twitch_user_agreement = false;
    
    // Twitch API Authentication
    std::string twitch_client_id;
    std::string twitch_client_secret;
    std::string twitch_access_token;
    std::string twitch_refresh_token;
    std::string twitch_channel_name;
    std::string twitch_bot_username;
    
    // Twitch Chat Bot Settings
    bool twitch_chat_enabled = false;
    std::string twitch_command_prefix = "!";
    std::string twitch_lock_command = "lock";
    std::string twitch_unlock_command = "unlock";
    std::string twitch_status_command = "status";
    
    // Twitch Donation Trigger Settings
    bool twitch_bits_enabled = false;
    int twitch_bits_minimum = 100;  // Minimum bits to trigger lock
    bool twitch_subs_enabled = false;
    bool twitch_donations_enabled = false;
    float twitch_donation_minimum = 5.0f;  // Minimum donation amount to trigger lock
    
    // Twitch Lock Duration Settings
    bool twitch_lock_duration_enabled = false;
    float twitch_lock_base_duration = 60.0f;  // Base lock duration in seconds
    float twitch_lock_per_dollar = 30.0f;     // Additional seconds per dollar/100 bits
    float twitch_lock_max_duration = 600.0f;  // Maximum lock duration in seconds
    
    // Twitch Device Targeting
    bool twitch_target_all_devices = true;
    bool twitch_target_hmd = false;
    bool twitch_target_left_hand = false;
    bool twitch_target_right_hand = false;
    bool twitch_target_left_foot = false;
    bool twitch_target_right_foot = false;
    bool twitch_target_hip = false;
    
    // Unlock Timer Settings (for Twitch and general use)
    bool unlock_timer_enabled = false;
    float unlock_timer_duration = 300.0f;  // Default 5 minutes in seconds
    bool unlock_timer_show_remaining = true;
    bool unlock_timer_audio_warnings = true;  // Audio warnings at 60s, 30s, 10s

    // Boundary Settings
    float warning_threshold;  // Warning zone distance in meters
    float bounds_threshold;   // Out of bounds distance in meters
    float disable_threshold;  // Disable distance in meters

    // Timer Settings
    bool cooldown_enabled;
    float cooldown_seconds;
    bool countdown_enabled;
    float countdown_seconds;

    // Notification Settings
    bool audio_enabled;
    float audio_volume;
    bool warning_audio;
    bool out_of_bounds_audio;
    bool lock_audio;
    bool unlock_audio;
    bool haptic_enabled;
    float haptic_intensity;

    // Application Settings
    bool start_with_steamvr;
    bool minimize_to_tray;
    bool show_notifications;

    // Device settings maps
    std::unordered_map<std::string, std::string> device_names; // serial -> name
    std::unordered_map<std::string, bool> device_settings; // serial -> include_in_locking
    std::unordered_map<std::string, int> device_roles; // serial -> role (stored as int)
    std::unordered_map<std::string, std::array<bool, 5>> device_shock_ids; // serial -> which shock IDs to use
};

} // namespace StayPutVR 