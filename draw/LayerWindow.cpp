#include "LayerWindow.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <strings.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wlr-layer-shell-client.h"

struct LayerWindow::Impl {
    wl_display* dpy = nullptr;
    wl_registry* reg = nullptr;
    wl_compositor* comp = nullptr;
    zwlr_layer_shell_v1* shell = nullptr;
    wl_seat* seat = nullptr;
    wl_pointer* pointer = nullptr;
    wl_shm* shm = nullptr;
    wl_cursor_theme* cursorTheme = nullptr; //the desktop's cursor, shown while the menu has the mouse
    wl_cursor_image* cursorImg = nullptr;
    wl_surface* cursorSurf = nullptr;
    struct Out { wl_output* o; std::string name; int scale = 1; uint32_t id = 0; }; //id: the registry name
    std::vector<Out> outs;
    wl_output* on = nullptr; //the output the overlay is on
    wl_output* target = nullptr; //the output asked for (nullptr: the compositor chooses)
    int scale = 1;           //buffer pixels per surface pixel (that output's wl_output.scale)

    wl_surface* surf = nullptr;
    zwlr_layer_surface_v1* ls = nullptr;
    wl_egl_window* ew = nullptr;
    EGLDisplay edpy = EGL_NO_DISPLAY;
    EGLContext ectx = EGL_NO_CONTEXT;
    EGLSurface esurf = EGL_NO_SURFACE;
    EGLConfig ecfg = nullptr;
    bool surfaceless = false; //the context can stay current without a surface (needed to take the surface away while hidden)

    int w = 0, h = 0;
    bool configured = false, closed = false, interactive = false;
    std::string monitor;
    std::string note; //set by create(): see note()
    std::vector<LayerInput> input;
    bool inside = false;
    wl_callback* frameCb = nullptr; //the compositor's "last frame is shown" signal, waited for in pump()

    static void onFrameDone(void* d, wl_callback* cb, uint32_t) {
        Impl* s = (Impl*)d;
        wl_callback_destroy(cb);
        if (s->frameCb == cb) s->frameCb = nullptr;
    }

