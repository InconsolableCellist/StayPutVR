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
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include "../../../common/WinsockCompat.hpp"
#endif
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "../../../common/DeviceTypes.hpp"
#include "../../../common/Config.hpp"
#include "../../../common/Audio.hpp"
#include "../../../common/Logger.hpp"
#include "../../../common/PathUtils.hpp"
#include "../DeviceManager/DeviceManager.hpp"
#include "../../../common/OSCManager.hpp"
#include "../../../common/OSCQueryServer.hpp"
#include "../managers/TwitchManager.hpp"
#include "../managers/PiShockManager.hpp"
#include "../managers/PiShockWebSocketManager.hpp"
#include "../managers/OpenShockManager.hpp"
#include "../managers/ButtplugManager.hpp"
#include "../managers/MicrophoneManager.hpp"
#include "panels/PiShockPanel.hpp"
#include "panels/OpenShockPanel.hpp"
#include "panels/ButtplugPanel.hpp"
#include "SplashScreen.hpp"

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
        TWITCH,
        INTEGRATIONS
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
        // Movement "heat" 0..1 for tracker identification: ramps up fast when the
        // device moves and cools slowly when still (updated in UpdateDevicePositions).
        float movement_heat = 0.0f;
        bool exceeds_threshold = false;
        bool in_warning_zone = false;
        
        // PiShock / OpenShock device selection - which shocker slots this device uses.
        // Tracked separately so a device can bind PiShock and OpenShock independently.
        std::array<bool, 5> pishock_enabled = {false, false, false, false, false};
        std::array<bool, 5> openshock_enabled = {false, false, false, false, false};

        // Buttplug device selection - which vibration IDs should be used for this device
        std::array<bool, 5> vibration_device_enabled = {false, false, false, false, false};
    };

    struct SimpleDevicePosition {
        std::string serial;
        float x;
        float y;
        float z;
    };

    // VRCFT JawOpen constraint state. JawOpen is a scalar OSC parameter (0..1),
    // not a tracked device, so it lives here rather than in device_positions_.
    // The baseline is captured when the HMD lock engages (after a grace window);
    // enforcement compares |current - baseline| against the configured margins,
    // mirroring the 3-D position-deviation engine in 1-D.
    struct JawOpenConstraint {
        float current = 0.0f;      // live value from OSC (SPVR_JawOpen)
        float baseline = 0.0f;     // captured ideal (frozen after grace)
        bool  active = false;      // gate satisfied and past the grace window
        bool  gate_active = false;  // previous-frame value of (armed && collar-Jaw && HMD locked)
        bool  in_grace = false;    // within the post-engage baseline-capture window
        std::chrono::steady_clock::time_point lock_time;
        float deviation = 0.0f;
        bool  in_warning_zone = false;
        bool  exceeds_threshold = false;
        std::array<bool, 5> pishock_enabled = {false, false, false, false, false};
        std::array<bool, 5> openshock_enabled = {false, false, false, false, false};
        std::array<bool, 5> vibration_device_enabled = {false, false, false, false, false};
    };

    // Microphone enforced-mute constraint state. Mirrors JawOpenConstraint in 1-D,
    // but the live value is pulled from MicrophoneManager (not OSC), the baseline is
    // the ambient room-noise floor captured during grace, and deviation is one-sided
    // (only louder-than-baseline matters). The runtime gate comes from the collar mode.
    struct MicrophoneConstraint {
        float current = 0.0f;      // live smoothed RMS level from MicrophoneManager (0..1)
        float baseline = 0.0f;     // ambient floor captured during grace (frozen after)
        float grace_floor = 1.0f;  // running minimum during grace (the captured floor)
        bool  active = false;
        bool  gate_active = false;
        bool  in_grace = false;
        std::chrono::steady_clock::time_point lock_time;
        float deviation = 0.0f;
        bool  in_warning_zone = false;
        bool  exceeds_threshold = false;
        // Refractory period: no new disobedience action fires until now passes this.
        std::chrono::steady_clock::time_point diso_cooldown_until;
        std::array<bool, 5> pishock_enabled = {false, false, false, false, false};
        std::array<bool, 5> openshock_enabled = {false, false, false, false, false};
        std::array<bool, 5> vibration_device_enabled = {false, false, false, false, false};
    };

    // Unified collar mode (replaces the old SPVR_JawEnabled radial). The avatar's
    // momentary SPVR_Collar_ToggleButton cycles through the modes whose integration
    // is enabled+agreed; the app echoes the result on SPVR_Collar_Mode.
    enum class CollarMode { Neither = 0, Jaw = 1, Mic = 2, Both = 3 };

    // In-game sound-effect events. Pulsed on SPVR_SoundEffect so an avatar
    // animation layer can play a clip. Order is the param's quantized enum.
    enum class InGameSound { None = 0, Lock = 1, Unlock = 2, Warning = 3, Disobedience = 4, CollarMode = 5 };

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

        // Record the outcome of a config save so the UI can warn (or stop
        // warning) about settings that are / are no longer being persisted.
        void NoteSaveResult(const ConfigResult& r);
        // Draw the config-health warning banner + one-time startup modal.
        void RenderConfigHealthWarning();
        
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

        // Config health, surfaced to the user. config_load_status_ is the outcome
        // of the startup load (NotFound is benign; AccessDenied/Corrupt are not).
        // config_save_failing_ latches true when a save is refused so the main
        // window can warn that settings are not being persisted. The detail/path
        // strings feed the warning banner and the modal shown once at startup.
        ConfigStatus config_load_status_ = ConfigStatus::Ok;
        bool config_save_failing_ = false;
        std::string config_health_detail_;   // OS error text for the most recent failure
        std::string config_health_path_;     // the config path involved
        bool config_health_modal_pending_ = false; // show the startup modal next frame
        
        // OSC status
        bool osc_enabled_ = false;

        // OSCQuery (mDNS) auto-discovery server. Lazily created when OSC is
        // started with config_.osc_query_enabled. Advertises our (ephemeral)
        // receive port to VRChat and discovers VRChat's OSC port for sends.
        std::unique_ptr<OSCQueryServer> osc_query_server_;

        // Startup splash / Welcome+About overlay and the What's New window.
        std::unique_ptr<SplashScreen> splash_;
        std::string assets_path_;            // resources dir (logo, whats_new.md, supporters)
        bool show_whats_new_ = false;
        bool whats_new_focus_ = false;       // bring the window to front next frame
        bool whats_new_loaded_ = false;      // lazy-load whats_new.md once
        bool whats_new_checked_ = false;     // auto-show evaluated once per launch
        std::string whats_new_text_;

        // Devices > Visual assignment view state.
        unsigned int effigy_tex_ = 0;        // GL texture id (0 = none/not loaded)
        int effigy_tex_w_ = 0, effigy_tex_h_ = 0;
        bool effigy_load_attempted_ = false; // attempt the PNG load only once
        DeviceRole selected_slot_role_ = DeviceRole::None; // slot whose config panel is open

        // VRCFT / Unified Expressions logos shown on the Integrations > VRCFT tab.
        unsigned int vrcft_logo_tex_ = 0; int vrcft_logo_w_ = 0, vrcft_logo_h_ = 0;
        unsigned int ue_logo_tex_ = 0;    int ue_logo_w_ = 0, ue_logo_h_ = 0;
        bool vrcft_logos_load_attempted_ = false;

        // VRC BiteTech logo shown on the Integrations > OSC Triggers (Bite) section.
        unsigned int bitetech_logo_tex_ = 0; int bitetech_logo_w_ = 0, bitetech_logo_h_ = 0;
        bool bitetech_logo_load_attempted_ = false;

        // Tab system
        TabType current_tab_ = TabType::MAIN;
        
        // Static callbacks for GLFW
        static void GlfwErrorCallback(int error, const char* description);
        
        // UI elements
        void RenderMainWindow();
        void RenderTabBar();
        
        // Tab content rendering methods
        void RenderMainTab();
        // Connection Status section on the Status tab: one row per enabled
        // communication integration (OSC, PiShock, OpenShock, Twitch) showing
        // live link state, detail, last error, and a manual reconnect button.
        void RenderConnectionStatusPanel();
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
        // VRCFT sub-tab: JawOpen constraint enable + paths + margins + grace + bindings.
        void RenderVRCFTTab();
        // Integrations tab: hosts PiShock / OpenShock / BPIO / Twitch / OSC Triggers as sub-tabs.
        void RenderIntegrationsTab();
        // OSC Triggers sub-tab: bite/shock enable + intensity/duration (paths live in Settings > OSC).
        void RenderOSCTriggersTab();

        // Startup splash + What's New overlays (see SplashScreen / UIManager_WhatsNew.cpp).
        void OpenWhatsNew();
        void RenderWhatsNew();
        void RenderSplashOverlay(); // draws splash + What's New on top of the main window
        
        // Apply the custom ImGui style/theme (rounded frames, soft-blue palette).
        void ApplyTheme();
        // Draw the radial boundary-zone map. Auto-fits the current content
        // region so the rings never clip; distance from center is literal
        // (device dot distance == real deviation from its locked origin).
        void RenderZoneMap();

        // Devices > Visual assignment view (effigy + drag-drop + per-slot config).
        void RenderVisualAssignment();
        void RenderShockerPalette();
        void ApplyIdBindingToDevice(DevicePosition& d, const char* code, bool enable);
        void ApplyIdBindingToRole(DeviceRole role, const char* code);
        void ApplyIdBindingToAllCuffs(const char* code, bool enable);
        void RenderSlotConfig(DeviceRole role);
        void LoadEffigyTexture();
        void LoadVRCFTLogos();
        void LoadBiteTechLogo();
        unsigned int LoadPngTexture(const std::string& path, int& w, int& h);
        void AssignRoleToSerial(const std::string& serial, DeviceRole role);
        std::string SerialForRole(DeviceRole role) const;
        static const char* RoleName(DeviceRole role);
        static const char* ShortRoleName(DeviceRole role);

        // Original UI elements (to be migrated to tabs)
        void RenderDeviceList();
        void RenderConfigControls();
        void RenderLockControls();
        void RenderGlobalLockControls();
        void RenderDeviceStatusTable();
        
        // Flag for window closing
        std::atomic<bool>* running_ptr_;
        
        // Configuration management
        void UpdateConfigFromUI();
        void UpdateUIFromConfig();
        
        // Device position handling
        void LockDevicePosition(const std::string& serial, bool lock);
        void ResetAllDevices();
        void ApplyLockedPositions();
        // play_sound=false suppresses the lock/unlock audio cue for automatic
        // (non-user-initiated) transitions, e.g. unlocking on avatar change.
        void ActivateGlobalLock(bool activate, bool play_sound = true);
        void ActivateGlobalLockInternal(bool activate, bool play_sound = true);
        void CheckDevicePositionDeviations();

        // VRCFT JawOpen constraint. Reserved serial used to key its shocker /
        // vibrator bindings in the existing config_.device_*_ids maps so the
        // standard Trigger*(serial) punishment pipeline works unchanged.
        static constexpr const char* kJawOpenSerial = "SPVR_JAWOPEN";
        void CheckJawOpenConstraint();          // called every frame from UpdateDevicePositions
        void ApplyIdBindingToJaw(const char* code); // bind/unbind a dragged shocker ID to the jaw
        void RenderJawConfig();                 // jaw config panel shown in the Visual view
        void LoadJawBindingsFromConfig();       // populate jaw_ binding arrays from config maps

        // Microphone enforced-mute constraint. Reserved serial keys its shocker /
        // vibrator bindings in the same config_.device_*_ids maps as the jaw.
        static constexpr const char* kMicSerial = "SPVR_MIC";
        void CheckMicrophoneConstraint();       // called every frame from UpdateDevicePositions
        void RenderMicTab();                    // Integrations -> Mic subtab
        void LoadMicBindingsFromConfig();       // populate mic_ binding arrays from config maps
        void StartMicCalibration();             // begin a background-noise sample
        void UpdateMicCalibration();            // per-frame: accumulate + finalize calibration

        // In-game sound effects: pulse SPVR_SoundEffect for a configured event, then
        // reset to 0 a moment later (UpdateInGameSoundPulse, called every frame).
        void TriggerInGameSound(InGameSound type);
        void UpdateInGameSoundPulse();

        // Unified collar-mode helpers (see UIManager_OSC.cpp).
        void SendCollarMode(int mode);          // guarded wrapper over OSCManager::SendCollarMode
        void RecomputeCollarValidMask();        // UI thread: which modes are selectable now
        int  NextValidCollarMode(int current) const; // advance to next enabled+agreed mode
        bool CollarModeIncludesJaw() const;     // collar_mode_ is Jaw or Both
        bool CollarModeIncludesMic() const;     // collar_mode_ is Mic or Both
        const char* CollarModeName(int mode) const;

        // Timestamp of last played sound for rate limiting
        std::chrono::steady_clock::time_point last_sound_time_ = std::chrono::steady_clock::now();

        // VRCFT JawOpen constraint runtime state (see CheckJawOpenConstraint).
        JawOpenConstraint jaw_;
        // True when the JawOpen head hotspot is selected in the Visual view, so
        // its config panel shows instead of a device slot's (selected_slot_role_).
        bool jaw_selected_ = false;

        // Microphone enforced-mute constraint runtime state (see CheckMicrophoneConstraint).
        MicrophoneConstraint mic_;
        // Live mic capture (WASAPI). Started when mic_enabled is on.
        std::unique_ptr<MicrophoneManager> microphone_manager_;

        // Unified collar mode. Written by the OSC receive thread (toggle) and the UI
        // thread (enable/disable snaps an invalid mode); read every frame by the
        // constraint code. collar_valid_mask_ is a bitmask over CollarMode values that
        // the UI thread recomputes so the OSC thread never touches config_ directly.
        std::atomic<int> collar_mode_{0};
        std::atomic<int> collar_valid_mask_{0x1}; // bit i set => mode i selectable; Neither always
        // Mirrors the incoming collar latch (SPVR_HMD_Latch_IsPosed) regardless of
        // whether a device is assigned to the HMD role. When the collar is latched on
        // the avatar but no HMD device is assigned, this lets the mic/jaw constraints
        // still engage off the latch alone -- the collar is real on the avatar even
        // when StayPutVR isn't locking a physical tracker's position. Set on the OSC
        // thread (OnDeviceLocked), read every frame by the constraint code.
        std::atomic<bool> collar_latched_via_osc_{false};
        bool collar_toggle_prev_ = false;         // rising-edge debounce (OSC thread only)
        // Time-based debounce: ignore toggle presses that arrive within this window of
        // the last accepted one (contact bounce / rapid repeats). OSC thread only.
        std::chrono::steady_clock::time_point collar_toggle_last_time_;

        // In-game sound-effect pulse state. Atomic because TriggerInGameSound can be
        // called from the OSC receive thread (collar toggle / avatar reset) as well as
        // the UI thread (constraints / lock funnels). ingame_sfx_pending_ holds the
        // value currently asserted on SPVR_SoundEffect; UpdateInGameSoundPulse (UI
        // thread) resets it to 0 once the pulse window elapses.
        std::atomic<int> ingame_sfx_pending_{0};
        std::atomic<long long> ingame_sfx_reset_ms_{0}; // steady_clock deadline, ms
        static constexpr int kInGameSfxPulseMs = 300;
        static constexpr float kCollarToggleDebounceSeconds = 0.4f;

        // Microphone background-noise calibration state (UI thread).
        bool mic_calibrating_ = false;
        std::chrono::steady_clock::time_point mic_calib_start_;
        float mic_calib_min_ = 1.0f;
        float mic_calib_max_ = 0.0f;

        DeviceManager* device_manager_ = nullptr;
        
        std::unique_ptr<TwitchManager> twitch_manager_;
        
        std::unique_ptr<PiShockManager> pishock_manager_;
        std::unique_ptr<PiShockWebSocketManager> pishock_ws_manager_;
        
        std::unique_ptr<OpenShockManager> openshock_manager_;
        
        std::unique_ptr<ButtplugManager> buttplug_manager_;

        // UI Panels
        std::unique_ptr<PiShockPanel> pishock_panel_;
        std::unique_ptr<OpenShockPanel> openshock_panel_;
        std::unique_ptr<ButtplugPanel> buttplug_panel_;
        
        // Countdown timer variables
        bool countdown_active_ = false;
        float countdown_remaining_ = 0.0f;
        std::chrono::steady_clock::time_point countdown_last_beep_;
        
        // OSC callbacks
        void OnDeviceLocked(OSCDeviceType device, bool locked);
        void OnDeviceIncluded(OSCDeviceType device, bool include);
        void TriggerGlobalOutOfBoundsActions();
        void TriggerBiteActions();
        void HandleAvatarChange();
        // Fire a direct shock on all enabled shock managers at the given
        // intensity (0..1) and duration (seconds). Blocked during emergency stop.
        void TriggerExternalShock(float intensity, float duration_seconds, const std::string& reason);
        // Like TriggerExternalShock but each shocker uses its per-device
        // disobedience intensity (OSC bite/shock "use individual" option).
        void TriggerExternalShockIndividual(float duration_seconds, const std::string& reason);
        void ResetEmergencyStop();
        
        // Helper functions
        void UpdateDeviceStatus(OSCDeviceType device, DeviceStatus status);
        void HandleOSCConnection();
        void DisconnectOSC();
        void RegisterOSCCallbacks();  // single source of truth for inbound-OSC callbacks
        void VerifyOSCCallbacks();    // re-registers on an open connection (guards + logs)

        // OSCQuery (mDNS) lifecycle. Started/stopped alongside the OSC sockets
        // when config_.osc_query_enabled is set.
        void StartOSCQuery();
        void StopOSCQuery();
        
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

        // Avatar-change re-sync: VRChat resets all avatar params on avatar load and
        // isn't ready to receive the echo at the instant /avatar/change fires, so the
        // immediate status/collar-mode push from HandleAvatarChange() races the load
        // and is often dropped. Schedule a one-shot delayed re-push that lands after
        // the avatar is ready, so the display restores even if the user never interacts.
        void ProcessAvatarResyncTimer();
        bool avatar_resync_pending_ = false;
        std::chrono::steady_clock::time_point avatar_resync_start_;
        static constexpr float AVATAR_RESYNC_DELAY = 1.0f; // Seconds after /avatar/change
        
        // Twitch donation callbacks
            void OnTwitchDonation(const std::string& username, float amount, const std::string& message);
    void OnTwitchBits(const std::string& username, int bits, const std::string& message);
    void OnTwitchSubscription(const std::string& username, int months, bool is_gift);
    void OnTwitchChatCommand(const std::string& username, const std::string& command, const std::string& args);
        
        // Last time OSC lock was toggled (for debouncing)
        std::chrono::steady_clock::time_point last_osc_toggle_time_;
    };
} 