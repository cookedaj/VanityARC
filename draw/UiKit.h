#pragma once
//The palette every drawing file shares (theme::) and the small widgets for the Insert menu, the player list and the
//inventory windows (ui::). The windows are black, white and grey only, so text keeps the most contrast it can have;
//every corner is rounded by the same moderate radius. A row is "label on the left, control on the right". Every widget
//gets its ID from its label, so labels must be different from each other within a page.
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
#include "../config.h"      //menuAccent, menuRoundness, menuSectionRules: the windows' look (Settings tab)

//The colours that mean something, for any file that draws: the one set of numbers. COLOR:: (ESP.h) takes its white,
//grey, red, green and dead grey from here and adds what the menu can change, so on the game picture keep calling
//COLOR::enemy() and COLOR::mate() for the living: they follow Box colour and Monochrome. In Monochrome draw the down
//in dead as well (downed is a hue).
namespace theme {
inline constexpr ImU32 text = IM_COL32(255, 255, 255, 255);      //COLOR::white
inline constexpr ImU32 textMuted = IM_COL32(170, 170, 170, 255); //COLOR::grey: notes, column heads. 9:1 on black
inline constexpr ImU32 accent = text;                            //what is chosen, on or focused: white, the strongest there is, not a hue
inline constexpr ImU32 panelBg = IM_COL32(0, 0, 0, 230);         //the label cards' black (Overlay.h); the windows are black at Menu opacity
inline constexpr ImU32 enemy = IM_COL32(207, 56, 56, 255);       //COLOR::red, what COLOR::enemy() gives until another Box colour is picked
inline constexpr ImU32 teammate = IM_COL32(34, 189, 39, 255);    //COLOR::green, what COLOR::mate() gives
//Down but not out: a strong orange, 9:1 on black. Redder and more vivid than the live ARC orange (COLOR::orange,
//240 168 43), but near it: keep telling the down apart by shape and by the word on the label too.
inline constexpr ImU32 downed = IM_COL32(255, 140, 0, 255);
//Dead: COLOR::deadGrey. No colour left in it, yet solid and 7:1 on black: never faint.
inline constexpr ImU32 dead = IM_COL32(150, 150, 150, 255);
} // namespace theme

