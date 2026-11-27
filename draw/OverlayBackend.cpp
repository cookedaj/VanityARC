#define GLFW_EXPOSE_NATIVE_WAYLAND
#include "OverlayBackend.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cfloat>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <strings.h>
#include <iostream>
#include <string>
#include <thread>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "LayerWindow.h"
#include "Overlay.h"
#include "../config.h"
#include "../utils/identity.h"

namespace {

enum class Kind { None, Layer, Glfw };
Kind g_kind = Kind::None;
LayerWindow* g_layer = nullptr;
GLFWwindow* g_glfw = nullptr;
bool g_lost = false;         //the connection to the compositor broke
bool g_down = false;         //the layer surface is gone and is being made again (OverlayReady)
std::chrono::steady_clock::time_point g_retry; //the next try to make it again
std::string g_downErr;       //why the last try failed (printed once, not every 2 s)
bool g_interactive = false;
std::chrono::steady_clock::time_point g_last;
std::string g_iniPath;
std::array<int, 4> g_drawnSize{}; //the size the last frame was drawn at; zeros: this surface has no frame yet

//The overlay's size now: width and height in surface units (what ImGui lays out in), then in buffer pixels
std::array<int, 4> sizeNow() {
    std::array<int, 4> s{};
    if (g_kind == Kind::Layer) s = {g_layer->width(), g_layer->height(), g_layer->width() * g_layer->scale(), g_layer->height() * g_layer->scale()};
    else if (g_kind == Kind::Glfw) { glfwGetWindowSize(g_glfw, &s[0], &s[1]); glfwGetFramebufferSize(g_glfw, &s[2], &s[3]); }
    return s;
}

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

    //The monitor start.sh chose (KWINHELPER_MONITOR, e.g. DP-1), else the primary one
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (const char* want = std::getenv("KWINHELPER_MONITOR"); want && *want) {
        int count = 0;
        GLFWmonitor** all = glfwGetMonitors(&count);
        GLFWmonitor* found = nullptr;
        for (int i = 0; i < count && !found; ++i) {
            const char* name = glfwGetMonitorName(all[i]);
            if (name && strcasecmp(name, want) == 0) found = all[i]; //"dp-2" finds DP-2
        }
        if (found) monitor = found;
        else if (count > 1) { //with one monitor the primary one is the right one
            std::cerr << "[!] Monitor '" << want << "' not found. Sizing the window from the primary monitor." << std::endl;
            g_OverlayNote += std::string(g_OverlayNote.empty() ? "" : "\n") + "Monitor '" + want + "' not found: the window is sized from the primary monitor";
        }
    }
    if (!monitor) {
        std::cerr << "[-] Failed to get primary monitor." << std::endl;
        return false;
    }
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!mode) {
        std::cerr << "[-] Failed to read the monitor's size." << std::endl;
        return false;
    }
    //The mode is in pixels; a Wayland window is sized in the compositor's scaled units
    float xs = 1.0f, ys = 1.0f;
    glfwGetMonitorContentScale(monitor, &xs, &ys);
    config::SCREEN_W = (int)(mode->width / (xs > 0 ? xs : 1.0f) + 0.5f);
    config::SCREEN_H = (int)(mode->height / (ys > 0 ? ys : 1.0f) + 0.5f);

    g_glfw = glfwCreateWindow(config::SCREEN_W, config::SCREEN_H, APP_TITLE, nullptr, nullptr);
    if (!g_glfw) return false;

    glfwSetWindowPos(g_glfw, 0, 0);
    glfwMakeContextCurrent(g_glfw);
    glfwSetWindowAttrib(g_glfw, GLFW_MOUSE_PASSTHROUGH, GLFW_TRUE);
    glfwSwapInterval(1); // VSync
    glfwShowWindow(g_glfw);
    return true;
}

