#pragma once
//Small widgets for the Insert menu: strictly black and white, square, flat. A row is "label on the left, control on
//the right". Every widget gets its ID from its label, so labels must be different from each other within a page.
#ifndef KWINHELPER_UIKIT_H
#define KWINHELPER_UIKIT_H

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "imgui.h"

namespace ui {

inline constexpr ImU32 WHITE = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 LIGHT = IM_COL32(205, 205, 205, 255);
inline constexpr ImU32 GREY = IM_COL32(135, 135, 135, 255);
inline constexpr ImU32 DIM = IM_COL32(78, 78, 78, 255);
inline constexpr float LABEL_W = 210.0f, CONTROL_W = 250.0f;

//The visible part of a label ("Name##id" shows "Name")
inline const char* LabelEnd(const char* label) {
    const char* p = std::strstr(label, "##");
    return p ? p : label + std::strlen(label);
}

//The black and white look. Call PushTheme() before drawing a window, PopTheme() after. menuOpacity: the background.
inline int g_themeColors = 0;
inline void PushTheme(float opacity) {
    auto c = [&](ImGuiCol i, ImVec4 v) { ImGui::PushStyleColor(i, v); g_themeColors++; };
    g_themeColors = 0;
    const ImVec4 black(0, 0, 0, opacity), white(1, 1, 1, 1), grey(0.53f, 0.53f, 0.53f, 1), dark(0.09f, 0.09f, 0.09f, 1), mid(0.22f, 0.22f, 0.22f, 1);
    c(ImGuiCol_WindowBg, black); c(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0)); c(ImGuiCol_PopupBg, ImVec4(0.04f, 0.04f, 0.04f, 0.98f));
    c(ImGuiCol_Border, ImVec4(1, 1, 1, 0.55f)); c(ImGuiCol_Text, white); c(ImGuiCol_TextDisabled, grey);
    c(ImGuiCol_TitleBg, black); c(ImGuiCol_TitleBgActive, black); c(ImGuiCol_TitleBgCollapsed, black);
    c(ImGuiCol_FrameBg, dark); c(ImGuiCol_FrameBgHovered, mid); c(ImGuiCol_FrameBgActive, mid);
    c(ImGuiCol_CheckMark, white); c(ImGuiCol_SliderGrab, white); c(ImGuiCol_SliderGrabActive, ImVec4(0.7f, 0.7f, 0.7f, 1));
    c(ImGuiCol_Separator, mid); c(ImGuiCol_Header, dark); c(ImGuiCol_HeaderHovered, mid); c(ImGuiCol_HeaderActive, mid);
    c(ImGuiCol_Button, dark); c(ImGuiCol_ButtonHovered, mid); c(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1));
    c(ImGuiCol_Tab, dark); c(ImGuiCol_TabHovered, mid); c(ImGuiCol_TabSelected, mid); c(ImGuiCol_TabSelectedOverline, white);
    c(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0)); c(ImGuiCol_ScrollbarGrab, mid); c(ImGuiCol_TextSelectedBg, mid);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 7));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 9.0f);
}
inline void PopTheme() {
    ImGui::PopStyleVar(12);
    ImGui::PopStyleColor(g_themeColors);
}

//A small grey heading with a thin line
inline void Section(const char* text) {
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextColored(ImColor(GREY), "%s", text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y - 2), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y - 2), DIM);
    ImGui::Dummy(ImVec2(0, 3));
}

inline void Note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.53f, 0.53f, 0.53f, 1));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

//A switch: an outlined box with a filled half. The whole row can be clicked.
inline bool Switch(const char* label, bool* v) {
    ImGui::PushID(label);
    const float h = 16.0f, w = 32.0f, row = h + 6.0f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##sw", ImVec2(std::max(60.0f, ImGui::GetContentRegionAvail().x), row));
    if (pressed) *v = !*v;
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x, p.y + 3), b(p.x + w, p.y + 3 + h);
    dl->AddRect(a, b, *v ? WHITE : GREY, 0.0f, 0, 1.0f);
    if (*v) dl->AddRectFilled(ImVec2(a.x + w / 2 + 1, a.y + 2), ImVec2(b.x - 2, b.y - 2), WHITE);
    else dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(a.x + w / 2 - 1, b.y - 2), GREY);
    dl->AddText(ImVec2(p.x + w + 10, p.y + 3 + (h - ImGui::GetFontSize()) / 2), (*v || hov) ? WHITE : LIGHT, label, LabelEnd(label));
    ImGui::PopID();
    return pressed;
}

inline bool SliderRow(const char* label, ImGuiDataType type, void* v, const void* lo, const void* hi, const char* fmt) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, LabelEnd(label));
    ImGui::SameLine(LABEL_W);
    ImGui::SetNextItemWidth(CONTROL_W);
    //The track has no text on it (the handle would cover the number); the value is shown to the right
    const bool changed = ImGui::SliderScalar("##v", type, v, lo, hi, "", ImGuiSliderFlags_AlwaysClamp);
    char text[48];
    if (type == ImGuiDataType_Float) std::snprintf(text, sizeof text, fmt, (double)*(float*)v);
    else std::snprintf(text, sizeof text, fmt, *(double*)v);
    ImGui::SameLine(0, 12);
    ImGui::TextUnformatted(text);
    ImGui::PopID();
    return changed;
}
inline bool Slider(const char* label, float* v, float lo, float hi, const char* fmt = "%.0f") {
    return SliderRow(label, ImGuiDataType_Float, v, &lo, &hi, fmt);
}
inline bool SliderD(const char* label, double* v, double lo, double hi, const char* fmt = "%.0f") {
    return SliderRow(label, ImGuiDataType_Double, v, &lo, &hi, fmt);
}

//A drop-down list; the chosen number is kept in a double (the settings are doubles)
inline bool Choice(const char* label, double* v, std::initializer_list<const char*> items) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, LabelEnd(label));
    ImGui::SameLine(LABEL_W);
    ImGui::SetNextItemWidth(CONTROL_W);
    int idx = (int)*v;
    idx = idx < 0 ? 0 : idx >= (int)items.size() ? (int)items.size() - 1 : idx;
    bool changed = false;
    if (ImGui::BeginCombo("##c", *(items.begin() + idx))) {
        int i = 0;
        for (const char* it : items) {
            if (ImGui::Selectable(it, i == idx)) { *v = i; changed = true; }
            i++;
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

//A tab on the left: the chosen one has a white bar
inline bool Tab(const char* label, bool selected, float w) {
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##t", ImVec2(w, 32));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 32), IM_COL32(32, 32, 32, 255));
    else if (hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 32), IM_COL32(18, 18, 18, 255));
    if (selected) dl->AddRectFilled(p, ImVec2(p.x + 3, p.y + 32), WHITE);
    dl->AddText(ImVec2(p.x + 16, p.y + (32 - ImGui::GetFontSize()) / 2), selected ? WHITE : (hov ? LIGHT : GREY), label, LabelEnd(label));
    ImGui::PopID();
    return pressed;
}

} // namespace ui

#endif //KWINHELPER_UIKIT_H