    //--- registry
    static void onGlobal(void* d, wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
        Impl* s = (Impl*)d;
        if (!std::strcmp(iface, "wl_compositor")) s->comp = (wl_compositor*)wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
        else if (!std::strcmp(iface, "zwlr_layer_shell_v1")) s->shell = (zwlr_layer_shell_v1*)wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, ver < 4 ? ver : 4);
        else if (!std::strcmp(iface, "wl_shm")) s->shm = (wl_shm*)wl_registry_bind(r, name, &wl_shm_interface, 1);
        else if (!std::strcmp(iface, "wl_seat") && !s->seat) {
            s->seat = (wl_seat*)wl_registry_bind(r, name, &wl_seat_interface, ver < 5 ? ver : 5);
            static const wl_seat_listener sl = {onSeatCaps, onSeatName};
            wl_seat_add_listener(s->seat, &sl, s);
        } else if (!std::strcmp(iface, "wl_output")) {
            wl_output* o = (wl_output*)wl_registry_bind(r, name, &wl_output_interface, ver < 4 ? ver : 4);
            s->outs.push_back({o, "", 1, name});
            static const wl_output_listener ol = {onOutGeometry, onOutMode, onOutDone, onOutScale, onOutName, onOutDesc};
            wl_output_add_listener(o, &ol, s);
        }
    }
    //The asked-for monitor went away: start over (shouldClose), also while stream hide has no surface for the compositor
    //to close, so create() looks at the outputs again and note() says where the overlay went
    static void onGlobalRemove(void* d, wl_registry*, uint32_t name) {
        Impl* s = (Impl*)d;
        for (const Out& x : s->outs) if (x.id == name && x.o == s->target) s->closed = true;
    }

    //--- outputs: only the name and the scale matter
    static void onOutGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
    static void onOutMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
    static void onOutDone(void*, wl_output*) {}
    static void onOutScale(void* d, wl_output* o, int32_t scale) {
        Impl* s = (Impl*)d;
        for (Out& x : s->outs) if (x.o == o) x.scale = scale;
        s->applyScale();
    }
    static void onOutName(void* d, wl_output* o, const char* name) {
        for (Out& x : ((Impl*)d)->outs) if (x.o == o) x.name = name;
    }
    static void onOutDesc(void*, wl_output*, const char*) {}

    //--- seat and pointer
    static void onSeatCaps(void* d, wl_seat* seat, uint32_t caps) {
        Impl* s = (Impl*)d;
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s->pointer) {
            s->pointer = wl_seat_get_pointer(seat);
            static const wl_pointer_listener pl = {onEnter, onLeave, onMotion, onButton, onAxis, onFrame, onAxisSource, onAxisStop, onAxisDiscrete};
            wl_pointer_add_listener(s->pointer, &pl, s);
        } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && s->pointer) {
            wl_pointer_release(s->pointer);
            s->pointer = nullptr;
        }
    }
    static void onSeatName(void*, wl_seat*, const char*) {}
    static void onEnter(void* d, wl_pointer* p, uint32_t serial, wl_surface*, wl_fixed_t x, wl_fixed_t y) {
        Impl* s = (Impl*)d;
        s->inside = true;
        //the desktop's own cursor (the theme and size the user chose); only when it could not be loaded does the menu draw one
        if (s->cursorSurf) {
            wl_pointer_set_cursor(p, serial, s->cursorSurf, (int32_t)s->cursorImg->hotspot_x, (int32_t)s->cursorImg->hotspot_y);
            wl_surface_attach(s->cursorSurf, wl_cursor_image_get_buffer(s->cursorImg), 0, 0);
            wl_surface_damage(s->cursorSurf, 0, 0, (int32_t)s->cursorImg->width, (int32_t)s->cursorImg->height);
            wl_surface_commit(s->cursorSurf);
        } else wl_pointer_set_cursor(p, serial, nullptr, 0, 0);
        LayerInput e; e.type = LayerInput::Motion; e.x = (float)wl_fixed_to_double(x); e.y = (float)wl_fixed_to_double(y);
        s->input.push_back(e);
    }
    static void onLeave(void* d, wl_pointer*, uint32_t, wl_surface*) {
        Impl* s = (Impl*)d;
        s->inside = false;
        LayerInput e; e.type = LayerInput::Leave;
        s->input.push_back(e);
    }
    static void onMotion(void* d, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
        LayerInput e; e.type = LayerInput::Motion; e.x = (float)wl_fixed_to_double(x); e.y = (float)wl_fixed_to_double(y);
        ((Impl*)d)->input.push_back(e);
    }
    static void onButton(void* d, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
        LayerInput e; e.type = LayerInput::Button; e.down = state == WL_POINTER_BUTTON_STATE_PRESSED;
        if (button == BTN_LEFT) e.button = 0; else if (button == BTN_RIGHT) e.button = 1; else if (button == BTN_MIDDLE) e.button = 2; else return;
        ((Impl*)d)->input.push_back(e);
    }
    static void onAxis(void* d, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
        LayerInput e; e.type = LayerInput::Wheel;
        const float v = (float)wl_fixed_to_double(value) / -10.0f; //about 10 units per notch, down = negative
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) e.y = v; else e.x = -v;
        ((Impl*)d)->input.push_back(e);
    }
    static void onFrame(void*, wl_pointer*) {}
    static void onAxisSource(void*, wl_pointer*, uint32_t) {}
    static void onAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
    static void onAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}

    //--- the layer surface
    static void onConfigure(void* d, zwlr_layer_surface_v1* ls, uint32_t serial, uint32_t w, uint32_t h) {
        Impl* s = (Impl*)d;
        zwlr_layer_surface_v1_ack_configure(ls, serial);
        if (w > 0 && h > 0) {
            s->w = (int)w;
            s->h = (int)h;
            if (s->ew) wl_egl_window_resize(s->ew, s->w * s->scale, s->h * s->scale, 0, 0);
            if (s->interactive && s->surf) s->setRegion(true); //the menu's click area follows the new size
        }
        s->configured = true;
    }
    static void onClosed(void* d, zwlr_layer_surface_v1*) { ((Impl*)d)->closed = true; }

    //--- the output scale: draw at the output's pixel density so the compositor does not stretch (blur) the overlay
    static void onSurfEnter(void* d, wl_surface*, wl_output* o) { Impl* s = (Impl*)d; s->on = o; s->applyScale(); }
    static void onSurfLeave(void*, wl_surface*, wl_output*) {}
    void applyScale() {
        int sc = 1;
        for (const Out& x : outs) if (x.o == on && x.scale > 1) sc = x.scale;
        if (!surf || sc == scale || wl_surface_get_version(surf) < WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION) return;
        scale = sc;
        wl_surface_set_buffer_scale(surf, scale); //applies with the next commit, which carries the resized buffer
        if (ew) wl_egl_window_resize(ew, w * scale, h * scale, 0, 0);
    }

    void setRegion(bool full) {
        wl_region* r = wl_compositor_create_region(comp);
        if (full) wl_region_add(r, 0, 0, w, h);
        wl_surface_set_input_region(surf, r);
        wl_region_destroy(r);
    }

    //The full-screen surface and its EGL window surface, made at the start and again when stream hide ends. The Wayland
    //connection and the OpenGL context (with the menu's shaders and font) live on in between. nullptr, or why it failed.
    const char* makeSurface() {
        w = h = 0;
        configured = closed = inside = false;
        surf = wl_compositor_create_surface(comp);
        static const wl_surface_listener sl = {onSurfEnter, onSurfLeave};
        wl_surface_add_listener(surf, &sl, this);
        on = target; //nullptr: learned from the surface's enter event
        scale = 1;   //a new surface starts at scale 1
        applyScale();
        ls = zwlr_layer_shell_v1_get_layer_surface(shell, surf, target, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "kwin-helper");
        static const zwlr_layer_surface_v1_listener ll = {onConfigure, onClosed};
        zwlr_layer_surface_v1_add_listener(ls, &ll, this);
        zwlr_layer_surface_v1_set_size(ls, 0, 0); //0 + all four edges anchored = the whole output
        zwlr_layer_surface_v1_set_anchor(ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                             ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(ls, -1); //cover panels too
        zwlr_layer_surface_v1_set_keyboard_interactivity(ls, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE); //never take the keyboard from the game
        setRegion(false); //no input at all: every click goes to the game (onConfigure widens it while the menu is open)
        wl_surface_commit(surf);

        const auto t0 = std::chrono::steady_clock::now();
        while (!configured && !closed) {
            if (wl_display_roundtrip(dpy) < 0) return "lost the connection to the compositor";
            if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(3)) return "the compositor never sized the overlay";
        }
        if (closed || w <= 0 || h <= 0) return "the compositor refused the overlay surface";

        ew = wl_egl_window_create(surf, w * scale, h * scale);
        if (!ew) return "could not create the Wayland EGL window";
        esurf = eglCreateWindowSurface(edpy, ecfg, (EGLNativeWindowType)ew, nullptr);
        if (esurf == EGL_NO_SURFACE) return "EGL could not create the window surface";
        if (!eglMakeCurrent(edpy, esurf, esurf, ectx)) return "EGL could not make the context current";
        //0: eglSwapBuffers never waits (with 1 it can wait forever while the screen is locked or asleep, and then F10 and
        //SIGTERM are never seen). Frames are paced on the frame callback in pump() instead, with a time limit.
        eglSwapInterval(edpy, 0);
        return nullptr;
    }

    //Takes the surface off the screen. The context stays current without one where the driver allows it.
    void dropSurface() {
        if (edpy != EGL_NO_DISPLAY) {
            if (!surfaceless || !eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ectx))
                eglMakeCurrent(edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (esurf != EGL_NO_SURFACE) eglDestroySurface(edpy, esurf);
        }
        esurf = EGL_NO_SURFACE;
        if (frameCb) wl_callback_destroy(frameCb);
        frameCb = nullptr;
        if (ew) wl_egl_window_destroy(ew);
        ew = nullptr;
        if (ls) zwlr_layer_surface_v1_destroy(ls);
        ls = nullptr;
        if (surf) wl_surface_destroy(surf);
        surf = nullptr;
        inside = false;
        input.clear();
    }
};