//For the Settings tab
std::string overlayWhere() {
    if (g_kind != Kind::Layer) return "Normal window (no layer-shell): it cannot show over a fullscreen game";
    const std::string& m = g_layer->monitorName();
    return "Layer-shell" + (m.empty() ? std::string() : " on " + m) + ": shows over fullscreen games";
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
            g_OverlayNote = g_layer->note();
        } else {
            std::cerr << "[!] No layer-shell overlay (" << err << "). Using a normal window: it cannot show over a fullscreen game." << std::endl;
            g_OverlayNote = "No layer-shell overlay: " + err + "\nUsing a normal window: it cannot show over a fullscreen game";
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
        const std::string dir = std::string(home ? home : ".") + "/.config/" + CONFIG_DIR_NAME;
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
    g_OverlayWhere = overlayWhere;
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
        //the surface's events were handled by OverlayReady(), so a closed surface is never drawn to
        ImGui_ImplOpenGL3_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)g_layer->width(), (float)g_layer->height());
        io.DisplayFramebufferScale = ImVec2((float)g_layer->scale(), (float)g_layer->scale());
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
    if (g_kind == Kind::Layer && !g_layer->mapped()) return; //stream hide took the surface away: nothing to draw on
    const std::array<int, 4> sz = sizeNow();
    glViewport(0, 0, sz[2], sz[3]);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!hidden) ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (g_kind == Kind::Layer) g_layer->swap();
    else glfwSwapBuffers(g_glfw);
    g_drawnSize = sz;
}

//no surface (stream hide): nothing to draw on, so no new size either; remap() clears g_drawnSize for the next one
bool OverlayNeedsFrame() { return !(g_kind == Kind::Layer && !g_layer->mapped()) && sizeNow() != g_drawnSize; }

void OverlayIdle() {
    if (g_kind == Kind::Glfw) glfwWaitEventsTimeout(0.002); //the window's events, as RenderBegin would handle them
    else std::this_thread::sleep_for(std::chrono::milliseconds(2)); //the layer surface's events: OverlayReady()
}

//The compositor closes the layer surface when its monitor is unplugged, switched off or reconfigured, and the connection
//breaks when the compositor restarts. Neither ends the program: the surface is taken down and made again, one try every
//2 s, while the tracker keeps reading the game.
bool OverlayReady() {
    if (g_kind != Kind::Layer) return true;
    if (!g_down) {
        if (!g_layer->pump()) g_lost = true;
        std::string why;
        if (g_lost) why = "lost the connection to the compositor";
        else if (g_layer->shouldClose()) why = "surface was closed by the compositor (monitor unplugged, off or changed)";
        else if (g_overlayHidden.load() == g_layer->mapped()) {
            //Stream hide takes the surface off the screen, so nothing covers the game and the desktop can show it directly
            //(smoother, less lag). Showing it again makes a new surface; the GL context with ImGui's shaders and font stays.
            std::string err;
            if (g_layer->mapped()) g_layer->unmap();
            else if (!g_layer->remap(&err)) why = "could not be shown again (" + err + ")";
            else g_drawnSize = {}; //the new surface shows nothing until it gets a frame
        }
        if (why.empty()) return true;
        std::cerr << "[!] The overlay " << why << ". Trying to show it again every 2 s." << std::endl;
        ImGui_ImplOpenGL3_Shutdown(); //its GL objects belong to the old context
        g_layer->destroy();
        ImGui::GetIO().ClearInputMouse(); //the pointer went with the surface: no button stays held
        g_down = true;
        g_lost = false;
        g_downErr.clear();
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < g_retry) return false;
    g_retry = now + std::chrono::seconds(2);
    const char* mon = std::getenv("KWINHELPER_MONITOR");
    std::string err;
    if (!g_layer->create(mon ? mon : "", &err)) {
        if (err != g_downErr) std::cerr << "[!] The overlay is not back yet (" << err << ")" << std::endl;
        g_downErr = err;
        return false;
    }
    ImGui_ImplOpenGL3_Init("#version 130"); //new GL context: shaders and the font texture are made again
    g_layer->setInteractive(g_interactive);  //the menu is open: the new surface takes the mouse too
    g_OverlayNote = g_layer->note();         //on the asked-for monitor again, or a new problem to show
    g_drawnSize = {};                        //the new surface shows nothing until it gets a frame
    g_down = false;
    g_last = now;
    std::cout << "[+] Overlay back (" << g_layer->width() << "x" << g_layer->height()
              << (g_layer->monitorName().empty() ? "" : " on " + g_layer->monitorName()) << ")" << std::endl;
    return true;
}

bool OverlayShouldClose() { return g_glfw && glfwWindowShouldClose(g_glfw); } //the layer surface is never final: OverlayReady

void OverlayShutdown() {
    if (ImGui::GetCurrentContext()) {
        if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
        if (ImGui::GetIO().BackendPlatformUserData) ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    g_SetInteractive = nullptr;
    g_OverlayWhere = nullptr;
    if (g_layer) { delete g_layer; g_layer = nullptr; }
    if (g_glfw) {
        glfwHideWindow(g_glfw);
        glfwDestroyWindow(g_glfw);
        g_glfw = nullptr;
    }
    // glfwTerminate() causes a segmentation fault, so it is not called
}
