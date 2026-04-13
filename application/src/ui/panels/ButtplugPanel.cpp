#include "ButtplugPanel.hpp"
#include "../ImGuiHelpers.hpp"
#include <imgui.h>
#include <string>

#include "../../managers/ButtplugManager.hpp"

namespace StayPutVR {

ButtplugPanel::ButtplugPanel(Config& config,
                             std::unique_ptr<ButtplugManager>& buttplug_manager,
                             std::function<void()> save_config)
    : config_(config)
    , buttplug_manager_(buttplug_manager)
    , save_config_(std::move(save_config))
{
}

void ButtplugPanel::Render() {
    ImGui::Text("Buttplug/Intiface Integration");
    ImGui::Separator();

    // Safety warning + agreement
    bool user_agreement = ImGuiHelpers::SafetyAgreementBlock(
        "Buttplug/Intiface devices should only be used in accordance with their safety instructions. The makers of StayPutVR accept and assume no liability for your usage of these devices, even if you use them in a manner you deem to be safe. This is for entertainment purposes only. When in doubt, use a low intensity and double-check all safety information, including safe placement of the device. The makers are not liable for any and all coding defects that may cause this feature to operate improperly. There is no express or implied guarantee that this feature will work properly.",
        config_.buttplug_user_agreement);
    if (user_agreement != config_.buttplug_user_agreement) {
        config_.buttplug_user_agreement = user_agreement;
        save_config_();
    }

    ImGui::BeginDisabled(!user_agreement);
    bool buttplug_enabled = config_.buttplug_enabled;
    if (ImGui::Checkbox("Enable Buttplug/Intiface Integration", &buttplug_enabled)) {
        config_.buttplug_enabled = buttplug_enabled;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Enable integration with Buttplug/Intiface for vibration feedback");

    ImGui::Separator();

    ImGui::Text("Buttplug Server Connection:");

    static char server_address_buffer[256] = "";
    if (config_.buttplug_server_address != server_address_buffer) {
        strcpy_s(server_address_buffer, sizeof(server_address_buffer), config_.buttplug_server_address.c_str());
    }

    if (ImGui::InputText("Server Address", server_address_buffer, sizeof(server_address_buffer))) {
        config_.buttplug_server_address = server_address_buffer;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Address of the Buttplug/Intiface server (default: localhost)");

    int server_port = config_.buttplug_server_port;
    if (ImGui::InputInt("Server Port", &server_port)) {
        if (server_port > 0 && server_port < 65536) {
            config_.buttplug_server_port = server_port;
            save_config_();
        }
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Port of the Buttplug/Intiface server (default: 12345)");

    ImGui::Spacing();
    ImGui::Text("Status: ");
    ImGui::SameLine();

    if (buttplug_manager_ && buttplug_manager_->IsConnected()) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), buttplug_manager_->GetConnectionStatus().c_str());

        ImGui::SameLine();
        if (ImGui::Button("Disconnect##ButtplugDisconnect")) {
            buttplug_manager_->Disconnect();
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Disconnected");

        ImGui::SameLine();
        bool can_connect = !config_.buttplug_server_address.empty() &&
                         config_.buttplug_server_port > 0;

        ImGui::BeginDisabled(!can_connect);
        if (ImGui::Button("Connect##ButtplugConnect")) {
            if (buttplug_manager_) {
                if (!buttplug_manager_->Connect()) {
                    Logger::Error("Failed to connect to Buttplug: " +
                                buttplug_manager_->GetLastError());
                }
            }
        }
        ImGui::EndDisabled();
    }

    ImGui::Separator();

    // Device Indices Configuration
    ImGui::Text("Device Indices:");
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Device indices from Intiface (0 is the first device).\nUse -1 for unused slots.\nDevice 0 is considered the master device.");

    for (int i = 0; i < 5; ++i) {
        int device_index = config_.buttplug_device_indices[i];

        std::string label = std::to_string(i) + ": ";
        if (i == 0) {
            label += "(master)";
        }

        ImGui::PushID(i);
        if (ImGui::InputInt(label.c_str(), &device_index)) {
            if (device_index >= -1) {
                config_.buttplug_device_indices[i] = device_index;
                save_config_();
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();

    // Zone Activation Settings
    ImGui::Text("Zone Activation:");
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Choose which zones trigger vibration");

    bool safe_zone = config_.buttplug_safe_zone_enabled;
    if (ImGui::Checkbox("Vibrate in Safe Zone", &safe_zone)) {
        config_.buttplug_safe_zone_enabled = safe_zone;
        save_config_();
    }

    bool warning_zone = config_.buttplug_warning_zone_enabled;
    if (ImGui::Checkbox("Vibrate in Warning Zone", &warning_zone)) {
        config_.buttplug_warning_zone_enabled = warning_zone;
        save_config_();
    }

    bool disobedience_zone = config_.buttplug_disobedience_zone_enabled;
    if (ImGui::Checkbox("Vibrate when Disobeying (Out of Bounds)", &disobedience_zone)) {
        config_.buttplug_disobedience_zone_enabled = disobedience_zone;
        save_config_();
    }

    ImGui::Separator();

    if (!config_.buttplug_enabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    }

    // Safe Zone Settings
    ImGui::Text("Safe Zone Settings:");
    ImGui::Separator();

    bool use_individual_safe = config_.buttplug_use_individual_safe_intensities;
    if (ImGui::Checkbox("Use Individual Device Safe Intensities", &use_individual_safe)) {
        config_.buttplug_use_individual_safe_intensities = use_individual_safe;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Enable to set different safe zone intensity levels for each device. When disabled, all devices use the master safe intensity.");

    if (!use_individual_safe) {
        float master_safe_intensity = config_.buttplug_master_safe_intensity;
        if (ImGui::SliderFloat("Master Safe Intensity", &master_safe_intensity, 0.0f, 1.0f, "%.2f")) {
            config_.buttplug_master_safe_intensity = master_safe_intensity;
            save_config_();
        }
    } else {
        ImGui::Text("Individual Safe Intensities:");
        for (int i = 0; i < 5; ++i) {
            if (config_.buttplug_device_indices[i] >= 0) {
                float intensity = config_.buttplug_individual_safe_intensities[i];
                std::string label = "Device " + std::to_string(i);
                ImGui::PushID(200 + i);
                if (ImGui::SliderFloat(label.c_str(), &intensity, 0.0f, 1.0f, "%.2f")) {
                    config_.buttplug_individual_safe_intensities[i] = intensity;
                    save_config_();
                }
                ImGui::PopID();
            }
        }
    }

    ImGui::Separator();

    // Warning Zone Settings
    ImGui::Text("Warning Zone Settings:");
    ImGui::Separator();

    bool use_individual_warning = config_.buttplug_use_individual_warning_intensities;
    if (ImGui::Checkbox("Use Individual Device Warning Intensities", &use_individual_warning)) {
        config_.buttplug_use_individual_warning_intensities = use_individual_warning;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Enable to set different warning intensity levels for each device. When disabled, all devices use the master warning intensity.");

    if (!use_individual_warning) {
        float master_warning_intensity = config_.buttplug_master_warning_intensity;
        if (ImGui::SliderFloat("Master Warning Intensity", &master_warning_intensity, 0.0f, 1.0f, "%.2f")) {
            config_.buttplug_master_warning_intensity = master_warning_intensity;
            save_config_();
        }
    } else {
        ImGui::Text("Individual Warning Intensities:");
        for (int i = 0; i < 5; ++i) {
            if (config_.buttplug_device_indices[i] >= 0) {
                float intensity = config_.buttplug_individual_warning_intensities[i];
                std::string label = "Device " + std::to_string(i);
                ImGui::PushID(i);
                if (ImGui::SliderFloat(label.c_str(), &intensity, 0.0f, 1.0f, "%.2f")) {
                    config_.buttplug_individual_warning_intensities[i] = intensity;
                    save_config_();
                }
                ImGui::PopID();
            }
        }
    }

    ImGui::Separator();

    ImGui::Text("Disobedience (Out of Bounds) Settings:");
    ImGui::Separator();

    bool use_individual_disobedience = config_.buttplug_use_individual_disobedience_intensities;
    if (ImGui::Checkbox("Use Individual Device Disobedience Intensities", &use_individual_disobedience)) {
        config_.buttplug_use_individual_disobedience_intensities = use_individual_disobedience;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Enable to set different disobedience intensity levels for each device. When disabled, all devices use the master disobedience intensity.");

    if (!use_individual_disobedience) {
        float master_disobedience_intensity = config_.buttplug_master_disobedience_intensity;
        if (ImGui::SliderFloat("Master Disobedience Intensity", &master_disobedience_intensity, 0.0f, 1.0f, "%.2f")) {
            config_.buttplug_master_disobedience_intensity = master_disobedience_intensity;
            save_config_();
        }
    } else {
        ImGui::Text("Individual Disobedience Intensities:");
        for (int i = 0; i < 5; ++i) {
            if (config_.buttplug_device_indices[i] >= 0) {
                float intensity = config_.buttplug_individual_disobedience_intensities[i];
                std::string label = "Device " + std::to_string(i);
                ImGui::PushID(100 + i);
                if (ImGui::SliderFloat(label.c_str(), &intensity, 0.0f, 1.0f, "%.2f")) {
                    config_.buttplug_individual_disobedience_intensities[i] = intensity;
                    save_config_();
                }
                ImGui::PopID();
            }
        }
    }

    if (!config_.buttplug_enabled) {
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Test buttons
    ImGui::BeginDisabled(!buttplug_enabled);

    bool can_test = buttplug_manager_ && buttplug_manager_->IsConnected();

    ImGui::BeginDisabled(!can_test);

    if (ImGui::Button("Start Test (Continuous Vibration)")) {
        if (buttplug_manager_) {
            buttplug_manager_->TestActions();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Stop Test")) {
        if (buttplug_manager_) {
            buttplug_manager_->StopAllDevices();
        }
    }

    ImGui::EndDisabled();

    if (!can_test && buttplug_enabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Connect to Buttplug server first");
    }

    if (can_test) {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Note: Vibration is continuous - use Stop Test to turn it off");
    }

    ImGui::EndDisabled(); // End buttplug_enabled disabled
    ImGui::EndDisabled(); // End user_agreement disabled
}

} // namespace StayPutVR
