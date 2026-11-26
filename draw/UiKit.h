#pragma once
//Small widgets for the Insert menu: strictly black and white, square, flat. A row is "label on the left, control on
//the right". Every widget gets its ID from its label, so labels must be different from each other within a page.
#ifndef KWINHELPER_UIKIT_H
#define KWINHELPER_UIKIT_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "imgui.h"
#include "imgui_internal.h" //DrawNavFrame: where the keyboard focus is

namespace ui {

inline constexpr ImU32 WHITE = IM_COL32(255, 255, 255, 255);
inline constexpr ImU32 LIGHT = IM_COL32(205, 205, 205, 255);
inline constexpr ImU32 GREY = IM_COL32(135, 135, 135, 255);
inline constexpr ImU32 DIM = IM_COL32(78, 78, 78, 255);
//Every size here is for 15 px text (BASE_TEXT). The "Menu text size" setting makes the menu, the player list and the
//inventory windows bigger: PushTheme sets g_scale (text size / 15) and S() multiplies a size by it.
inline constexpr float BASE_TEXT = 15.0f;
inline constexpr float LABEL_W = 210.0f, CONTROL_W = 250.0f;
inline float g_scale = 1.0f;
inline float S(float px) { return px * g_scale; }

//The width of the control after a row's label: CONTROL_W, narrower only when the page is too narrow for it (big menu
//text on a small screen), leaving room for what follows it (a slider's value)
inline float ControlW(float room) {
    return std::min(S(CONTROL_W), std::max(S(60), ImGui::GetContentRegionAvail().x - room));
}

//The next item (w wide) to the right of the last one if it still fits, else at the start of the next line, at x
//(window coordinates). Rows of buttons wrap instead of running off the page with big menu text.
inline void SameLineIfFits(float w, float x) {
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x >= w) return;
    ImGui::NewLine();
    ImGui::SetCursorPosX(x);
}

//The visible part of a label ("Name##id" shows "Name")
inline const char* LabelEnd(const char* label) {
    const char* p = std::strstr(label, "##");
    return p ? p : label + std::strlen(label);
}

//The black and white look. Call PushTheme() before drawing a window, PopTheme() after. menuOpacity: the background.
//Buttons, drop-downs, slider tracks and boxes are mid grey with a light outline, so they stand out from the black;
//lighter under the mouse and while pressed. White text stays at least 4.5:1 on every one of them.
//textSize: the window's text in px; padding, spacing and the scrollbar grow with it.
inline int g_themeColors = 0;
inline void PushTheme(float opacity, float textSize = BASE_TEXT) {
    auto c = [&](ImGuiCol i, ImVec4 v) { ImGui::PushStyleColor(i, v); g_themeColors++; };
    g_themeColors = 0;
    g_scale = textSize / BASE_TEXT;
    ImGui::PushFont(nullptr, textSize);
    const ImVec4 black(0, 0, 0, opacity), white(1, 1, 1, 1), grey(0.53f, 0.53f, 0.53f, 1),
                 fill(0.28f, 0.28f, 0.28f, 1), hover(0.38f, 0.38f, 0.38f, 1), press(0.45f, 0.45f, 0.45f, 1);
    c(ImGuiCol_WindowBg, black); c(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0)); c(ImGuiCol_PopupBg, ImVec4(0.04f, 0.04f, 0.04f, 0.98f));
    c(ImGuiCol_Border, ImVec4(1, 1, 1, 0.55f)); c(ImGuiCol_Text, white); c(ImGuiCol_TextDisabled, grey);
    c(ImGuiCol_TitleBg, black); c(ImGuiCol_TitleBgActive, black); c(ImGuiCol_TitleBgCollapsed, black);
    c(ImGuiCol_FrameBg, fill); c(ImGuiCol_FrameBgHovered, hover); c(ImGuiCol_FrameBgActive, hover);
    c(ImGuiCol_CheckMark, white); c(ImGuiCol_SliderGrab, white); c(ImGuiCol_SliderGrabActive, white); //white on any track
    c(ImGuiCol_Separator, hover); c(ImGuiCol_Header, fill); c(ImGuiCol_HeaderHovered, hover); c(ImGuiCol_HeaderActive, press);
    c(ImGuiCol_Button, fill); c(ImGuiCol_ButtonHovered, hover); c(ImGuiCol_ButtonActive, press);
    c(ImGuiCol_Tab, fill); c(ImGuiCol_TabHovered, hover); c(ImGuiCol_TabSelected, hover); c(ImGuiCol_TabSelectedOverline, white);
    //a grey bar on a visible track, so a page that scrolls looks like one
    c(ImGuiCol_ScrollbarBg, ImVec4(0.12f, 0.12f, 0.12f, 1)); c(ImGuiCol_ScrollbarGrab, grey);
    c(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.75f, 0.75f, 0.75f, 1)); c(ImGuiCol_ScrollbarGrabActive, white);
    c(ImGuiCol_TextSelectedBg, hover);
    c(ImGuiCol_NavCursor, white);
    const float border = std::max(1.0f, std::round(g_scale)); //whole pixels: 1, and 2 from 23 px text
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, border); //the Border colour around every button, drop-down, slider and box
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8), S(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(7)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(S(4), S(4)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, S(16));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, S(14));
}
inline void PopTheme() {
    ImGui::PopStyleVar(15);
    ImGui::PopStyleColor(g_themeColors);
    ImGui::PopFont();
    g_scale = 1.0f;
}

