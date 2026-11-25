#define GLFW_EXPOSE_NATIVE_WAYLAND
#include "OverlayBackend.h"

#include <GLFW/glfw3.h>

#include <cfloat>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "LayerWindow.h"
#include "Overlay.h"
#include "../config.h"

namespace {

enum class Kind { None, Layer, Glfw };
Kind g_kind = Kind::None;
LayerWindow* g_layer = nullptr;
GLFWwindow* g_glfw = nullptr;
bool g_lost = false;         //the connection to the compositor broke
bool g_interactive = false;
std::chrono::steady_clock::time_point g_last;
std::string g_iniPath;

void setInteractive(bool on) {
    g_interactive = on;
    if (g_kind == Kind::Layer) g_layer->setInteractive(on);
    else if (g_kind == Kind::Glfw) glfwSetWindowAttrib(g_glfw, GLFW_MOUSE_PASSTHROUGH, on ? GLFW_FALSE : GLFW_TRUE);
}

//The old window: a normal Wayland window made see-through and click-through
bool initGlfw() {
    // 1. FORCE WAYLAND
    setenv("GLFW_PLATFORM", "wayland", 1);

    if (!glfwInit()) {
        std::cerr << "[-] GLFW Init Failed! Install 'libglfw3-dev' and 'libwayland-dev'." << std::endl;
        return false;
    }

    // 2. Verify we are on Wayland
#if GLFW_VERSION_MAJOR >= 3 && GLFW_VERSION_MINOR >= 4
    if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
        std::cerr << "[-] FATAL: GLFW initialized into X11 mode! Aborting." << std::endl;
        glfwTerminate();
        return false;
    }
#endif

    // 3. Window Hints
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);     // Always on top
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);   // No borders
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);     // Create hidden
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) {
        std::cerr << "[-] Failed to get primary monitor." << std::endl;
        return false;
    }
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    config::SCREEN_W = mode->width;
    config::SCREEN_H = mode->height;

    g_glfw = glfwCreateWindow(config::SCREEN_W, config::SCREEN_H, config::OVERLAY_NAME, nullptr, nullptr);
    if (!g_glfw) return false;

    glfwSetWindowPos(g_glfw, 0, 0);
    glfwMakeContextCurrent(g_glfw);
    glfwSetWindowAttrib(g_glfw, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
    glfwSwapInterval(1); // VSync
    glfwShowWindow(g_glfw);
    return true;
}

} // namespace

bool InitOverlay() {
    const char* force = std::getenv("KWINHELPER_BACKEND");
    if (!(force && std::strcmp(force, "glfw") == 0)) {
        const char* mon = std::getenv("KWINHELPER_MONITOR");
        g_layer = new LayerWindow;
        std::string err;
        if (g_layer->create(mon ? mon : "", &err)) {
            g_kind = Kind::Layer;
            config::SCREEN_W = g_layer->width();
            config::SCREEN_H = g_layer->height();
        } else {
            std::cerr << "[!] No layer-shell overlay (" << err << "). Using a normal window: it cannot show over a fullscreen game." << std::endl;
            delete g_layer;
            g_layer = nullptr;
        }
    }
    if (g_kind == Kind::None) {
        if (!initGlfw()) return false;
        g_kind = Kind::Glfw;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    //Where the menu remembers its window position: with the other saved settings, not in whatever folder kwin-helper was started from
    {
        const char* home = std::getenv("HOME");
        const std::string dir = std::string(home ? home : ".") + "/.config/kwin-helper";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        g_iniPath = dir + "/imgui.ini";
        ImGui::GetIO().IniFilename = g_iniPath.c_str();
    }
    LoadOverlayFonts();

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    style.WindowBorderSize = 0.0f;
    style.MouseCursorScale = 2.5f; //the fallback arrow (and the tooltip gap next to the cursor) large enough to see

    if (g_kind == Kind::Glfw) ImGui_ImplGlfw_InitForOpenGL(g_glfw, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    g_SetInteractive = setInteractive;
    g_last = std::chrono::steady_clock::now();
    std::cout << "[+] Overlay Initialized (" << OverlayBackendName();
    if (g_kind == Kind::Layer) std::cout << ", " << g_layer->width() << "x" << g_layer->height()
                                         << (g_layer->monitorName().empty() ? "" : " on " + g_layer->monitorName());
    std::cout << ")" << std::endl;
    return true;
}

const char* OverlayBackendName() { return g_kind == Kind::Layer ? "layer-shell, shows over fullscreen" : "normal window"; }

void RenderBegin() {
    if (g_kind == Kind::Layer) {
        if (!g_layer->pump()) g_lost = true;
        ImGui_ImplOpenGL3_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)g_layer->width(), (float)g_layer->height());
        io.DisplayFramebufferScale = ImVec2(1, 1);
        const auto now = std::chrono::steady_clock::now();
        io.DeltaTime = std::min(0.1f, std::max(0.001f, std::chrono::duration<float>(now - g_last).count()));
        g_last = now;
        for (const LayerInput& e : g_layer->takeInput()) {
            switch (e.type) {
                case LayerInput::Motion: io.AddMousePosEvent(e.x, e.y); break;
                case LayerInput::Leave: io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); break;
                case LayerInput::Button: io.AddMouseButtonEvent(e.button, e.down); break;
                case LayerInput::Wheel: io.AddMouseWheelEvent(e.x, e.y); break;
            }
        }
        io.MouseDrawCursor = g_interactive && !g_layer->hasCursor(); //an arrow only when the desktop's cursor could not be loaded
    } else {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }
    ImGui::NewFrame();

    //Follow the real size of the window (the compositor may not give exactly what was asked for)
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if (ds.x >= 320 && ds.y >= 240) {
        config::SCREEN_W = (int)ds.x;
        config::SCREEN_H = (int)ds.y;
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(config::SCREEN_W, config::SCREEN_H));
    ImGui::Begin("##Overlay", nullptr,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus
    );
}

static std::atomic<bool> g_overlayHidden{false};
void SetOverlayHidden(bool on) { g_overlayHidden = on; }

void RenderEnd(void (*extra)()) {
    ImGui::End();
    const bool hidden = g_overlayHidden.load();
    if (extra && !hidden) extra(); //the menu is not drawn while hidden
    ImGui::Render();
    int w, h;
    if (g_kind == Kind::Layer) { w = g_layer->width(); h = g_layer->height(); }
    else glfwGetFramebufferSize(g_glfw, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!hidden) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (g_kind == Kind::Layer) g_layer->swap();
    else glfwSwapBuffers(g_glfw);
}

bool OverlayShouldClose() {
    if (g_kind == Kind::Layer) return g_lost || g_layer->shouldClose();
    return g_glfw && glfwWindowShouldClose(g_glfw);
}

void OverlayShutdown() {
    if (ImGui::GetCurrentContext()) {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    g_SetInteractive = nullptr;
    if (g_layer) { delete g_layer; g_layer = nullptr; }
    if (g_glfw) {
        glfwHideWindow(g_glfw);
        glfwDestroyWindow(g_glfw);
        g_glfw = nullptr;
    }
    // glfwTerminate() causes a segmentation fault, so it is not called
}
