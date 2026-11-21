#pragma once
//Drawing helpers for the overlay (fonts, outlined shapes, labels). The window itself is in OverlayBackend.h.

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>

#include "imgui.h"

#include "../config.h"

//Set by the window code: true while the menu is open (the overlay then takes the mouse), false otherwise
inline void (*g_SetInteractive)(bool) = nullptr;

inline ImFont* g_overlayFont = nullptr; //bold font for the labels on screen (null: ImGui's default)

//Menu font (regular) first so it becomes the default, then the bold one for labels. System fonts are used when
//they exist (crisp at any size); otherwise ImGui's own scalable font.
inline void LoadOverlayFonts() {
    ImGuiIO& io = ImGui::GetIO();
    auto firstExisting = [](std::initializer_list<const char*> paths) -> const char* {
        std::error_code ec;
        for (const char* p : paths) if (std::filesystem::exists(p, ec)) return p;
        return nullptr;
    };
    const char* regular = firstExisting({"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                                         "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                                         "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                         "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                         "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"});
    const char* bold = firstExisting({"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
                                      "/usr/share/fonts/noto/NotoSans-Bold.ttf",
                                      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                                      "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                                      "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"});
    ImFont* r = regular ? io.Fonts->AddFontFromFileTTF(regular, 15.0f) : nullptr;
    if (!r) r = io.Fonts->AddFontDefaultVector();
    ImFont* b = bold ? io.Fonts->AddFontFromFileTTF(bold, 15.0f) : nullptr;
    g_overlayFont = b ? b : r;
}

inline ImFont* OverlayFont() { return g_overlayFont ? g_overlayFont : ImGui::GetFont(); }

//Everything the overlay draws goes through Fade(): the "Overlay opacity" setting (the menu itself is not affected)
inline ImU32 Fade(ImU32 c) {
    const float a = std::min(1.0f, std::max(0.0f, config::overlayOpacity));
    if (a >= 0.999f) return c;
    const unsigned alpha = (unsigned)(((c >> IM_COL32_A_SHIFT) & 0xFFu) * a + 0.5f);
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

inline void DrawCircleFilled(float x, float y, float radius, ImU32 color) {
    ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(x, y), radius, Fade(color), 0);
}

inline void DrawCircleRing(float x, float y, float radius, ImU32 color, float thickness = 1.0f) {
    ImGui::GetBackgroundDrawList()->AddCircle(ImVec2(x, y), radius, Fade(color), 0, thickness);
}

inline void DrawBox(float x, float y, float w, float h, ImU32 color, float thickness = 1.5f) {
    ImGui::GetBackgroundDrawList()->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), Fade(color), 0.0f, 0, thickness);
}

inline void DrawLine(float x1, float y1, float x2, float y2, ImU32 color, float thickness = 1.5f) {
    ImGui::GetBackgroundDrawList()->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), Fade(color), thickness);
}

inline void DrawTextImGui(float x, float y, ImU32 color, const char* text) {
    ImGui::GetBackgroundDrawList()->AddText(ImVec2(x, y), Fade(color), text);
}

inline void DrawTextCentered(float x, float y, ImU32 color, const char* text) {
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 pos = ImVec2(x - (textSize.x / 2.0f), y - (textSize.y / 2.0f));
    ImGui::GetBackgroundDrawList()->AddText(pos, Fade(color), text);
}

//Width and height a label will take
inline ImVec2 LabelSize(const char* text, float size) { return OverlayFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text); }

//Text centred on x. Plain: white with a black outline, readable over any background. Card: on a dark rectangle.
inline void DrawLabelCentered(float x, float y, const char* text, float size = 15.0f, ImU32 color = IM_COL32(255, 255, 255, 255), bool card = false) {
    ImFont* font = OverlayFont();
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    ImVec2 p(x - ts.x / 2.0f, y - ts.y / 2.0f);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (card) {
        dl->AddRectFilled(ImVec2(p.x - 4, p.y - 2), ImVec2(p.x + ts.x + 4, p.y + ts.y + 2), Fade(IM_COL32(0, 0, 0, 185)));
        dl->AddText(font, size, p, Fade(color), text);
        return;
    }
    const ImU32 black = Fade(IM_COL32(0, 0, 0, 255));
    for (int dx = -1; dx <= 1; dx += 2)
        for (int dy = -1; dy <= 1; dy += 2) dl->AddText(font, size, ImVec2(p.x + dx, p.y + dy), black, text);
    dl->AddText(font, size, p, Fade(color), text);
}

