#pragma once
//The overlay window and the per-frame start / end. Two kinds of window:
//  - a layer-shell surface (draw/LayerWindow.h): stays above fullscreen games and gamescope. Used when the
//    compositor has it (KDE does). Which monitor: environment variable VANITYARC_MONITOR (start.sh sets it).
//  - a normal GLFW window with the KDE "keep above" rule (fix-overlay-rule.sh): the fallback, windowed games only.
//    Force it with VANITYARC_BACKEND=glfw.
#ifndef VANITYARC_OVERLAYBACKEND_H
#define VANITYARC_OVERLAYBACKEND_H

bool InitOverlay();
void RenderBegin();                          //events, new ImGui frame, the full-screen host window for the ESP
void RenderEnd(void (*extra)() = nullptr);   //extra: more UI on top (the Insert menu); then draws and shows the frame
bool OverlayShouldClose();
void OverlayShutdown();
const char* OverlayBackendName();            //"layer-shell" or "glfw window"

#endif //VANITYARC_OVERLAYBACKEND_H
