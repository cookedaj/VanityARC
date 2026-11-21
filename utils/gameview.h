#pragma once
//Where the game's picture is on the overlay. Normally the picture fills the screen and has the screen's size. With
//gamescope the game draws at one size (say 1440x1080) and gamescope stretches that to the screen, so a 3D position
//has to be worked out for the game's own pixels first, then stretched the same way.
#ifndef KWINHELPER_GAMEVIEW_H
#define KWINHELPER_GAMEVIEW_H

#include <algorithm>
#include <cmath>
#include <string>

#include "gamescope.h"

struct GameView {
    int renderW = 0, renderH = 0;           //the game's own pixels; 0 = not set (use the overlay size)
    float dx = 0, dy = 0, dw = 0, dh = 0;   //where that picture shows on the overlay, in overlay pixels
    std::string how = "Normal: the game's picture fills the screen."; //for the menu
};

struct GameViewSettings {
    bool autoDetect = true;
    int manualW = 1440, manualH = 1080;
    bool manualStretch = true;              //false: keep the picture's shape, bars at the sides
};

inline GameView ComputeGameView(int overlayW, int overlayH, const gamescope::Info& gs, const GameViewSettings& st) {
    GameView v;
    if (overlayW < 1 || overlayH < 1) return v;
    int rw = overlayW, rh = overlayH; //the game's pixels
    int ow = overlayW, oh = overlayH; //what is shown (gamescope's output)
    std::string scaler = "stretch";
    if (!st.autoDetect) {
        rw = std::max(1, st.manualW);
        rh = std::max(1, st.manualH);
        scaler = st.manualStretch ? "stretch" : "fit";
        v.how = "set by hand: " + std::to_string(rw) + "x" + std::to_string(rh) + (st.manualStretch ? ", stretched to the screen" : ", kept in shape");
    } else if (gs.found && gs.renderW() > 0 && gs.renderH() > 0) {
        rw = gs.renderW();
        rh = gs.renderH();
        if (gs.outW > 0 && gs.outH > 0) { ow = gs.outW; oh = gs.outH; }
        scaler = gs.scaler;
        v.how = "gamescope: game " + std::to_string(rw) + "x" + std::to_string(rh) + ", shown as " + std::to_string(ow) + "x" + std::to_string(oh) + " (" + scaler + ")";
    } else if (gs.found) {
        v.how = "gamescope found, but its command line has no sizes (-w -h): using the screen size";
    }

    //the picture inside gamescope's output
    const double fit = std::min((double)ow / rw, (double)oh / rh);
    double iw = ow, ih = oh;
    if (scaler == "fill") { const double s = std::max((double)ow / rw, (double)oh / rh); iw = rw * s; ih = rh * s; }
    else if (scaler == "integer") { double s = std::floor(fit); if (s < 1) s = fit; iw = rw * s; ih = rh * s; }
    else if (scaler == "none") { iw = rw; ih = rh; }
    else if (scaler != "stretch") { iw = rw * fit; ih = rh * fit; } //auto / fit
    const double ix = (ow - iw) / 2, iy = (oh - ih) / 2;

    //the output fills the overlay (a fullscreen gamescope window fills the monitor)
    const double kx = (double)overlayW / ow, ky = (double)overlayH / oh;
    v.renderW = rw;
    v.renderH = rh;
    v.dx = (float)(ix * kx);
    v.dy = (float)(iy * ky);
    v.dw = (float)(iw * kx);
    v.dh = (float)(ih * ky);
    return v;
}

inline GameView g_gameView; //set once per frame by the main loop; ESP.h reads it

#endif //KWINHELPER_GAMEVIEW_H