LayerWindow::LayerWindow() : p_(new Impl) {}
LayerWindow::~LayerWindow() { destroy(); delete p_; }

bool LayerWindow::create(const std::string& monitor, std::string* error) {
    Impl& d = *p_;
    auto fail = [&](const std::string& why) { if (error) *error = why; destroy(); return false; };
    //A new surface starts clean, also when this is a retry after the compositor closed the old one
    d.w = d.h = 0;
    d.configured = d.closed = d.interactive = d.inside = false;
    d.monitor.clear();
    d.note.clear();
    d.input.clear();

    d.dpy = wl_display_connect(nullptr);
    if (!d.dpy) return fail("no Wayland display (WAYLAND_DISPLAY is not set or the compositor is not running)");
    d.reg = wl_display_get_registry(d.dpy);
    static const wl_registry_listener rl = {Impl::onGlobal, Impl::onGlobalRemove};
    wl_registry_add_listener(d.reg, &rl, &d);
    wl_display_roundtrip(d.dpy);
    wl_display_roundtrip(d.dpy); //output names arrive after the outputs are bound
    if (!d.comp) return fail("the compositor has no wl_compositor");
    if (!d.shell) return fail("this compositor has no layer-shell (KDE and sway have it, GNOME does not)");

    //The cursor over the menu: the desktop's theme and size (KDE sets XCURSOR_THEME and XCURSOR_SIZE)
    if (d.shm) {
        const char* sz = std::getenv("XCURSOR_SIZE");
        const int size = sz && std::atoi(sz) > 0 ? std::atoi(sz) : 24;
        d.cursorTheme = wl_cursor_theme_load(std::getenv("XCURSOR_THEME"), size, d.shm);
        wl_cursor* c = d.cursorTheme ? wl_cursor_theme_get_cursor(d.cursorTheme, "left_ptr") : nullptr;
        if (!c && d.cursorTheme) c = wl_cursor_theme_get_cursor(d.cursorTheme, "default");
        if (c && c->image_count > 0 && wl_cursor_image_get_buffer(c->images[0])) {
            d.cursorImg = c->images[0];
            d.cursorSurf = wl_compositor_create_surface(d.comp);
        }
    }

    wl_output* out = nullptr;
    std::string have;
    for (const Impl::Out& o : d.outs) {
        have += (have.empty() ? "" : ", ") + o.name;
        if (!monitor.empty() && strcasecmp(o.name.c_str(), monitor.c_str()) == 0) { out = o.o; d.monitor = o.name; } //"dp-2" finds DP-2
    }
    if (!monitor.empty() && !out && d.outs.size() > 1) { //with one output the compositor can only pick that one
        std::fprintf(stderr, "[!] Monitor '%s' not found (have: %s). The compositor will choose.\n", monitor.c_str(), have.c_str());
        d.note = "Monitor '" + monitor + "' not found (have: " + have + ")\nThe desktop chose the monitor: the overlay may be on the wrong screen";
    }
    d.target = out;

    //EGL (OpenGL 3.0 with an alpha channel, so the overlay can be see-through)
    d.edpy = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, d.dpy, nullptr);
    EGLint major = 0, minor = 0;
    if (d.edpy == EGL_NO_DISPLAY || !eglInitialize(d.edpy, &major, &minor)) return fail("EGL could not start on this Wayland display");
    if (!eglBindAPI(EGL_OPENGL_API)) return fail("EGL has no desktop OpenGL");
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(d.edpy, cfgAttr, &d.ecfg, 1, &n) || n < 1) return fail("EGL has no 8-bit RGBA configuration");
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    d.ectx = eglCreateContext(d.edpy, d.ecfg, EGL_NO_CONTEXT, ctxAttr);
    if (d.ectx == EGL_NO_CONTEXT) return fail("EGL could not create an OpenGL 3.0 context");
    const char* ext = eglQueryString(d.edpy, EGL_EXTENSIONS);
    d.surfaceless = ext && std::strstr(ext, "EGL_KHR_surfaceless_context");
    if (const char* why = d.makeSurface()) return fail(why);
    return true;
}