namespace ui {

//The four greys of the windows, brightest first. On black: 21:1, 13:1, 9:1 and 4.8:1.
inline constexpr ImU32 WHITE = theme::text;
inline constexpr ImU32 LIGHT = IM_COL32(205, 205, 205, 255);
inline constexpr ImU32 GREY = theme::textMuted;
inline constexpr ImU32 DIM = IM_COL32(120, 120, 120, 255);
//Every size here is for 15 px text (BASE_TEXT). The "Menu text size" setting makes the menu, the player list and the
//inventory windows bigger: PushTheme sets g_scale (text size / 15) and S() multiplies a size by it.
inline constexpr float BASE_TEXT = 15.0f;
inline constexpr float LABEL_W = 210.0f, CONTROL_W = 250.0f;
//The corner radius: RADIUS for a control (button, drop-down, slider, tab) and what sits inside a window, PANEL_RADIUS
//for a window or a card. Draw your own rounded shapes with S(RADIUS) or S(PANEL_RADIUS), so every corner matches.
inline constexpr float RADIUS = 5.0f, PANEL_RADIUS = 8.0f;
inline float g_scale = 1.0f;
inline float S(float px) { return px * g_scale; }
//menuRoundness: 1 the corners above (as always), 0 square, up to 1.5. NaN: square. Corner() is S() with it, for the
//windows and their controls only: the cards on the game picture keep S(RADIUS) / PANEL_RADIUS whatever it is.
inline float Roundness() { return (float)std::min(1.5, std::max(0.0, config::menuRoundness)); }
inline float Corner(float px) { return S(px) * Roundness(); }

//What is chosen or on (the chosen tab's bar, an on switch, a heading's bar, a slider's handle, an open drop-down's
//outline): menuAccent. 0 white as always; then Box colour's hues in its order with white and red swapped: 1 red,
//2 yellow, 3 blue, 4 green, 5 violet (2 to 5 are COLOR::chosen's numbers, ESP.h). Every one is at least 4.5:1 on
//black, so an on switch's black knob stays as readable; Box colour's red (207 56 56) is only 4.3:1, so this one is a
//brighter red of the same hue (4.9:1).
inline ImU32 AccentColor() {
    static constexpr ImU32 hues[6] = {WHITE, IM_COL32(224, 60, 60, 255), IM_COL32(245, 215, 60, 255),
                                      IM_COL32(70, 120, 255, 255), IM_COL32(40, 200, 70, 255), IM_COL32(170, 90, 255, 255)};
    return hues[std::min(5, std::max(0, config::menuAccent))];
}

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

//The look of a window. Call PushTheme() before drawing a window, PopTheme() after. opacity: the background's.
//Buttons, drop-downs, slider tracks and boxes are mid grey with a light outline, so they stand out from the black;
//lighter under the mouse and while pressed. White text stays at least 4.5:1 on every one of them, which is why the
//pressed grey is no lighter. Windows, controls, pop-ups, handles and scrollbars all have rounded corners.
//textSize: the window's text in px; padding, spacing, corners and the scrollbar grow with it.
inline int g_themeColors = 0;
inline void PushTheme(float opacity, float textSize = BASE_TEXT) {
    auto c = [&](ImGuiCol i, ImVec4 v) { ImGui::PushStyleColor(i, v); g_themeColors++; };
    g_themeColors = 0;
    g_scale = textSize / BASE_TEXT;
    ImGui::PushFont(nullptr, textSize);
    const ImVec4 black(0, 0, 0, opacity), white(1, 1, 1, 1), light = ImColor(LIGHT), grey = ImColor(GREY), dim = ImColor(DIM),
                 fill(0.28f, 0.28f, 0.28f, 1), hover(0.38f, 0.38f, 0.38f, 1), press(0.45f, 0.45f, 0.45f, 1),
                 accent = ImColor(AccentColor());
    c(ImGuiCol_WindowBg, black); c(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0)); c(ImGuiCol_PopupBg, ImVec4(0.04f, 0.04f, 0.04f, 0.98f));
    c(ImGuiCol_Border, ImVec4(1, 1, 1, 0.55f)); c(ImGuiCol_Text, white); c(ImGuiCol_TextDisabled, grey);
    c(ImGuiCol_TitleBg, black); c(ImGuiCol_TitleBgActive, black); c(ImGuiCol_TitleBgCollapsed, black);
    c(ImGuiCol_FrameBg, fill); c(ImGuiCol_FrameBgHovered, hover); c(ImGuiCol_FrameBgActive, hover);
    c(ImGuiCol_CheckMark, white); c(ImGuiCol_SliderGrab, white); c(ImGuiCol_SliderGrabActive, white); //white on any track: a coloured handle loses contrast
    c(ImGuiCol_Separator, dim); c(ImGuiCol_Header, fill); c(ImGuiCol_HeaderHovered, hover); c(ImGuiCol_HeaderActive, press);
    c(ImGuiCol_Button, fill); c(ImGuiCol_ButtonHovered, hover); c(ImGuiCol_ButtonActive, press);
    c(ImGuiCol_Tab, fill); c(ImGuiCol_TabHovered, hover); c(ImGuiCol_TabSelected, hover); c(ImGuiCol_TabSelectedOverline, accent);
    //a grey bar on a visible track, so a page that scrolls looks like one
    c(ImGuiCol_ScrollbarBg, ImVec4(0.12f, 0.12f, 0.12f, 1)); c(ImGuiCol_ScrollbarGrab, grey);
    c(ImGuiCol_ScrollbarGrabHovered, light); c(ImGuiCol_ScrollbarGrabActive, white);
    //an inventory window's corner grip and the edge it is resized by: grey to white as everything else, not ImGui's blue
    c(ImGuiCol_ResizeGrip, grey); c(ImGuiCol_ResizeGripHovered, light); c(ImGuiCol_ResizeGripActive, white);
    c(ImGuiCol_SeparatorHovered, light); c(ImGuiCol_SeparatorActive, white);
    c(ImGuiCol_TextSelectedBg, hover);
    c(ImGuiCol_NavCursor, white);
    const float border = std::max(1.0f, std::round(g_scale)); //whole pixels: 1, and 2 from 23 px text
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, border); //the Border colour around every button, drop-down, slider and box
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, border);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, border); //an open drop-down list and a tooltip too
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Corner(PANEL_RADIUS));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, Corner(RADIUS));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Corner(RADIUS));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Corner(RADIUS));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, Corner(RADIUS - 2)); //a slider's handle sits 2 px inside its track
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, Corner(RADIUS));
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, Corner(RADIUS));
    //the spacing is as it was: the pages are measured to fit a 1280 px screen at 30 px text, and the player list's
    //height decides where the cards on the game picture go
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8), S(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(7)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(S(4), S(4)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14), S(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, S(16));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, S(14));
}
inline void PopTheme() {
    ImGui::PopStyleVar(16);
    ImGui::PopStyleColor(g_themeColors);
    ImGui::PopFont();
    g_scale = 1.0f;
}

