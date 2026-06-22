#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <array>
#include <mutex>
#include <shared_mutex>
#include <nlohmann/json.hpp>

namespace StayPutVR {

// Thread-safety contract:
// - Manager lifetime must be a subset of Config lifetime. UIManager enforces
//   this by shutting down all managers before its own destructor destroys
//   config_ (the Config is a direct member of UIManager).
// - Worker threads that read config fields must hold a shared lock via
//   ReadLock(). The UI thread must hold a unique lock via WriteLock() when
//   mutating config fields that worker threads may concurrently read.
// - ImGui render callbacks bind directly to config fields on the UI thread;
//   these do not need locks because they run single-threaded. Only batch
//   operations (Load/Save) and fields read by worker threads require locking.

class Config {
public:
    static constexpr int CURRENT_CONFIG_VERSION = 1;

    Config();
    ~Config() = default;

    // Lock helpers for cross-thread access. Workers call ReadLock();
    // the UI thread calls WriteLock() around batch mutations.
    [[nodiscard]] std::shared_lock<std::shared_mutex> ReadLock() const { return std::shared_lock(mutex_); }
    [[nodiscard]] std::unique_lock<std::shared_mutex> WriteLock() { return std::unique_lock(mutex_); }

    mutable std::shared_mutex mutex_;

    // These methods expect just the filename (e.g., "config.ini"), not a full path.
    // The path will be constructed internally using GetAppDataPath() + "\\config\\" + filename
    bool LoadFromFile(const std::string& filename);
    bool SaveToFile(const std::string& filename) const;
    bool CreateDefaultConfigFile(const std::string& filename);

    // Config versioning (for one-time migrations)
    int config_version = 0;

    // Logging Settings
    std::string log_level;
    float ui_font_scale = 1.0f; // UI font size multiplier (Settings > Display)

    // Splash / What's New (see SplashScreen + UIManager_WhatsNew).
    bool splash_auto_close = false;          // auto-dismiss the startup splash after a brief delay
    std::string whats_new_seen_version = ""; // last app version whose What's New the user dismissed

    // OSC Settings
    bool osc_enabled = false;
    std::string osc_address = "127.0.0.1";
    int osc_send_port = 9000;
    int osc_receive_port = 9001;
    // When enabled, the receive port is auto-negotiated (ephemeral bind) and
    // advertised to VRChat via OSCQuery/mDNS, and the send port is discovered
    // from VRChat. Fixes conflicts with other OSC apps holding 9001. When off,
    // the fixed send/receive ports above are used.
    bool osc_query_enabled = true;
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

    // External shock triggers (issue #7): the bite param and the new Shock param
    // each fire a direct shock on all configured shockers at their own intensity
    // (0..1) and duration (seconds). Both are blocked while emergency stop is active.
    std::string osc_shock_path = "/avatar/parameters/Shock";
    bool osc_shock_enabled = true;
    float osc_shock_intensity = 0.25f;
    float osc_shock_duration = 1.0f;
    float osc_bite_intensity = 0.25f;
    float osc_bite_duration = 1.0f;

    // Global lock/unlock paths
    std::string osc_global_lock_path = "/avatar/parameters/SPVR_Global_Lock";
    std::string osc_global_unlock_path = "/avatar/parameters/SPVR_Global_Unlock";
    
    // Global out-of-bounds path and setting
    std::string osc_global_out_of_bounds_path = "/avatar/parameters/SPVR_Global_OutOfBounds";
    bool osc_global_out_of_bounds_enabled = true;
    
    // Emergency stop stretch path and setting
    std::string osc_estop_stretch_path = "/avatar/parameters/SPVR_EStop_Stretch";
    bool osc_estop_stretch_enabled = true;
    
    // PiShock Mode Selection
    enum class PiShockMode {
        LEGACY_API = 0,
        WEBSOCKET_V2 = 1
    };
    
    // PiShock Settings via VRCOSC
    bool pishock_enabled = false;
    int pishock_group = 0;
    bool pishock_user_agreement = false;
    PiShockMode pishock_mode = PiShockMode::WEBSOCKET_V2;
    
    // PiShock Direct API Settings (common to both modes)
    std::string pishock_api_key;
    std::string pishock_username;
    int pishock_user_id = 0;         // WebSocket v2: Numeric User ID (for log metadata)
    std::string pishock_share_code;
    std::string pishock_client_id;   // WebSocket v2: Client ID for ops channel
    std::array<int, 5> pishock_shocker_ids; // WebSocket v2: The actual shocker device IDs (numeric), support up to 5 devices
    