void LayerWindow::destroy() {
    Impl& d = *p_;
    d.dropSurface();
    if (d.edpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(d.edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (d.ectx != EGL_NO_CONTEXT) eglDestroyContext(d.edpy, d.ectx);
        eglTerminate(d.edpy);
    }
    d.ectx = EGL_NO_CONTEXT; d.edpy = EGL_NO_DISPLAY; d.ecfg = nullptr; d.surfaceless = false;
    if (d.pointer) wl_pointer_release(d.pointer);
    d.pointer = nullptr;
    if (d.cursorSurf) wl_surface_destroy(d.cursorSurf);
    if (d.cursorTheme) wl_cursor_theme_destroy(d.cursorTheme);
    d.cursorSurf = nullptr; d.cursorTheme = nullptr; d.cursorImg = nullptr;
    if (d.dpy) { wl_display_flush(d.dpy); wl_display_disconnect(d.dpy); }
    d.dpy = nullptr; d.reg = nullptr; d.comp = nullptr; d.shell = nullptr; d.seat = nullptr; d.shm = nullptr;
    d.outs.clear();
    d.on = nullptr; d.target = nullptr; d.scale = 1;
}

bool LayerWindow::pump() {
    Impl& d = *p_;
    if (!d.dpy) return false;
    //While the last frame is not shown yet, wait for it (this paces the frames to the display), but at most 100 ms:
    //a locked or sleeping screen sends no frame callbacks, and the main loop must still see F10 and SIGTERM
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    for (;;) {
        while (wl_display_prepare_read(d.dpy) != 0)
            if (wl_display_dispatch_pending(d.dpy) < 0) return false; //dead connection: events stay queued, prepare_read never succeeds
        wl_display_flush(d.dpy);
        int wait = 0;
        if (d.frameCb) wait = (int)std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now()).count());
        else if (!d.surf) wait = 33; //no surface (stream hide), so no frame callback paces the loop: about 30 times a second
        pollfd pfd{wl_display_get_fd(d.dpy), POLLIN, 0};
        if (poll(&pfd, 1, wait) > 0) wl_display_read_events(d.dpy);
        else wl_display_cancel_read(d.dpy);
        wl_display_dispatch_pending(d.dpy);
        if (wl_display_get_error(d.dpy) != 0) return false;
        if (!d.frameCb || wait == 0) return true;
    }
}

