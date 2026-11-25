#pragma once
//The overlay window as a Wayland "layer-shell" surface on the overlay layer. KDE draws an active fullscreen window
//(a fullscreen game, or gamescope -f) above every normal window, even ones marked "keep above"; the overlay layer is
//the one thing that stays above it. Needs a compositor with wlr-layer-shell (KDE, sway, Hyprland; not GNOME).
#ifndef KWINHELPER_LAYERWINDOW_H
#define KWINHELPER_LAYERWINDOW_H

#include <string>
#include <vector>

struct LayerInput {
    enum Type { Motion, Button, Wheel, Leave } type = Motion;
    float x = 0, y = 0;      //Motion: position. Wheel: amount (x, y)
    int button = 0;          //Button: 0 left, 1 right, 2 middle
    bool down = false;
};

class LayerWindow {
public:
    LayerWindow();
    ~LayerWindow();
    LayerWindow(const LayerWindow&) = delete;
    LayerWindow& operator=(const LayerWindow&) = delete;

    //monitor: an output name such as "DP-2", or "" to let the compositor choose. False (with a reason in *error) when
    //there is no Wayland, no layer-shell or no EGL. On success an OpenGL 3.0 context is current on the calling thread.
    bool create(const std::string& monitor, std::string* error);
    void destroy();

    bool pump();                         //handle waiting events without blocking; false when the connection is gone
    void swap();                         //show the frame (waits for the display's refresh)
    void setInteractive(bool on);        //true: takes the mouse (the menu); false: every click goes to the game

    bool shouldClose() const;
    bool hasCursor() const;              //the desktop's cursor shows over the menu (false: the menu must draw one)
    int width() const;
    int height() const;
    const std::string& monitorName() const;
    std::vector<LayerInput> takeInput(); //pointer events since the last call (only arrive while interactive)

private:
    struct Impl;
    Impl* p_;
};

#endif //KWINHELPER_LAYERWINDOW_H
