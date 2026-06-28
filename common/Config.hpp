#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <array>
#include <mutex>
#include <shared_mutex>
#include <nlohmann/json.hpp>

namespace StayPutVR {

// Outcome of a config load/save attempt. The point of the enum (vs. a bare
// bool) is to tell apart the cases that look identical to std::ifstream but
// mean very different things to the user:
//   - NotFound:     no file yet — a benign first run; defaults are fine.
//   - AccessDenied: the file (or its folder) exists but the OS refused access.
//                   Usually a permissions/ownership problem left over from a
//                   build that ran elevated, a read-only file, antivirus /
//                   Controlled Folder Access, or another process holding it
//                   open. THIS is the one worth surfacing to the user.
//   - Corrupt:      the file opened but its contents are not valid JSON.
//   - OtherError:   anything else (unexpected I/O failure).
enum class ConfigStatus {
    Ok = 0,
    NotFound,
    AccessDenied,
    Corrupt,
    OtherError
};

// Rich result for LoadFromFileEx / SaveToFileEx. Carries the resolved path and
// an OS error string so the UI and the logs can give the user a specific,
// actionable message instead of a generic "failed to open config file".
struct ConfigResult {
    ConfigStatus status = ConfigStatus::Ok;
    std::string path;            // the path actually acted upon
    std::string detail;          // human-readable OS / parse error, if any
    int os_error = 0;            // errno snapshot at the point of failure (0 if none)
    std::string quarantine_path; // where a corrupt file was moved aside, if any

    bool ok() const { return status == ConfigStatus::Ok; }
};

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
    // v1: PiShock durations migrated 0..1 -> seconds.
    // v2: OpenShock durations migrated 0..1 -> seconds.
    static constexpr int CURRENT_CONFIG_VERSION = 2;

    Config();
    ~Config() = default;

    // Lock helpers for cross-thread access. Workers call ReadLock();
    // the UI thread calls WriteLock() around batch mutations.
    [[nodiscard]] std::shared_lock<std::shared_mutex> ReadLock() const { return std::shared_lock(mutex_); }
    [[nodiscard]] std::unique_lock<std::shared_mutex> WriteLock() { return std::unique_lock(mutex_); }

    mutable std::shared_mutex mutex_;

    // These methods expect just the filename (e.g., "config.ini"), not a full path.
    // The path will be constructed internally using GetAppDataPath() + "\\config\\" + filename
    //
    // The *Ex variants report exactly what happened (missing / access denied /
    // corrupt / ok) so callers can distinguish a benign first run from a real
    // permissions failure. The bool overloads are thin wrappers kept for the
    // many existing call sites; they return true only on ConfigStatus::Ok.
    ConfigResult LoadFromFileEx(const std::string& filename);
    ConfigResult SaveToFileEx(const std::string& filename) const;
    bool LoadFromFile(const std::string& filename) { return LoadFromFileEx(filename).ok(); }
    bool SaveToFile(const std::string& filename) const { return SaveToFileEx(filename).ok(); }
    bool CreateDefaultConfigFile(const std::string& filename);

    // Startup self-check: logs the resolved config path, whether it exists and
    // its read/write permissions, and actively write-probes the config folder so
    // a "settings won't save" permissions problem is detected and logged up front
    // (and reflected in the returned status) rather than only when the user first
    // changes a setting. Returns the writability verdict (Ok / AccessDenied /
    // OtherError) for the config directory.
    static ConfigResult RunStartupDiagnostics(const std::string& filename);

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
    // When true, the trigger fires each shocker at its individual per-device
    // disobedience intensity instead of the single intensity above.
    bool osc_bite_use_individual_intensities = false;
    bool osc_shock_use_individual_intensities = false;

    // Global lock/unlock paths
    std::string osc_global_lock_path = "/avatar/parameters/SPVR_Global_Lock";
    std::string osc_global_unlock_path = "/avatar/parameters/SPVR_Global_Unlock";
    
    // Global out-of-bounds path and setting
    std::string osc_global_out_of_bounds_path = "/avatar/parameters/SPVR_Global_OutOfBounds";
    bool osc_global_out_of_bounds_enabled = true;
    
    // Emergency stop stretch path and setting
    std::string osc_estop_stretch_path = "/avatar/parameters/SPVR_EStop_Stretch";
    bool osc_estop_stretch_enabled = true;