    // Warning Zone PiShock Settings
    bool pishock_warning_beep = false;
    bool pishock_warning_shock = false;
    bool pishock_warning_vibrate = false;
    float pishock_warning_intensity = 0.25f;
    float pishock_warning_duration = 1.0f;
    
    // Disobedience (Out of Bounds) PiShock Settings
    bool pishock_disobedience_beep = false;
    bool pishock_disobedience_shock = false;
    bool pishock_disobedience_vibrate = false;
    float pishock_disobedience_intensity = 0.25f;
    float pishock_disobedience_duration = 1.0f;
    
    // Individual device intensities for PiShock WebSocket v2 (disobedience for each of 5 devices)
    bool pishock_use_individual_disobedience_intensities = false;
    std::array<float, 5> pishock_individual_disobedience_intensities = {0.25f, 0.25f, 0.25f, 0.25f, 0.25f}; 

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
    
    // Master intensity settings for OpenShock
    bool openshock_use_individual_warning_intensities = false; // false = use master, true = use individual for warning
    bool openshock_use_individual_disobedience_intensities = false; // false = use master, true = use individual for disobedience
    float openshock_master_warning_intensity = 0.25f;
    float openshock_master_disobedience_intensity = 0.25f;
    
    // Individual device intensities for OpenShock (warning and disobedience for each of 5 devices)
    std::array<float, 5> openshock_individual_warning_intensities = {0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    std::array<float, 5> openshock_individual_disobedience_intensities = {0.25f, 0.25f, 0.25f, 0.25f, 0.25f};

    // Buttplug/Intiface Settings
    bool buttplug_enabled = false;
    bool buttplug_user_agreement = false;
    
    // Buttplug Server Settings
    std::string buttplug_server_address = "localhost";
    int buttplug_server_port = 12345;
    std::array<int, 5> buttplug_device_indices = {-1, -1, -1, -1, -1}; // Support up to 5 device indices, -1 means not configured
    
    // Zone activation settings for Buttplug (which zones trigger vibration)
    bool buttplug_safe_zone_enabled = false;         // Vibrate when in safe zone
    bool buttplug_warning_zone_enabled = false;      // Vibrate when in warning zone
    bool buttplug_disobedience_zone_enabled = true;  // Vibrate when disobeying (out of bounds)
    
    // Safe Zone Buttplug Settings
    float buttplug_safe_intensity = 0.15f;
    float buttplug_safe_duration = 1.0f;
    
    // Warning Zone Buttplug Settings
    float buttplug_warning_intensity = 0.25f;
    float buttplug_warning_duration = 1.0f;
    
    // Disobedience (Out of Bounds) Buttplug Settings
    float buttplug_disobedience_intensity = 0.5f;
    float buttplug_disobedience_duration = 2.0f;
    
    // Master intensity settings for Buttplug
    bool buttplug_use_individual_safe_intensities = false;
    bool buttplug_use_individual_warning_intensities = false;
    bool buttplug_use_individual_disobedience_intensities = false;
    float buttplug_master_safe_intensity = 0.15f;
    float buttplug_master_warning_intensity = 0.25f;
    float buttplug_master_disobedience_intensity = 0.5f;
    
    // Individual device intensities for Buttplug (safe, warning and disobedience for each of 5 devices)
    std::array<float, 5> buttplug_individual_safe_intensities = {0.15f, 0.15f, 0.15f, 0.15f, 0.15f};
    std::array<float, 5> buttplug_individual_warning_intensities = {0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    std::array<float, 5> buttplug_individual_disobedience_intensities = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

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
    
    // Shock Cooldown Timer Settings
    bool shock_cooldown_enabled = false;
    float shock_cooldown_seconds = 10.0f;

    // Notification / Audio Settings (grouped as AudioConfig)
    struct AudioConfig {
        bool enabled = true;
        float volume = 0.8f;
        bool warning = true;
        bool out_of_bounds = true;
        bool lock = true;
        bool unlock = true;
        bool haptic_enabled = true;
        float haptic_intensity = 0.5f;
    };
    AudioConfig audio;

    // Application Settings
    bool start_with_steamvr;
    bool minimize_to_tray;
    bool show_notifications;

    // Device settings maps
    std::unordered_map<std::string, std::string> device_names; // serial -> name
    std::unordered_map<std::string, bool> device_settings; // serial -> include_in_locking
    std::unordered_map<std::string, int> device_roles; // serial -> role (stored as int)
    std::unordered_map<std::string, std::array<bool, 5>> device_shock_ids; // serial -> which shock IDs to use (for PiShock/OpenShock)
    std::unordered_map<std::string, std::array<bool, 5>> device_vibration_ids; // serial -> which vibration IDs to use (for Buttplug)
};

} // namespace StayPutVR 