//A heading: white, with a thick bar in the accent (white) under its words and, with menuSectionRules, a thin grey line
//from there to the page's right side
inline void Section(const char* text) {
    ImGui::Dummy(ImVec2(0, S(4)));
    ImGui::TextColored(ImColor(WHITE), "%s", text);
    const float words = ImGui::GetItemRectSize().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (config::menuSectionRules)
        dl->AddLine(ImVec2(p.x + words, p.y - S(2)), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y - S(2)), DIM, S(1));
    dl->AddRectFilled(ImVec2(p.x, p.y - S(3)), ImVec2(p.x + words, p.y - S(1)), AccentColor()); //a filled bar: sharp, where a 2 px line would blur over 3
    ImGui::Dummy(ImVec2(0, S(3)));
}

inline void Note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, GREY);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

//A switch: a pill with a rounded knob, the knob as big as the square half it used to have. Off: a grey outline, empty,
//the knob on the left. On: filled white (the accent), the knob black on the right. So on and off differ by fill and by
//side, not by colour. Under the mouse the label is underlined (and an off switch turns white); while the mouse button
//is down the knob is half way. The whole row can be clicked.
inline bool Switch(const char* label, bool* v) {
    ImGui::PushID(label);
    const float h = S(16), w = S(32), row = h + S(6), e = S(2); //e: the gap between the pill's edge and the knob
    const float pill = h / 2 * Roundness(); //the pill's and the knob's corners: square with menuRoundness 0
    const ImVec2 p = ImGui::GetCursorScreenPos();
    //EnableNav: ImGui leaves invisible buttons out of Tab and the arrows unless asked
    const bool pressed = ImGui::InvisibleButton("##sw", ImVec2(std::max(S(60), ImGui::GetContentRegionAvail().x), row), ImGuiButtonFlags_EnableNav);
    if (pressed) *v = !*v;
    const bool on = *v, hov = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a(p.x, p.y + S(3)), b(p.x + w, p.y + S(3) + h);
    if (on) dl->AddRectFilled(a, b, AccentColor(), pill);
    else dl->AddRect(a, b, hov ? WHITE : GREY, pill, 0, S(1));
    const float kw = w / 2 - S(1) - e, kx = held ? a.x + (w - kw) / 2 : on ? b.x - e - kw : a.x + e; //the knob's width and left side
    dl->AddRectFilled(ImVec2(kx, a.y + e), ImVec2(kx + kw, b.y - e), on ? IM_COL32_BLACK : hov ? WHITE : GREY, pill);
    const ImVec2 t(p.x + w + S(10), a.y + (h - ImGui::GetFontSize()) / 2);
    dl->AddText(t, (on || hov) ? WHITE : LIGHT, label, LabelEnd(label));
    if (hov) dl->AddRectFilled(ImVec2(t.x, b.y + S(1)), ImVec2(t.x + ImGui::CalcTextSize(label, LabelEnd(label)).x, b.y + S(3)), WHITE);
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

//A drop-down list of n items; the chosen number is kept in a double (the settings are doubles). While it is open, it
//and its list are outlined in the accent colour, solid; with the white accent they keep the usual outline (most hues
//at the outline's 55 % would fall under 4.5:1 on black).
inline bool Choice(const char* label, double* v, const char* const* items, int n) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, LabelEnd(label));
    ImGui::SameLine(S(LABEL_W));
    ImGui::SetNextItemWidth(ControlW(0));
    int idx = (int)*v;
    idx = idx < 0 ? 0 : idx >= n ? n - 1 : idx;
    bool changed = false;
    //BeginCombo's own popup ID: open since an earlier frame (so it lights the frame after the click that opens it)
    const bool lit = AccentColor() != WHITE && ImGui::IsPopupOpen(ImHashStr("##ComboPopup", 0, ImGui::GetID("##c")), 0);
    if (lit) ImGui::PushStyleColor(ImGuiCol_Border, AccentColor());
    if (ImGui::BeginCombo("##c", items[idx])) {
        for (int i = 0; i < n; i++)
            if (ImGui::Selectable(items[i], i == idx)) { *v = i; changed = true; }
        ImGui::EndCombo();
    }
    if (lit) ImGui::PopStyleColor(); //after EndCombo: the list's window ends with the colour stack it began with
    ImGui::PopID();
    return changed;
}
inline bool Choice(const char* label, double* v, std::initializer_list<const char*> items) {
    return Choice(label, v, items.begin(), (int)items.size());
}