    // VRCFT JawOpen constraint (see UIManager_Devices CheckJawOpenConstraint).
    // While the HMD is locked, the avatar's JawOpen value must stay within a
    // margin of the value captured at lock time, escalating warning->disobedience
    // like the controller position constraint. Off by default.
    //
    // NOTE: VRChat does NOT echo the raw VRCFT JawOpen (FT/v2/JawOpen) back out
    // over OSC (it arrived as OSC input, so it's loopback-suppressed). The
    // StayPutVR JawOpen bridge (a VRCFury Full Controller + the editor-generated
    // .controller) reads FT/v2/JawOpen in an FX layer and drives a quantized,
    // driver-set parameter SPVR_JawOpen, which VRChat DOES send out. So the app
    // listens for SPVR_JawOpen, not the raw VRCFT param.
    bool jawopen_enabled = false;
    bool jawopen_user_agreement = false;        // safety gate, mirrors pishock_user_agreement
    std::string osc_jawopen_path = "/avatar/parameters/SPVR_JawOpen"; // bridge output param
    float jawopen_warning_margin = 0.10f;       // |current-baseline| > this => warning
    float jawopen_disobedience_margin = 0.20f;  // |current-baseline| > this => disobedience
    float jawopen_grace_seconds = 1.0f;         // baseline-capture window after HMD lock

    // Microphone enforced-mute constraint (see UIManager_Devices CheckMicrophoneConstraint).
    // While the HMD is locked AND the collar mode includes Mic, the captured mic
    // loudness must stay within a margin of the ambient floor captured at lock time,
    // escalating warning->disobedience like the JawOpen constraint. Off by default.
    // The level comes from MicrophoneManager (WASAPI capture), not from OSC.
    bool mic_enabled = false;
    bool mic_user_agreement = false;            // safety gate, mirrors pishock_user_agreement
    std::string mic_device_id = "";             // stable WASAPI endpoint id; "" => system default
    float mic_warning_margin = 0.05f;           // (level-baseline) > this => warning
    float mic_disobedience_margin = 0.10f;      // (level-baseline) > this => disobedience
    float mic_grace_seconds = 2.0f;             // ambient-floor capture window after HMD lock
    float mic_disobedience_cooldown_seconds = 1.0f; // refractory period after a mic disobedience fires

    // Unified collar mode runtime gate. The avatar's momentary SPVR_Collar_ToggleButton
    // cycles SPVR_Collar_Mode (0=Neither,1=Jaw,2=Mic,3=Both) among the integrations the
    // user has enabled+agreed; it replaces the old per-feature SPVR_JawEnabled radial.
    std::string osc_collar_toggle_path = "/avatar/parameters/SPVR_Collar_ToggleButton";

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
    // Durations are in SECONDS (0.3..15), converted to API ms at send time.
    // Existing 0..1 normalized configs are migrated to seconds on load (v2).
    int openshock_warning_action = 0; // 0=none, 1=shock, 2=vibrate
    float openshock_warning_intensity = 0.25f;
    float openshock_warning_duration = 1.0f;

    // Disobedience (Out of Bounds) OpenShock Settings
    int openshock_disobedience_action = 0; // 0=none, 1=shock, 2=vibrate
    float openshock_disobedience_intensity = 0.1f;
    float openshock_disobedience_duration = 1.0f;
    
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

    // In-game sound effects. On configured events the app pulses an int enum on
    // SPVR_SoundEffect (0=None,1=Lock,2=Unlock,3=Warning,4=Disobedience,
    // 5=CollarMode) so an avatar animation layer can play a sound. Independent of
    // the app's own (App Sound Effects) audio cues. Per-event toggles default on.
    bool ingame_sfx_enabled = true;
    bool ingame_sfx_lock = true;
    bool ingame_sfx_unlock = true;
    bool ingame_sfx_warning = true;
    bool ingame_sfx_disobedience = true;
    bool ingame_sfx_collar_mode = true;
    std::string osc_sound_effect_path = "/avatar/parameters/SPVR_SoundEffect";

    // Application Settings
    bool start_with_steamvr;
    bool minimize_to_tray;
    bool show_notifications;

    // Device settings maps
    std::unordered_map<std::string, std::string> device_names; // serial -> name
    std::unordered_map<std::string, bool> device_settings; // serial -> include_in_locking
    std::unordered_map<std::string, int> device_roles; // serial -> role (stored as int)
    std::unordered_map<std::string, std::array<bool, 5>> device_pishock_ids;   // serial -> which PiShock shocker slots to use
    std::unordered_map<std::string, std::array<bool, 5>> device_openshock_ids; // serial -> which OpenShock device slots to use
    std::unordered_map<std::string, std::array<bool, 5>> device_vibration_ids; // serial -> which vibration IDs to use (for Buttplug)
};

} // namespace StayPutVR 