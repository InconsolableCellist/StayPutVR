#include "Config.hpp"
#include <fstream>
#include <unordered_set>
#include <filesystem>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include "Logger.hpp"
#include <nlohmann/json.hpp>
#include "PathUtils.hpp"

namespace StayPutVR {

Config::Config()
    : log_level("WARNING")
    , osc_address("127.0.0.1")
    , osc_send_port(9000)
    , osc_receive_port(9001)
    , osc_query_enabled(true)
    , osc_address_bounds("/stayputvr/bounds")
    , osc_address_warning("/stayputvr/warning")
    , osc_address_disable("/stayputvr/disable")
    , osc_enabled(true)
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
    , osc_estop_stretch_path("/avatar/parameters/SPVR_EStop_Stretch")
    , osc_estop_stretch_enabled(true)
    , jawopen_enabled(false)
    , jawopen_user_agreement(false)
    , osc_jawopen_path("/avatar/parameters/SPVR_JawOpen")
    , jawopen_warning_margin(0.10f)
    , jawopen_disobedience_margin(0.20f)
    , jawopen_grace_seconds(1.0f)
    , mic_enabled(false)
    , mic_user_agreement(false)
    , mic_device_id("")
    , mic_warning_margin(0.05f)
    , mic_disobedience_margin(0.10f)
    , mic_grace_seconds(2.0f)
    , mic_disobedience_cooldown_seconds(1.0f)
    , osc_collar_toggle_path("/avatar/parameters/SPVR_Collar_ToggleButton")
    , pishock_enabled(false)
    , pishock_group(0)
    , pishock_user_agreement(false)
    , pishock_mode(PiShockMode::WEBSOCKET_V2)
    , pishock_api_key("")
    , pishock_username("")
    , pishock_user_id(0)
    , pishock_share_code("")
    , pishock_client_id("")
    , pishock_shocker_ids({0, 0, 0, 0, 0})
    , pishock_warning_beep(false)
    , pishock_warning_shock(false)
    , pishock_warning_vibrate(false)
    , pishock_warning_intensity(0.25f)
    , pishock_warning_duration(1.0f) 
    , pishock_disobedience_beep(false)
    , pishock_disobedience_shock(false)
    , pishock_disobedience_vibrate(false)
    , pishock_disobedience_intensity(0.25f)
    , pishock_disobedience_duration(1.0f)
    , pishock_use_individual_disobedience_intensities(false)
    , pishock_individual_disobedience_intensities({0.25f, 0.25f, 0.25f, 0.25f, 0.25f})
    , warning_threshold(0.1f)
    , bounds_threshold(0.2f)
    , disable_threshold(0.5f)
    , cooldown_enabled(false)
    , cooldown_seconds(5.0f)
    , countdown_enabled(false)
    , countdown_seconds(3.0f)
    , shock_cooldown_enabled(false)
    , shock_cooldown_seconds(5.0f)
    , audio{}
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

namespace {
    // True if the name already carries an explicit directory component (a caller
    // passing a full path); in that case we use it verbatim.
    bool HasDirComponent(const std::string& p) {
        return p.find('/') != std::string::npos || p.find('\\') != std::string::npos;
    }

    // Canonical home for the main config: %APPDATA%/StayPutVR/config/<file> on
    // Windows (XDG equivalent on Linux). This is where the app now reads/writes
    // and where Settings > Folders > "Open Settings Folder" points.
    std::string CanonicalConfigPath(const std::string& filename) {
        return GetAppDataPath() + "/config/" + filename;
    }

    // Resolve where to READ an existing config from. Older builds opened the bare
    // filename relative to the working directory (which, depending on launch,
    // could be the install dir or elsewhere), so honor those legacy locations to
    // migrate a user's settings instead of silently starting fresh. First hit
    // wins; returns the canonical path if nothing exists yet.
    std::string ResolveConfigForRead(const std::string& filename) {
        std::error_code ec;
        std::string canonical = CanonicalConfigPath(filename);
        if (std::filesystem::exists(canonical, ec)) return canonical;
        if (std::filesystem::exists(filename, ec)) return filename;            // CWD (legacy)
        std::string exeLocal = GetExecutableDir() + "/" + filename;
        if (std::filesystem::exists(exeLocal, ec)) return exeLocal;            // exe dir (legacy)
        return canonical;
    }

    // Per-field type isolation. nlohmann's jval(j, key, default) THROWS
    // (type_error.302) when the key is present but holds an incompatible type --
    // and that single throw, caught at the top of LoadFromFile, used to discard
    // the ENTIRE config and silently revert every setting to its default. jval
    // contains the damage to the one offending field: a hand-edited or partially
    // corrupt value is logged and replaced with its default while every other
    // setting still loads.
    template <typename T>
    T jval(const nlohmann::json& j, const char* key, T def) {
        auto it = j.find(key);
        if (it == j.end() || it->is_null()) return def;
        try {
            return it->template get<T>();
        } catch (const std::exception& e) {
            if (Logger::IsInitialized()) {
                Logger::Warning(std::string("Config field '") + key +
                                "' has an unexpected type; using default (" + e.what() + ")");
            }
            return def;
        }
    }

    // Overload for string-literal defaults: deduce std::string rather than
    // const char* (nlohmann can't get<const char*>()). Lets call sites keep the
    // natural jval(j, "key", "default") form.
    inline std::string jval(const nlohmann::json& j, const char* key, const char* def) {
        return jval<std::string>(j, key, std::string(def));
    }

    // Map a C errno (set by the CRT when std::ifstream/ofstream fails to open) to
    // a ConfigStatus. On Windows the CRT translates Win32 errors -- including
    // sharing violations and ACL denials -- to EACCES, so this classification
    // works the same on both platforms.
    ConfigStatus ClassifyOpenErrno(int err) {
        switch (err) {
            case 0:        return ConfigStatus::OtherError; // open failed but errno unset
            case ENOENT:   return ConfigStatus::NotFound;
            case EACCES:
#ifdef EPERM
            case EPERM:
#endif
                           return ConfigStatus::AccessDenied;
            default:       return ConfigStatus::OtherError;
        }
    }

