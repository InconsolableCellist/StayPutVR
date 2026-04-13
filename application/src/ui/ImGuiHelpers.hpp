#pragma once

#include <imgui.h>
#include <string>
#include <functional>

namespace StayPutVR {
namespace ImGuiHelpers {

// Renders a "(?) " marker followed by a tooltip on hover.
// Place this after the widget it describes (typically after SameLine()).
inline void HelpTooltip(const char* text) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(text);
        ImGui::EndTooltip();
    }
}

// Renders the standard safety-agreement block used on integration tabs.
// Returns true if the user has agreed (i.e., the checkbox is checked).
// The caller is responsible for calling ImGui::BeginDisabled(!agreed)
// after this, and ImGui::EndDisabled() at the end of the tab.
//
// Usage:
//   bool agreed = SafetyAgreementBlock(warning_text, config_.xyz_user_agreement);
//   if (agreed != config_.xyz_user_agreement) { config_.xyz_user_agreement = agreed; SaveConfig(); }
//   ImGui::BeginDisabled(!agreed);
//   ... tab contents ...
//   ImGui::EndDisabled();
inline bool SafetyAgreementBlock(const char* warning_text, bool current_agreement) {
    ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 20);
    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "WARNING: Safety Information");
    ImGui::Text("%s", warning_text);
    ImGui::PopTextWrapPos();

    bool agreed = current_agreement;
    ImGui::Checkbox("I understand and agree to the safety information above", &agreed);

    ImGui::Separator();
    return agreed;
}

} // namespace ImGuiHelpers
} // namespace StayPutVR