//A small grey heading with a thin line
inline void Section(const char* text) {
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::TextColored(ImColor(GREY), "%s", text);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y - S(2)), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y - S(2)), DIM, S(1));
    ImGui::Dummy(ImVec2(0, S(3)));
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
    const float h = S(16), w = S(32), row = h + S(6), e = S(2); //e: the gap between the outline and the filled half
    const ImVec2 p = ImGui::GetCursorScreenPos();
    //EnableNav: ImGui leaves invisible buttons out of Tab and the arrows unless asked
    const bool pressed = ImGui::InvisibleButton("##sw", ImVec2(std::max(S(60), ImGui::GetContentRegionAvail().x), row), ImGuiButtonFlags_EnableNav);
    if (pressed) *v = !*v;
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x, p.y + S(3)), b(p.x + w, p.y + S(3) + h);
    dl->AddRect(a, b, *v ? WHITE : GREY, 0.0f, 0, S(1));
    if (*v) dl->AddRectFilled(ImVec2(a.x + w / 2 + S(1), a.y + e), ImVec2(b.x - e, b.y - e), WHITE);
    else dl->AddRectFilled(ImVec2(a.x + e, a.y + e), ImVec2(a.x + w / 2 - S(1), b.y - e), GREY);
    dl->AddText(ImVec2(p.x + w + S(10), a.y + (h - ImGui::GetFontSize()) / 2), (*v || hov) ? WHITE : LIGHT, label, LabelEnd(label));
    ImGui::PopID();
    return pressed;
}

inline bool SliderRow(const char* label, ImGuiDataType type, void* v, const void* lo, const void* hi, const char* fmt) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, LabelEnd(label));
    ImGui::SameLine(S(LABEL_W));
    ImGui::SetNextItemWidth(ControlW(S(70)));
    //The track has no text on it (the handle would cover the number; ImGui draws nothing before "##"); the value is
    //shown to the right. ImGui still rounds to fmt, so a drag or the arrow keys stop on the numbers shown (with "" a
    //drag kept 7.63 for a List rows shown as 8). The arrow keys move a whole-number slider of up to 100 by 1.
    char track[48];
    std::snprintf(track, sizeof track, "##%s", fmt);
    const bool changed = ImGui::SliderScalar("##v", type, v, lo, hi, track, ImGuiSliderFlags_AlwaysClamp);
    char text[48];
    if (type == ImGuiDataType_Float) std::snprintf(text, sizeof text, fmt, (double)*(float*)v);
    else std::snprintf(text, sizeof text, fmt, *(double*)v);
    if (changed) { //a typed number too is kept as shown (35.4 typed into a whole-number slider is 35)
        if (type == ImGuiDataType_Float) *(float*)v = (float)std::strtod(text, nullptr);
        else *(double*)v = std::strtod(text, nullptr);
    }
    ImGui::SameLine(0, S(12));
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

//A drop-down list of n items; the chosen number is kept in a double (the settings are doubles)
inline bool Choice(const char* label, double* v, const char* const* items, int n) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, LabelEnd(label));
    ImGui::SameLine(S(LABEL_W));
    ImGui::SetNextItemWidth(ControlW(0));
    int idx = (int)*v;
    idx = idx < 0 ? 0 : idx >= n ? n - 1 : idx;
    bool changed = false;
    if (ImGui::BeginCombo("##c", items[idx])) {
        for (int i = 0; i < n; i++)
            if (ImGui::Selectable(items[i], i == idx)) { *v = i; changed = true; }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}
inline bool Choice(const char* label, double* v, std::initializer_list<const char*> items) {
    return Choice(label, v, items.begin(), (int)items.size());
}

//A tab on the left: the chosen one is lit grey with a white bar (white text on it: about 9:1)
inline bool Tab(const char* label, bool selected, float w) {
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = S(32);
    const bool pressed = ImGui::InvisibleButton("##t", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(72, 72, 72, 255));
    else if (hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(44, 44, 44, 255));
    if (selected) dl->AddRectFilled(p, ImVec2(p.x + S(3), p.y + h), WHITE);
    dl->AddText(ImVec2(p.x + S(16), p.y + (h - ImGui::GetFontSize()) / 2), selected ? WHITE : (hov ? LIGHT : GREY), label, LabelEnd(label));
    ImGui::PopID();
    return pressed;
}

//The keyboard focus (arrows / Tab): a thick white frame with black edges around the focused control, far easier to
//find than ImGui's own thin line. Draws nothing after a mouse click. Call once per frame after the windows are drawn.
inline void DrawNavFrame() {
    const ImGuiContext& g = *GImGui;
    ImGuiWindow* w = g.NavWindow;
    if (!w || !g.NavId || !g.NavIdIsAlive || !g.NavCursorVisible) return;
    ImRect r = ImGui::WindowRectRelToAbs(w, w->NavRectRel[g.NavLayer]);
    r.ClipWith(w->InnerClipRect); //a control half scrolled out of the page: frame the part that shows
    if (r.Min.x >= r.Max.x || r.Min.y >= r.Max.y) return;
    r.Expand(5.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRect(r.Min, r.Max, IM_COL32(0, 0, 0, 255), 0.0f, 0, 8.0f);
    dl->AddRect(r.Min, r.Max, WHITE, 0.0f, 0, 4.0f);
}

} // namespace ui

#endif //KWINHELPER_UIKIT_H
