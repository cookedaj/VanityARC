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
void SetOverlayHidden(bool on);             //stream-hide: while on, nothing is shown. The layer surface is taken off the screen
                                             //(OverlayReady) and made again when it goes off; the normal window draws empty frames
bool OverlayReady();                         //once per frame before RenderBegin(): the layer surface's events. False while the surface
                                             //is gone (monitor unplugged / off, compositor restart) and being made again: skip the frame
bool OverlayNeedsFrame();                    //the surface is new or changed size: it needs a frame even if nothing in it changed
void OverlayIdle();                          //instead of a frame that would look like the last: window events, then a ~2 ms wait
bool OverlayShouldClose();
void OverlayShutdown();
const char* OverlayBackendName();            //"layer-shell, shows over fullscreen" or "normal window"

#endif //KWINHELPER_OVERLAYBACKEND_H