    // A filesystem-safe local timestamp (YYYYMMDD-HHMMSS) for naming quarantined
    // copies of a corrupt config so successive failures don't overwrite earlier
    // evidence.
    std::string TimestampSuffix() {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tmv);
        return std::string(buf);
    }
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

ConfigResult Config::RunStartupDiagnostics(const std::string& filename) {
    ConfigResult result;
    std::error_code ec;

    const std::string canonical = HasDirComponent(filename)
        ? filename : CanonicalConfigPath(filename);
    const std::string dir = std::filesystem::path(canonical).parent_path().string();
    result.path = canonical;

    auto log = [](const std::string& m) { if (Logger::IsInitialized()) Logger::Info(m); };
    auto warn = [](const std::string& m) { if (Logger::IsInitialized()) Logger::Warning(m); };

    log("Config diagnostics: canonical config path = " + canonical);

    // Where would we actually READ from? Surfacing a legacy hit explains "my old
    // settings are being picked up from somewhere unexpected" reports.
    if (!HasDirComponent(filename)) {
        const std::string resolved = ResolveConfigForRead(filename);
        if (resolved != canonical) {
            warn("Config diagnostics: an existing config will be read from a LEGACY location "
                 "and migrated: " + resolved);
        }
    }

    // Report existence + perms of the config file itself.
    if (std::filesystem::exists(canonical, ec)) {
        auto st = std::filesystem::status(canonical, ec);
        auto p = st.permissions();
        const bool owner_read  = (p & std::filesystem::perms::owner_read)  != std::filesystem::perms::none;
        const bool owner_write = (p & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
        auto size = std::filesystem::file_size(canonical, ec);
        log("Config diagnostics: config.ini exists (size=" +
            std::to_string(ec ? 0 : size) + " bytes, owner_read=" +
            (owner_read ? "yes" : "no") + ", owner_write=" + (owner_write ? "yes" : "no") + ")");
        if (!owner_write) {
            warn("Config diagnostics: config.ini is NOT writable by the current user -- saved "
                 "settings will not persist until this is fixed (read-only attribute or an ACL "
                 "from a past elevated/admin run).");
        }
    } else {
        log("Config diagnostics: config.ini does not exist yet (first run / fresh profile).");
    }

    // Active write-probe: the definitive test of "can settings be saved here?".
    std::filesystem::create_directories(dir, ec);
    const std::string probe = dir + "/.spvr_write_probe";
    errno = 0;
    {
        std::ofstream f(probe, std::ios::trunc);
        if (!f.is_open()) {
            int err = errno;
            result.status = ClassifyOpenErrno(err);
            if (result.status == ConfigStatus::NotFound) result.status = ConfigStatus::OtherError;
            result.os_error = err;
            result.detail = err ? std::strerror(err) : "unknown error";
            if (Logger::IsInitialized()) {
                Logger::Error("Config diagnostics: CONFIG FOLDER IS NOT WRITABLE: " + dir +
                              " (" + result.detail + "). Settings cannot be saved. Likely causes: "
                              "StayPutVR was run as Administrator before, the folder is owned by "
                              "another account, or antivirus / Controlled Folder Access is blocking it.");
            }
            return result;
        }
        f << "ok";
    }
    std::error_code rmec;
    std::filesystem::remove(probe, rmec);
    log("Config diagnostics: config folder is writable -- settings can be saved.");
    result.status = ConfigStatus::Ok;
    return result;
}

ConfigResult Config::LoadFromFileEx(const std::string& filename) {
    // Resolve a bare filename to the canonical store, falling back to legacy
    // locations so an existing config is migrated rather than ignored.
    std::string path = HasDirComponent(filename) ? filename : ResolveConfigForRead(filename);
    ConfigResult result;
    result.path = path;

    try {
        auto lock = WriteLock();

        // Distinguish "no config yet" (a benign first run) from "config exists
        // but I can't read it" (a real permissions problem) BEFORE we blame the
        // open. exists() with an error_code never throws; a true ec here means
        // even the existence check was denied (e.g. a locked-down parent dir).
        std::error_code ec;
        bool exists = std::filesystem::exists(path, ec);
        if (!exists && !ec) {
            if (Logger::IsInitialized()) {
                Logger::Info("Config not found (first run or fresh profile): " + path +
                             " -- using defaults until first save");
            }
            result.status = ConfigStatus::NotFound;
            return result;
        }

        errno = 0;
        std::ifstream file(path);
        if (!file.is_open()) {
            int err = errno;
            ConfigStatus st = ec ? ConfigStatus::AccessDenied : ClassifyOpenErrno(err);
            result.status = st;
            result.os_error = err;
            result.detail = err ? std::strerror(err) : (ec ? ec.message() : "unknown error");
            if (Logger::IsInitialized()) {
                if (st == ConfigStatus::AccessDenied) {
                    Logger::Error("ACCESS DENIED opening config file: " + path +
                                  " (" + result.detail + "). The file or its folder may be "
                                  "owned by another account, marked read-only, locked by another "
                                  "process, or blocked by antivirus / Controlled Folder Access.");
                } else {
                    Logger::Error("Failed to open config file: " + path +
                                  " (" + result.detail + ")");
                }
            }
            return result;
        }
        if (Logger::IsInitialized()) {
            Logger::Info("Loading config from: " + path);
        }

        nlohmann::json j;
        // Parse in its own scope so a syntax error is reported as Corrupt (and
        // the bad file quarantined) instead of looking like an I/O failure.
        try {
            file >> j;
        } catch (const std::exception& e) {
            file.close();
            result.status = ConfigStatus::Corrupt;
            result.detail = e.what();
            // Move the unreadable file aside so the next SaveToFile doesn't
            // silently overwrite it -- the user (or we) may want to recover it.
            std::error_code rec;
            std::string quarantine = path + ".corrupt-" + TimestampSuffix();
            std::filesystem::rename(path, quarantine, rec);
            if (!rec) result.quarantine_path = quarantine;
            if (Logger::IsInitialized()) {
                Logger::Error("Config file is corrupt (not valid JSON): " + path +
                              " (" + result.detail + ")" +
                              (rec ? "" : " -- moved aside to " + quarantine));
            }
            return result;
        }

        // Config versioning. Capture the on-disk version up front: each migration
        // below gates on this original value, and we only stamp the member to the
        // current version once at the end. (Previously the PiShock migration wrote
        // config_version inline, which would cause a later migration gated on a
        // higher version to be skipped.)
        config_version = jval(j, "config_version", 0);
        const int loaded_config_version = config_version;

        // OSC settings
        osc_enabled = jval(j, "osc_enabled", true);
        osc_address = jval(j, "osc_address", "127.0.0.1");
        
        // Check if we're loading from an old config that had a single osc_port
        if (j.contains("osc_port")) {
            int old_port = jval(j, "osc_port", 9000);
            osc_send_port = old_port;
            osc_receive_port = 9001;
        } else {
            osc_send_port = jval(j, "osc_send_port", 9000);
            osc_receive_port = jval(j, "osc_receive_port", 9001);
        }
        
        osc_query_enabled = jval(j, "osc_query_enabled", true);
        chaining_mode = jval(j, "chaining_mode", false);

        // Load OSC lock paths
        osc_lock_path_hmd = jval(j, "osc_lock_path_hmd", "/avatar/parameters/SPVR_HMD_Latch_IsPosed");
        osc_lock_path_left_hand = jval(j, "osc_lock_path_left_hand", "/avatar/parameters/SPVR_ControllerLeft_Latch_IsPosed");
        osc_lock_path_right_hand = jval(j, "osc_lock_path_right_hand", "/avatar/parameters/SPVR_ControllerRight_Latch_IsPosed");
        osc_lock_path_left_foot = jval(j, "osc_lock_path_left_foot", "/avatar/parameters/SPVR_FootLeft_Latch_IsPosed");
        osc_lock_path_right_foot = jval(j, "osc_lock_path_right_foot", "/avatar/parameters/SPVR_FootRight_Latch_IsPosed");
        osc_lock_path_hip = jval(j, "osc_lock_path_hip", "/avatar/parameters/SPVR_Hip_Latch_IsPosed");
        
        // Load OSC include paths
        osc_include_path_hmd = jval(j, "osc_include_path_hmd", "/avatar/parameters/SPVR_HMD_include");
        osc_include_path_left_hand = jval(j, "osc_include_path_left_hand", "/avatar/parameters/SPVR_ControllerLeft_include");
        osc_include_path_right_hand = jval(j, "osc_include_path_right_hand", "/avatar/parameters/SPVR_ControllerRight_include");
        osc_include_path_left_foot = jval(j, "osc_include_path_left_foot", "/avatar/parameters/SPVR_FootLeft_include");
        osc_include_path_right_foot = jval(j, "osc_include_path_right_foot", "/avatar/parameters/SPVR_FootRight_include");
        osc_include_path_hip = jval(j, "osc_include_path_hip", "/avatar/parameters/SPVR_Hip_include");
        
        // Load global lock/unlock paths
        osc_global_lock_path = jval(j, "osc_global_lock_path", "/avatar/parameters/SPVR_Global_Lock");
        osc_global_unlock_path = jval(j, "osc_global_unlock_path", "/avatar/parameters/SPVR_Global_Unlock");
        osc_global_out_of_bounds_path = jval(j, "osc_global_out_of_bounds_path", "/avatar/parameters/SPVR_Global_OutOfBounds");
        osc_global_out_of_bounds_enabled = jval(j, "osc_global_out_of_bounds_enabled", true);
        osc_estop_stretch_path = jval(j, "osc_estop_stretch_path", "/avatar/parameters/SPVR_EStop_Stretch");
        osc_estop_stretch_enabled = jval(j, "osc_estop_stretch_enabled", true);
        jawopen_enabled = jval(j, "jawopen_enabled", false);
        jawopen_user_agreement = jval(j, "jawopen_user_agreement", false);
        osc_jawopen_path = jval(j, "osc_jawopen_path", "/avatar/parameters/SPVR_JawOpen");
        jawopen_warning_margin = jval(j, "jawopen_warning_margin", 0.10f);
        jawopen_disobedience_margin = jval(j, "jawopen_disobedience_margin", 0.20f);
        jawopen_grace_seconds = jval(j, "jawopen_grace_seconds", 1.0f);
        mic_enabled = jval(j, "mic_enabled", false);
        mic_user_agreement = jval(j, "mic_user_agreement", false);
        mic_device_id = jval(j, "mic_device_id", std::string(""));
        mic_warning_margin = jval(j, "mic_warning_margin", 0.05f);
        mic_disobedience_margin = jval(j, "mic_disobedience_margin", 0.10f);
        mic_grace_seconds = jval(j, "mic_grace_seconds", 2.0f);
        mic_disobedience_cooldown_seconds = jval(j, "mic_disobedience_cooldown_seconds", 1.0f);
        osc_collar_toggle_path = jval(j, "osc_collar_toggle_path", "/avatar/parameters/SPVR_Collar_ToggleButton");
        osc_bite_path = jval(j, "osc_bite_path", "/avatar/parameters/SPVR_Bite");
        osc_bite_enabled = jval(j, "osc_bite_enabled", true);
        osc_shock_path = jval(j, "osc_shock_path", "/avatar/parameters/Shock");
        osc_shock_enabled = jval(j, "osc_shock_enabled", true);
        osc_shock_intensity = jval(j, "osc_shock_intensity", 0.25f);
        osc_shock_duration = jval(j, "osc_shock_duration", 1.0f);
        osc_bite_intensity = jval(j, "osc_bite_intensity", 0.25f);
        osc_bite_duration = jval(j, "osc_bite_duration", 1.0f);
        osc_bite_use_individual_intensities = jval(j, "osc_bite_use_individual_intensities", false);
        osc_shock_use_individual_intensities = jval(j, "osc_shock_use_individual_intensities", false);

        // PiShock settings
        pishock_enabled = jval(j, "pishock_enabled", false);
        pishock_group = jval(j, "pishock_group", 0);
        pishock_user_agreement = jval(j, "pishock_user_agreement", false);
        // New installs default to WebSocket v2. Preserve existing legacy users:
        // if pishock_mode was never saved but a legacy share code is present, the
        // user configured the legacy HTTP API, so keep them on LEGACY_API.
        if (j.contains("pishock_mode")) {
            pishock_mode = static_cast<PiShockMode>(jval(j, "pishock_mode", static_cast<int>(PiShockMode::WEBSOCKET_V2)));
        } else {
            bool has_legacy_share_code = !jval(j, "pishock_share_code", std::string("")).empty();
            pishock_mode = has_legacy_share_code ? PiShockMode::LEGACY_API : PiShockMode::WEBSOCKET_V2;
        }
        
        // PiShock API settings
        pishock_api_key = jval(j, "pishock_api_key", "");
        pishock_username = jval(j, "pishock_username", "");
        pishock_user_id = jval(j, "pishock_user_id", 0);
        pishock_share_code = jval(j, "pishock_share_code", "");
        pishock_client_id = jval(j, "pishock_client_id", "");
        
        // Load multiple shocker IDs - support both old single ID and new array format
        if (j.contains("pishock_shocker_ids") && j["pishock_shocker_ids"].is_array()) {
            auto shocker_ids_json = j["pishock_shocker_ids"];
            for (size_t i = 0; i < min(shocker_ids_json.size(), static_cast<size_t>(5)); ++i) {
                if (shocker_ids_json[i].is_number_integer()) {
                    pishock_shocker_ids[i] = shocker_ids_json[i];
                }
            }
        } else if (j.contains("pishock_shocker_id") && j["pishock_shocker_id"].is_number_integer()) {
            // Legacy single shocker ID - put it in slot 0
            pishock_shocker_ids[0] = j["pishock_shocker_id"];
        }
        
        // Warning Zone PiShock Settings
        pishock_warning_beep = jval(j, "pishock_warning_beep", false);
        pishock_warning_shock = jval(j, "pishock_warning_shock", false);
        pishock_warning_vibrate = jval(j, "pishock_warning_vibrate", false);
        pishock_warning_intensity = jval(j, "pishock_warning_intensity", 0.25f);
        pishock_warning_duration = jval(j, "pishock_warning_duration", 1.0f);

        // Disobedience (Out of Bounds) PiShock Settings
        pishock_disobedience_beep = jval(j, "pishock_disobedience_beep", false);
        pishock_disobedience_shock = jval(j, "pishock_disobedience_shock", false);
        pishock_disobedience_vibrate = jval(j, "pishock_disobedience_vibrate", false);
        pishock_disobedience_intensity = jval(j, "pishock_disobedience_intensity", 0.25f);
        pishock_disobedience_duration = jval(j, "pishock_disobedience_duration", 1.0f);

        // Migrate old normalized PiShock durations (0.0-1.0) to seconds (1.0-15.0).
        // Only for configs that predate seconds-based PiShock durations (v < 1).
        if (loaded_config_version < 1) {
            if (pishock_warning_duration >= 0.0f && pishock_warning_duration <= 1.0f) {
                pishock_warning_duration = (std::max)(1.0f, pishock_warning_duration * 15.0f);
            }
            if (pishock_disobedience_duration >= 0.0f && pishock_disobedience_duration <= 1.0f) {
                pishock_disobedience_duration = (std::max)(1.0f, pishock_disobedience_duration * 15.0f);
            }
        }
        
        // Individual device intensities for PiShock
        pishock_use_individual_disobedience_intensities = jval(j, "pishock_use_individual_disobedience_intensities", false);
        
        if (j.contains("pishock_individual_disobedience_intensities") && j["pishock_individual_disobedience_intensities"].is_array()) {
            auto intensities_json = j["pishock_individual_disobedience_intensities"];
            for (size_t i = 0; i < min(intensities_json.size(), static_cast<size_t>(5)); ++i) {
                if (intensities_json[i].is_number()) {
                    pishock_individual_disobedience_intensities[i] = intensities_json[i];
                }
            }
        }

        // OpenShock Settings
        openshock_enabled = jval(j, "openshock_enabled", false);
        openshock_user_agreement = jval(j, "openshock_user_agreement", false);
        
        // OpenShock API Settings
        openshock_api_token = jval(j, "openshock_api_token", "");
        
        // Load multiple device IDs - support both old single ID and new array format
        if (j.contains("openshock_device_ids") && j["openshock_device_ids"].is_array()) {
            auto device_ids_json = j["openshock_device_ids"];
            for (size_t i = 0; i < min(device_ids_json.size(), static_cast<size_t>(5)); ++i) {
                if (device_ids_json[i].is_string()) {
                    openshock_device_ids[i] = device_ids_json[i];
                }
            }
        } else if (j.contains("openshock_device_id") && j["openshock_device_id"].is_string()) {
            // Legacy single device ID - put it in slot 0
            openshock_device_ids[0] = j["openshock_device_id"];
        }
        
        openshock_server_url = jval(j, "openshock_server_url", "https://api.openshock.app");
        
        // Warning Zone OpenShock Settings
        openshock_warning_action = jval(j, "openshock_warning_action", 0);
        openshock_warning_intensity = jval(j, "openshock_warning_intensity", 0.25f);
        openshock_warning_duration = jval(j, "openshock_warning_duration", 0.25f);
        
        // Disobedience (Out of Bounds) OpenShock Settings
        openshock_disobedience_action = jval(j, "openshock_disobedience_action", 0);
        openshock_disobedience_intensity = jval(j, "openshock_disobedience_intensity", 0.25f);
        openshock_disobedience_duration = jval(j, "openshock_disobedience_duration", 0.25f);

        // Migrate old normalized OpenShock durations (0.0-1.0) to seconds (v < 2).
        // The old API mapping was ms = 300 + norm*10714, so convert back to the
        // real second value to preserve each user's actual shock length (0.25 ->
        // ~3.0s, 1.0 -> ~11.0s) instead of reinterpreting 0.25 as a literal 0.25s.
        // Guarded by the on-disk version so already-seconds values are left alone.
        if (loaded_config_version < 2) {
            openshock_warning_duration = (std::max)(0.3f, (std::min)(15.0f,
                (300.0f + openshock_warning_duration * 10714.0f) / 1000.0f));
            openshock_disobedience_duration = (std::max)(0.3f, (std::min)(15.0f,
                (300.0f + openshock_disobedience_duration * 10714.0f) / 1000.0f));
        }

        // All version-gated migrations are done; stamp the member current.
        config_version = CURRENT_CONFIG_VERSION;

        // Master intensity settings for OpenShock
        openshock_use_individual_warning_intensities = jval(j, "openshock_use_individual_warning_intensities", false);
        openshock_use_individual_disobedience_intensities = jval(j, "openshock_use_individual_disobedience_intensities", false);
        openshock_master_warning_intensity = jval(j, "openshock_master_warning_intensity", 0.25f);
        openshock_master_disobedience_intensity = jval(j, "openshock_master_disobedience_intensity", 0.25f);
        
        // Individual device intensities for OpenShock
        if (j.contains("openshock_individual_warning_intensities") && j["openshock_individual_warning_intensities"].is_array()) {
            auto warning_intensities = j["openshock_individual_warning_intensities"];
            for (size_t i = 0; i < min(warning_intensities.size(), static_cast<size_t>(5)); ++i) {
                if (warning_intensities[i].is_number()) {
                    openshock_individual_warning_intensities[i] = warning_intensities[i];
                }
            }
        }
        
        if (j.contains("openshock_individual_disobedience_intensities") && j["openshock_individual_disobedience_intensities"].is_array()) {
            auto disobedience_intensities = j["openshock_individual_disobedience_intensities"];
            for (size_t i = 0; i < min(disobedience_intensities.size(), static_cast<size_t>(5)); ++i) {
                if (disobedience_intensities[i].is_number()) {
                    openshock_individual_disobedience_intensities[i] = disobedience_intensities[i];
                }
            }
        }

        // Buttplug/Intiface Settings
        buttplug_enabled = jval(j, "buttplug_enabled", false);
        buttplug_user_agreement = jval(j, "buttplug_user_agreement", false);
        
        // Buttplug Server Settings
        buttplug_server_address = jval(j, "buttplug_server_address", "localhost");
        buttplug_server_port = jval(j, "buttplug_server_port", 12345);
        
        // Load multiple device indices
        if (j.contains("buttplug_device_indices") && j["buttplug_device_indices"].is_array()) {
            auto device_indices_json = j["buttplug_device_indices"];
            for (size_t i = 0; i < min(device_indices_json.size(), static_cast<size_t>(5)); ++i) {
                if (device_indices_json[i].is_number_integer()) {
                    buttplug_device_indices[i] = device_indices_json[i];
                }
            }
        }
        
        // Zone activation settings
        buttplug_safe_zone_enabled = jval(j, "buttplug_safe_zone_enabled", false);
        buttplug_warning_zone_enabled = jval(j, "buttplug_warning_zone_enabled", true);
        buttplug_disobedience_zone_enabled = jval(j, "buttplug_disobedience_zone_enabled", true);
        
        // Safe Zone Buttplug Settings
        buttplug_safe_intensity = jval(j, "buttplug_safe_intensity", 0.15f);
        buttplug_safe_duration = jval(j, "buttplug_safe_duration", 1.0f);
        
        // Warning Zone Buttplug Settings
        buttplug_warning_intensity = jval(j, "buttplug_warning_intensity", 0.25f);
        buttplug_warning_duration = jval(j, "buttplug_warning_duration", 1.0f);
        
        // Disobedience (Out of Bounds) Buttplug Settings
        buttplug_disobedience_intensity = jval(j, "buttplug_disobedience_intensity", 0.5f);
        buttplug_disobedience_duration = jval(j, "buttplug_disobedience_duration", 2.0f);
        
        // Master intensity settings for Buttplug
        buttplug_use_individual_safe_intensities = jval(j, "buttplug_use_individual_safe_intensities", false);
        buttplug_use_individual_warning_intensities = jval(j, "buttplug_use_individual_warning_intensities", false);
        buttplug_use_individual_disobedience_intensities = jval(j, "buttplug_use_individual_disobedience_intensities", false);
        buttplug_master_safe_intensity = jval(j, "buttplug_master_safe_intensity", 0.15f);
        buttplug_master_warning_intensity = jval(j, "buttplug_master_warning_intensity", 0.25f);
        buttplug_master_disobedience_intensity = jval(j, "buttplug_master_disobedience_intensity", 0.5f);
        
        // Individual device intensities for Buttplug
        if (j.contains("buttplug_individual_safe_intensities") && j["buttplug_individual_safe_intensities"].is_array()) {
            auto safe_intensities = j["buttplug_individual_safe_intensities"];
            for (size_t i = 0; i < min(safe_intensities.size(), static_cast<size_t>(5)); ++i) {
                if (safe_intensities[i].is_number()) {
                    buttplug_individual_safe_intensities[i] = safe_intensities[i];
                }
            }
        }
        
        if (j.contains("buttplug_individual_warning_intensities") && j["buttplug_individual_warning_intensities"].is_array()) {
            auto warning_intensities = j["buttplug_individual_warning_intensities"];
            for (size_t i = 0; i < min(warning_intensities.size(), static_cast<size_t>(5)); ++i) {
                if (warning_intensities[i].is_number()) {
                    buttplug_individual_warning_intensities[i] = warning_intensities[i];
                }
            }
        }
        
        if (j.contains("buttplug_individual_disobedience_intensities") && j["buttplug_individual_disobedience_intensities"].is_array()) {
            auto disobedience_intensities = j["buttplug_individual_disobedience_intensities"];
            for (size_t i = 0; i < min(disobedience_intensities.size(), static_cast<size_t>(5)); ++i) {
                if (disobedience_intensities[i].is_number()) {
                    buttplug_individual_disobedience_intensities[i] = disobedience_intensities[i];
                }
            }
        }

        // Twitch Integration Settings
        twitch_enabled = jval(j, "twitch_enabled", false);
        twitch_user_agreement = jval(j, "twitch_user_agreement", false);
        
        // Twitch API Authentication
        twitch_client_id = jval(j, "twitch_client_id", "");
        twitch_client_secret = jval(j, "twitch_client_secret", "");
        twitch_access_token = jval(j, "twitch_access_token", "");
        twitch_refresh_token = jval(j, "twitch_refresh_token", "");
        twitch_channel_name = jval(j, "twitch_channel_name", "");
        twitch_bot_username = jval(j, "twitch_bot_username", "");
        
        // Twitch Chat Bot Settings
        twitch_chat_enabled = jval(j, "twitch_chat_enabled", false);
        twitch_command_prefix = jval(j, "twitch_command_prefix", "!");
        twitch_lock_command = jval(j, "twitch_lock_command", "lock");
        twitch_unlock_command = jval(j, "twitch_unlock_command", "unlock");
        twitch_status_command = jval(j, "twitch_status_command", "status");
        
        // Twitch Donation Trigger Settings
        twitch_bits_enabled = jval(j, "twitch_bits_enabled", false);
        twitch_bits_minimum = jval(j, "twitch_bits_minimum", 100);
        twitch_subs_enabled = jval(j, "twitch_subs_enabled", false);
        twitch_donations_enabled = jval(j, "twitch_donations_enabled", false);
        twitch_donation_minimum = jval(j, "twitch_donation_minimum", 5.0f);
        
        // Twitch Lock Duration Settings
        twitch_lock_duration_enabled = jval(j, "twitch_lock_duration_enabled", false);
        twitch_lock_base_duration = jval(j, "twitch_lock_base_duration", 60.0f);
        twitch_lock_per_dollar = jval(j, "twitch_lock_per_dollar", 30.0f);
        twitch_lock_max_duration = jval(j, "twitch_lock_max_duration", 600.0f);
        
        // Twitch Device Targeting
        twitch_target_all_devices = jval(j, "twitch_target_all_devices", true);
        twitch_target_hmd = jval(j, "twitch_target_hmd", false);
        twitch_target_left_hand = jval(j, "twitch_target_left_hand", false);
        twitch_target_right_hand = jval(j, "twitch_target_right_hand", false);
        twitch_target_left_foot = jval(j, "twitch_target_left_foot", false);
        twitch_target_right_foot = jval(j, "twitch_target_right_foot", false);
        twitch_target_hip = jval(j, "twitch_target_hip", false);
        
        // Unlock Timer Settings
        unlock_timer_enabled = jval(j, "unlock_timer_enabled", false);
        unlock_timer_duration = jval(j, "unlock_timer_duration", 300.0f);
        unlock_timer_show_remaining = jval(j, "unlock_timer_show_remaining", true);
        unlock_timer_audio_warnings = jval(j, "unlock_timer_audio_warnings", true);

        // Load logging settings
        log_level = jval(j, "log_level", "WARNING");
        ui_font_scale = jval(j, "ui_font_scale", 1.0f);
        splash_auto_close = jval(j, "splash_auto_close", false);
        whats_new_seen_version = jval(j, "whats_new_seen_version", std::string(""));

        // Load boundary settings
        warning_threshold = jval(j, "warning_threshold", 0.1f);
        bounds_threshold = jval(j, "bounds_threshold", 0.2f);
        disable_threshold = jval(j, "disable_threshold", 0.5f);
        
        // Load timer settings
        cooldown_enabled = jval(j, "cooldown_enabled", false);
        cooldown_seconds = jval(j, "cooldown_seconds", 5.0f);
        countdown_enabled = jval(j, "countdown_enabled", false);
        countdown_seconds = jval(j, "countdown_seconds", 3.0f);
        shock_cooldown_enabled = jval(j, "shock_cooldown_enabled", false);
        shock_cooldown_seconds = jval(j, "shock_cooldown_seconds", 5.0f);
        
        // Load notification settings
        // Audio settings — read flat keys for backward compatibility with pre-1.3 configs
        audio.enabled = jval(j, "audio_enabled", true);
        audio.volume = jval(j, "audio_volume", 0.8f);
        audio.warning = jval(j, "warning_audio", true);
        audio.out_of_bounds = jval(j, "out_of_bounds_audio", true);
        audio.lock = jval(j, "lock_audio", true);
        audio.unlock = jval(j, "unlock_audio", true);
        audio.haptic_enabled = jval(j, "haptic_enabled", true);
        audio.haptic_intensity = jval(j, "haptic_intensity", 0.5f);

        // In-game sound effects
        ingame_sfx_enabled = jval(j, "ingame_sfx_enabled", true);
        ingame_sfx_lock = jval(j, "ingame_sfx_lock", true);
        ingame_sfx_unlock = jval(j, "ingame_sfx_unlock", true);
        ingame_sfx_warning = jval(j, "ingame_sfx_warning", true);
        ingame_sfx_disobedience = jval(j, "ingame_sfx_disobedience", true);
        ingame_sfx_collar_mode = jval(j, "ingame_sfx_collar_mode", true);
        osc_sound_effect_path = jval(j, "osc_sound_effect_path", "/avatar/parameters/SPVR_SoundEffect");

        // Load application settings
        start_with_steamvr = jval(j, "start_with_steamvr", true);
        minimize_to_tray = jval(j, "minimize_to_tray", false);
        show_notifications = jval(j, "show_notifications", true);
        
        // Clear existing device data
        device_names.clear();
        device_settings.clear();
        device_roles.clear();
        device_pishock_ids.clear();
        device_openshock_ids.clear();
        device_vibration_ids.clear();
        
        // Load device names, settings, and roles from new format (direct properties)
        if (j.contains("device_names") && j["device_names"].is_object()) {
            for (auto& [serial, name] : j["device_names"].items()) {
                std::string n = name.get<std::string>();
                if (n.empty() || n == "Unknown Device") continue; // drop placeholder pollution
                device_names[serial] = n;
                if (Logger::IsInitialized()) {
                    Logger::Debug("Loaded device name from direct property: " + serial + " -> " + n);
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
        
        // Helper: load a serial -> array<bool,5> map from a JSON object.
        auto load_bool5_map = [](const nlohmann::json& obj,
                                 std::unordered_map<std::string, std::array<bool, 5>>& out) {
            for (auto& [serial, arr] : obj.items()) {
                if (arr.is_array() && arr.size() >= 5) {
                    std::array<bool, 5> vals;
                    for (size_t i = 0; i < 5; ++i) vals[i] = arr[i].get<bool>();
                    out[serial] = vals;
                }
            }
        };

        // New split maps (PiShock and OpenShock are bound to devices separately).
        if (j.contains("device_pishock_ids") && j["device_pishock_ids"].is_object())
            load_bool5_map(j["device_pishock_ids"], device_pishock_ids);
        if (j.contains("device_openshock_ids") && j["device_openshock_ids"].is_object())
            load_bool5_map(j["device_openshock_ids"], device_openshock_ids);

        // Migration: older configs stored a single shared "device_shock_ids" that
        // drove both PiShock and OpenShock. Seed both split maps from it so users
        // keep their existing per-device bindings.
        if (j.contains("device_shock_ids") && j["device_shock_ids"].is_object()) {
            std::unordered_map<std::string, std::array<bool, 5>> legacy;
            load_bool5_map(j["device_shock_ids"], legacy);
            for (const auto& [serial, vals] : legacy) {
                if (device_pishock_ids.find(serial) == device_pishock_ids.end())
                    device_pishock_ids[serial] = vals;
                if (device_openshock_ids.find(serial) == device_openshock_ids.end())
                    device_openshock_ids[serial] = vals;
            }
        }

        if (j.contains("device_vibration_ids") && j["device_vibration_ids"].is_object()) {
            for (auto& [serial, vibration_ids_json] : j["device_vibration_ids"].items()) {
                if (vibration_ids_json.is_array() && vibration_ids_json.size() >= 5) {
                    std::array<bool, 5> vibration_ids;
                    for (size_t i = 0; i < 5; ++i) {
                        vibration_ids[i] = vibration_ids_json[i].get<bool>();
                    }
                    device_vibration_ids[serial] = vibration_ids;
                    if (Logger::IsInitialized()) {
                        Logger::Debug("Loaded device vibration IDs for " + serial);
                    }
                }
            }
        }
        
        // Also check the old format (devices array) for backward compatibility
        // This will add any devices that weren't already loaded from the direct properties
        const nlohmann::json& devices = jval(j, "devices", nlohmann::json::array());
        for (const auto& device : devices) {
            if (!device.contains("serial")) continue;
            
            std::string serial = device.value("serial", "Unknown Device");
            
            // Load device name if present and not already loaded. Ignore the old
            // "Unknown Device" placeholder so the real serial shows through.
            if (device.contains("name") && device_names.find(serial) == device_names.end()) {
                std::string name = device.value("name", "");
                if (!name.empty() && name != "Unknown Device") {
                    device_names[serial] = name;
                    if (Logger::IsInitialized()) {
                        Logger::Debug("Loaded device name from devices array: " + serial + " -> " + name);
                    }
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
        result.status = ConfigStatus::Ok;
        return result;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("Error loading config: " + std::string(e.what()));
        }
        result.status = ConfigStatus::OtherError;
        result.detail = e.what();
        return result;
    }
}

ConfigResult Config::SaveToFileEx(const std::string& filename) const {
    ConfigResult result;
    try {
        auto lock = ReadLock();
        nlohmann::json j;

        // Config versioning
        j["config_version"] = CURRENT_CONFIG_VERSION;

        // OSC settings
        j["osc_enabled"] = osc_enabled;
        j["osc_address"] = osc_address;
        j["osc_send_port"] = osc_send_port;
        j["osc_receive_port"] = osc_receive_port;
        j["osc_query_enabled"] = osc_query_enabled;
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
        j["osc_estop_stretch_path"] = osc_estop_stretch_path;
        j["osc_estop_stretch_enabled"] = osc_estop_stretch_enabled;
        j["jawopen_enabled"] = jawopen_enabled;
        j["jawopen_user_agreement"] = jawopen_user_agreement;
        j["osc_jawopen_path"] = osc_jawopen_path;
        j["jawopen_warning_margin"] = jawopen_warning_margin;
        j["jawopen_disobedience_margin"] = jawopen_disobedience_margin;
        j["jawopen_grace_seconds"] = jawopen_grace_seconds;
        j["mic_enabled"] = mic_enabled;
        j["mic_user_agreement"] = mic_user_agreement;
        j["mic_device_id"] = mic_device_id;
        j["mic_warning_margin"] = mic_warning_margin;
        j["mic_disobedience_margin"] = mic_disobedience_margin;
        j["mic_grace_seconds"] = mic_grace_seconds;
        j["mic_disobedience_cooldown_seconds"] = mic_disobedience_cooldown_seconds;
        j["osc_collar_toggle_path"] = osc_collar_toggle_path;
        j["osc_bite_path"] = osc_bite_path;
        j["osc_bite_enabled"] = osc_bite_enabled;
        j["osc_shock_path"] = osc_shock_path;
        j["osc_shock_enabled"] = osc_shock_enabled;
        j["osc_shock_intensity"] = osc_shock_intensity;
        j["osc_shock_duration"] = osc_shock_duration;
        j["osc_bite_intensity"] = osc_bite_intensity;
        j["osc_bite_duration"] = osc_bite_duration;
        j["osc_bite_use_individual_intensities"] = osc_bite_use_individual_intensities;
        j["osc_shock_use_individual_intensities"] = osc_shock_use_individual_intensities;

        // PiShock settings
        j["pishock_enabled"] = pishock_enabled;
        j["pishock_group"] = pishock_group;
        j["pishock_user_agreement"] = pishock_user_agreement;
        j["pishock_mode"] = static_cast<int>(pishock_mode);
        
        // PiShock API settings
        j["pishock_api_key"] = pishock_api_key;
        j["pishock_username"] = pishock_username;
        j["pishock_user_id"] = pishock_user_id;
        j["pishock_share_code"] = pishock_share_code;
        j["pishock_client_id"] = pishock_client_id;
        
        // Save shocker IDs array
        nlohmann::json shocker_ids_json = nlohmann::json::array();
        for (const auto& id : pishock_shocker_ids) {
            shocker_ids_json.push_back(id);
        }
        j["pishock_shocker_ids"] = shocker_ids_json;

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
        
        // Individual device intensities for PiShock
        j["pishock_use_individual_disobedience_intensities"] = pishock_use_individual_disobedience_intensities;
        
        nlohmann::json pishock_intensities_json = nlohmann::json::array();
        for (const auto& intensity : pishock_individual_disobedience_intensities) {
            pishock_intensities_json.push_back(intensity);
        }
        j["pishock_individual_disobedience_intensities"] = pishock_intensities_json;

        // OpenShock Settings
        j["openshock_enabled"] = openshock_enabled;
        j["openshock_user_agreement"] = openshock_user_agreement;
        
        // OpenShock API Settings
        j["openshock_api_token"] = openshock_api_token;
        
        // Save device IDs array
        nlohmann::json device_ids_json = nlohmann::json::array();
        for (const auto& id : openshock_device_ids) {
            device_ids_json.push_back(id);
        }
        j["openshock_device_ids"] = device_ids_json;
        
        j["openshock_server_url"] = openshock_server_url;
        
        // Warning Zone OpenShock Settings
        j["openshock_warning_action"] = openshock_warning_action;
        j["openshock_warning_intensity"] = openshock_warning_intensity;
        j["openshock_warning_duration"] = openshock_warning_duration;
        
        // Disobedience (Out of Bounds) OpenShock Settings
        j["openshock_disobedience_action"] = openshock_disobedience_action;
        j["openshock_disobedience_intensity"] = openshock_disobedience_intensity;
        j["openshock_disobedience_duration"] = openshock_disobedience_duration;
        
        // Master intensity settings for OpenShock
        j["openshock_use_individual_warning_intensities"] = openshock_use_individual_warning_intensities;
        j["openshock_use_individual_disobedience_intensities"] = openshock_use_individual_disobedience_intensities;
        j["openshock_master_warning_intensity"] = openshock_master_warning_intensity;
        j["openshock_master_disobedience_intensity"] = openshock_master_disobedience_intensity;
        
        // Individual device intensities for OpenShock
        nlohmann::json warning_intensities_json = nlohmann::json::array();
        for (const auto& intensity : openshock_individual_warning_intensities) {
            warning_intensities_json.push_back(intensity);
        }
        j["openshock_individual_warning_intensities"] = warning_intensities_json;
        
        nlohmann::json disobedience_intensities_json = nlohmann::json::array();
        for (const auto& intensity : openshock_individual_disobedience_intensities) {
            disobedience_intensities_json.push_back(intensity);
        }
        j["openshock_individual_disobedience_intensities"] = disobedience_intensities_json;

        // Buttplug/Intiface Settings
        j["buttplug_enabled"] = buttplug_enabled;
        j["buttplug_user_agreement"] = buttplug_user_agreement;
        
        // Buttplug Server Settings
        j["buttplug_server_address"] = buttplug_server_address;
        j["buttplug_server_port"] = buttplug_server_port;
        
        // Save device indices array
        nlohmann::json buttplug_device_indices_json = nlohmann::json::array();
        for (const auto& idx : buttplug_device_indices) {
            buttplug_device_indices_json.push_back(idx);
        }
        j["buttplug_device_indices"] = buttplug_device_indices_json;
        
        // Zone activation settings
        j["buttplug_safe_zone_enabled"] = buttplug_safe_zone_enabled;
        j["buttplug_warning_zone_enabled"] = buttplug_warning_zone_enabled;
        j["buttplug_disobedience_zone_enabled"] = buttplug_disobedience_zone_enabled;
        
        // Safe Zone Buttplug Settings
        j["buttplug_safe_intensity"] = buttplug_safe_intensity;
        j["buttplug_safe_duration"] = buttplug_safe_duration;
        
        // Warning Zone Buttplug Settings
        j["buttplug_warning_intensity"] = buttplug_warning_intensity;
        j["buttplug_warning_duration"] = buttplug_warning_duration;
        
        // Disobedience (Out of Bounds) Buttplug Settings
        j["buttplug_disobedience_intensity"] = buttplug_disobedience_intensity;
        j["buttplug_disobedience_duration"] = buttplug_disobedience_duration;
        
        // Master intensity settings for Buttplug
        j["buttplug_use_individual_safe_intensities"] = buttplug_use_individual_safe_intensities;
        j["buttplug_use_individual_warning_intensities"] = buttplug_use_individual_warning_intensities;
        j["buttplug_use_individual_disobedience_intensities"] = buttplug_use_individual_disobedience_intensities;
        j["buttplug_master_safe_intensity"] = buttplug_master_safe_intensity;
        j["buttplug_master_warning_intensity"] = buttplug_master_warning_intensity;
        j["buttplug_master_disobedience_intensity"] = buttplug_master_disobedience_intensity;
        
        // Individual device intensities for Buttplug
        nlohmann::json buttplug_safe_intensities_json = nlohmann::json::array();
        for (const auto& intensity : buttplug_individual_safe_intensities) {
            buttplug_safe_intensities_json.push_back(intensity);
        }
        j["buttplug_individual_safe_intensities"] = buttplug_safe_intensities_json;
        
        nlohmann::json buttplug_warning_intensities_json = nlohmann::json::array();
        for (const auto& intensity : buttplug_individual_warning_intensities) {
            buttplug_warning_intensities_json.push_back(intensity);
        }
        j["buttplug_individual_warning_intensities"] = buttplug_warning_intensities_json;
        
        nlohmann::json buttplug_disobedience_intensities_json = nlohmann::json::array();
        for (const auto& intensity : buttplug_individual_disobedience_intensities) {
            buttplug_disobedience_intensities_json.push_back(intensity);
        }
        j["buttplug_individual_disobedience_intensities"] = buttplug_disobedience_intensities_json;

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
        j["ui_font_scale"] = ui_font_scale;
        j["splash_auto_close"] = splash_auto_close;
        j["whats_new_seen_version"] = whats_new_seen_version;

        // Boundary settings
        j["warning_threshold"] = warning_threshold;
        j["bounds_threshold"] = bounds_threshold;
        j["disable_threshold"] = disable_threshold;
        
        // Timer settings
        j["cooldown_enabled"] = cooldown_enabled;
        j["cooldown_seconds"] = cooldown_seconds;
        j["countdown_enabled"] = countdown_enabled;
        j["countdown_seconds"] = countdown_seconds;
        j["shock_cooldown_enabled"] = shock_cooldown_enabled;
        j["shock_cooldown_seconds"] = shock_cooldown_seconds;
        
        // Notification settings
        j["audio_enabled"] = audio.enabled;
        j["audio_volume"] = audio.volume;
        j["warning_audio"] = audio.warning;
        j["out_of_bounds_audio"] = audio.out_of_bounds;
        j["lock_audio"] = audio.lock;
        j["unlock_audio"] = audio.unlock;
        j["haptic_enabled"] = audio.haptic_enabled;
        j["haptic_intensity"] = audio.haptic_intensity;

        j["ingame_sfx_enabled"] = ingame_sfx_enabled;
        j["ingame_sfx_lock"] = ingame_sfx_lock;
        j["ingame_sfx_unlock"] = ingame_sfx_unlock;
        j["ingame_sfx_warning"] = ingame_sfx_warning;
        j["ingame_sfx_disobedience"] = ingame_sfx_disobedience;
        j["ingame_sfx_collar_mode"] = ingame_sfx_collar_mode;
        j["osc_sound_effect_path"] = osc_sound_effect_path;

        // Application settings
        j["start_with_steamvr"] = start_with_steamvr;
        j["minimize_to_tray"] = minimize_to_tray;
        j["show_notifications"] = show_notifications;
        
        // Save device names and settings directly at the root level
        // Create JSON objects for device_roles, device_settings, and shock/vibe IDs
        nlohmann::json device_roles_json = nlohmann::json::object();
        nlohmann::json device_settings_json = nlohmann::json::object();
        nlohmann::json device_names_json = nlohmann::json::object();
        nlohmann::json device_pishock_ids_json = nlohmann::json::object();
        nlohmann::json device_openshock_ids_json = nlohmann::json::object();
        nlohmann::json device_vibration_ids_json = nlohmann::json::object();
        
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
        
        // Populate device PiShock + OpenShock slot bindings (now separate).
        for (const auto& [serial, ids] : device_pishock_ids) {
            nlohmann::json arr = nlohmann::json::array();
            for (bool enabled : ids) arr.push_back(enabled);
            device_pishock_ids_json[serial] = arr;
        }
        j["device_pishock_ids"] = device_pishock_ids_json;

        for (const auto& [serial, ids] : device_openshock_ids) {
            nlohmann::json arr = nlohmann::json::array();
            for (bool enabled : ids) arr.push_back(enabled);
            device_openshock_ids_json[serial] = arr;
        }
        j["device_openshock_ids"] = device_openshock_ids_json;
        
        // Populate device vibration IDs
        for (const auto& [serial, vibration_ids] : device_vibration_ids) {
            nlohmann::json vibration_ids_array = nlohmann::json::array();
            for (bool enabled : vibration_ids) {
                vibration_ids_array.push_back(enabled);
            }
            device_vibration_ids_json[serial] = vibration_ids_array;
        }
        j["device_vibration_ids"] = device_vibration_ids_json;
        
        // Populate the devices array for backward compatibility
        nlohmann::json devices = nlohmann::json::array();
        // Create a set of all serials across all device maps
        std::unordered_set<std::string> all_serials;
        for (const auto& [serial, _] : device_names) all_serials.insert(serial);
        for (const auto& [serial, _] : device_settings) all_serials.insert(serial);
        for (const auto& [serial, _] : device_roles) all_serials.insert(serial);
        for (const auto& [serial, _] : device_pishock_ids) all_serials.insert(serial);
        for (const auto& [serial, _] : device_openshock_ids) all_serials.insert(serial);
        for (const auto& [serial, _] : device_vibration_ids) all_serials.insert(serial);
        
        // Create device objects
        for (const auto& serial : all_serials) {
            nlohmann::json device;
            device["serial"] = serial;
            
            // Add name only if a real custom name exists. Writing a placeholder
            // like "Unknown Device" here used to get read back as the device's
            // name on the next load, masking the real SteamVR serial.
            auto name_it = device_names.find(serial);
            if (name_it != device_names.end() && !name_it->second.empty() &&
                name_it->second != "Unknown Device") {
                device["name"] = name_it->second;
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
        
        // Resolve a bare filename to the canonical AppData store and make sure
        // its directory exists, so saves always land in one well-known place.
        std::string path = HasDirComponent(filename) ? filename : CanonicalConfigPath(filename);
        result.path = path;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

        // Write to a sibling temp file first, then atomically rename it over the
        // real config. A crash or a disk-full midway can no longer leave a
        // half-written (corrupt) config.ini -- the original stays intact until
        // the complete new copy is in place.
        std::string tmp = path + ".tmp";
        errno = 0;
        {
            std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                int err = errno;
                result.status = ClassifyOpenErrno(err);
                if (result.status == ConfigStatus::NotFound) result.status = ConfigStatus::OtherError;
                result.os_error = err;
                result.detail = err ? std::strerror(err) : "unknown error";
                if (Logger::IsInitialized()) {
                    if (result.status == ConfigStatus::AccessDenied) {
                        Logger::Error("ACCESS DENIED writing config file: " + path +
                                      " (" + result.detail + "). Settings cannot be saved -- the "
                                      "config folder may be owned by another account, read-only, or "
                                      "blocked by antivirus / Controlled Folder Access.");
                    } else {
                        Logger::Error("Failed to open config file for writing: " + path +
                                      " (" + result.detail + ")");
                    }
                }
                return result;
            }

            file << j.dump(4);
            file.flush();
            // Catch write failures (e.g. disk full) that don't show up at open time.
            if (!file.good()) {
                result.status = ConfigStatus::OtherError;
                result.detail = "write/flush failed (disk full?)";
                if (Logger::IsInitialized()) {
                    Logger::Error("Failed while writing config file: " + tmp + " (" + result.detail + ")");
                }
                file.close();
                std::error_code rmec;
                std::filesystem::remove(tmp, rmec);
                return result;
            }
        } // ofstream closed here, before the rename

        std::error_code rnec;
        std::filesystem::rename(tmp, path, rnec);
        if (rnec) {
            // Some Windows configurations refuse a replace-rename (e.g. the
            // destination is locked). Fall back to a direct overwrite so we at
            // least try to persist, and report the underlying error if that fails too.
            std::error_code cpec;
            std::filesystem::copy_file(tmp, path,
                std::filesystem::copy_options::overwrite_existing, cpec);
            std::error_code rmec;
            std::filesystem::remove(tmp, rmec);
            if (cpec) {
                result.status = ConfigStatus::AccessDenied;
                result.detail = rnec.message() + " / " + cpec.message();
                if (Logger::IsInitialized()) {
                    Logger::Error("Could not replace config file: " + path + " (" + result.detail + ")");
                }
                return result;
            }
        }

        if (Logger::IsInitialized()) {
            Logger::Info("Saved config file: " + path);
            Logger::Debug("Saved " + std::to_string(device_roles.size()) + " device roles, " +
                         std::to_string(device_settings.size()) + " device settings, and " +
                         std::to_string(device_names.size()) + " device names");
        }
        result.status = ConfigStatus::Ok;
        return result;
    }
    catch (const std::exception& e) {
        if (Logger::IsInitialized()) {
            Logger::Error("Error saving config: " + std::string(e.what()));
        }
        result.status = ConfigStatus::OtherError;
        result.detail = e.what();
        return result;
    }
}

} // namespace StayPutVR 