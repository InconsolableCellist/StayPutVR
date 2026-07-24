#include "DGLabPanel.hpp"
#include "../ImGuiHelpers.hpp"
#include <imgui.h>
#include <string>
#include <vector>

#include "../../managers/DGLabManager.hpp"
#include "thirdparty/qrcodegen/qrcodegen.hpp"

namespace StayPutVR {

DGLabPanel::DGLabPanel(Config& config,
                       std::unique_ptr<DGLabManager>& dglab_manager,
                       std::function<void()> save_config)
    : config_(config)
    , dglab_manager_(dglab_manager)
    , save_config_(std::move(save_config))
{
}

void DGLabPanel::RenderQrCode(const std::string& payload) {
    // The QR payload only changes when port/IP/clientId change; cache the
    // generated module grid instead of re-encoding every frame.
    static std::string cached_payload;
    static std::vector<std::vector<bool>> modules;
    if (payload != cached_payload) {
        cached_payload = payload;
        modules.clear();
        try {
            const qrcodegen::QrCode qr =
                qrcodegen::QrCode::encodeText(payload.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
            modules.assign(qr.getSize(), std::vector<bool>(qr.getSize(), false));
            for (int y = 0; y < qr.getSize(); ++y)
                for (int x = 0; x < qr.getSize(); ++x)
                    modules[y][x] = qr.getModule(x, y);
        } catch (const std::exception&) {
            // leave modules empty; nothing to draw
        }
    }
    if (modules.empty()) return;

    const int size = static_cast<int>(modules.size());
    const float scale = 4.0f;
    const float quiet = 4.0f * scale; // quiet zone border required by scanners

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float total = size * scale + 2.0f * quiet;

    draw->AddRectFilled(origin, ImVec2(origin.x + total, origin.y + total),
                        IM_COL32(255, 255, 255, 255));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (!modules[y][x]) continue;
            const float px = origin.x + quiet + x * scale;
            const float py = origin.y + quiet + y * scale;
            draw->AddRectFilled(ImVec2(px, py), ImVec2(px + scale, py + scale),
                                IM_COL32(0, 0, 0, 255));
        }
    }
    ImGui::Dummy(ImVec2(total, total));
}