void LayerWindow::swap() {
    Impl& d = *p_;
    if (d.esurf == EGL_NO_SURFACE) return; //no display, or no surface while hidden
    if (!d.frameCb) { //goes out with the commit eglSwapBuffers makes
        static const wl_callback_listener fl = {Impl::onFrameDone};
        d.frameCb = wl_surface_frame(d.surf);
        wl_callback_add_listener(d.frameCb, &fl, &d);
    }
    eglSwapBuffers(d.edpy, d.esurf);
}

void LayerWindow::setInteractive(bool on) {
    Impl& d = *p_;
    if (d.interactive == on) return;
    d.interactive = on; //also while there is no surface: remap() gives the new one this click area
    if (d.surf) d.setRegion(on); //takes effect with the next frame
    if (!on) d.input.clear();
}

void LayerWindow::unmap() {
    Impl& d = *p_;
    if (!d.surf || !d.surfaceless) return;
    //The context (the menu's shaders and font) must stay current without a surface. Where the driver refuses, the
    //surface stays and shows empty frames, as before.
    if (!eglMakeCurrent(d.edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, d.ectx)) {
        eglMakeCurrent(d.edpy, d.esurf, d.esurf, d.ectx);
        d.surfaceless = false;
        return;
    }
    d.dropSurface();
}

bool LayerWindow::remap(std::string* error) {
    Impl& d = *p_;
    if (d.surf) return true;
    if (!d.dpy) { if (error) *error = "no connection to the compositor"; return false; }
    const char* why = d.makeSurface();
    if (!why) return true;
    if (error) *error = why;
    d.dropSurface();
    return false;
}

bool LayerWindow::mapped() const { return p_->surf != nullptr; }

bool LayerWindow::shouldClose() const { return p_->closed || (p_->dpy && wl_display_get_error(p_->dpy) != 0); } //also a connection lost during swap()
bool LayerWindow::hasCursor() const { return p_->cursorSurf != nullptr; }
int LayerWindow::width() const { return p_->w; }
int LayerWindow::height() const { return p_->h; }
int LayerWindow::scale() const { return p_->scale; }
const std::string& LayerWindow::monitorName() const {
    if (p_->monitor.empty()) for (const Impl::Out& o : p_->outs) if (o.o == p_->on) return o.name; //learned from the surface's enter event
    return p_->monitor;
}
const std::string& LayerWindow::note() const { return p_->note; }

std::vector<LayerInput> LayerWindow::takeInput() {
    std::vector<LayerInput> v;
    v.swap(p_->input);
    return v;
}
