#pragma once

#include <string>
#include <cstdlib>

// One place for the overlay's cover identity. Keep these matching the "KWin Helper" cover.
inline constexpr char APP_ID[]          = "kwin-helper";   // process name (prctl), Wayland layer-shell namespace, ImGui ids, pkill target
inline constexpr char APP_TITLE[]       = "KWin Helper";   // GLFW window title + KWin window-rule match
inline constexpr char CONFIG_DIR_NAME[] = "kwin-helper";   // the ~/.config/<name> subdir

// inline: this header is included by many TUs; a non-inline definition would be a multiple-definition link error.
inline std::string configDir() {
    return std::string(getenv("HOME") ? getenv("HOME") : "") + "/.config/" + CONFIG_DIR_NAME;
}