void DGLabPanel::Render() {
    ImGui::Text("DG-Lab Coyote Integration");
    ImGui::Separator();

    // Safety warning + agreement
    bool user_agreement = ImGuiHelpers::SafetyAgreementBlock(
        "E-stim devices should only be used in accordance with DG-Lab's safety instructions. "
        "Never place electrodes above the waist, across the chest or heart, on the head or neck, or on damaged skin. "
        "Do not use with a pacemaker or other implanted device, or with any cardiovascular or neurological condition. "
        "ALWAYS calibrate your strength limits by hand in the DG-Lab app before connecting, and start low. "
        "The physical buttons on the Coyote immediately zero both channels at any time. "
        "The makers of StayPutVR accept and assume no liability for your usage of DG-Lab devices, even if you use them in a manner you deem to be safe. "
        "This is for entertainment purposes only. The makers are not liable for any and all coding defects that may cause this feature to operate improperly. "
        "There is no express or implied guarantee that this feature will work properly.",
        config_.dglab_user_agreement);
    if (user_agreement != config_.dglab_user_agreement) {
        config_.dglab_user_agreement = user_agreement;
        save_config_();
    }

    ImGui::BeginDisabled(!user_agreement);
    bool dglab_enabled = config_.dglab_enabled;
    if (ImGui::Checkbox("Enable DG-Lab Integration", &dglab_enabled)) {
        config_.dglab_enabled = dglab_enabled;
        save_config_();
    }
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip(
        "Runs a local WebSocket server for the DG-Lab app to connect to.\n"
        "Scan the QR code below with the DG-Lab app (v3); your phone relays\n"
        "commands to the Coyote over Bluetooth. Phone and PC must be on the\n"
        "same network, and the app must stay open and connected to the device.");

    // Connection settings
    ImGui::Separator();
    ImGui::Text("Connection:");

    int port = config_.dglab_server_port;
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Server Port", &port)) {
        if (port < 1024) port = 1024;
        if (port > 65535) port = 65535;
        config_.dglab_server_port = port;
        save_config_();
    }
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Local port for the embedded WebSocket server. Change requires toggling the integration off and on.");

    static char ip_buffer[64] = "";
    if (config_.dglab_server_ip != ip_buffer) {
        strcpy_s(ip_buffer, sizeof(ip_buffer), config_.dglab_server_ip.c_str());
    }
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("LAN IP Override", ip_buffer, sizeof(ip_buffer))) {
        config_.dglab_server_ip = ip_buffer;
        save_config_();
    }
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip(
        "Leave empty to auto-detect this PC's LAN address for the QR code.\n"
        "Set manually if you have multiple network adapters and the phone\n"
        "can't reach the auto-detected one.");

    // Status + QR code
    ImGui::Separator();
    if (dglab_manager_) {
        const std::string status = dglab_manager_->GetConnectionStatus();
        if (dglab_manager_->IsBound()) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: %s", status.c_str());
        } else if (status == "Disabled") {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Status: %s", status.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Status: %s", status.c_str());
        }

        const std::string last_error = dglab_manager_->GetLastError();
        if (!last_error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Last Error: %s", last_error.c_str());
        }

        if (dglab_manager_->IsServerRunning() && !dglab_manager_->IsBound()) {
            ImGui::Text("Scan with the DG-Lab app (Socket Control):");
            const std::string payload = dglab_manager_->GetQrPayload();
            if (!payload.empty()) {
                RenderQrCode(payload);
                ImGui::TextDisabled("%s", payload.c_str());
            }
        }
    }

    // Channel configuration
    ImGui::Separator();
    ImGui::Text("Channels:");
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip(
        "The Coyote has two independent output channels (A and B).\n"
        "Punishments fire on every enabled channel. The strength limit is the\n"
        "hardware ceiling (0-200) this app will ever drive a channel to; the\n"
        "limits configured in the DG-Lab phone app are always respected too.");

    bool channel_a = config_.dglab_channel_a;
    if (ImGui::Checkbox("Channel A", &channel_a)) {
        config_.dglab_channel_a = channel_a;
        save_config_();
    }
    ImGui::SameLine();
    bool channel_b = config_.dglab_channel_b;
    if (ImGui::Checkbox("Channel B", &channel_b)) {
        config_.dglab_channel_b = channel_b;
        save_config_();
    }

    int limit_a = config_.dglab_limit_a;
    if (ImGui::SliderInt("Strength Limit A", &limit_a, 0, 200)) {
        config_.dglab_limit_a = limit_a;
        save_config_();
        if (dglab_manager_) dglab_manager_->ApplyStrengthLimits();
    }
    if (dglab_manager_ && dglab_manager_->IsBound()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(app limit: %d)", dglab_manager_->GetAppLimit(0));
    }

    int limit_b = config_.dglab_limit_b;
    if (ImGui::SliderInt("Strength Limit B", &limit_b, 0, 200)) {
        config_.dglab_limit_b = limit_b;
        save_config_();
        if (dglab_manager_) dglab_manager_->ApplyStrengthLimits();
    }
    if (dglab_manager_ && dglab_manager_->IsBound()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(app limit: %d)", dglab_manager_->GetAppLimit(1));
    }

    // Waveform configuration
    ImGui::Separator();
    ImGui::Text("Waveform:");

    const char* waveforms[] = {"Steady", "Pulse", "Ramp"};
    int waveform = config_.dglab_waveform;
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("Pattern", &waveform, waveforms, IM_ARRAYSIZE(waveforms))) {
        config_.dglab_waveform = waveform;
        save_config_();
    }

    int frequency = config_.dglab_frequency;
    if (ImGui::SliderInt("Frequency", &frequency, 10, 240)) {
        config_.dglab_frequency = frequency;
        save_config_();
    }
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip("Pulse frequency (protocol value 10-240). Lower feels like slow taps, higher like buzzing.");

    // Zone actions
    ImGui::Separator();
    ImGui::Text("Warning Zone Actions:");
    ImGui::SameLine();
    ImGuiHelpers::HelpTooltip(
        "Fires on trackers bound to a DG-Lab channel in the Devices tab.\n"
        "Bind a channel by dragging the green A/B chip onto a body slot.");

    int warning_action = config_.dglab_warning_action;
    if (ImGui::RadioButton("None##DGWarning", warning_action == 0)) {
        config_.dglab_warning_action = 0;
        save_config_();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Pulse##DGWarning", warning_action == 1)) {
        config_.dglab_warning_action = 1;
        save_config_();
    }

    float warning_intensity = config_.dglab_warning_intensity;
    if (ImGui::SliderFloat("Warning Intensity", &warning_intensity, 0.0f, 1.0f, "%.2f")) {
        config_.dglab_warning_intensity = warning_intensity;
        save_config_();
    }
    float warning_duration = config_.dglab_warning_duration;
    if (ImGuiHelpers::SliderFloatWithButtons("Warning Duration", &warning_duration, 0.3f, 10.0f, 0.1f, "%.2f seconds")) {
        config_.dglab_warning_duration = warning_duration;
        save_config_();
    }

    ImGui::Separator();
    ImGui::Text("Disobedience (Out of Bounds) Actions:");

    int disobedience_action = config_.dglab_disobedience_action;
    if (ImGui::RadioButton("None##DGDisobedience", disobedience_action == 0)) {
        config_.dglab_disobedience_action = 0;
        save_config_();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Pulse##DGDisobedience", disobedience_action == 1)) {
        config_.dglab_disobedience_action = 1;
        save_config_();
    }

    float disobedience_intensity = config_.dglab_disobedience_intensity;
    if (ImGui::SliderFloat("Disobedience Intensity", &disobedience_intensity, 0.0f, 1.0f, "%.2f")) {
        config_.dglab_disobedience_intensity = disobedience_intensity;
        save_config_();
    }
    float disobedience_duration = config_.dglab_disobedience_duration;
    if (ImGuiHelpers::SliderFloatWithButtons("Disobedience Duration", &disobedience_duration, 0.3f, 10.0f, 0.1f, "%.2f seconds")) {
        config_.dglab_disobedience_duration = disobedience_duration;
        save_config_();
    }

    // Test section
    ImGui::Separator();
    ImGui::Text("Test:");

    ImGui::SliderFloat("Test Intensity", &test_intensity_, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Test Duration", &test_duration_, 0.3f, 10.0f, "%.1f seconds");

    const bool can_test = dglab_manager_ && dglab_manager_->IsBound() && config_.dglab_enabled;
    ImGui::BeginDisabled(!can_test);
    if (ImGui::Button("Test A", ImVec2(80, 30))) {
        dglab_manager_->TestChannel(0, test_intensity_, test_duration_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Test B", ImVec2(80, 30))) {
        dglab_manager_->TestChannel(1, test_intensity_, test_duration_);
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button("STOP", ImVec2(80, 30))) {
        dglab_manager_->StopAll();
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    if (!can_test && config_.dglab_enabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                           "Scan the QR code with the DG-Lab app to enable testing");
    }

    ImGui::EndDisabled(); // user_agreement block
}

} // namespace StayPutVR
