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

    void UIManager::RenderIntegrationsTab() {
        if (ImGui::BeginTabBar("IntegrationsSubTabs")) {
            if (ImGui::BeginTabItem("PiShock")) {
                RenderPiShockTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("OpenShock")) {
                RenderOpenShockTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("OSC Triggers")) {
                RenderOSCTriggersTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("BPIO")) {
                RenderButtplugTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Twitch")) {
                RenderTwitchTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("VRCFT")) {
                RenderVRCFTTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Mic")) {
                RenderMicTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void UIManager::RenderVRCFTTab() {
        // VRCFT / Unified Expressions branding in the top-left.
        LoadVRCFTLogos();
        const float logoH = 28.0f;
        bool drew_logo = false;
        if (vrcft_logo_tex_ != 0 && vrcft_logo_h_ > 0) {
            float w = logoH * (float)vrcft_logo_w_ / (float)vrcft_logo_h_;
            ImGui::Image((ImTextureID)(intptr_t)vrcft_logo_tex_, ImVec2(w, logoH));
            drew_logo = true;
        }
        if (ue_logo_tex_ != 0 && ue_logo_h_ > 0) {
            if (drew_logo) ImGui::SameLine();
            float w = logoH * (float)ue_logo_w_ / (float)ue_logo_h_;
            ImGui::Image((ImTextureID)(intptr_t)ue_logo_tex_, ImVec2(w, logoH));
            drew_logo = true;
        }
        if (drew_logo) ImGui::Spacing();

        ImGui::Text("VRCFT JawOpen Constraint");
        ImGui::TextWrapped(
	    "With VRCFaceTracking (VRCFT) and this feature, when you lock your collar (HMD/neck) "
	    "your JawOpen value must remain where it was when locked (i.e., your real mouth must " 
	    "remain open or closed). Opening or closing your mouth too far will result in the "
	    "usual sort of warning and disobedience actions (shocks). "
	    "Assign shocker ID(s) in the Devices -> Visual tab.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Requires the StayPutVR JawOpen bridge on your avatar (prefab v1.4.0+) and "
	    "VRCFT. If your avatar uses a non-de facto-standard VRCFT FT/v2/JawOpen path, "
	    "then update the JawOpen paramater path in your Unity project. (See docs or Discord)");
        ImGui::Separator();

        bool agreed = ImGuiHelpers::SafetyAgreementBlock(
            "Locking your jaw open/closed and shocking you for moving it is a physical-restraint "
            "play feature. Use it only with safe words/limits in place, never alone if a shocker "
            "could cause harm, and stop if you feel unwell.",
            config_.jawopen_user_agreement);
        if (agreed != config_.jawopen_user_agreement) {
            config_.jawopen_user_agreement = agreed;
            RecomputeCollarValidMask();
            SaveConfig();
        }

        ImGui::BeginDisabled(!agreed);

        bool enabled = config_.jawopen_enabled;
        if (ImGui::Checkbox("Enable VRCFT JawOpen Integration", &enabled)) {
            config_.jawopen_enabled = enabled;
            if (enabled) LoadJawBindingsFromConfig();
            RecomputeCollarValidMask();
            SaveConfig();
        }
        ImGuiHelpers::HelpTooltip("App master switch (off by default). When enabled, the jaw is \n"
                                  "enforced while the HMD is locked AND the collar mode includes Jaw \n"
                                  "(tap the in-game collar button to cycle Neither/Jaw/Mic/Both). \n"
                                  "Switching the mode away from Jaw while locked suspends it (resuming \n"
                                  "re-captures the baseline) without unlocking.");

        ImGui::SameLine();
        ImGui::Text("| Collar includes Jaw:");
        ImGui::SameLine();
        if (CollarModeIncludesJaw()) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "YES");
        else ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.4f, 1.0f), "NO");

        // Live value + state readout.
        ImGui::Spacing();
        ImGui::Text("Live JawOpen:");
        ImGui::SameLine();
        ImGui::ProgressBar(jaw_.current, ImVec2(180, 0));
        if (jaw_.active) {
            ImGui::Text("Baseline: %.2f   Deviation: %.2f   %s",
                        jaw_.baseline, jaw_.deviation,
                        jaw_.exceeds_threshold ? "[DISOBEDIENCE]" :
                        jaw_.in_warning_zone ? "[WARNING]" : "[SAFE]");
        } else if (config_.jawopen_enabled && jaw_.in_grace) {
            ImGui::TextDisabled("Capturing baseline (grace window)...");
        } else if (config_.jawopen_enabled && !CollarModeIncludesJaw()) {
            ImGui::TextDisabled("Suspended (collar mode excludes Jaw).");
        } else if (config_.jawopen_enabled) {
            ImGui::TextDisabled("Inactive (lock the HMD to engage).");
        }

        ImGui::Separator();
        // Collapsed by default - the defaults match the editor-generated bridge,
        // so most users never need to touch these.
        if (ImGui::CollapsingHeader("Parameter Paths")) {
            static char jaw_path[128] = "";
            if (strlen(jaw_path) == 0) strcpy_s(jaw_path, sizeof(jaw_path), config_.osc_jawopen_path.c_str());

            if (ImGui::InputText("Bridge Path", jaw_path, IM_ARRAYSIZE(jaw_path))) {
                config_.osc_jawopen_path = jaw_path;
            }
            // Push the edited path into the live OSCManager so it takes effect at
            // once. Without this, the receiver keeps matching the OLD path until OSC
            // is reconnected, which is why an edited JawOpen path appears to do nothing.
            if (ImGui::IsItemDeactivatedAfterEdit()) { SaveConfig(); OSCManager::GetInstance().SetConfig(config_); }
            ImGui::SameLine();
            if (ImGui::Button("Reset##jawpath")) {
                config_.osc_jawopen_path = "/avatar/parameters/SPVR_JawOpen";
                strcpy_s(jaw_path, sizeof(jaw_path), config_.osc_jawopen_path.c_str());
                SaveConfig();
                OSCManager::GetInstance().SetConfig(config_);
            }
            ImGuiHelpers::HelpTooltip("StayPutVR v1.4.0 parameter name. Shouldn't need to change this.");
        }

        ImGui::Separator();
        ImGui::Text("Constraint Tuning");

        float warn = config_.jawopen_warning_margin;
        if (ImGui::SliderFloat("Warning margin", &warn, 0.01f, 0.5f, "%.2f")) {
            config_.jawopen_warning_margin = warn;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveConfig();
        ImGuiHelpers::HelpTooltip("Jaw may deviate this far from the baseline before a warning is issued.");

        float diso = config_.jawopen_disobedience_margin;
        if (ImGui::SliderFloat("Disobedience margin", &diso, 0.02f, 0.8f, "%.2f")) {
            config_.jawopen_disobedience_margin = diso;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveConfig();
        ImGuiHelpers::HelpTooltip("Past this deviation the disobedience response fires (shockers, etc.).");

        float grace = config_.jawopen_grace_seconds;
        if (ImGui::SliderFloat("Grace window (s)", &grace, 0.0f, 10.0f, "%.1f")) {
            config_.jawopen_grace_seconds = grace;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveConfig();
        ImGuiHelpers::HelpTooltip("Grace period between locking and the JawOpen value being selected.");

        ImGui::Separator();
        ImGui::Text("Shocker / Vibrator bindings");
        ImGui::TextWrapped("Which devices fire on jaw disobedience. (You can also drag ID chips "
                           "onto the head on the Devices > Visual screen.)");

        bool changed = false;
        for (int i = 0; i < 5; ++i) {
            if (config_.pishock_shocker_ids[i] != 0) {
                ImGui::PushID(i);
                bool b = jaw_.pishock_enabled[i];
                if (ImGui::Checkbox(("PiShock " + std::to_string(i)).c_str(), &b)) {
                    jaw_.pishock_enabled[i] = b; changed = true;
                }
                ImGui::PopID();
            }
        }
        for (int i = 0; i < 5; ++i) {
            if (!config_.openshock_device_ids[i].empty()) {
                ImGui::PushID(100 + i);
                bool b = jaw_.openshock_enabled[i];
                if (ImGui::Checkbox(("OpenShock " + std::to_string(i)).c_str(), &b)) {
                    jaw_.openshock_enabled[i] = b; changed = true;
                }
                ImGui::PopID();
            }
        }
        for (int i = 0; i < 5; ++i) {
            if (config_.buttplug_device_indices[i] >= 0) {
                ImGui::PushID(200 + i);
                bool b = jaw_.vibration_device_enabled[i];
                if (ImGui::Checkbox(("BPIO " + std::to_string(i)).c_str(), &b)) {
                    jaw_.vibration_device_enabled[i] = b; changed = true;
                }
                ImGui::PopID();
            }
        }
        if (changed) {
            config_.device_pishock_ids[kJawOpenSerial] = jaw_.pishock_enabled;
            config_.device_openshock_ids[kJawOpenSerial] = jaw_.openshock_enabled;
            config_.device_vibration_ids[kJawOpenSerial] = jaw_.vibration_device_enabled;
            SaveConfig();
        }

        ImGui::EndDisabled(); // end of jawopen_user_agreement gate
    }

    void UIManager::RenderMicTab() {
        ImGui::Text("Microphone Enforced-Mute");
        ImGui::TextWrapped(
            "When you lock your collar (HMD/neck) and the collar mode includes Mic, your "
            "microphone must stay near the ambient room level captured at lock time -- i.e. you "
            "must stay quiet. Talking too loud triggers the usual warning and disobedience actions "
            "(shocks). Assign shocker ID(s) below.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "This reads your OS microphone directly (independent of VRChat's mute), so it only "
            "punishes speaking -- it does not mute you in VRChat.");
        ImGui::Separator();

        bool agreed = ImGuiHelpers::SafetyAgreementBlock(
            "Enforced silence with a shock penalty is a physical-restraint play feature. Use it "
            "with care and consideration for your limits. Check this box to enable this feature.",
            config_.mic_user_agreement);
        if (agreed != config_.mic_user_agreement) {
            config_.mic_user_agreement = agreed;
            RecomputeCollarValidMask();
            SaveConfig();
        }

        ImGui::BeginDisabled(!agreed);

        bool enabled = config_.mic_enabled;
        if (ImGui::Checkbox("Enable Microphone Volume Integration", &enabled)) {
            config_.mic_enabled = enabled;
            if (microphone_manager_) {
                if (enabled) { microphone_manager_->SetDevice(config_.mic_device_id); microphone_manager_->Start(); }
                else microphone_manager_->Stop();
            }
            if (enabled) LoadMicBindingsFromConfig();
            RecomputeCollarValidMask();
            SaveConfig();
        }
        ImGuiHelpers::HelpTooltip("App master switch (off by default). When enabled, the mic is \n"
                                  "enforced while the HMD is locked AND the collar mode includes Mic.");
        ImGui::SameLine();
        ImGui::Text("| Collar includes Mic:");
        ImGui::SameLine();
        if (CollarModeIncludesMic()) ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "YES");
        else ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.4f, 1.0f), "NO");

        // Capture device selection.
        ImGui::Spacing();
        std::string cur_name = microphone_manager_ ? microphone_manager_->GetCurrentDeviceName() : "None";
        if (ImGui::BeginCombo("Input Device", config_.mic_device_id.empty() ? "System Default" : cur_name.c_str())) {
            bool def_sel = config_.mic_device_id.empty();
            if (ImGui::Selectable("System Default", def_sel)) {
                config_.mic_device_id.clear();
                if (microphone_manager_) microphone_manager_->SetDevice("");
                SaveConfig();
            }
            if (microphone_manager_) {
                for (const auto& d : microphone_manager_->GetDevices()) {
                    bool sel = (d.id == config_.mic_device_id);
                    if (ImGui::Selectable(d.name.c_str(), sel)) {
                        config_.mic_device_id = d.id;
                        microphone_manager_->SetDevice(d.id);
                        SaveConfig();
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (microphone_manager_) {
            if (microphone_manager_->IsRunning() && microphone_manager_->IsConnected())
                ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "Capturing: %s", cur_name.c_str());
            else if (microphone_manager_->IsRunning())
                ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.4f, 1.0f), "Connecting / no device...");
            else
                ImGui::TextDisabled("Capture stopped.");
            std::string err = microphone_manager_->GetLastError();
            if (!err.empty()) ImGui::TextDisabled("Last error: %s", err.c_str());
        }

        // Live VU + state readout.
        ImGui::Spacing();
        ImGui::Text("Mic level:");
        ImGui::SameLine();
        ImGui::ProgressBar(microphone_manager_ ? microphone_manager_->GetLevel() : 0.0f, ImVec2(180, 0));
        if (mic_.active) {
            ImGui::Text("Floor: %.2f   Deviation: %.2f   %s",
                        mic_.baseline, mic_.deviation,
                        mic_.exceeds_threshold ? "[DISOBEDIENCE]" :
                        mic_.in_warning_zone ? "[WARNING]" : "[SAFE]");
        } else if (config_.mic_enabled && mic_.in_grace) {
            ImGui::TextDisabled("Capturing ambient floor (grace window)...");
        } else if (config_.mic_enabled && !CollarModeIncludesMic()) {
            ImGui::TextDisabled("Suspended (collar mode excludes Mic).");
        } else if (config_.mic_enabled) {
            ImGui::TextDisabled("Inactive (lock the HMD to engage).");
        }

        ImGui::Separator();
        ImGui::Text("Constraint Tuning");

        float warn = config_.mic_warning_margin;
        if (ImGui::SliderFloat("Warning margin", &warn, 0.01f, 0.5f, "%.2f"))
            config_.mic_warning_margin = warn;
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveConfig();
        ImGuiHelpers::HelpTooltip("How far above the ambient floor the mic level may rise before a warning.");

        float diso = config_.mic_disobedience_margin;
        if (ImGui::SliderFloat("Disobedience margin", &diso, 0.02f, 0.8f, "%.2f"))
            config_.mic_disobedience_margin = diso;
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveConfig();
        ImGuiHelpers::HelpTooltip("Past this rise above the floor the disobedience response fires (shockers, etc.).");

        float grace = config_.mic_grace_seconds;
        if (ImGui::SliderFloat("Grace window (s)", &grace, 0.0f, 10.0f, "%.1f"))
            config_.mic_grace_seconds = grace;
        if (ImGui::IsItemDeactivatedAfterEdit()) SaveConfig();
        ImGuiHelpers::HelpTooltip("Quiet window after locking during which the ambient noise floor is captured.");

        ImGui::Separator();
        ImGui::Text("Shocker / Vibrator bindings");
        ImGui::TextWrapped("Which devices fire on mic disobedience.");

        bool changed = false;
        for (int i = 0; i < 5; ++i) {
            if (config_.pishock_shocker_ids[i] != 0) {
                ImGui::PushID(i);
                bool b = mic_.pishock_enabled[i];
                if (ImGui::Checkbox(("PiShock " + std::to_string(i)).c_str(), &b)) {
                    mic_.pishock_enabled[i] = b; changed = true;
                }
                ImGui::PopID();
            }
        }
        for (int i = 0; i < 5; ++i) {
            if (!config_.openshock_device_ids[i].empty()) {
                ImGui::PushID(100 + i);
                bool b = mic_.openshock_enabled[i];
                if (ImGui::Checkbox(("OpenShock " + std::to_string(i)).c_str(), &b)) {
                    mic_.openshock_enabled[i] = b; changed = true;
                }
                ImGui::PopID();
            }
        }
        for (int i = 0; i < 5; ++i) {
            if (config_.buttplug_device_indices[i] >= 0) {
                ImGui::PushID(200 + i);
                bool b = mic_.vibration_device_enabled[i];
                if (ImGui::Checkbox(("BPIO " + std::to_string(i)).c_str(), &b)) {
                    mic_.vibration_device_enabled[i] = b; changed = true;
                }
                ImGui::PopID();
            }
        }
        if (changed) {
            config_.device_pishock_ids[kMicSerial] = mic_.pishock_enabled;
            config_.device_openshock_ids[kMicSerial] = mic_.openshock_enabled;
            config_.device_vibration_ids[kMicSerial] = mic_.vibration_device_enabled;
            SaveConfig();
        }

        ImGui::Separator();
        if (ImGui::Button("Test warning action")) TriggerPiShockWarning(kMicSerial);
        ImGui::SameLine();
        if (ImGui::Button("Test disobedience action")) TriggerPiShockDisobedience(kMicSerial);

        ImGui::EndDisabled(); // end of mic_user_agreement gate
    }

    void UIManager::RenderPiShockTab() {
        if (pishock_panel_) {
            pishock_panel_->Render();
        }
    }

    // PiShock Helper Methods Implementation
    void UIManager::InitializePiShockManager() {
        pishock_manager_ = std::make_unique<PiShockManager>();
        
        if (pishock_manager_->Initialize(&config_)) {
            // Set up callback for PiShock action results
            pishock_manager_->SetActionCallback(
                [this](const std::string& action_type, bool success, const std::string& message) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("PiShock " + action_type + " " + (success ? "succeeded" : "failed") + 
                                   (message.empty() ? "" : ": " + message));
                    }
                }
            );
            
            if (Logger::IsInitialized()) {
                Logger::Info("PiShockManager initialized successfully");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to initialize PiShockManager");
            }
        }
    }

    void UIManager::InitializePiShockWebSocketManager() {
        pishock_ws_manager_ = std::make_unique<PiShockWebSocketManager>();
        
        if (pishock_ws_manager_->Initialize(&config_)) {
            // Set up callback for PiShock WebSocket action results
            pishock_ws_manager_->SetActionCallback(
                [this](const std::string& action_type, bool success, const std::string& message) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("PiShock WebSocket " + action_type + " " + (success ? "succeeded" : "failed") + 
                                   (message.empty() ? "" : ": " + message));
                    }
                }
            );
            
            if (Logger::IsInitialized()) {
                Logger::Info("PiShockWebSocketManager initialized successfully");
            }
            
            // Auto-connect if WebSocket v2 is selected and fully configured
            if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2 && 
                config_.pishock_enabled &&
                pishock_ws_manager_->IsFullyConfigured()) {
                Logger::Info("Auto-connecting to PiShock WebSocket v2...");
                if (pishock_ws_manager_->Connect()) {
                    Logger::Info("Auto-connected to PiShock WebSocket v2");
                } else {
                    Logger::Warning("Failed to auto-connect to PiShock WebSocket v2: " + 
                                  pishock_ws_manager_->GetLastError());
                }
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to initialize PiShockWebSocketManager");
            }
        }
    }

    void UIManager::ShutdownPiShockManager() {
        if (pishock_manager_) {
            pishock_manager_->Shutdown();
            pishock_manager_.reset();
            
            if (Logger::IsInitialized()) {
                Logger::Info("PiShockManager shut down");
            }
        }
        
        if (pishock_ws_manager_) {
            pishock_ws_manager_->Shutdown();
            pishock_ws_manager_.reset();
            
            if (Logger::IsInitialized()) {
                Logger::Info("PiShockWebSocketManager shut down");
            }
        }
    }

    void UIManager::TriggerPiShockDisobedience(const std::string& device_serial) {
        if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
            if (pishock_manager_ && pishock_manager_->IsEnabled()) {
                pishock_manager_->TriggerDisobedienceActions(device_serial);
            }
        } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
            if (pishock_ws_manager_ && pishock_ws_manager_->IsEnabled()) {
                pishock_ws_manager_->TriggerDisobedienceActions(device_serial);
            }
        }
        
        // Also trigger Buttplug disobedience actions if enabled
        if (buttplug_manager_ && buttplug_manager_->IsEnabled()) {
            buttplug_manager_->TriggerDisobedienceActions(device_serial);
        }
    }

    void UIManager::TriggerPiShockWarning(const std::string& device_serial) {
        if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
            if (pishock_manager_ && pishock_manager_->IsEnabled()) {
                pishock_manager_->TriggerWarningActions(device_serial);
            }
        } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
            if (pishock_ws_manager_ && pishock_ws_manager_->IsEnabled()) {
                pishock_ws_manager_->TriggerWarningActions(device_serial);
            }
        }
        
        // Also trigger Buttplug warning actions if enabled
        if (buttplug_manager_ && buttplug_manager_->IsEnabled()) {
            buttplug_manager_->TriggerWarningActions(device_serial);
        }
    }

    bool UIManager::CanTriggerPiShock() const {
        if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
            return pishock_manager_ && pishock_manager_->IsEnabled() && pishock_manager_->CanTriggerAction();
        } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
            return pishock_ws_manager_ && pishock_ws_manager_->IsEnabled() && pishock_ws_manager_->CanTriggerAction();
        }
        return false;
    }

    void UIManager::RenderButtplugTab() {
        if (buttplug_panel_) {
            buttplug_panel_->Render();
        }
    }

    void UIManager::RenderTwitchTab() {
        ImGui::Text("Twitch Integration");
        ImGui::Separator();
        
        // Safety warning + agreement
        bool user_agreement = ImGuiHelpers::SafetyAgreementBlock(
            "Twitch integration allows viewers to trigger device locking through donations/bits/subscriptions. This should only be used with people you trust and in safe environments. The makers of StayPutVR accept no liability for misuse of this feature. Always have a safety mechanism to quickly disconnect devices if needed. Test all features thoroughly before use with live viewers.",
            config_.twitch_user_agreement);
        if (user_agreement != config_.twitch_user_agreement) {
            config_.twitch_user_agreement = user_agreement;
            SaveConfig();
        }
        
        // Main enable/disable checkbox (disabled until agreement is checked)
        ImGui::BeginDisabled(!user_agreement);
        bool twitch_enabled = config_.twitch_enabled;
        if (ImGui::Checkbox("Enable Twitch Integration", &twitch_enabled)) {
            config_.twitch_enabled = twitch_enabled;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Enable Twitch API integration for chat bot and donation triggers");
        
        // Connection status
        if (config_.twitch_enabled && twitch_manager_) {
            std::string status = twitch_manager_->GetConnectionStatus();
            if (twitch_manager_->IsConnected()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[OK] %s", status.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "[X] %s", status.c_str());
                std::string error = twitch_manager_->GetLastError();
                if (!error.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), ("Error: " + error).c_str());
                }
            }
        }
        
        ImGui::Separator();
        
        // Twitch API Authentication
        ImGui::Text("Twitch API Authentication:");
        ImGui::TextWrapped("You'll need to create a Twitch application at https://dev.twitch.tv/console to get these credentials.");
        
        static char client_id_buffer[128] = "";
        // Always sync buffer with current config value
        if (config_.twitch_client_id != client_id_buffer) {
            strcpy_s(client_id_buffer, sizeof(client_id_buffer), config_.twitch_client_id.c_str());
        }
        
        if (ImGui::InputText("Client ID", client_id_buffer, sizeof(client_id_buffer))) {
            config_.twitch_client_id = client_id_buffer;
            SaveConfig();
        }
        
        static char client_secret_buffer[128] = "";
        // Always sync buffer with current config value
        if (config_.twitch_client_secret != client_secret_buffer) {
            strcpy_s(client_secret_buffer, sizeof(client_secret_buffer), config_.twitch_client_secret.c_str());
        }
        
        if (ImGui::InputText("Client Secret", client_secret_buffer, sizeof(client_secret_buffer), ImGuiInputTextFlags_Password)) {
            config_.twitch_client_secret = client_secret_buffer;
            SaveConfig();
        }
        
        static char channel_name_buffer[128] = "";
        // Always sync buffer with current config value
        if (config_.twitch_channel_name != channel_name_buffer) {
            strcpy_s(channel_name_buffer, sizeof(channel_name_buffer), config_.twitch_channel_name.c_str());
        }
        
        if (ImGui::InputText("Channel Name", channel_name_buffer, sizeof(channel_name_buffer))) {
            config_.twitch_channel_name = channel_name_buffer;
            SaveConfig();
        }
        
        static char bot_username_buffer[128] = "";
        // Always sync buffer with current config value
        if (config_.twitch_bot_username != bot_username_buffer) {
            strcpy_s(bot_username_buffer, sizeof(bot_username_buffer), config_.twitch_bot_username.c_str());
        }
        
        if (ImGui::InputText("Bot Username", bot_username_buffer, sizeof(bot_username_buffer))) {
            config_.twitch_bot_username = bot_username_buffer;
            SaveConfig();
        }
        
        // OAuth buttons
        ImGui::Spacing();
        ImGui::BeginDisabled(!config_.twitch_enabled || config_.twitch_client_id.empty() || config_.twitch_client_secret.empty());
        
        if (ImGui::Button("Connect to Twitch")) {
            if (twitch_manager_) {
                twitch_manager_->ConnectToTwitch();
            }
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Disconnect from Twitch")) {
            if (twitch_manager_) {
                twitch_manager_->DisconnectFromTwitch();
            }
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Chat Bot Settings
        ImGui::Text("Chat Bot Settings:");
        
        bool chat_enabled = config_.twitch_chat_enabled;
        if (ImGui::Checkbox("Enable Chat Commands", &chat_enabled)) {
            config_.twitch_chat_enabled = chat_enabled;
            SaveConfig();
        }
        
        ImGui::BeginDisabled(!config_.twitch_chat_enabled);
        
        static char prefix_buffer[16] = "";
        // Always sync buffer with current config value
        if (config_.twitch_command_prefix != prefix_buffer) {
            strcpy_s(prefix_buffer, sizeof(prefix_buffer), config_.twitch_command_prefix.c_str());
        }
        
        if (ImGui::InputText("Command Prefix", prefix_buffer, sizeof(prefix_buffer))) {
            config_.twitch_command_prefix = prefix_buffer;
            SaveConfig();
        }
        
        static char lock_cmd_buffer[64] = "";
        // Always sync buffer with current config value
        if (config_.twitch_lock_command != lock_cmd_buffer) {
            strcpy_s(lock_cmd_buffer, sizeof(lock_cmd_buffer), config_.twitch_lock_command.c_str());
        }
        
        if (ImGui::InputText("Lock Command", lock_cmd_buffer, sizeof(lock_cmd_buffer))) {
            config_.twitch_lock_command = lock_cmd_buffer;
            SaveConfig();
        }
        
        static char unlock_cmd_buffer[64] = "";
        // Always sync buffer with current config value
        if (config_.twitch_unlock_command != unlock_cmd_buffer) {
            strcpy_s(unlock_cmd_buffer, sizeof(unlock_cmd_buffer), config_.twitch_unlock_command.c_str());
        }
        
        if (ImGui::InputText("Unlock Command", unlock_cmd_buffer, sizeof(unlock_cmd_buffer))) {
            config_.twitch_unlock_command = unlock_cmd_buffer;
            SaveConfig();
        }
        
        static char status_cmd_buffer[64] = "";
        // Always sync buffer with current config value
        if (config_.twitch_status_command != status_cmd_buffer) {
            strcpy_s(status_cmd_buffer, sizeof(status_cmd_buffer), config_.twitch_status_command.c_str());
        }
        
        if (ImGui::InputText("Status Command", status_cmd_buffer, sizeof(status_cmd_buffer))) {
            config_.twitch_status_command = status_cmd_buffer;
            SaveConfig();
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Donation Trigger Settings
        ImGui::Text("Donation Trigger Settings:");
        ImGui::TextWrapped("Configure which viewer actions can trigger device locking. These provide security by requiring financial contribution.");
        
        bool bits_enabled = config_.twitch_bits_enabled;
        if (ImGui::Checkbox("Enable Bits/Cheering Triggers", &bits_enabled)) {
            config_.twitch_bits_enabled = bits_enabled;
            SaveConfig();
        }
        
        ImGui::BeginDisabled(!config_.twitch_bits_enabled);
        int bits_minimum = config_.twitch_bits_minimum;
        if (ImGui::InputInt("Minimum Bits", &bits_minimum)) {
            config_.twitch_bits_minimum = (std::max)(1, bits_minimum);
            SaveConfig();
        }
        ImGui::EndDisabled();
        
        bool subs_enabled = config_.twitch_subs_enabled;
        if (ImGui::Checkbox("Enable Subscription Triggers", &subs_enabled)) {
            config_.twitch_subs_enabled = subs_enabled;
            SaveConfig();
        }
        
        bool donations_enabled = config_.twitch_donations_enabled;
        if (ImGui::Checkbox("Enable Donation Triggers", &donations_enabled)) {
            config_.twitch_donations_enabled = donations_enabled;
            SaveConfig();
        }
        
        ImGui::BeginDisabled(!config_.twitch_donations_enabled);
        float donation_minimum = config_.twitch_donation_minimum;
        if (ImGui::InputFloat("Minimum Donation ($)", &donation_minimum)) {
            config_.twitch_donation_minimum = (std::max)(0.01f, donation_minimum);
            SaveConfig();
        }
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Lock Duration Settings
        ImGui::Text("Lock Duration Settings:");
        
        bool duration_enabled = config_.twitch_lock_duration_enabled;
        if (ImGui::Checkbox("Enable Dynamic Lock Duration", &duration_enabled)) {
            config_.twitch_lock_duration_enabled = duration_enabled;
            SaveConfig();
        }
        
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("When enabled, lock duration scales with donation amount");
        
        ImGui::BeginDisabled(!config_.twitch_lock_duration_enabled);
        
        float base_duration = config_.twitch_lock_base_duration;
        if (ImGui::SliderFloat("Base Duration", &base_duration, 10.0f, 300.0f, "%.0f seconds")) {
            config_.twitch_lock_base_duration = base_duration;
            SaveConfig();
        }
        
        float per_dollar = config_.twitch_lock_per_dollar;
        if (ImGui::SliderFloat("Per Dollar/100 Bits", &per_dollar, 1.0f, 120.0f, "%.0f seconds")) {
            config_.twitch_lock_per_dollar = per_dollar;
            SaveConfig();
        }
        
        float max_duration = config_.twitch_lock_max_duration;
        if (ImGui::SliderFloat("Maximum Duration", &max_duration, 60.0f, 3600.0f, "%.0f seconds")) {
            config_.twitch_lock_max_duration = max_duration;
            SaveConfig();
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Device Targeting
        ImGui::Text("Device Targeting:");
        ImGui::TextWrapped("Choose which devices can be locked by Twitch triggers.");
        
        bool target_all = config_.twitch_target_all_devices;
        if (ImGui::Checkbox("Target All Devices", &target_all)) {
            config_.twitch_target_all_devices = target_all;
            SaveConfig();
        }
        
        ImGui::BeginDisabled(config_.twitch_target_all_devices);
        
        bool target_hmd = config_.twitch_target_hmd;
        if (ImGui::Checkbox("HMD", &target_hmd)) {
            config_.twitch_target_hmd = target_hmd;
            SaveConfig();
        }
        
        ImGui::SameLine();
        bool target_left_hand = config_.twitch_target_left_hand;
        if (ImGui::Checkbox("Left Hand", &target_left_hand)) {
            config_.twitch_target_left_hand = target_left_hand;
            SaveConfig();
        }
        
        ImGui::SameLine();
        bool target_right_hand = config_.twitch_target_right_hand;
        if (ImGui::Checkbox("Right Hand", &target_right_hand)) {
            config_.twitch_target_right_hand = target_right_hand;
            SaveConfig();
        }
        
        bool target_left_foot = config_.twitch_target_left_foot;
        if (ImGui::Checkbox("Left Foot", &target_left_foot)) {
            config_.twitch_target_left_foot = target_left_foot;
            SaveConfig();
        }
        
        ImGui::SameLine();
        bool target_right_foot = config_.twitch_target_right_foot;
        if (ImGui::Checkbox("Right Foot", &target_right_foot)) {
            config_.twitch_target_right_foot = target_right_foot;
            SaveConfig();
        }
        
        ImGui::SameLine();
        bool target_hip = config_.twitch_target_hip;
        if (ImGui::Checkbox("Hip", &target_hip)) {
            config_.twitch_target_hip = target_hip;
            SaveConfig();
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // OAuth Setup and Test Buttons
        ImGui::Text("OAuth Setup:");
        ImGui::BeginDisabled(!config_.twitch_enabled || config_.twitch_client_id.empty() || config_.twitch_client_secret.empty());
        
        // OAuth Status
        static std::string oauth_url = "";
        static bool oauth_server_running = false;
        
        if (twitch_manager_) {
            // Check if we already have tokens
            if (!config_.twitch_access_token.empty()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[OK] OAuth tokens available");
                
                if (ImGui::Button("Test Connection")) {
                    twitch_manager_->ConnectToTwitch();
                }
                
                ImGui::SameLine();
                if (ImGui::Button("Clear Tokens")) {
                    config_.twitch_access_token.clear();
                    config_.twitch_refresh_token.clear();
                    SaveConfig();
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "OAuth setup required");
                
                if (!oauth_server_running) {
                    if (ImGui::Button("Start OAuth Setup")) {
                        // Start the OAuth server and generate URL
                        twitch_manager_->StartOAuthServer();
                        oauth_url = twitch_manager_->GenerateOAuthURL();
                        oauth_server_running = true;
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "OAuth server running on localhost:8080");
                    
                    if (ImGui::Button("Stop OAuth Server")) {
                        twitch_manager_->StopOAuthServer();
                        oauth_server_running = false;
                        oauth_url.clear();
                    }
                }
            }
        }
        
        // Show OAuth URL if available
        if (oauth_server_running && !oauth_url.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("Step 1: Click the button below to open Twitch authorization in your browser:");
            
            if (ImGui::Button("Open Twitch Authorization", ImVec2(300, 30))) {
                // Open URL in default browser
                std::string command = "start \"\" \"" + oauth_url + "\"";
                system(command.c_str());
            }
            
            ImGui::Spacing();
            ImGui::TextWrapped("Step 2: Authorize the application in your browser. You'll be redirected automatically and this will complete the setup!");
            
            ImGui::Spacing();
            ImGui::Text("OAuth URL (for manual copy if needed):");
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
            ImGui::InputTextMultiline("##oauth_url", const_cast<char*>(oauth_url.c_str()), oauth_url.length(), 
                                     ImVec2(-1, 60), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
            
            if (ImGui::Button("Copy URL to Clipboard")) {
                ImGui::SetClipboardText(oauth_url.c_str());
            }
        }
        
        ImGui::EndDisabled();
        
        ImGui::Separator();
        
        // Test Buttons
        ImGui::Text("Test Functions:");
        ImGui::BeginDisabled(!config_.twitch_enabled || !twitch_manager_ || !twitch_manager_->IsConnected());
        
        if (ImGui::Button("Test Chat Message")) {
            if (twitch_manager_) {
                twitch_manager_->TestChatMessage();
            }
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Test Donation Lock")) {
            if (twitch_manager_) {
                twitch_manager_->TestDonationEvent("TestUser", 5.0f);
            }
        }
        
        ImGui::EndDisabled();
        
        ImGui::EndDisabled(); // End of user_agreement disabled block
    }

    // Twitch Helper Methods Implementation
    void UIManager::InitializeTwitchManager() {
        twitch_manager_ = std::make_unique<TwitchManager>();
        
        if (twitch_manager_->Initialize(&config_)) {
            // Set up callbacks for Twitch events
            twitch_manager_->SetDonationCallback(
                [this](const std::string& username, float amount, const std::string& message) {
                    OnTwitchDonation(username, amount, message);
                }
            );
            
            twitch_manager_->SetBitsCallback(
                [this](const std::string& username, int bits, const std::string& message) {
                    OnTwitchBits(username, bits, message);
                }
            );
            
            twitch_manager_->SetSubscriptionCallback(
                [this](const std::string& username, int months, bool is_gift) {
                    OnTwitchSubscription(username, months, is_gift);
                }
            );
            
            // Set up chat command callback to handle lock/unlock commands
            twitch_manager_->SetChatCommandCallback(
                [this](const std::string& username, const std::string& command, const std::string& args) {
                    OnTwitchChatCommand(username, command, args);
                }
            );
            
            if (Logger::IsInitialized()) {
                Logger::Info("TwitchManager initialized successfully");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to initialize TwitchManager");
            }
        }
    }

    void UIManager::ShutdownTwitchManager() {
        if (twitch_manager_) {
            twitch_manager_->Shutdown();
            twitch_manager_.reset();
            
            if (Logger::IsInitialized()) {
                Logger::Info("TwitchManager shut down");
            }
        }
    }

    void UIManager::OnTwitchDonation(const std::string& username, float amount, const std::string& message) {
        if (!config_.twitch_enabled) {
            return;
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("Processing Twitch donation from " + username + ": $" + std::to_string(amount));
        }
        
        // Check if donation meets minimum requirement
        if (amount < config_.twitch_donation_minimum) {
            if (Logger::IsInitialized()) {
                Logger::Info("Donation amount below minimum threshold");
            }
            return;
        }
        
        // Calculate lock duration if dynamic duration is enabled
        float lock_duration = 0.0f;
        if (config_.twitch_lock_duration_enabled) {
            lock_duration = config_.twitch_lock_base_duration + (amount * config_.twitch_lock_per_dollar);
            lock_duration = (std::min)(lock_duration, config_.twitch_lock_max_duration);
        } else {
            lock_duration = config_.unlock_timer_duration; // Use default unlock timer duration
        }
        
        // Activate appropriate device locks
        if (config_.twitch_target_all_devices) {
            ActivateGlobalLock(true);
            
            if (config_.twitch_chat_enabled && twitch_manager_ && twitch_manager_->IsConnected()) {
                twitch_manager_->SendChatMessage("@" + username + " Thank you for the $" + std::to_string(amount) + 
                                    " donation! All devices locked for " + std::to_string((int)lock_duration) + " seconds!");
            }
        } else {
            // Lock specific devices based on targeting settings
            int locked_count = 0;
            for (auto& device : device_positions_) {
                bool should_lock = false;
                
                switch (device.role) {
                    case DeviceRole::HMD:
                        should_lock = config_.twitch_target_hmd;
                        break;
                    case DeviceRole::LeftController:
                        should_lock = config_.twitch_target_left_hand;
                        break;
                    case DeviceRole::RightController:
                        should_lock = config_.twitch_target_right_hand;
                        break;
                    case DeviceRole::LeftFoot:
                        should_lock = config_.twitch_target_left_foot;
                        break;
                    case DeviceRole::RightFoot:
                        should_lock = config_.twitch_target_right_foot;
                        break;
                    case DeviceRole::Hip:
                        should_lock = config_.twitch_target_hip;
                        break;
                    default:
                        should_lock = false;
                        break;
                }
                
                if (should_lock) {
                    LockDevicePosition(device.serial, true);
                    locked_count++;
                }
            }
            
            if (config_.twitch_chat_enabled && twitch_manager_ && twitch_manager_->IsConnected()) {
                twitch_manager_->SendChatMessage("@" + username + " Thank you for the $" + std::to_string(amount) + 
                                    " donation! " + std::to_string(locked_count) + " devices locked for " + 
                                    std::to_string((int)lock_duration) + " seconds!");
            }
        }
        
        // Start unlock timer if enabled
        if (config_.unlock_timer_enabled && lock_duration > 0) {
            twitch_unlock_timer_active_ = true;
            twitch_unlock_timer_remaining_ = lock_duration;
            twitch_unlock_timer_start_ = std::chrono::steady_clock::now();
        }
    }

    void UIManager::OnTwitchBits(const std::string& username, int bits, const std::string& message) {
        if (!config_.twitch_enabled || !config_.twitch_bits_enabled) {
            return;
        }
        
        if (bits < config_.twitch_bits_minimum) {
            return;
        }
        
        // Convert bits to dollar equivalent for lock duration calculation
        float dollar_equivalent = bits / 100.0f; // 100 bits = $1
        OnTwitchDonation(username, dollar_equivalent, message);
    }

    void UIManager::OnTwitchSubscription(const std::string& username, int months, bool is_gift) {
        if (!config_.twitch_enabled || !config_.twitch_subs_enabled) {
            return;
        }
        
        // Treat subscription as a $5 donation for lock duration calculation
        float sub_value = is_gift ? 10.0f : 5.0f; // Gift subs worth more
        OnTwitchDonation(username, sub_value, is_gift ? "Gift subscription!" : "Subscription!");
    }

    void UIManager::ProcessTwitchUnlockTimer() {
        if (!twitch_unlock_timer_active_) {
            return;
        }
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - twitch_unlock_timer_start_).count() / 1000.0f;
        
        twitch_unlock_timer_remaining_ = (std::max)(0.0f, twitch_unlock_timer_remaining_ - elapsed);
        twitch_unlock_timer_start_ = current_time;
        
        // Check for audio warnings
        if (config_.unlock_timer_audio_warnings) {
            if ((twitch_unlock_timer_remaining_ <= 60.0f && twitch_unlock_timer_remaining_ > 59.0f) ||
                (twitch_unlock_timer_remaining_ <= 30.0f && twitch_unlock_timer_remaining_ > 29.0f) ||
                (twitch_unlock_timer_remaining_ <= 10.0f && twitch_unlock_timer_remaining_ > 9.0f)) {
                
                // Play warning sound (using existing audio system)
                if (config_.audio.enabled) {
                    AudioManager::PlayWarningSound(config_.audio.volume);
                }
                
                // Send chat message if enabled
                if (config_.twitch_chat_enabled && twitch_manager_ && twitch_manager_->IsConnected()) {
                    twitch_manager_->SendChatMessage("⏰ " + std::to_string((int)twitch_unlock_timer_remaining_) + " seconds until unlock!");
                }
            }
        }
        
        // Check if timer has expired
        if (twitch_unlock_timer_remaining_ <= 0.0f) {
            twitch_unlock_timer_active_ = false;
            
            // Unlock all devices
            ActivateGlobalLock(false);
            
            // Reset individual locks as well
            for (auto& device : device_positions_) {
                if (device.locked) {
                    LockDevicePosition(device.serial, false);
                }
            }
            
            if (config_.twitch_chat_enabled && twitch_manager_ && twitch_manager_->IsConnected()) {
                twitch_manager_->SendChatMessage("🔓 Timer expired - all devices unlocked!");
            }
            
            if (Logger::IsInitialized()) {
                Logger::Info("Twitch unlock timer expired - all devices unlocked");
            }
        }
    }

    void UIManager::OnTwitchChatCommand(const std::string& username, const std::string& command, const std::string& args) {
        if (!config_.twitch_enabled || !config_.twitch_chat_enabled) {
            return;
        }
        
        if (Logger::IsInitialized()) {
            Logger::Info("Processing Twitch chat command '" + command + "' from " + username);
        }
        
        // Handle lock command - equivalent to clicking "Lock All Included Devices"
        if (command == config_.twitch_lock_command) {
            // Count devices that will be locked
            int devices_to_lock = 0;
            for (const auto& device : device_positions_) {
                if (device.include_in_locking) {
                    devices_to_lock++;
                }
            }
            
            if (devices_to_lock == 0) {
                if (Logger::IsInitialized()) {
                    Logger::Warning("No devices selected for locking via chat command");
                }
                return;
            }
            
            if (Logger::IsInitialized()) {
                Logger::Info("Executing global lock via chat command - " + std::to_string(devices_to_lock) + " devices will be locked");
            }
            
            // Use the same method as the UI button
            ActivateGlobalLock(true);
            
        }
        // Handle unlock command - equivalent to clicking "Unlock All Included Devices"  
        else if (command == config_.twitch_unlock_command) {
            if (Logger::IsInitialized()) {
                Logger::Info("Executing global unlock via chat command");
            }
            
            // Use the same method as the UI button
            ActivateGlobalLock(false);
            
        }
        // Handle status command - report current lock state
        else if (command == config_.twitch_status_command) {
            int total_devices = 0;
            int included_devices = 0;
            int locked_devices = 0;
            
            for (const auto& device : device_positions_) {
                total_devices++;
                if (device.include_in_locking) {
                    included_devices++;
                    if (device.locked || global_lock_active_) {
                        locked_devices++;
                    }
                }
            }
            
            std::string status_message = "@" + username + " StayPutVR Status: " + 
                                       std::to_string(total_devices) + " devices detected, " +
                                       std::to_string(included_devices) + " included in locking";
            
            if (global_lock_active_) {
                status_message += ", GLOBAL LOCK ACTIVE (" + std::to_string(locked_devices) + " devices locked)";
            } else if (locked_devices > 0) {
                status_message += ", " + std::to_string(locked_devices) + " devices individually locked";
            } else {
                status_message += ", all devices unlocked";
            }
            
            // Send status response to chat
            if (twitch_manager_ && twitch_manager_->IsConnected()) {
                twitch_manager_->SendChatMessage(status_message);
            }
            
            if (Logger::IsInitialized()) {
                Logger::Info("Sent status response: " + status_message);
            }
        }
        else {
            if (Logger::IsInitialized()) {
                Logger::Warning("Unknown chat command: " + command);
            }
        }
    }

    // OpenShock Helper Methods Implementation
    void UIManager::InitializeOpenShockManager() {
        openshock_manager_ = std::make_unique<OpenShockManager>();
        
        if (openshock_manager_->Initialize(&config_)) {
            // Set up callback for OpenShock action results
            openshock_manager_->SetActionCallback(
                [this](const std::string& action_type, bool success, const std::string& message) {
                    if (Logger::IsInitialized()) {
                        Logger::Info("OpenShock " + action_type + " " + (success ? "succeeded" : "failed") + 
                                   (message.empty() ? "" : ": " + message));
                    }
                }
            );
            
            if (Logger::IsInitialized()) {
                Logger::Info("OpenShockManager initialized successfully");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to initialize OpenShockManager");
            }
        }
    }

    void UIManager::ShutdownOpenShockManager() {
        if (openshock_manager_) {
            openshock_manager_->Shutdown();
            openshock_manager_.reset();
            
            if (Logger::IsInitialized()) {
                Logger::Info("OpenShockManager shut down");
            }
        }
    }

    void UIManager::InitializeButtplugManager() {
        buttplug_manager_ = std::make_unique<ButtplugManager>();
        
        if (buttplug_manager_->Initialize(&config_)) {
            // Set up callback for Buttplug action results
            buttplug_manager_->SetActionCallback(
                [this](const std::string& action_type, bool success, const std::string& message) {
                    if (Logger::IsInitialized()) {
                        if (success) {
                            Logger::Info("Buttplug action completed: " + action_type + " - " + message);
                        } else {
                            Logger::Error("Buttplug action failed: " + action_type + " - " + message);
                        }
                    }
                }
            );
            
            if (Logger::IsInitialized()) {
                Logger::Info("ButtplugManager initialized successfully");
            }
            
            // Auto-connect if enabled and user agreement is checked
            if (config_.buttplug_enabled && config_.buttplug_user_agreement) {
                if (Logger::IsInitialized()) {
                    Logger::Info("Auto-connecting to Buttplug/Intiface on startup");
                }
                if (!buttplug_manager_->Connect()) {
                    if (Logger::IsInitialized()) {
                        Logger::Warning("Failed to auto-connect to Buttplug: " + buttplug_manager_->GetLastError());
                    }
                }
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to initialize ButtplugManager");
            }
        }
    }

    void UIManager::ShutdownButtplugManager() {
        if (buttplug_manager_) {
            buttplug_manager_->Shutdown();
            buttplug_manager_.reset();
            
            if (Logger::IsInitialized()) {
                Logger::Info("ButtplugManager shut down");
            }
        }
    }

    void UIManager::RenderOpenShockTab() {
        if (openshock_panel_) {
            openshock_panel_->Render();
        }
    }

} // namespace StayPutVR
