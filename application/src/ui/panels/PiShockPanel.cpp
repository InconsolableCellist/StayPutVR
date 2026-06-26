#include "PiShockPanel.hpp"
#include "../ImGuiHelpers.hpp"
#include <imgui.h>
#include <algorithm>
#include <string>

#include "../../managers/PiShockManager.hpp"
#include "../../managers/PiShockWebSocketManager.hpp"

namespace StayPutVR {

PiShockPanel::PiShockPanel(Config& config,
                           std::unique_ptr<PiShockManager>& pishock_manager,
                           std::unique_ptr<PiShockWebSocketManager>& pishock_ws_manager,
                           std::function<void()> save_config)
    : config_(config)
    , pishock_manager_(pishock_manager)
    , pishock_ws_manager_(pishock_ws_manager)
    , save_config_(std::move(save_config))
{
}

void PiShockPanel::Render() {
    ImGui::Text("PiShock Integration");
    ImGui::Separator();

    // Safety warning + agreement
    bool user_agreement = ImGuiHelpers::SafetyAgreementBlock(
        "PiShock should only be used in accordance with their safety instructions. The makers of StayPutVR accept and assume no liability for your usage of PiShock, even if you use it in a manner you deem to be safe. This is for entertainment purposes only. When in doubt, use a low intensity and double-check all safety information, including safe placement of the device. The makers are not liable for any and all coding defects that may cause this feature to operate improperly. There is no express or implied guarantee that this feature will work properly.",
        config_.pishock_user_agreement);
    if (user_agreement != config_.pishock_user_agreement) {
        config_.pishock_user_agreement = user_agreement;
        save_config_();
    }

    // ===== COMMON SETTINGS (both modes) =====
    ImGui::BeginDisabled(!user_agreement);

    // Enable checkbox
    bool pishock_enabled = config_.pishock_enabled;
    if (ImGui::Checkbox("Enable PiShock Integration", &pishock_enabled)) {
        config_.pishock_enabled = pishock_enabled;
        save_config_();
    }

    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Enable direct integration with PiShock API for out-of-bounds enforcement");

    ImGui::Spacing();

    // Common username field
    ImGui::Text("Username:");
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Username you use to log into PiShock.com");

    static char username_buffer[128] = "";
    if (strlen(username_buffer) == 0 && !config_.pishock_username.empty()) {
        strcpy_s(username_buffer, sizeof(username_buffer), config_.pishock_username.c_str());
    }

    if (ImGui::InputText("##Username", username_buffer, sizeof(username_buffer))) {
        config_.pishock_username = username_buffer;
        save_config_();
    }

    ImGui::Separator();

    // ===== MODE SELECTION =====
    ImGui::Text("API Mode:");
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Legacy API: HTTP-based API (deprecated)\nWebSocket v2: Real-time persistent connection with lower latency and continuous shocking (recommended)");

    const char* modes[] = { "Legacy HTTP API", "WebSocket v2" };
    int current_mode = static_cast<int>(config_.pishock_mode);
    if (ImGui::Combo("##PiShockMode", &current_mode, modes, 2)) {
        auto old_mode = config_.pishock_mode;
        config_.pishock_mode = static_cast<Config::PiShockMode>(current_mode);
        save_config_();

        // Handle mode switching - disconnect old, prepare new
        if (old_mode == Config::PiShockMode::WEBSOCKET_V2 && pishock_ws_manager_) {
            pishock_ws_manager_->Disconnect();
        }

        Logger::Info("PiShock mode changed to: " + std::string(modes[current_mode]));
    }

    ImGui::Separator();

    // ===== MODE-SPECIFIC SUBTABS =====
    if (ImGui::BeginTabBar("##PiShockSubTabs", ImGuiTabBarFlags_None)) {

        // Configuration Tab
        if (ImGui::BeginTabItem("Configuration")) {
            ImGui::Spacing();

            if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
                // ===== LEGACY API CONFIGURATION =====
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Legacy HTTP API Configuration");
                ImGui::Separator();

                ImGui::Text("API Key:");
                ImGui::SameLine();
                ImGuiHelpers::HelpTooltip("API Key generated on PiShock.com (found in Account section)");

                static char apikey_buffer[128] = "";
                if (strlen(apikey_buffer) == 0 && !config_.pishock_api_key.empty()) {
                    strcpy_s(apikey_buffer, sizeof(apikey_buffer), config_.pishock_api_key.c_str());
                }

                if (ImGui::InputText("##APIKey", apikey_buffer, sizeof(apikey_buffer), ImGuiInputTextFlags_Password)) {
                    config_.pishock_api_key = apikey_buffer;
                    save_config_();
                }

                ImGui::Text("Share Code:");
                ImGui::SameLine();
                ImGuiHelpers::HelpTooltip("Share Code generated on PiShock.com for the device you want to control");

                static char sharecode_buffer[128] = "";
                if (strlen(sharecode_buffer) == 0 && !config_.pishock_share_code.empty()) {
                    strcpy_s(sharecode_buffer, sizeof(sharecode_buffer), config_.pishock_share_code.c_str());
                }

                if (ImGui::InputText("##ShareCode", sharecode_buffer, sizeof(sharecode_buffer), ImGuiInputTextFlags_Password)) {
                    config_.pishock_share_code = sharecode_buffer;
                    save_config_();
                }

                // Status
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Status:");
                if (pishock_manager_) {
                    ImGui::SameLine();
                    std::string status = pishock_manager_->GetConnectionStatus();
                    if (status == "Ready") {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", status.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", status.c_str());
                    }
                }

            } else {
                // ===== WEBSOCKET V2 CONFIGURATION =====
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "WebSocket v2 Configuration");
                ImGui::Separator();

                ImGui::Text("API Key:");
                ImGui::SameLine();
                ImGuiHelpers::HelpTooltip("API Key generated on PiShock.com (Account section)\nRequired for WebSocket authentication");

                static char ws_apikey_buffer[128] = "";
                if (strlen(ws_apikey_buffer) == 0 && !config_.pishock_api_key.empty()) {
                    strcpy_s(ws_apikey_buffer, sizeof(ws_apikey_buffer), config_.pishock_api_key.c_str());
                }

                if (ImGui::InputText("##WSAPIKey", ws_apikey_buffer, sizeof(ws_apikey_buffer), ImGuiInputTextFlags_Password)) {
                    config_.pishock_api_key = ws_apikey_buffer;
                    save_config_();
                }

                ImGui::Text("Client ID:");
                ImGui::SameLine();
                ImGuiHelpers::HelpTooltip("Your PiShock Client ID (hub ID)\nUsed to identify the device that your shockers connect to");

                static char ws_clientid_buffer[128] = "";
                if (strlen(ws_clientid_buffer) == 0 && !config_.pishock_client_id.empty()) {
                    strcpy_s(ws_clientid_buffer, sizeof(ws_clientid_buffer), config_.pishock_client_id.c_str());
                }

                if (ImGui::InputText("##WSClientID", ws_clientid_buffer, sizeof(ws_clientid_buffer))) {
                    config_.pishock_client_id = ws_clientid_buffer;
                    save_config_();
                }

                ImGui::Text("Shocker IDs:");
                ImGui::SameLine();
                ImGuiHelpers::HelpTooltip("PiShock shocker device IDs. Device 0 is considered the master device.\nLeave unused slots at 0.");

                static int shocker_id_buffers[5] = {0, 0, 0, 0, 0};

                for (int i = 0; i < 5; ++i) {
                    if (config_.pishock_shocker_ids[i] != shocker_id_buffers[i]) {
                        shocker_id_buffers[i] = config_.pishock_shocker_ids[i];
                    }

                    std::string label = std::to_string(i) + ": ";
                    if (i == 0) {
                        label += "(master)";
                    }

                    ImGui::PushID(i);
                    // step=0, step_fast=0 hides the InputInt's +/- stepper buttons
                    // (shocker IDs are typed in directly, not nudged).
                    if (ImGui::InputInt(label.c_str(), &shocker_id_buffers[i], 0, 0)) {
                        config_.pishock_shocker_ids[i] = shocker_id_buffers[i];
                        save_config_();
                    }
                    ImGui::PopID();
                }

                ImGui::Spacing();
                ImGui::Separator();

                // Connection controls
                ImGui::Text("Connection:");

                bool is_connected = pishock_ws_manager_ && pishock_ws_manager_->IsConnected();

                if (is_connected) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected");

                    ImGui::SameLine();
                    if (ImGui::Button("Disconnect##WSDisconnect")) {
                        if (pishock_ws_manager_) {
                            pishock_ws_manager_->Disconnect();
                        }
                    }
                } else {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Disconnected");

                    ImGui::SameLine();
                    bool has_shocker_id = false;
                    for (const auto& id : config_.pishock_shocker_ids) {
                        if (id != 0) {
                            has_shocker_id = true;
                            break;
                        }
                    }

                    bool can_connect = !config_.pishock_username.empty() &&
                                     !config_.pishock_api_key.empty() &&
                                     !config_.pishock_client_id.empty() &&
                                     has_shocker_id;

                    ImGui::BeginDisabled(!can_connect);
                    if (ImGui::Button("Connect##WSConnect")) {
                        if (pishock_ws_manager_) {
                            if (!pishock_ws_manager_->Connect()) {
                                Logger::Error("Failed to connect to PiShock WebSocket: " +
                                            pishock_ws_manager_->GetLastError());
                            }
                        }
                    }
                    ImGui::EndDisabled();
                }

