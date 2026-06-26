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

    void UIManager::RenderOSCTriggersTab() {
        bool changed = false;
        ImGui::TextWrapped("Avatar OSC triggers fire a shock on all configured shockers when the "
                           "matching parameter is received. Change the parameter PATHS in "
                           "Settings -> OSC.");
        ImGui::Separator();

        ImGui::SeparatorText("Bite  (SPVR_Bite)");
        // VRC BiteTech branding: logo + "Supports" line.
        LoadBiteTechLogo();
        if (bitetech_logo_tex_ != 0 && bitetech_logo_h_ > 0) {
            const float h = 22.0f;
            float w = h * (float)bitetech_logo_w_ / (float)bitetech_logo_h_;
            ImGui::Image((ImTextureID)(intptr_t)bitetech_logo_tex_, ImVec2(w, h));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
        }
        ImGui::TextDisabled("Supports VRC BiteTech");
        if (ImGui::Checkbox("Enable Bite trigger", &config_.osc_bite_enabled)) changed = true;
        if (ImGui::Checkbox("Use per-device disobedience intensities##bite", &config_.osc_bite_use_individual_intensities)) changed = true;
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("When on, each shocker fires at its individual disobedience intensity "
                                  "(configured in the PiShock/OpenShock tabs) instead of the single Bite intensity below.");
        ImGui::BeginDisabled(config_.osc_bite_use_individual_intensities);
        if (ImGuiHelpers::SliderFloatWithButtons("Bite intensity", &config_.osc_bite_intensity, 0.0f, 1.0f, 0.01f, "%.2f")) changed = true;
        ImGui::EndDisabled();
        if (ImGuiHelpers::SliderFloatWithButtons("Bite duration (s)", &config_.osc_bite_duration, 0.1f, 15.0f, 0.1f, "%.1f")) changed = true;

        ImGui::SeparatorText("Shock  (/avatar/parameters/Shock)");
        ImGui::TextDisabled("Supports Simple Shock System");
        if (ImGui::Checkbox("Enable Shock trigger", &config_.osc_shock_enabled)) changed = true;
        if (ImGui::Checkbox("Use per-device disobedience intensities##shock", &config_.osc_shock_use_individual_intensities)) changed = true;
        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("When on, each shocker fires at its individual disobedience intensity "
                                  "(configured in the PiShock/OpenShock tabs) instead of the single Shock intensity below.");
        ImGui::BeginDisabled(config_.osc_shock_use_individual_intensities);
        if (ImGuiHelpers::SliderFloatWithButtons("Shock intensity", &config_.osc_shock_intensity, 0.0f, 1.0f, 0.01f, "%.2f")) changed = true;
        ImGui::EndDisabled();
        if (ImGuiHelpers::SliderFloatWithButtons("Shock duration (s)", &config_.osc_shock_duration, 0.1f, 15.0f, 0.1f, "%.1f")) changed = true;

        ImGui::Spacing();
        ImGui::TextDisabled("Both are blocked while emergency stop is active.");

        if (changed) SaveConfig();
    }

    void UIManager::RenderOSCTab() {
        ImGui::Text("OSC Configuration");
        ImGui::Separator();

        if (osc_enabled_) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Disable OSC", ImVec2(150, 40))) {
                DisconnectOSC();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "ACTIVE - Listening for OSC commands");
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            if (ImGui::Button("Enable OSC", ImVec2(150, 40))) {
                HandleOSCConnection();
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "INACTIVE - Not listening for OSC commands");
        }

        // Surface a silent OSCQuery failure: if OSC Query is on and running but the
        // mDNS socket couldn't bind (UDP 5353 in use), VRChat can't discover us.
        if (osc_enabled_ && config_.osc_query_enabled && osc_query_server_ &&
            osc_query_server_->IsRunning() && !osc_query_server_->IsAdvertising()) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                "OSC Query: mDNS unavailable (UDP 5353 in use) - VRChat may not discover StayPutVR. "
                "Turn off OSC Query to use manual ports, or free port 5353.");
        }

        ImGui::Spacing();

        // Inputs for OSC settings
        bool changed = false;

        // --- Editing buffers, declared at function scope so the per-section
        //     "Reset" buttons can refresh them. Initialized from config on first
        //     use (when still empty). ---
        static char osc_ip[128];
        static int osc_send_port = 9000;
        static int osc_receive_port = 9001;
        static char hmd_path[128];
        static char left_hand_path[128];
        static char right_hand_path[128];
        static char left_foot_path[128];
        static char right_foot_path[128];
        static char hip_path[128];
        static char hmd_include_path[128];
        static char left_hand_include_path[128];
        static char right_hand_include_path[128];
        static char left_foot_include_path[128];
        static char right_foot_include_path[128];
        static char hip_include_path[128];
        static char global_lock_path[128];
        static char global_unlock_path[128];
        static char global_out_of_bounds_path[128];
        static char bite_path[128];
        static char shock_path[128];
        static char estop_stretch_path[128];

        if (strlen(osc_ip) == 0) strcpy_s(osc_ip, sizeof(osc_ip), config_.osc_address.c_str());
        if (osc_send_port != config_.osc_send_port) osc_send_port = config_.osc_send_port;
        if (osc_receive_port != config_.osc_receive_port) osc_receive_port = config_.osc_receive_port;
        if (strlen(hmd_path) == 0) strcpy_s(hmd_path, sizeof(hmd_path), config_.osc_lock_path_hmd.c_str());
        if (strlen(left_hand_path) == 0) strcpy_s(left_hand_path, sizeof(left_hand_path), config_.osc_lock_path_left_hand.c_str());
        if (strlen(right_hand_path) == 0) strcpy_s(right_hand_path, sizeof(right_hand_path), config_.osc_lock_path_right_hand.c_str());
        if (strlen(left_foot_path) == 0) strcpy_s(left_foot_path, sizeof(left_foot_path), config_.osc_lock_path_left_foot.c_str());
        if (strlen(right_foot_path) == 0) strcpy_s(right_foot_path, sizeof(right_foot_path), config_.osc_lock_path_right_foot.c_str());
        if (strlen(hip_path) == 0) strcpy_s(hip_path, sizeof(hip_path), config_.osc_lock_path_hip.c_str());
        if (strlen(hmd_include_path) == 0) strcpy_s(hmd_include_path, sizeof(hmd_include_path), config_.osc_include_path_hmd.c_str());
        if (strlen(left_hand_include_path) == 0) strcpy_s(left_hand_include_path, sizeof(left_hand_include_path), config_.osc_include_path_left_hand.c_str());
        if (strlen(right_hand_include_path) == 0) strcpy_s(right_hand_include_path, sizeof(right_hand_include_path), config_.osc_include_path_right_hand.c_str());
        if (strlen(left_foot_include_path) == 0) strcpy_s(left_foot_include_path, sizeof(left_foot_include_path), config_.osc_include_path_left_foot.c_str());
        if (strlen(right_foot_include_path) == 0) strcpy_s(right_foot_include_path, sizeof(right_foot_include_path), config_.osc_include_path_right_foot.c_str());
        if (strlen(hip_include_path) == 0) strcpy_s(hip_include_path, sizeof(hip_include_path), config_.osc_include_path_hip.c_str());
        if (strlen(global_lock_path) == 0) strcpy_s(global_lock_path, sizeof(global_lock_path), config_.osc_global_lock_path.c_str());
        if (strlen(global_unlock_path) == 0) strcpy_s(global_unlock_path, sizeof(global_unlock_path), config_.osc_global_unlock_path.c_str());
        if (strlen(global_out_of_bounds_path) == 0) strcpy_s(global_out_of_bounds_path, sizeof(global_out_of_bounds_path), config_.osc_global_out_of_bounds_path.c_str());
        if (strlen(bite_path) == 0) strcpy_s(bite_path, sizeof(bite_path), config_.osc_bite_path.c_str());
        if (strlen(shock_path) == 0) strcpy_s(shock_path, sizeof(shock_path), config_.osc_shock_path.c_str());
        if (strlen(estop_stretch_path) == 0) strcpy_s(estop_stretch_path, sizeof(estop_stretch_path), config_.osc_estop_stretch_path.c_str());

        // ===== Connection =====
        if (ImGui::CollapsingHeader("Connection", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::InputText("OSC IP Address", osc_ip, IM_ARRAYSIZE(osc_ip))) {
                config_.osc_address = osc_ip;
                changed = true;
            }
            if (ImGui::InputInt("OSC Send Port", &osc_send_port)) {
                config_.osc_send_port = osc_send_port;
                changed = true;
            }
            ImGui::SameLine();
            ImGuiHelpers::HelpTooltip("Port used to send locking status to VRChat");
            if (ImGui::InputInt("OSC Receive Port", &osc_receive_port)) {
                config_.osc_receive_port = osc_receive_port;
                changed = true;
            }
            ImGui::SameLine();
            ImGuiHelpers::HelpTooltip("Port used to receive interaction data from VRChat, such as locking and unlocking");

            if (ImGui::Checkbox("OSC Query (mDNS auto-discovery)", &config_.osc_query_enabled)) {
                changed = true;
            }
            ImGui::SameLine();
            ImGuiHelpers::HelpTooltip("When enabled, OSC ports are auto-negotiated via mDNS/OSCQuery: "
                "StayPutVR binds an ephemeral receive port (avoiding conflicts with other OSC apps "
                "that hold 9001) and advertises it to VRChat, and discovers VRChat's OSC port for "
                "sending. When disabled, the manual Send/Receive ports above are used. "
                "Changes take effect the next time OSC is enabled.");

            ImGui::Spacing();
            if (ImGui::SmallButton("Reset to Defaults##conn")) {
                config_.osc_address = "127.0.0.1";
                strcpy_s(osc_ip, sizeof(osc_ip), config_.osc_address.c_str());
                config_.osc_send_port = 9000; osc_send_port = 9000;
                config_.osc_receive_port = 9001; osc_receive_port = 9001;
                config_.osc_query_enabled = true;
                changed = true;
            }
        }

        // ===== Bite Trigger (path only; enable/intensity live in OSC Triggers) =====
        if (ImGui::CollapsingHeader("Bite Trigger")) {
            ImGui::TextWrapped("OSC parameter path for the bite trigger. Enable it and tune "
                "intensity/duration in Integrations > OSC Triggers.");
            if (ImGui::InputText("Bite Path", bite_path, IM_ARRAYSIZE(bite_path))) {
                config_.osc_bite_path = bite_path;
                changed = true;
            }
            if (ImGui::SmallButton("Reset to Defaults##bite")) {
                config_.osc_bite_path = "/avatar/parameters/SPVR_Bite";
                strcpy_s(bite_path, sizeof(bite_path), config_.osc_bite_path.c_str());
                changed = true;
            }
        }

        // ===== Shock Trigger (path only; enable/intensity live in OSC Triggers) =====
        if (ImGui::CollapsingHeader("Shock Trigger")) {
            ImGui::TextWrapped("OSC parameter path for the shock trigger (e.g. the ChilloutCharles "
                "shock param). Enable it and tune intensity/duration in Integrations > OSC Triggers.");
            if (ImGui::InputText("Shock Path", shock_path, IM_ARRAYSIZE(shock_path))) {
                config_.osc_shock_path = shock_path;
                changed = true;
            }
            if (ImGui::SmallButton("Reset to Defaults##shock")) {
                config_.osc_shock_path = "/avatar/parameters/Shock";
                strcpy_s(shock_path, sizeof(shock_path), config_.osc_shock_path.c_str());
                changed = true;
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Advanced Settings");
        ImGui::TextWrapped("Advanced Settings. The following values are generally safe and desirable "
            "to leave at their defaults.");
        ImGui::Spacing();

        // ===== Device Lock Paths =====
        if (ImGui::CollapsingHeader("Device Lock Paths")) {
            ImGui::TextWrapped("These are the OSC addresses that the application will listen to for locking/unlocking signals. Sending a value of true/1 to these addresses will lock the corresponding device, false/0 will unlock it.");
            if (ImGui::InputText("HMD Lock Path", hmd_path, IM_ARRAYSIZE(hmd_path))) {
                config_.osc_lock_path_hmd = hmd_path;
                changed = true;
            }
            if (ImGui::InputText("Left Hand Lock Path", left_hand_path, IM_ARRAYSIZE(left_hand_path))) {
                config_.osc_lock_path_left_hand = left_hand_path;
                changed = true;
            }
            if (ImGui::InputText("Right Hand Lock Path", right_hand_path, IM_ARRAYSIZE(right_hand_path))) {
                config_.osc_lock_path_right_hand = right_hand_path;
                changed = true;
            }
            if (ImGui::InputText("Left Foot Lock Path", left_foot_path, IM_ARRAYSIZE(left_foot_path))) {
                config_.osc_lock_path_left_foot = left_foot_path;
                changed = true;
            }
            if (ImGui::InputText("Right Foot Lock Path", right_foot_path, IM_ARRAYSIZE(right_foot_path))) {
                config_.osc_lock_path_right_foot = right_foot_path;
                changed = true;
            }
            if (ImGui::InputText("Hip Lock Path", hip_path, IM_ARRAYSIZE(hip_path))) {
                config_.osc_lock_path_hip = hip_path;
                changed = true;
            }
            ImGui::Spacing();
            if (ImGui::SmallButton("Reset to Defaults##lockpaths")) {
                config_.osc_lock_path_hmd = "/avatar/parameters/SPVR_HMD_Latch_IsPosed";
                strcpy_s(hmd_path, sizeof(hmd_path), config_.osc_lock_path_hmd.c_str());
                config_.osc_lock_path_left_hand = "/avatar/parameters/SPVR_ControllerLeft_Latch_IsPosed";
                strcpy_s(left_hand_path, sizeof(left_hand_path), config_.osc_lock_path_left_hand.c_str());
                config_.osc_lock_path_right_hand = "/avatar/parameters/SPVR_ControllerRight_Latch_IsPosed";
                strcpy_s(right_hand_path, sizeof(right_hand_path), config_.osc_lock_path_right_hand.c_str());
                config_.osc_lock_path_left_foot = "/avatar/parameters/SPVR_FootLeft_Latch_IsPosed";
                strcpy_s(left_foot_path, sizeof(left_foot_path), config_.osc_lock_path_left_foot.c_str());
                config_.osc_lock_path_right_foot = "/avatar/parameters/SPVR_FootRight_Latch_IsPosed";
                strcpy_s(right_foot_path, sizeof(right_foot_path), config_.osc_lock_path_right_foot.c_str());
                config_.osc_lock_path_hip = "/avatar/parameters/SPVR_Hip_Latch_IsPosed";
                strcpy_s(hip_path, sizeof(hip_path), config_.osc_lock_path_hip.c_str());
                changed = true;
            }
        }

        // ===== Device Include Paths =====
        if (ImGui::CollapsingHeader("Device Include Paths")) {
            ImGui::TextWrapped("These are the OSC addresses that toggle whether a device is included in locking. When a true/1 value is received, the include state for that device will be toggled.");
            if (ImGui::InputText("HMD Include Path", hmd_include_path, IM_ARRAYSIZE(hmd_include_path))) {
                config_.osc_include_path_hmd = hmd_include_path;
                changed = true;
            }
            if (ImGui::InputText("Left Hand Include Path", left_hand_include_path, IM_ARRAYSIZE(left_hand_include_path))) {
                config_.osc_include_path_left_hand = left_hand_include_path;
                changed = true;
            }
            if (ImGui::InputText("Right Hand Include Path", right_hand_include_path, IM_ARRAYSIZE(right_hand_include_path))) {
                config_.osc_include_path_right_hand = right_hand_include_path;
                changed = true;
            }
            if (ImGui::InputText("Left Foot Include Path", left_foot_include_path, IM_ARRAYSIZE(left_foot_include_path))) {
                config_.osc_include_path_left_foot = left_foot_include_path;
                changed = true;
            }
            if (ImGui::InputText("Right Foot Include Path", right_foot_include_path, IM_ARRAYSIZE(right_foot_include_path))) {
                config_.osc_include_path_right_foot = right_foot_include_path;
                changed = true;
            }
            if (ImGui::InputText("Hip Include Path", hip_include_path, IM_ARRAYSIZE(hip_include_path))) {
                config_.osc_include_path_hip = hip_include_path;
                changed = true;
            }
            ImGui::Spacing();
            if (ImGui::SmallButton("Reset to Defaults##includepaths")) {
                config_.osc_include_path_hmd = "/avatar/parameters/SPVR_HMD_include";
                strcpy_s(hmd_include_path, sizeof(hmd_include_path), config_.osc_include_path_hmd.c_str());
                config_.osc_include_path_left_hand = "/avatar/parameters/SPVR_ControllerLeft_include";
                strcpy_s(left_hand_include_path, sizeof(left_hand_include_path), config_.osc_include_path_left_hand.c_str());
                config_.osc_include_path_right_hand = "/avatar/parameters/SPVR_ControllerRight_include";
                strcpy_s(right_hand_include_path, sizeof(right_hand_include_path), config_.osc_include_path_right_hand.c_str());
                config_.osc_include_path_left_foot = "/avatar/parameters/SPVR_FootLeft_include";
                strcpy_s(left_foot_include_path, sizeof(left_foot_include_path), config_.osc_include_path_left_foot.c_str());
                config_.osc_include_path_right_foot = "/avatar/parameters/SPVR_FootRight_include";
                strcpy_s(right_foot_include_path, sizeof(right_foot_include_path), config_.osc_include_path_right_foot.c_str());
                config_.osc_include_path_hip = "/avatar/parameters/SPVR_Hip_include";
                strcpy_s(hip_include_path, sizeof(hip_include_path), config_.osc_include_path_hip.c_str());
                changed = true;
            }
        }

        // ===== Global Lock / Unlock / Out-of-Bounds =====
        if (ImGui::CollapsingHeader("Global Lock / Unlock / Out-of-Bounds")) {
            ImGui::TextWrapped("These are the OSC addresses that will trigger global lock or unlock of all enabled devices.");
            if (ImGui::InputText("Global Lock Path", global_lock_path, IM_ARRAYSIZE(global_lock_path))) {
                config_.osc_global_lock_path = global_lock_path;
                changed = true;
            }
            if (ImGui::InputText("Global Unlock Path", global_unlock_path, IM_ARRAYSIZE(global_unlock_path))) {
                config_.osc_global_unlock_path = global_unlock_path;
                changed = true;
            }

            ImGui::Separator();
            bool global_out_of_bounds_enabled = config_.osc_global_out_of_bounds_enabled;
            if (ImGui::Checkbox("Enable Global Out-of-Bounds Trigger", &global_out_of_bounds_enabled)) {
                config_.osc_global_out_of_bounds_enabled = global_out_of_bounds_enabled;
                changed = true;
            }
            ImGui::SameLine();
            ImGuiHelpers::HelpTooltip("When enabled, receiving the /avatar/parameters/SPVR_Global_OutOfBounds parameter will trigger out-of-bounds actions");
            if (ImGui::InputText("Global Out-of-Bounds Path", global_out_of_bounds_path, IM_ARRAYSIZE(global_out_of_bounds_path))) {
                config_.osc_global_out_of_bounds_path = global_out_of_bounds_path;
                changed = true;
            }
            ImGui::Spacing();
            if (ImGui::SmallButton("Reset to Defaults##globalpaths")) {
                config_.osc_global_lock_path = "/avatar/parameters/SPVR_Global_Lock";
                strcpy_s(global_lock_path, sizeof(global_lock_path), config_.osc_global_lock_path.c_str());
                config_.osc_global_unlock_path = "/avatar/parameters/SPVR_Global_Unlock";
                strcpy_s(global_unlock_path, sizeof(global_unlock_path), config_.osc_global_unlock_path.c_str());
                config_.osc_global_out_of_bounds_path = "/avatar/parameters/SPVR_Global_OutOfBounds";
                strcpy_s(global_out_of_bounds_path, sizeof(global_out_of_bounds_path), config_.osc_global_out_of_bounds_path.c_str());
                changed = true;
            }
        }

        // ===== Emergency Stop Stretch =====
        if (ImGui::CollapsingHeader("Emergency Stop Stretch")) {
            ImGui::TextWrapped("Enable emergency unlock when receiving the SPVR_EStop_Stretch parameter from VRChat with a value >= 0.5.");
            bool estop_stretch_enabled = config_.osc_estop_stretch_enabled;
            if (ImGui::Checkbox("Enable Emergency Stop Stretch", &estop_stretch_enabled)) {
                config_.osc_estop_stretch_enabled = estop_stretch_enabled;
                changed = true;
            }
            ImGui::SameLine();
            ImGuiHelpers::HelpTooltip("When enabled, receiving the /avatar/parameters/SPVR_EStop_Stretch parameter with value >= 0.5 will immediately unlock all devices (emergency stop)");
            if (ImGui::InputText("Emergency Stop Stretch Path", estop_stretch_path, IM_ARRAYSIZE(estop_stretch_path))) {
                config_.osc_estop_stretch_path = estop_stretch_path;
                changed = true;
            }
            ImGui::Spacing();
            if (ImGui::SmallButton("Reset to Defaults##estop")) {
                config_.osc_estop_stretch_path = "/avatar/parameters/SPVR_EStop_Stretch";
                strcpy_s(estop_stretch_path, sizeof(estop_stretch_path), config_.osc_estop_stretch_path.c_str());
                changed = true;
            }
        }

        // ===== Chaining Mode =====
        if (ImGui::CollapsingHeader("Chaining Mode")) {
            bool chaining_mode = config_.chaining_mode;
            if (ImGui::Checkbox("Enable Chaining Mode", &chaining_mode)) {
                config_.chaining_mode = chaining_mode;
                changed = true;
            }
            ImGui::SameLine();
            ImGuiHelpers::HelpTooltip("When a device is locked via OSC, all devices will be locked");
        }

        // ===== Debug / OSC Simulation =====
        // Drive the avatar's SPVR_*_Status params without a headset, to develop
        // and test the prefab. Sends go to VRChat on the configured send port.
        if (ImGui::CollapsingHeader("Debug / OSC Simulation")) {
            ImGui::TextWrapped("Send device status to VRChat to drive the avatar prefab "
                               "(no headset required). Requires OSC to be enabled above.");
            if (!osc_enabled_) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "OSC is not enabled - sends are no-ops until you enable it.");
            }

            static const char* kStatusNames[] = {
                "0 - Disabled", "1 - Unlocked", "2 - Locked (Safe)",
                "3 - Locked (Warning)", "4 - Locked (Disobedience)", "5 - Locked (Out of Bounds)"
            };
            struct DebugDevice { const char* label; OSCDeviceType type; };
            static const DebugDevice kDebugDevices[] = {
                { "HMD",              OSCDeviceType::HMD },
                { "Controller Left",  OSCDeviceType::ControllerLeft },
                { "Controller Right", OSCDeviceType::ControllerRight },
                { "Foot Left",        OSCDeviceType::FootLeft },
                { "Foot Right",       OSCDeviceType::FootRight },
                { "Hip",              OSCDeviceType::Hip },
                { "Jaw",              OSCDeviceType::Jaw },
            };
            static int debug_status[IM_ARRAYSIZE(kDebugDevices)] = {};

            for (int i = 0; i < IM_ARRAYSIZE(kDebugDevices); ++i) {
                ImGui::PushID(i);
                ImGui::SetNextItemWidth(280);
                if (ImGui::Combo(kDebugDevices[i].label, &debug_status[i], kStatusNames, IM_ARRAYSIZE(kStatusNames))) {
                    OSCManager::GetInstance().SendDeviceStatus(kDebugDevices[i].type, static_cast<DeviceStatus>(debug_status[i]));
                }
                ImGui::PopID();
            }

            ImGui::Spacing();
            auto sendAll = [&](DeviceStatus s) {
                for (int i = 0; i < IM_ARRAYSIZE(kDebugDevices); ++i) {
                    debug_status[i] = static_cast<int>(s);
                    OSCManager::GetInstance().SendDeviceStatus(kDebugDevices[i].type, s);
                }
            };
            if (ImGui::Button("All Disabled")) sendAll(DeviceStatus::Disabled);
            ImGui::SameLine();
            if (ImGui::Button("All Unlocked")) sendAll(DeviceStatus::Unlocked);
            ImGui::SameLine();
            if (ImGui::Button("All Locked")) sendAll(DeviceStatus::LockedSafe);
            ImGui::SameLine();
            if (ImGui::Button("All Out of Bounds")) sendAll(DeviceStatus::LockedOutOfBounds);
        }

        // Save changes
        if (changed) {
            SaveConfig();

            // If OSC is enabled, update the paths in the OSCManager
            if (osc_enabled_) {
                OSCManager::GetInstance().SetConfig(config_);
            }
        }
    }

    void UIManager::HandleOSCConnection() {
        if (osc_enabled_) {
            if (Logger::IsInitialized()) {
                Logger::Debug("HandleOSCConnection: OSC already enabled, verifying callbacks");
            }
            VerifyOSCCallbacks();
            return;
        }

        // Initialize OSC manager. When OSCQuery is enabled, bind the receive
        // socket to an ephemeral port (avoids the crash/conflict when another
        // OSC app already holds 9001) and advertise the real port via mDNS.
        bool use_query = config_.osc_query_enabled;
        if (OSCManager::GetInstance().Initialize(config_.osc_address, config_.osc_send_port,
                                                 config_.osc_receive_port, use_query)) {
            osc_enabled_ = true;
            config_.osc_enabled = true;
            SaveConfig();

            // Set the config for OSC paths
            OSCManager::GetInstance().SetConfig(config_);

            // Set up callbacks
            VerifyOSCCallbacks();

            // Start the OSCQuery (mDNS) server so VRChat can auto-discover our
            // actual (ephemeral) receive port, and so we can discover VRChat's
            // OSC port and retarget sends to it.
            if (use_query) {
                StartOSCQuery();
            }

            if (Logger::IsInitialized()) {
                Logger::Info("OSC connection established");
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Error("Failed to establish OSC connection");
            }
        }
    }

    void UIManager::StartOSCQuery() {
        int receive_port = OSCManager::GetInstance().GetActualReceivePort();

        if (!osc_query_server_) {
            osc_query_server_ = std::make_unique<OSCQueryServer>();
        }

        // Advertise the SPVR avatar params VRChat should send to us. VRChat only
        // needs the receive port to start sending; the parameter tree improves
        // discoverability in OSCQuery-aware tooling. Mark them WriteOnly since
        // these are inputs VRChat writes to StayPutVR.
        using A = OSCQueryServer::Access;
        const std::string p = "/avatar/parameters/";
        osc_query_server_->AddParameter(config_.osc_lock_path_hmd, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_lock_path_left_hand, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_lock_path_right_hand, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_lock_path_left_foot, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_lock_path_right_foot, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_lock_path_hip, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_include_path_hmd, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_include_path_left_hand, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_include_path_right_hand, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_include_path_left_foot, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_include_path_right_foot, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_include_path_hip, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_global_lock_path, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_global_unlock_path, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_global_out_of_bounds_path, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_bite_path, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_shock_path, "T", A::WriteOnly, false);
        osc_query_server_->AddParameter(config_.osc_estop_stretch_path, "f", A::WriteOnly, 0.0f);
        if (config_.jawopen_enabled) {
            osc_query_server_->AddParameter(config_.osc_jawopen_path, "f", A::WriteOnly, 0.0f);
        }
        // Unified collar mode: the momentary toggle (inbound) and the mode echo
        // (outbound) are advertised whenever either gated integration is enabled.
        if (config_.jawopen_enabled || config_.mic_enabled) {
            if (!config_.osc_collar_toggle_path.empty())
                osc_query_server_->AddParameter(config_.osc_collar_toggle_path, "T", A::WriteOnly, false);
            osc_query_server_->AddParameter(p + "avatar/parameters/SPVR_Collar_Mode", "i", A::WriteOnly, 0);
        }
        osc_query_server_->AddParameter(p + "avatar/change", "s", A::WriteOnly, std::string());

        // When VRChat's OSC port is discovered, retarget our send socket to it.
        // Until then sends fall back to the configured osc_send_port (9000).
        osc_query_server_->SetVRChatPortDiscoveredCallback([](int port) {
            OSCManager::GetInstance().SetSendPort(port);
        });

        if (osc_query_server_->Start(receive_port)) {
            if (Logger::IsInitialized()) {
                Logger::Info("OSCQuery started, advertising receive port " + std::to_string(receive_port));
            }
        } else {
            if (Logger::IsInitialized()) {
                Logger::Warning("OSCQuery failed to start; falling back to fixed OSC ports");
            }
        }
    }

    void UIManager::StopOSCQuery() {
        if (osc_query_server_) {
            osc_query_server_->Stop();
            osc_query_server_.reset();
            if (Logger::IsInitialized()) {
                Logger::Info("OSCQuery stopped");
            }
        }
    }

    void UIManager::DisconnectOSC() {
        if (!osc_enabled_) {
            return;
        }

        // Stop OSCQuery before tearing down the OSC sockets.
        StopOSCQuery();

        OSCManager::GetInstance().Shutdown();
        osc_enabled_ = false;
        config_.osc_enabled = false;
        SaveConfig();

        if (Logger::IsInitialized()) {
            Logger::Info("OSC connection closed");
        }
    }

    void UIManager::OnDeviceLocked(OSCDeviceType device, bool locked) {
        // Find devices with the appropriate role
        for (auto& dev : device_positions_) {
            // Check if this device has the role that matches the OSC device type
            DeviceRole targetRole = DeviceRole::None;
            
            switch (device) {
                case OSCDeviceType::HMD:
                    targetRole = DeviceRole::HMD;
                    break;
                case OSCDeviceType::ControllerLeft:
                    targetRole = DeviceRole::LeftController;
                    break;
                case OSCDeviceType::ControllerRight:
                    targetRole = DeviceRole::RightController;
                    break;
                case OSCDeviceType::FootLeft:
                    targetRole = DeviceRole::LeftFoot;
                    break;
                case OSCDeviceType::FootRight:
                    targetRole = DeviceRole::RightFoot;
                    break;
                case OSCDeviceType::Hip:
                    targetRole = DeviceRole::Hip;
                    break;
            }
            
            // Skip if no matching role
            if (dev.role != targetRole) {
                continue;
            }
            
            // Lock or unlock the device
            LockDevicePosition(dev.serial, locked);
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Device " + dev.serial + " " + 
                    (locked ? "locked" : "unlocked") + " via OSC (role matching)");
            }
            
            // Play appropriate sound effect
            if (config_.audio.enabled) {
                if (locked && config_.audio.lock) {
                    AudioManager::PlayLockSound(config_.audio.volume);
                } else if (!locked && config_.audio.unlock) {
                    AudioManager::PlayUnlockSound(config_.audio.volume);
                }
            }
            
            // If chaining mode is enabled and we're locking, activate global lock
            if (locked && config_.chaining_mode) {
                ActivateGlobalLock(true);
            }
            
            // Update the device status via OSC
            UpdateDeviceStatus(device, locked ? DeviceStatus::LockedSafe : DeviceStatus::Unlocked);
        }
        
        // Diagnostic logging if no device was found with the matching role
        bool found_matching_device = false;
        for (auto& dev : device_positions_) {
            DeviceRole targetRole = DeviceRole::None;
            switch (device) {
                case OSCDeviceType::HMD: targetRole = DeviceRole::HMD; break;
                case OSCDeviceType::ControllerLeft: targetRole = DeviceRole::LeftController; break;
                case OSCDeviceType::ControllerRight: targetRole = DeviceRole::RightController; break;
                case OSCDeviceType::FootLeft: targetRole = DeviceRole::LeftFoot; break;
                case OSCDeviceType::FootRight: targetRole = DeviceRole::RightFoot; break;
                case OSCDeviceType::Hip: targetRole = DeviceRole::Hip; break;
            }
            if (dev.role == targetRole) {
                found_matching_device = true;
                break;
            }
        }
        
        if (!found_matching_device && StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Warning("OSC received lock command for " + GetOSCDeviceString(device) + 
                                      " but no device with matching role was found. Command ignored.");
            
            // Print all current device roles for debugging
            std::string device_roles = "Current device roles: ";
            for (const auto& dev : device_positions_) {
                device_roles += dev.serial + "=";
                switch (dev.role) {
                    case DeviceRole::None: device_roles += "None"; break;
                    case DeviceRole::HMD: device_roles += "HMD"; break;
                    case DeviceRole::LeftController: device_roles += "LeftController"; break;
                    case DeviceRole::RightController: device_roles += "RightController"; break;
                    case DeviceRole::Hip: device_roles += "Hip"; break;
                    case DeviceRole::LeftFoot: device_roles += "LeftFoot"; break;
                    case DeviceRole::RightFoot: device_roles += "RightFoot"; break;
                    default: device_roles += "Unknown"; break;
                }
                device_roles += ", ";
            }
            if (!device_positions_.empty()) {
                device_roles.pop_back(); // Remove last comma
                device_roles.pop_back(); // Remove last space
            }
            StayPutVR::Logger::Debug(device_roles);
        }
    }

    void UIManager::UpdateDeviceStatus(OSCDeviceType device, DeviceStatus status) {
        if (!osc_enabled_) {
            return;
        }

        OSCManager::GetInstance().SendDeviceStatus(device, status);
    }

    void UIManager::SendCollarMode(int mode) {
        if (!osc_enabled_) {
            return;
        }
        OSCManager::GetInstance().SendCollarMode(mode);
    }

    const char* UIManager::CollarModeName(int mode) const {
        switch (mode) {
            case static_cast<int>(CollarMode::Jaw):  return "Jaw";
            case static_cast<int>(CollarMode::Mic):  return "Mic";
            case static_cast<int>(CollarMode::Both): return "Both";
            default: return "Neither";
        }
    }

    bool UIManager::CollarModeIncludesJaw() const {
        int m = collar_mode_.load();
        return m == static_cast<int>(CollarMode::Jaw) || m == static_cast<int>(CollarMode::Both);
    }

    bool UIManager::CollarModeIncludesMic() const {
        int m = collar_mode_.load();
        return m == static_cast<int>(CollarMode::Mic) || m == static_cast<int>(CollarMode::Both);
    }

    // UI thread: recompute which collar modes are selectable from the enabled+agreed
    // integrations, and snap the current mode to a valid one if it just became invalid
    // (e.g. the user disabled an integration). The OSC receive thread reads only the
    // resulting atomic mask, never config_.
    void UIManager::RecomputeCollarValidMask() {
        bool jaw_ok = config_.jawopen_enabled && config_.jawopen_user_agreement;
        bool mic_ok = config_.mic_enabled && config_.mic_user_agreement;
        int mask = 1 << static_cast<int>(CollarMode::Neither); // Neither always valid
        if (jaw_ok) mask |= 1 << static_cast<int>(CollarMode::Jaw);
        if (mic_ok) mask |= 1 << static_cast<int>(CollarMode::Mic);
        if (jaw_ok && mic_ok) mask |= 1 << static_cast<int>(CollarMode::Both);
        collar_valid_mask_.store(mask);

        int cur = collar_mode_.load();
        if (!((mask >> cur) & 1)) {
            collar_mode_.store(static_cast<int>(CollarMode::Neither));
            SendCollarMode(static_cast<int>(CollarMode::Neither));
        }
    }

    // Advance to the next selectable mode in cycle order 0->1->2->3->0, skipping
    // modes whose integration isn't enabled+agreed. Returns the same mode if none
    // other is valid (so a lone Neither just stays Neither).
    int UIManager::NextValidCollarMode(int current) const {
        int mask = collar_valid_mask_.load();
        for (int i = 1; i <= 4; ++i) {
            int cand = (current + i) % 4;
            if ((mask >> cand) & 1) return cand;
        }
        return current;
    }

    // Helper function to map DeviceType to OSCDeviceType
    OSCDeviceType UIManager::MapToOSCDeviceType(DeviceType type) {
        switch (type) {
            case DeviceType::HMD: 
                return OSCDeviceType::HMD;
            case DeviceType::CONTROLLER: 
                // This is a simplification - in reality you'd need to determine which controller
                return OSCDeviceType::ControllerRight;
            default:
                return OSCDeviceType::HMD; // Default to HMD
        }
    }

    // Helper function to convert OSCDeviceType to string
    std::string UIManager::GetOSCDeviceString(OSCDeviceType device) const {
        switch (device) {
            case OSCDeviceType::HMD: return "HMD";
            case OSCDeviceType::ControllerLeft: return "ControllerLeft";
            case OSCDeviceType::ControllerRight: return "ControllerRight";
            case OSCDeviceType::FootLeft: return "FootLeft";
            case OSCDeviceType::FootRight: return "FootRight";
            case OSCDeviceType::Hip: return "Hip";
            case OSCDeviceType::Jaw: return "Jaw";
            case OSCDeviceType::Mic: return "Mic";
            default: return "Unknown";
        }
    }

    // Register every inbound-OSC callback. Single source of truth shared by the
    // startup auto-connect path (UIManager.cpp) and HandleOSCConnection (via
    // VerifyOSCCallbacks), so the two sites can never drift to different sets.
    void UIManager::RegisterOSCCallbacks() {
        OSCManager::GetInstance().SetLockCallback(
            [this](OSCDeviceType device, bool locked) {
                OnDeviceLocked(device, locked);
            }
        );
        
        OSCManager::GetInstance().SetIncludeCallback(
            [this](OSCDeviceType device, bool include) {
                OnDeviceIncluded(device, include);
            }
        );
        
        OSCManager::GetInstance().SetGlobalLockCallback(
            [this](bool lock) {
                if (Logger::IsInitialized()) {
                    Logger::Info("Global " + std::string(lock ? "lock" : "unlock") + " triggered via OSC");
                }
                // Use the same method the UI uses, which handles countdown and sound effects
                ActivateGlobalLock(lock);
            }
        );
        
        OSCManager::GetInstance().SetGlobalOutOfBoundsCallback(
            [this](bool triggered) {
                if (!config_.osc_global_out_of_bounds_enabled) {
                    return; // Feature disabled
                }
                if (Logger::IsInitialized()) {
                    Logger::Info("Global out-of-bounds triggered via OSC");
                }
                TriggerGlobalOutOfBoundsActions();
            }
        );
        
        OSCManager::GetInstance().SetBiteCallback(
            [this](bool triggered) {
                if (!config_.osc_bite_enabled) {
                    return;
                }
                if (Logger::IsInitialized()) {
                    Logger::Info("Bite triggered via OSC");
                }
                TriggerBiteActions();
            }
        );

        OSCManager::GetInstance().SetAvatarChangeCallback(
            [this]() {
                HandleAvatarChange();
            }
        );

        OSCManager::GetInstance().SetShockCallback(
            [this](bool triggered) {
                bool enabled, use_individual; float intensity, duration;
                {
                    auto cfg_lock = config_.ReadLock();
                    enabled = config_.osc_shock_enabled;
                    intensity = config_.osc_shock_intensity;
                    duration = config_.osc_shock_duration;
                    use_individual = config_.osc_shock_use_individual_intensities;
                }
                if (!enabled) {
                    return;
                }
                if (Logger::IsInitialized()) {
                    Logger::Info("Shock param triggered via OSC");
                }
                if (use_individual) {
                    TriggerExternalShockIndividual(duration, "Shock param");
                } else {
                    TriggerExternalShock(intensity, duration, "Shock param");
                }
            }
        );
        
        OSCManager::GetInstance().SetEStopStretchCallback(
            [this](float stretch_value) {
                if (!config_.osc_estop_stretch_enabled) {
                    return;
                }
                if (Logger::IsInitialized()) {
                    Logger::Info("Emergency stop stretch triggered via OSC with value: " + std::to_string(stretch_value) + " - entering emergency stop mode");
                }
                
                // Enter emergency stop mode
                emergency_stop_active_ = true;
                
                // Unlock all devices immediately
                ActivateGlobalLock(false);
                
                // Also unlock any individually locked devices
                for (auto& device : device_positions_) {
                    if (device.locked) {
                        LockDevicePosition(device.serial, false);
                    }
                }
                
                if (Logger::IsInitialized()) {
                    Logger::Warning("EMERGENCY STOP MODE ACTIVE - All actions disabled until reset");
                }
            }
        );

        // VRCFT JawOpen: store every inbound value so the Visual heat bar and the
        // constraint engine (CheckJawOpenConstraint) always see the live jaw value.
        OSCManager::GetInstance().SetJawOpenCallback(
            [this](float jaw_value) {
                jaw_.current = jaw_value;
            }
        );

        // Unified collar-mode toggle button (momentary contact). Runs on the OSC
        // receive thread: rising-edge detect, advance to the next enabled+agreed mode,
        // and echo SPVR_Collar_Mode. Reads only collar_valid_mask_ (atomic), never config_.
        OSCManager::GetInstance().SetCollarToggleCallback(
            [this](bool pressed) {
                if (!pressed) { collar_toggle_prev_ = false; return; }
                if (collar_toggle_prev_) return;  // ignore repeats while held
                collar_toggle_prev_ = true;
                int next = NextValidCollarMode(collar_mode_.load());
                collar_mode_.store(next);
                SendCollarMode(next);
            }
        );

        // Seed the avatar with the current collar mode after (re)registration so the
        // animator is in sync following an OSC reconnect.
        SendCollarMode(collar_mode_.load());
    }

    // Re-register callbacks on an already-open connection (manual OSC toggle /
    // reconnect). Thin wrapper over RegisterOSCCallbacks so existing callers keep
    // the same entry point and the not-enabled guard.
    void UIManager::VerifyOSCCallbacks() {
        if (!osc_enabled_) {
            if (Logger::IsInitialized()) {
                Logger::Warning("VerifyOSCCallbacks: OSC is not enabled");
            }
            return;
        }
        RegisterOSCCallbacks();
        if (Logger::IsInitialized()) {
            Logger::Info("VerifyOSCCallbacks: OSC callbacks verified and re-registered");
        }
    }

    void UIManager::OnDeviceIncluded(OSCDeviceType device, bool include) {
        // Find devices with the appropriate role
        for (auto& dev : device_positions_) {
            // Check if this device has the role that matches the OSC device type
            DeviceRole targetRole = DeviceRole::None;
            
            switch (device) {
                case OSCDeviceType::HMD:
                    targetRole = DeviceRole::HMD;
                    break;
                case OSCDeviceType::ControllerLeft:
                    targetRole = DeviceRole::LeftController;
                    break;
                case OSCDeviceType::ControllerRight:
                    targetRole = DeviceRole::RightController;
                    break;
                case OSCDeviceType::FootLeft:
                    targetRole = DeviceRole::LeftFoot;
                    break;
                case OSCDeviceType::FootRight:
                    targetRole = DeviceRole::RightFoot;
                    break;
                case OSCDeviceType::Hip:
                    targetRole = DeviceRole::Hip;
                    break;
            }
            
            // Skip if no matching role
            if (dev.role != targetRole) {
                continue;
            }
            
            // Toggle the include_in_locking flag rather than setting it directly
            dev.include_in_locking = !dev.include_in_locking;
            
            // Save to config
            config_.device_settings[dev.serial] = dev.include_in_locking;
            SaveConfig();
            
            if (StayPutVR::Logger::IsInitialized()) {
                StayPutVR::Logger::Info("Device " + dev.serial + " include_in_locking toggled to " + 
                                       (dev.include_in_locking ? "true" : "false") + " via OSC");
            }
        }
        
        bool found_matching_device = false;
        for (auto& dev : device_positions_) {
            DeviceRole targetRole = DeviceRole::None;
            switch (device) {
                case OSCDeviceType::HMD: targetRole = DeviceRole::HMD; break;
                case OSCDeviceType::ControllerLeft: targetRole = DeviceRole::LeftController; break;
                case OSCDeviceType::ControllerRight: targetRole = DeviceRole::RightController; break;
                case OSCDeviceType::FootLeft: targetRole = DeviceRole::LeftFoot; break;
                case OSCDeviceType::FootRight: targetRole = DeviceRole::RightFoot; break;
                case OSCDeviceType::Hip: targetRole = DeviceRole::Hip; break;
            }
            if (dev.role == targetRole) {
                found_matching_device = true;
                break;
            }
        }
        
        if (!found_matching_device && StayPutVR::Logger::IsInitialized()) {
            StayPutVR::Logger::Warning("OSC received include command for " + GetOSCDeviceString(device) + 
                                      " but no device with matching role was found. Command ignored.");
            
            // Print all current device roles for debugging
            std::string device_roles = "Current device roles: ";
            for (const auto& dev : device_positions_) {
                device_roles += dev.serial + "=";
                switch (dev.role) {
                    case DeviceRole::None: device_roles += "None"; break;
                    case DeviceRole::HMD: device_roles += "HMD"; break;
                    case DeviceRole::LeftController: device_roles += "LeftController"; break;
                    case DeviceRole::RightController: device_roles += "RightController"; break;
                    case DeviceRole::Hip: device_roles += "Hip"; break;
                    case DeviceRole::LeftFoot: device_roles += "LeftFoot"; break;
                    case DeviceRole::RightFoot: device_roles += "RightFoot"; break;
                    default: device_roles += "Unknown"; break;
                }
                device_roles += ", ";
            }
            if (!device_positions_.empty()) {
                device_roles.pop_back(); // Remove last comma
                device_roles.pop_back(); // Remove last space
            }
            StayPutVR::Logger::Debug(device_roles);
        }
    }

    // Helper function to convert DeviceRole to OSCDeviceType
    OSCDeviceType UIManager::DeviceRoleToOSCDeviceType(DeviceRole role) const {
        switch (role) {
            case DeviceRole::HMD: return OSCDeviceType::HMD;
            case DeviceRole::LeftController: return OSCDeviceType::ControllerLeft;
            case DeviceRole::RightController: return OSCDeviceType::ControllerRight;
            case DeviceRole::LeftFoot: return OSCDeviceType::FootLeft;
            case DeviceRole::RightFoot: return OSCDeviceType::FootRight;
            case DeviceRole::Hip: return OSCDeviceType::Hip;
            default: return OSCDeviceType::HMD; // Default fallback, though this shouldn't be used for None role
        }
    }

    void UIManager::TriggerGlobalOutOfBoundsActions() {
        if (Logger::IsInitialized()) {
            Logger::Info("Triggering global out-of-bounds actions with " + std::to_string(GLOBAL_OUT_OF_BOUNDS_DURATION) + "s timer");
        }
        
        // Start the timer for resetting back to normal state
        global_out_of_bounds_timer_active_ = true;
        global_out_of_bounds_timer_start_ = std::chrono::steady_clock::now();
        
        // Play out-of-bounds audio if enabled
        if (config_.audio.out_of_bounds) {
            std::string filePath = StayPutVR::GetResourcesPath() + "/disobedience.wav";
            if (std::filesystem::exists(filePath)) {
                if (Logger::IsInitialized()) {
                    Logger::Debug("Playing disobedience.wav for global out-of-bounds trigger");
                }
                AudioManager::PlaySound("disobedience.wav", config_.audio.volume);
            } else {
                if (Logger::IsInitialized()) {
                    Logger::Warning("disobedience.wav not found, cannot play out-of-bounds sound");
                }
            }
        }
        
        // Trigger PiShock disobedience actions if enabled
        if (Logger::IsInitialized()) {
            Logger::Info("Triggering PiShock disobedience actions for global out-of-bounds");
        }
        TriggerPiShockDisobedience("GLOBAL");
        
        // Trigger OpenShock disobedience actions if enabled
        if (openshock_manager_ && openshock_manager_->IsEnabled()) {
            if (Logger::IsInitialized()) {
                Logger::Info("Triggering OpenShock disobedience actions for global out-of-bounds");
            }
            openshock_manager_->TriggerDisobedienceActions("GLOBAL");
        }
    }

    void UIManager::TriggerBiteActions() {
        // Issue #7: no shocking while emergency stop is active.
        if (emergency_stop_active_) {
            if (Logger::IsInitialized()) {
                Logger::Warning("Bite trigger ignored - emergency stop is active");
            }
            return;
        }

        if (Logger::IsInitialized()) {
            Logger::Info("Triggering bite disobedience actions with " + std::to_string(BITE_DURATION) + "s timer");
        }
        
        // Start the timer for resetting back to normal state
        bite_timer_active_ = true;
        bite_timer_start_ = std::chrono::steady_clock::now();
        
        // Play out-of-bounds audio if enabled
        if (config_.audio.out_of_bounds) {
            std::string filePath = StayPutVR::GetResourcesPath() + "/disobedience.wav";
            if (std::filesystem::exists(filePath)) {
                if (Logger::IsInitialized()) {
                    Logger::Debug("Playing disobedience.wav for bite trigger");
                }
                AudioManager::PlaySound("disobedience.wav", config_.audio.volume);
            } else {
                if (Logger::IsInitialized()) {
                    Logger::Warning("disobedience.wav not found, cannot play bite sound");
                }
            }
        }
        
        // Fire a direct shock on all configured shockers at the bite intensity/
        // duration (issue #7). Replaces the old beep+vibrate+shock disobedience.
        float bite_intensity, bite_duration; bool bite_use_individual;
        {
            auto cfg_lock = config_.ReadLock();
            bite_intensity = config_.osc_bite_intensity;
            bite_duration = config_.osc_bite_duration;
            bite_use_individual = config_.osc_bite_use_individual_intensities;
        }
        if (bite_use_individual) {
            TriggerExternalShockIndividual(bite_duration, "Bite");
        } else {
            TriggerExternalShock(bite_intensity, bite_duration, "Bite");
        }
        
        // Update all device statuses to show out-of-bounds
        for (auto& device : device_positions_) {
            if (device.role != DeviceRole::None) {
                OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                UpdateDeviceStatus(oscDevice, DeviceStatus::LockedOutOfBounds);
                
                if (Logger::IsInitialized()) {
                    Logger::Debug("Updated device " + device.serial + " status to LockedOutOfBounds for bite trigger");
                }
            }
        }
    }

    void UIManager::ResetEmergencyStop() {
        if (!emergency_stop_active_) {
            return;
        }

        emergency_stop_active_ = false;

        if (Logger::IsInitialized()) {
            Logger::Info("Emergency stop mode reset - normal operation resumed");
        }
    }

    void UIManager::HandleAvatarChange() {
        // Issue #6: switching avatars should unlock and reset everything so a
        // stale locked/shocked status doesn't carry over to the new avatar.
        //
        // NOTE: we deliberately do NOT clear emergency stop here. VRChat sends
        // /avatar/change on every avatar load/reload (incl. world joins), so
        // clearing the EStop safety latch on this event would silently re-enable
        // shocks without the user's intent. EStop stays latched until the user
        // explicitly resets it. (Devices are already unlocked while EStop is active.)
        if (Logger::IsInitialized()) {
            Logger::Info("Avatar changed (/avatar/change) - unlocking and resetting all devices");
        }

        // Release the global lock and any individually-locked devices. Suppress
        // the unlock sound: this is an automatic transition triggered by VRChat's
        // /avatar/change, not a user-initiated unlock, so the audio cue is spurious.
        ActivateGlobalLock(false, /*play_sound=*/false);
        for (auto& device : device_positions_) {
            if (device.locked) {
                LockDevicePosition(device.serial, false);
            }
        }

        // Reset every status param to Unlocked so none retain a stale
        // locked/shocked value (covers param slots even if a device isn't
        // currently tracked, e.g. on the headless/dev build).
        const OSCDeviceType allDevices[] = {
            OSCDeviceType::HMD, OSCDeviceType::ControllerLeft, OSCDeviceType::ControllerRight,
            OSCDeviceType::FootLeft, OSCDeviceType::FootRight, OSCDeviceType::Hip,
            OSCDeviceType::Jaw, OSCDeviceType::Mic
        };
        for (OSCDeviceType d : allDevices) {
            OSCManager::GetInstance().SendDeviceStatus(d, DeviceStatus::Unlocked);
        }

        // Reset the unified collar mode to Neither on avatar change so enforcement
        // never silently carries over to a freshly-loaded avatar.
        collar_mode_.store(static_cast<int>(CollarMode::Neither));
        SendCollarMode(static_cast<int>(CollarMode::Neither));
    }

    void UIManager::TriggerExternalShock(float intensity, float duration_seconds, const std::string& reason) {
        // Issue #7: bite and the Shock param fire a direct shock on all shockers,
        // never while emergency stop is active.
        if (emergency_stop_active_) {
            if (Logger::IsInitialized()) {
                Logger::Warning(reason + " shock ignored - emergency stop is active");
            }
            return;
        }

        // Read the mode under a brief lock, then release before calling the
        // managers (which take their own ReadLock) to avoid nested shared locks.
        Config::PiShockMode mode;
        {
            auto cfg_lock = config_.ReadLock();
            mode = config_.pishock_mode;
        }

        // PiShock (legacy or WebSocket v2, per the configured mode).
        if (mode == Config::PiShockMode::LEGACY_API) {
            if (pishock_manager_ && pishock_manager_->IsEnabled()) {
                pishock_manager_->TriggerShock(intensity, duration_seconds, reason);
            }
        } else {
            if (pishock_ws_manager_ && pishock_ws_manager_->IsEnabled()) {
                pishock_ws_manager_->TriggerShock(intensity, duration_seconds, reason);
            }
        }

        // OpenShock.
        if (openshock_manager_ && openshock_manager_->IsEnabled()) {
            openshock_manager_->TriggerShock(intensity, duration_seconds, reason);
        }
    }

    void UIManager::TriggerExternalShockIndividual(float duration_seconds, const std::string& reason) {
        // Same gating as TriggerExternalShock, but each shocker fires at its own
        // per-device disobedience intensity instead of a single supplied value.
        if (emergency_stop_active_) {
            if (Logger::IsInitialized()) {
                Logger::Warning(reason + " shock ignored - emergency stop is active");
            }
            return;
        }

        Config::PiShockMode mode;
        {
            auto cfg_lock = config_.ReadLock();
            mode = config_.pishock_mode;
        }

        if (mode == Config::PiShockMode::LEGACY_API) {
            if (pishock_manager_ && pishock_manager_->IsEnabled()) {
                pishock_manager_->TriggerShockIndividual(duration_seconds, reason);
            }
        } else {
            if (pishock_ws_manager_ && pishock_ws_manager_->IsEnabled()) {
                pishock_ws_manager_->TriggerShockIndividual(duration_seconds, reason);
            }
        }

        if (openshock_manager_ && openshock_manager_->IsEnabled()) {
            openshock_manager_->TriggerShockIndividual(duration_seconds, reason);
        }
    }

    void UIManager::ProcessGlobalOutOfBoundsTimer() {
        if (!global_out_of_bounds_timer_active_) {
            return;
        }
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - global_out_of_bounds_timer_start_);
        float elapsed_seconds = elapsed.count() / 1000.0f;
        
        if (elapsed_seconds >= GLOBAL_OUT_OF_BOUNDS_DURATION) {
            // Timer expired, reset devices back to normal locked state
            global_out_of_bounds_timer_active_ = false;
            
            if (Logger::IsInitialized()) {
                Logger::Info("Global out-of-bounds timer expired, resetting devices to their normal states");
            }
            
            // Reset all devices back to their appropriate state (locked or unlocked)
            for (auto& device : device_positions_) {
                if (device.role != DeviceRole::None) {
                    OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                    
                    // Determine if device should be locked (either globally or individually)
                    bool should_be_locked = (device.include_in_locking && global_lock_active_) || device.locked;
                    
                    DeviceStatus newStatus;
                    
                    if (should_be_locked) {
                        // Device should be locked - determine appropriate locked status
                        newStatus = DeviceStatus::LockedSafe;
                        
                        // If the device is actually still out of bounds physically, keep it as disobedience
                        if (device.exceeds_threshold) {
                            newStatus = DeviceStatus::LockedDisobedience;
                        } else if (device.in_warning_zone) {
                            newStatus = DeviceStatus::LockedWarning;
                        }
                    } else {
                        // Device should be unlocked
                        newStatus = DeviceStatus::Unlocked;
                    }
                    
                    UpdateDeviceStatus(oscDevice, newStatus);
                    
                    if (Logger::IsInitialized()) {
                        Logger::Debug("Reset device " + device.serial + " to status: " + 
                                     std::to_string(static_cast<int>(newStatus)) + 
                                     " (should_be_locked=" + std::to_string(should_be_locked) + ")");
                    }
                }
            }
        }
    }

    void UIManager::ProcessBiteTimer() {
        if (!bite_timer_active_) {
            return;
        }
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - bite_timer_start_);
        float elapsed_seconds = elapsed.count() / 1000.0f;
        
        if (elapsed_seconds >= BITE_DURATION) {
            // Timer expired, reset devices back to normal locked state
            bite_timer_active_ = false;
            
            if (Logger::IsInitialized()) {
                Logger::Info("Bite timer expired, resetting devices to their normal states");
            }
            
            // Reset all devices back to their appropriate state (locked or unlocked)
            for (auto& device : device_positions_) {
                if (device.role != DeviceRole::None) {
                    OSCDeviceType oscDevice = DeviceRoleToOSCDeviceType(device.role);
                    
                    // Determine if device should be locked (either globally or individually)
                    bool should_be_locked = (device.include_in_locking && global_lock_active_) || device.locked;
                    
                    DeviceStatus newStatus;
                    
                    if (should_be_locked) {
                        // Device should be locked - determine appropriate locked status
                        newStatus = DeviceStatus::LockedSafe;
                        
                        // If the device is actually still out of bounds physically, keep it as disobedience
                        if (device.exceeds_threshold) {
                            newStatus = DeviceStatus::LockedDisobedience;
                        } else if (device.in_warning_zone) {
                            newStatus = DeviceStatus::LockedWarning;
                        }
                    } else {
                        // Device should be unlocked
                        newStatus = DeviceStatus::Unlocked;
                    }
                    
                    UpdateDeviceStatus(oscDevice, newStatus);
                    
                    if (Logger::IsInitialized()) {
                        Logger::Debug("Reset device " + device.serial + " to status: " + 
                                     std::to_string(static_cast<int>(newStatus)) + 
                                     " (should_be_locked=" + std::to_string(should_be_locked) + ")");
                    }
                }
            }
        }
    }

} // namespace StayPutVR
