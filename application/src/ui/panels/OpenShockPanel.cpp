#include "OpenShockPanel.hpp"
#include "../ImGuiHelpers.hpp"
#include <imgui.h>
#include <string>

#include "../../managers/OpenShockManager.hpp"

namespace StayPutVR {

OpenShockPanel::OpenShockPanel(Config& config,
                               std::unique_ptr<OpenShockManager>& openshock_manager,
                               std::function<void()> save_config)
    : config_(config)
    , openshock_manager_(openshock_manager)
    , save_config_(std::move(save_config))
{
}

void OpenShockPanel::Render() {
    ImGui::Text("OpenShock Integration");
    ImGui::Separator();

    // Safety warning + agreement
    bool user_agreement = ImGuiHelpers::SafetyAgreementBlock(
        "OpenShock should only be used in accordance with their safety instructions. The makers of StayPutVR accept and assume no liability for your usage of OpenShock, even if you use it in a manner you deem to be safe. This is for entertainment purposes only. When in doubt, use a low intensity and double-check all safety information, including safe placement of the device. The makers are not liable for any and all coding defects that may cause this feature to operate improperly. There is no express or implied guarantee that this feature will work properly.",
        config_.openshock_user_agreement);
    if (user_agreement != config_.openshock_user_agreement) {
        config_.openshock_user_agreement = user_agreement;
        save_config_();
    }

    ImGui::BeginDisabled(!user_agreement);
    bool openshock_enabled = config_.openshock_enabled;
    if (ImGui::Checkbox("Enable OpenShock Integration", &openshock_enabled)) {
        config_.openshock_enabled = openshock_enabled;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Enable direct integration with OpenShock API for out-of-bounds enforcement");

    // OpenShock API Credentials
    ImGui::Separator();
    ImGui::Text("OpenShock API Credentials:");

    static char api_token_buffer[256] = "";
    if (config_.openshock_api_token != api_token_buffer) {
        strcpy_s(api_token_buffer, sizeof(api_token_buffer), config_.openshock_api_token.c_str());
    }

    if (ImGui::InputText("API Token", api_token_buffer, sizeof(api_token_buffer), ImGuiInputTextFlags_Password)) {
        config_.openshock_api_token = api_token_buffer;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("API Token from your OpenShock account (found in Account settings)");

    // Device IDs section
    ImGui::Text("Device IDs:");
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("OpenShock device IDs. Device 0 is considered the master device.\nLeave unused slots empty.");

    static char device_id_buffers[5][128] = {"", "", "", "", ""};

    for (int i = 0; i < 5; ++i) {
        if (config_.openshock_device_ids[i] != device_id_buffers[i]) {
            strcpy_s(device_id_buffers[i], sizeof(device_id_buffers[i]), config_.openshock_device_ids[i].c_str());
        }

        std::string label = std::to_string(i) + ": ";
        if (i == 0) {
            label += "(master)";
        }

        ImGui::PushID(i);
        if (ImGui::InputText(label.c_str(), device_id_buffers[i], sizeof(device_id_buffers[i]))) {
            config_.openshock_device_ids[i] = device_id_buffers[i];
            save_config_();
        }
        ImGui::PopID();
    }

    static char server_url_buffer[256] = "";
    if (config_.openshock_server_url != server_url_buffer) {
        strcpy_s(server_url_buffer, sizeof(server_url_buffer), config_.openshock_server_url.c_str());
    }

    if (ImGui::InputText("Server URL", server_url_buffer, sizeof(server_url_buffer))) {
        config_.openshock_server_url = server_url_buffer;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("OpenShock server URL (default: https://api.openshock.app)");

    ImGui::Separator();

    if (!config_.openshock_enabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    }

    // Warning Zone Actions
    ImGui::Text("Warning Zone Actions:");
    ImGui::Separator();

    ImGui::Text("Action Type:");
    int warning_action = config_.openshock_warning_action;

    if (ImGui::RadioButton("None##Warning", warning_action == 0)) {
        config_.openshock_warning_action = 0;
        save_config_();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Shock##Warning", warning_action == 1)) {
        config_.openshock_warning_action = 1;
        save_config_();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Vibrate##Warning", warning_action == 2)) {
        config_.openshock_warning_action = 2;
        save_config_();
    }

    bool use_individual_warning = config_.openshock_use_individual_warning_intensities;
    if (ImGui::Checkbox("Use Individual Device Warning Intensities", &use_individual_warning)) {
        config_.openshock_use_individual_warning_intensities = use_individual_warning;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Enable to set different warning intensity levels for each OpenShock device. When disabled, all devices use the master warning intensity.");

    if (!use_individual_warning) {
        float master_warning_intensity = config_.openshock_master_warning_intensity;
        if (ImGui::SliderFloat("Warning Intensity", &master_warning_intensity, 0.0f, 1.0f, "%.2f")) {
            config_.openshock_master_warning_intensity = master_warning_intensity;
            save_config_();
        }

        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Master intensity level for warning zone actions (applies to all devices)");
    } else {
        ImGui::Text("Individual Device Warning Intensities:");
        ImGui::Indent();

        for (int i = 0; i < 5; ++i) {
            if (!config_.openshock_device_ids[i].empty()) {
                ImGui::PushID(i);

                std::string warning_label = "Device " + std::to_string(i) + " Warning Intensity";
                float individual_warning_intensity = config_.openshock_individual_warning_intensities[i];

                if (ImGui::SliderFloat(warning_label.c_str(), &individual_warning_intensity, 0.0f, 1.0f, "%.2f")) {
                    config_.openshock_individual_warning_intensities[i] = individual_warning_intensity;
                    save_config_();
                }

                ImGui::PopID();
            }
        }

        ImGui::Unindent();
    }

    float warning_duration = config_.openshock_warning_duration;
    if (ImGui::SliderFloat("Warning Duration", &warning_duration, 0.0f, 1.0f, "%.2f")) {
        config_.openshock_warning_duration = warning_duration;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Duration for warning zone actions (0.0 = shortest, 1.0 = longest)");

    ImGui::Separator();

    // Disobedience (Out of Bounds) Actions
    ImGui::Text("Disobedience (Out of Bounds) Actions:");
    ImGui::Separator();

    ImGui::Text("Action Type:");
    int disobedience_action = config_.openshock_disobedience_action;

    if (ImGui::RadioButton("None", disobedience_action == 0)) {
        config_.openshock_disobedience_action = 0;
        save_config_();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Shock", disobedience_action == 1)) {
        config_.openshock_disobedience_action = 1;
        save_config_();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Vibrate", disobedience_action == 2)) {
        config_.openshock_disobedience_action = 2;
        save_config_();
    }

    bool use_individual_disobedience = config_.openshock_use_individual_disobedience_intensities;
    if (ImGui::Checkbox("Use Individual Device Disobedience Intensities", &use_individual_disobedience)) {
        config_.openshock_use_individual_disobedience_intensities = use_individual_disobedience;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Enable to set different disobedience intensity levels for each OpenShock device. When disabled, all devices use the master disobedience intensity.");

    if (!use_individual_disobedience) {
        float master_disobedience_intensity = config_.openshock_master_disobedience_intensity;
        if (ImGui::SliderFloat("Disobedience Intensity", &master_disobedience_intensity, 0.0f, 1.0f, "%.2f")) {
            config_.openshock_master_disobedience_intensity = master_disobedience_intensity;
            save_config_();
        }

        ImGui::SameLine();
        ImGuiHelpers::HelpTooltip("Master intensity level for disobedience actions (applies to all devices)");
    } else {
        ImGui::Text("Individual Device Disobedience Intensities:");
        ImGui::Indent();

        for (int i = 0; i < 5; ++i) {
            if (!config_.openshock_device_ids[i].empty()) {
                ImGui::PushID(i + 100);

                std::string disobedience_label = "Device " + std::to_string(i) + " Disobedience Intensity";
                float individual_disobedience_intensity = config_.openshock_individual_disobedience_intensities[i];

                if (ImGui::SliderFloat(disobedience_label.c_str(), &individual_disobedience_intensity, 0.0f, 1.0f, "%.2f")) {
                    config_.openshock_individual_disobedience_intensities[i] = individual_disobedience_intensity;
                    save_config_();
                }

                ImGui::PopID();
            }
        }

        ImGui::Unindent();
    }

    float disobedience_duration = config_.openshock_disobedience_duration;
    if (ImGui::SliderFloat("Disobedience Duration", &disobedience_duration, 0.0f, 1.0f, "%.2f")) {
        config_.openshock_disobedience_duration = disobedience_duration;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Duration for disobedience actions (0.0 = shortest, 1.0 = longest)");

    if (!config_.openshock_enabled) {
        ImGui::PopStyleColor();
    }

    // Status and Test Section
    ImGui::Separator();
    ImGui::Text("Status:");

    if (openshock_manager_) {
        std::string status = openshock_manager_->GetConnectionStatus();

        if (status == "Ready") {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: %s", status.c_str());
        } else if (status == "Disabled") {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Status: %s", status.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Status: %s", status.c_str());
        }

        std::string last_error = openshock_manager_->GetLastError();
        if (!last_error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Last Error: %s", last_error.c_str());
        }
    }

    // Test button
    ImGui::Separator();

    bool can_test = config_.openshock_enabled &&
                   openshock_manager_ &&
                   openshock_manager_->IsFullyConfigured();

    ImGui::BeginDisabled(!can_test);
    if (ImGui::Button("Test OpenShock Actions", ImVec2(200, 30))) {
        if (openshock_manager_) {
            openshock_manager_->TestActions();
        }
    }
    ImGui::EndDisabled();

    if (!can_test) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Configure OpenShock settings to enable testing");
    }

    ImGui::EndDisabled(); // End of the user_agreement disabled block
}

} // namespace StayPutVR