inline void DrawRectFilled(float x0, float y0, float x1, float y1, ImU32 color) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), Fade(color));
}

inline void DrawRectOutline(float x0, float y0, float x1, float y1, ImU32 color, float t = 1.0f) {
    ImGui::GetBackgroundDrawList()->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), Fade(color), 0.0f, 0, t);
}

inline void DrawCircleOutlined(float x, float y, float radius, ImU32 color) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddCircleFilled(ImVec2(x, y), radius, Fade(color), 0);
    dl->AddCircle(ImVec2(x, y), radius, Fade(IM_COL32(0, 0, 0, 200)), 0, 1.5f);
}

//Full rectangle with a dark edge so it shows on any background
inline void DrawBoxOutlined(float x, float y, float w, float h, ImU32 color, float t = 1.5f) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), Fade(IM_COL32(0, 0, 0, 220)), 0.0f, 0, t + 2.0f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), Fade(color), 0.0f, 0, t);
}

//Only the left and right edges, with short ticks at the corners
inline void DrawSidesBox(float x, float y, float w, float h, ImU32 color, float t = 1.5f) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float a = std::max(3.0f, std::min(w * 0.32f, 25.0f));
    const ImVec2 L[4] = {ImVec2(x + a, y), ImVec2(x, y), ImVec2(x, y + h), ImVec2(x + a, y + h)};
    const ImVec2 R[4] = {ImVec2(x + w - a, y), ImVec2(x + w, y), ImVec2(x + w, y + h), ImVec2(x + w - a, y + h)};
    for (const ImVec2* c : {L, R}) dl->AddPolyline(c, 4, Fade(IM_COL32(0, 0, 0, 220)), ImDrawFlags_None, t + 2.0f);
    for (const ImVec2* c : {L, R}) dl->AddPolyline(c, 4, Fade(color), ImDrawFlags_None, t);
}

//Four corner brackets instead of a whole rectangle: less in the way
inline void DrawCornerBox(float x, float y, float w, float h, ImU32 color, float t = 1.5f) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float cx = std::max(3.0f, w * 0.28f), cy = std::max(3.0f, std::min(h * 0.18f, w * 0.5f));
    const ImVec2 pts[4][3] = {
        {ImVec2(x, y + cy), ImVec2(x, y), ImVec2(x + cx, y)},
        {ImVec2(x + w - cx, y), ImVec2(x + w, y), ImVec2(x + w, y + cy)},
        {ImVec2(x + w, y + h - cy), ImVec2(x + w, y + h), ImVec2(x + w - cx, y + h)},
        {ImVec2(x + cx, y + h), ImVec2(x, y + h), ImVec2(x, y + h - cy)},
    };
    for (const auto& c : pts) dl->AddPolyline(c, 3, Fade(IM_COL32(0, 0, 0, 220)), ImDrawFlags_None, t + 2.0f);
    for (const auto& c : pts) dl->AddPolyline(c, 3, Fade(color), ImDrawFlags_None, t);
}

//Arrow with its tip at (x, y) pointing along the unit vector (dx, dy)
inline void DrawArrow(float x, float y, float dx, float dy, float size, ImU32 color) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 tip(x, y), base(x - dx * size, y - dy * size);
    const ImVec2 a(base.x - dy * size * 0.55f, base.y + dx * size * 0.55f), b(base.x + dy * size * 0.55f, base.y - dx * size * 0.55f);
    dl->AddTriangleFilled(tip, a, b, Fade(color));
    dl->AddTriangle(tip, a, b, Fade(IM_COL32(0, 0, 0, 220)), 1.5f);
}

inline void DrawDiamondFilled(float x, float y, float r, ImU32 color) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 p[4] = {ImVec2(x, y - r), ImVec2(x + r, y), ImVec2(x, y + r), ImVec2(x - r, y)};
    dl->AddConvexPolyFilled(p, 4, Fade(color));
    dl->AddPolyline(p, 4, Fade(IM_COL32(0, 0, 0, 200)), ImDrawFlags_Closed, 1.5f);
}
