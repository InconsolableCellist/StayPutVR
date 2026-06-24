#pragma once

#include <imgui.h>
#include <string>
#include <functional>
#include <cstring>

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

// Renders a float slider with [-]/[+] nudge buttons (both to the right of the
// slider) so users can step in fixed increments (handy for fine shock
// duration/intensity tuning). Steps by `step`, clamps to [v_min, v_max], and
// returns true if the value changed this frame. `label` follows ImGui's
// convention: text before "##" is shown after the buttons; "##id" labels are
// hidden (use when the caller draws its own label, e.g. a Text() above).
inline bool SliderFloatWithButtons(const char* label, float* value, float v_min, float v_max,
                                   float step, const char* fmt = "%.2f", float slider_width = 200.0f) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(slider_width);
    if (ImGui::SliderFloat("##slider", value, v_min, v_max, fmt)) changed = true;
    ImGui::SameLine();
    if (ImGui::Button("-")) { *value -= step; changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("+")) { *value += step; changed = true; }
    // Visible label = text before "##" (ImGui convention); render only if present.
    const char* hash = std::strstr(label, "##");
    if (hash != label) { // not a purely-hidden label
        ImGui::SameLine();
        if (hash) ImGui::TextUnformatted(label, hash);
        else ImGui::TextUnformatted(label);
    }
    ImGui::PopID();
    if (changed) {
        if (*value < v_min) *value = v_min;
        if (*value > v_max) *value = v_max;
    }
    return changed;
}

} // namespace ImGuiHelpers
} // namespace StayPutVR
