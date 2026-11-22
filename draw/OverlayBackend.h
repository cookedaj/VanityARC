#pragma once
//The overlay window and the per-frame start / end. Two kinds of window:
//  - a layer-shell surface (draw/LayerWindow.h): stays above fullscreen games and gamescope. Used when the
//    compositor has it (KDE does). Which monitor: environment variable KWINHELPER_MONITOR (start.sh sets it).
//  - a normal GLFW window with the KDE "keep above" rule (fix-overlay-rule.sh): the fallback, windowed games only.
//    Force it with KWINHELPER_BACKEND=glfw.
#ifndef KWINHELPER_OVERLAYBACKEND_H
#define KWINHELPER_OVERLAYBACKEND_H

bool InitOverlay();
void RenderBegin();                          //events, new ImGui frame, the full-screen host window for the ESP
void RenderEnd(void (*extra)() = nullptr);   //extra: more UI on top (the Insert menu); then draws and shows the frame
void SetOverlayHidden(bool on);             //stream-hide: while on, every frame is empty (transparent), so nothing is shown
bool OverlayShouldClose();
void OverlayShutdown();
const char* OverlayBackendName();            //"layer-shell" or "glfw window"

#endif //KWINHELPER_OVERLAYBACKEND_H