                // Status
                ImGui::Spacing();
                ImGui::Text("Status:");
                if (pishock_ws_manager_) {
                    ImGui::SameLine();
                    std::string status = pishock_ws_manager_->GetConnectionStatus();
                    if (status == "Connected") {
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", status.c_str());
                    } else if (status == "Disconnected") {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", status.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", status.c_str());
                    }

                    std::string last_error = pishock_ws_manager_->GetLastError();
                    if (!last_error.empty()) {
                        ImGui::Text("Last Error:");
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", last_error.c_str());
                    }
                }
            }

            ImGui::EndTabItem();
        }

        // Actions Tab
        if (ImGui::BeginTabItem("Actions")) {
            ImGui::Spacing();

            ImGui::TextWrapped("PiShock fires its warning-zone actions when a device enters the warning band, "
                               "and its out-of-bounds actions when it crosses the disobedience threshold. "
                               "Leave a zone's actions unchecked to keep that zone silent.");
            ImGui::Spacing();
            ImGui::Separator();

            ImGui::BeginDisabled(!config_.pishock_enabled);

            // Warning Zone Actions
            ImGui::Text("Warning Zone Actions:");

            bool warning_beep = config_.pishock_warning_beep;
            if (ImGui::Checkbox("Beep on Warning", &warning_beep)) {
                config_.pishock_warning_beep = warning_beep;
                save_config_();
            }

            bool warning_vibrate = config_.pishock_warning_vibrate;
            if (ImGui::Checkbox("Vibrate on Warning", &warning_vibrate)) {
                config_.pishock_warning_vibrate = warning_vibrate;
                save_config_();
            }

            bool warning_shock = config_.pishock_warning_shock;
            if (ImGui::Checkbox("Shock on Warning", &warning_shock)) {
                config_.pishock_warning_shock = warning_shock;
                save_config_();
            }

            float warning_intensity = config_.pishock_warning_intensity;
            if (ImGuiHelpers::SliderFloatWithButtons("Warning Intensity", &warning_intensity, 0.0f, 1.0f, 0.01f, "%.2f")) {
                config_.pishock_warning_intensity = warning_intensity;
                save_config_();
            }

            float warning_duration = config_.pishock_warning_duration;
            if (ImGuiHelpers::SliderFloatWithButtons("Warning Duration", &warning_duration, 1.0f, 15.0f, 0.1f, "%.2f seconds")) {
                config_.pishock_warning_duration = warning_duration;
                save_config_();
            }

            ImGui::Spacing();
            ImGui::Separator();

            // Out of Bounds Actions
            ImGui::Text("Out of Bounds Actions:");

            bool disobedience_beep = config_.pishock_disobedience_beep;
            if (ImGui::Checkbox("Beep on Out of Bounds", &disobedience_beep)) {
                config_.pishock_disobedience_beep = disobedience_beep;
                save_config_();
            }

            bool disobedience_vibrate = config_.pishock_disobedience_vibrate;
            if (ImGui::Checkbox("Vibrate on Out of Bounds", &disobedience_vibrate)) {
                config_.pishock_disobedience_vibrate = disobedience_vibrate;
                save_config_();
            }

            bool disobedience_shock = config_.pishock_disobedience_shock;
            if (ImGui::Checkbox("Shock on Out of Bounds", &disobedience_shock)) {
                config_.pishock_disobedience_shock = disobedience_shock;
                save_config_();
            }

            ImGui::Spacing();

            // Individual device disobedience intensity settings
            bool use_individual_disobedience = config_.pishock_use_individual_disobedience_intensities;
            if (ImGui::Checkbox("Use Individual Device Disobedience Intensities", &use_individual_disobedience)) {
                config_.pishock_use_individual_disobedience_intensities = use_individual_disobedience;
                save_config_();
            }

            ImGui::SameLine();
            ImGuiHelpers::HelpTooltip("Enable to set different disobedience intensity levels for each PiShock device. When disabled, all devices use the master disobedience intensity.");

            if (!use_individual_disobedience) {
                float disobedience_intensity = config_.pishock_disobedience_intensity;
                if (ImGuiHelpers::SliderFloatWithButtons("Intensity", &disobedience_intensity, 0.0f, 1.0f, 0.01f, "%.2f")) {
                    config_.pishock_disobedience_intensity = disobedience_intensity;
                    save_config_();
                }
            } else {
                ImGui::Text("Individual Device Disobedience Intensities:");
                ImGui::Indent();

                for (int i = 0; i < 5; ++i) {
                    if (config_.pishock_shocker_ids[i] != 0) {
                        ImGui::PushID(i + 200);

                        std::string disobedience_label = "Device " + std::to_string(i) + " Intensity";
                        float individual_disobedience_intensity = config_.pishock_individual_disobedience_intensities[i];

                        if (ImGuiHelpers::SliderFloatWithButtons(disobedience_label.c_str(), &individual_disobedience_intensity, 0.0f, 1.0f, 0.01f, "%.2f")) {
                            config_.pishock_individual_disobedience_intensities[i] = individual_disobedience_intensity;
                            save_config_();
                        }

                        ImGui::PopID();
                    }
                }

                ImGui::Unindent();
            }

            float disobedience_duration = config_.pishock_disobedience_duration;
            if (ImGuiHelpers::SliderFloatWithButtons("Duration", &disobedience_duration, 1.0f, 15.0f, 0.1f, "%.2f seconds")) {
                config_.pishock_disobedience_duration = disobedience_duration;
                save_config_();
            }

            ImGui::Spacing();
            ImGui::Separator();

            // Test buttons
            ImGui::Text("Test:");

            bool can_test = config_.pishock_enabled;

            if (config_.pishock_mode == Config::PiShockMode::LEGACY_API) {
                can_test = can_test &&
                          !config_.pishock_username.empty() &&
                          !config_.pishock_api_key.empty() &&
                          !config_.pishock_share_code.empty();
            } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
                can_test = can_test && pishock_ws_manager_ && pishock_ws_manager_->IsConnected();
            }

            ImGui::BeginDisabled(!can_test);

            if (ImGui::Button("Test Out of Bounds Actions")) {
                if (config_.pishock_mode == Config::PiShockMode::LEGACY_API && pishock_manager_) {
                    pishock_manager_->TestActions();
                } else if (config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2 && pishock_ws_manager_) {
                    pishock_ws_manager_->TestActions();
                }
            }

            ImGui::EndDisabled();

            if (!can_test && config_.pishock_mode == Config::PiShockMode::WEBSOCKET_V2) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Connect to WebSocket first");
            }

            ImGui::EndDisabled(); // End pishock_enabled disabled

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndDisabled(); // End of the user_agreement disabled block
}

} // namespace StayPutVR