//A tab on the left, rounded. The chosen one is lit grey with a white (accent) bar down its left side (white text on
//it: about 9:1). Under the mouse: a darker grey with a grey outline and lighter text. While the mouse button is down:
//lit as the chosen one, without the bar.
inline bool Tab(const char* label, bool selected, float w) {
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = S(32), r = Corner(RADIUS);
    const ImVec2 q(p.x + w, p.y + h);
    const bool pressed = ImGui::InvisibleButton("##t", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    const bool hov = ImGui::IsItemHovered(), lit = selected || ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (lit) dl->AddRectFilled(p, q, IM_COL32(72, 72, 72, 255), r);
    else if (hov) dl->AddRectFilled(p, q, IM_COL32(44, 44, 44, 255), r);
    if (hov && !selected) dl->AddRect(p, q, GREY, r, 0, S(1));
    if (selected) { //the bar: the left S(4) of the same rounded shape, so it follows the corners
        dl->PushClipRect(p, ImVec2(p.x + S(4), q.y), true);
        dl->AddRectFilled(p, q, AccentColor(), r);
        dl->PopClipRect();
    }
    dl->AddText(ImVec2(p.x + S(16), p.y + (h - ImGui::GetFontSize()) / 2), lit ? WHITE : (hov ? LIGHT : GREY), label, LabelEnd(label));
    ImGui::PopID();
    return pressed;
}

//The keyboard focus (arrows / Tab): a thick white frame with black edges around the focused control, far easier to
//find than ImGui's own thin line; rounded as the controls are. Draws nothing after a mouse click. Call once per frame
//after the windows are drawn.
inline void DrawNavFrame() {
    const ImGuiContext& g = *GImGui;
    ImGuiWindow* w = g.NavWindow;
    if (!w || !g.NavId || !g.NavIdIsAlive || !g.NavCursorVisible) return;
    ImRect r = ImGui::WindowRectRelToAbs(w, w->NavRectRel[g.NavLayer]);
    r.ClipWith(w->InnerClipRect); //a control half scrolled out of the page: frame the part that shows
    if (r.Min.x >= r.Max.x || r.Min.y >= r.Max.y) return;
    r.Expand(5.0f);
    const float corner = RADIUS + 4.0f; //about the control's own corner, 5 px further out
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRect(r.Min, r.Max, IM_COL32(0, 0, 0, 255), corner, 0, 8.0f);
    dl->AddRect(r.Min, r.Max, WHITE, corner, 0, 4.0f);
}

} // namespace ui

#endif //KWINHELPER_UIKIT_H
