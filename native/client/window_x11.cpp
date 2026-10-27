#include "client/window.h"
#include "client/opengl_shared.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <dlfcn.h>

namespace gtc::native {
namespace {

struct _XDisplay;
struct _XVisual;
struct _XGC;
using Display = _XDisplay;
using Visual = _XVisual;
using GC = _XGC*;
using XID = unsigned long;
using Window = XID;
using Drawable = XID;
using Colormap = XID;
using Atom = unsigned long;
using Bool = int;
using Time = unsigned long;
using KeySym = unsigned long;
using GLXContext = void*;
using GLXDrawable = XID;

struct XKeyEvent {
    int type;
    unsigned long serial;
    Bool send_event;
    Display* display;
    Window window, root, subwindow;
    Time time;
    int x, y, x_root, y_root;
    unsigned int state, keycode;
    Bool same_screen;
};

struct XButtonEvent {
    int type;
    unsigned long serial;
    Bool send_event;
    Display* display;
    Window window, root, subwindow;
    Time time;
    int x, y, x_root, y_root;
    unsigned int state, button;
    Bool same_screen;
};

struct XMotionEvent {
    int type;
    unsigned long serial;
    Bool send_event;
    Display* display;
    Window window, root, subwindow;
    Time time;
    int x, y;
    unsigned int state;
    char is_hint;
    Bool same_screen;
};

struct XConfigureEvent {
    int type;
    unsigned long serial;
    Bool send_event;
    Display* display;
    Window event, window;
    int x, y, width, height, border_width;
    Window above;
    Bool override_redirect;
};

struct XClientMessageEvent {
    int type;
    unsigned long serial;
    Bool send_event;
    Display* display;
    Window window;
    Atom message_type;
    int format;
    union Data { char b[20]; short s[10]; long l[5]; } data;
};

union XEvent {
    int type;
    XKeyEvent xkey;
    XButtonEvent xbutton;
    XMotionEvent xmotion;
    XConfigureEvent xconfigure;
    XClientMessageEvent xclient;
    long pad[24];
};

struct XImage;

struct XVisualInfo {
    Visual* visual;
    XID visualid;
    int screen;
    int depth;
    int visualClass;
    unsigned long red_mask, green_mask, blue_mask;
    int colormap_size;
    int bits_per_rgb;
};

struct XSetWindowAttributes {
    XID background_pixmap;
    unsigned long background_pixel;
    XID border_pixmap;
    unsigned long border_pixel;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    Bool save_under;
    long event_mask;
    long do_not_propagate_mask;
    Bool override_redirect;
    Colormap colormap;
    XID cursor;
};

struct Api {
    void* library = nullptr;
    Display* (*openDisplay)(const char*) = nullptr;
    int (*closeDisplay)(Display*) = nullptr;
    int (*defaultScreen)(Display*) = nullptr;
    Window (*rootWindow)(Display*, int) = nullptr;
    Visual* (*defaultVisual)(Display*, int) = nullptr;
    int (*defaultDepth)(Display*, int) = nullptr;
    Window (*createWindow)(Display*, Window, int, int, unsigned int, unsigned int, unsigned int,
                           int, unsigned int, Visual*, unsigned long, XSetWindowAttributes*) = nullptr;
    Window (*createSimpleWindow)(Display*, Window, int, int, unsigned int, unsigned int,
                                 unsigned int, unsigned long, unsigned long) = nullptr;
    Colormap (*createColormap)(Display*, Window, Visual*, int) = nullptr;
    int (*freeColormap)(Display*, Colormap) = nullptr;
    int (*selectInput)(Display*, Window, long) = nullptr;
    int (*mapWindow)(Display*, Window) = nullptr;
    int (*storeName)(Display*, Window, const char*) = nullptr;
    GC (*createGC)(Display*, Drawable, unsigned long, void*) = nullptr;
    int (*freeGC)(Display*, GC) = nullptr;
    XImage* (*createImage)(Display*, Visual*, unsigned int, int, int, char*, int, int, int, int) = nullptr;
    int (*putImage)(Display*, Drawable, GC, XImage*, int, int, int, int, unsigned int, unsigned int) = nullptr;
    int (*free)(void*) = nullptr;
    int (*flush)(Display*) = nullptr;
    int (*pending)(Display*) = nullptr;
    int (*nextEvent)(Display*, XEvent*) = nullptr;
    int (*peekEvent)(Display*, XEvent*) = nullptr;
    KeySym (*lookupKeysym)(XKeyEvent*, int) = nullptr;
    Atom (*internAtom)(Display*, const char*, Bool) = nullptr;
    int (*setProtocols)(Display*, Window, Atom*, int) = nullptr;
    int (*grabPointer)(Display*, Window, Bool, unsigned int, int, int, Window, unsigned long, Time) = nullptr;
    int (*ungrabPointer)(Display*, Time) = nullptr;
    int (*warpPointer)(Display*, Window, Window, int, int, unsigned int, unsigned int, int, int) = nullptr;
};

struct GlxApi {
    XVisualInfo* (*chooseVisual)(Display*, int, int*) = nullptr;
    GLXContext (*createContext)(Display*, XVisualInfo*, GLXContext, Bool) = nullptr;
    void (*destroyContext)(Display*, GLXContext) = nullptr;
    Bool (*makeCurrent)(Display*, GLXDrawable, GLXContext) = nullptr;
    void (*swapBuffers)(Display*, GLXDrawable) = nullptr;
    void* (*getProcAddress)(const char*) = nullptr;
};

int keySymbol(Api& api, XKeyEvent& event) {
    const KeySym symbol = api.lookupKeysym(&event, 0);
    if (symbol >= 'A' && symbol <= 'Z') return static_cast<int>(symbol - 'A' + 'a');
    if (symbol < 128) return static_cast<int>(symbol);
    switch (symbol) {
        case 0xff1b: return kKeyEscape;
        case 0xffbe: return kKeyF1;
        case 0xffc9: return kKeyF12;
        case 0xffe1: case 0xffe2: return kKeyShift;
        case 0xff0d: return kKeyEnter;
        case 0xff51: return kKeyLeft;
        case 0xff53: return kKeyRight;
        case 0xff52: return kKeyUp;
        case 0xff54: return kKeyDown;
        case 0xff55: return kKeyPageUp;
        case 0xff56: return kKeyPageDown;
        case 0xff09: return kKeyTab;
        default: return 0;
    }
}

} // namespace

struct NativeWindow::Impl {
    Api api;
    GlxApi glx;
    Display* display = nullptr;
    Window window = 0;
    GC gc = nullptr;
    Visual* visual = nullptr;
    int depth = 0;
    Colormap colormap = 0;
    GLXContext context = nullptr;
    void* glLibrary = nullptr;
    OpenGlRenderer renderer;
    Atom wmDelete = 0;
    bool gpu = false;
    bool open = false;
    bool captured = false;
    bool skipMotion = false;
    double lastX = 240;
    double lastY = 320;
    std::vector<unsigned char> bgra;
    int width = 480;
    int height = 640;

    bool load() {
        api.library = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        if (!api.library) api.library = dlopen("libX11.so", RTLD_NOW | RTLD_LOCAL);
        if (!api.library) return false;
#define GTC_LOAD_X11(member, symbol) \
        do { api.member = reinterpret_cast<decltype(api.member)>(dlsym(api.library, symbol)); \
             if (!api.member) return false; } while (false)
        GTC_LOAD_X11(openDisplay, "XOpenDisplay");
        GTC_LOAD_X11(closeDisplay, "XCloseDisplay");
        GTC_LOAD_X11(defaultScreen, "XDefaultScreen");
        GTC_LOAD_X11(rootWindow, "XRootWindow");
        GTC_LOAD_X11(defaultVisual, "XDefaultVisual");
        GTC_LOAD_X11(defaultDepth, "XDefaultDepth");
        GTC_LOAD_X11(createWindow, "XCreateWindow");
        GTC_LOAD_X11(createSimpleWindow, "XCreateSimpleWindow");
        GTC_LOAD_X11(createColormap, "XCreateColormap");
        GTC_LOAD_X11(freeColormap, "XFreeColormap");
        GTC_LOAD_X11(selectInput, "XSelectInput");
        GTC_LOAD_X11(mapWindow, "XMapWindow");
        GTC_LOAD_X11(storeName, "XStoreName");
        GTC_LOAD_X11(createGC, "XCreateGC");
        GTC_LOAD_X11(freeGC, "XFreeGC");
        GTC_LOAD_X11(createImage, "XCreateImage");
        GTC_LOAD_X11(putImage, "XPutImage");
        GTC_LOAD_X11(free, "XFree");
        GTC_LOAD_X11(flush, "XFlush");
        GTC_LOAD_X11(pending, "XPending");
        GTC_LOAD_X11(nextEvent, "XNextEvent");
        GTC_LOAD_X11(peekEvent, "XPeekEvent");
        GTC_LOAD_X11(lookupKeysym, "XLookupKeysym");
        GTC_LOAD_X11(internAtom, "XInternAtom");
        GTC_LOAD_X11(setProtocols, "XSetWMProtocols");
        GTC_LOAD_X11(grabPointer, "XGrabPointer");
        GTC_LOAD_X11(ungrabPointer, "XUngrabPointer");
        GTC_LOAD_X11(warpPointer, "XWarpPointer");
#undef GTC_LOAD_X11
        return true;
    }

    bool loadGl() {
        glLibrary = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!glLibrary) glLibrary = dlopen("libGL.so", RTLD_NOW | RTLD_LOCAL);
        if (!glLibrary) return false;
#define GTC_LOAD_GLX(member, symbol) \
        do { glx.member = reinterpret_cast<decltype(glx.member)>(dlsym(glLibrary, symbol)); \
             if (!glx.member) return false; } while (false)
        GTC_LOAD_GLX(chooseVisual, "glXChooseVisual");
        GTC_LOAD_GLX(createContext, "glXCreateContext");
        GTC_LOAD_GLX(destroyContext, "glXDestroyContext");
        GTC_LOAD_GLX(makeCurrent, "glXMakeCurrent");
        GTC_LOAD_GLX(swapBuffers, "glXSwapBuffers");
#undef GTC_LOAD_GLX
        glx.getProcAddress = reinterpret_cast<decltype(glx.getProcAddress)>(dlsym(glLibrary, "glXGetProcAddressARB"));
        if (!glx.getProcAddress) glx.getProcAddress = reinterpret_cast<decltype(glx.getProcAddress)>(dlsym(glLibrary, "glXGetProcAddress"));
        return true;
    }

    void disableVsync() {
        if (!glx.getProcAddress) return;
        using SwapIntervalExt = void (*)(Display*, GLXDrawable, int);
        using SwapIntervalMesa = int (*)(unsigned int);
        using SwapIntervalSgi = int (*)(int);
        if (void* address = glx.getProcAddress("glXSwapIntervalEXT")) {
            reinterpret_cast<SwapIntervalExt>(address)(display, window, 0);
        } else if (void* address = glx.getProcAddress("glXSwapIntervalMESA")) {
            reinterpret_cast<SwapIntervalMesa>(address)(0);
        } else if (void* address = glx.getProcAddress("glXSwapIntervalSGI")) {
            reinterpret_cast<SwapIntervalSgi>(address)(0);
        }
    }

    void shutdown() {
        if (display) {
            if (captured) api.ungrabPointer(display, 0);
            if (context) {
                glx.makeCurrent(display, window, context);
                renderer.shutdown();
                glx.makeCurrent(display, 0, nullptr);
                glx.destroyContext(display, context);
            }
            if (gc) api.freeGC(display, gc);
            if (colormap) api.freeColormap(display, colormap);
            api.closeDisplay(display);
        }
        if (glLibrary) dlclose(glLibrary);
        if (api.library) dlclose(api.library);
        display = nullptr;
        window = 0;
        gc = nullptr;
        visual = nullptr;
        depth = 0;
        colormap = 0;
        context = nullptr;
        glLibrary = nullptr;
        api = {};
        glx = {};
        gpu = false;
        open = false;
        captured = false;
    }
};

namespace {

NativeWindow::Impl* activeGpu = nullptr;

bool renderGpu(const GpuScene& scene, NativeFrame& frame) {
    if (!activeGpu || !activeGpu->gpu) return false;
    if (!activeGpu->glx.makeCurrent(activeGpu->display, activeGpu->window, activeGpu->context)) return false;
    return activeGpu->renderer.render(scene, frame);
}

} // namespace

NativeWindow::NativeWindow() = default;
NativeWindow::~NativeWindow() { close(); }

bool NativeWindow::open(const std::string& title, std::string& error) {
    close();
    impl_ = new Impl;
    if (!impl_->load()) {
        error = "Native X11 is unavailable; use --headless on this system";
        close();
        return false;
    }
    impl_->display = impl_->api.openDisplay(nullptr);
    if (!impl_->display) {
        error = "No native X11 display; use --headless or run inside an X11 session";
        close();
        return false;
    }
    const int screen = impl_->api.defaultScreen(impl_->display);
    const Window root = impl_->api.rootWindow(impl_->display, screen);
    impl_->visual = impl_->api.defaultVisual(impl_->display, screen);
    impl_->depth = impl_->api.defaultDepth(impl_->display, screen);
    XVisualInfo* visualInfo = nullptr;
    if (impl_->loadGl()) {
        int attributes[] = {4, 12, 24, 5, 0}; // GLX_RGBA, 24-bit depth, GLX_DOUBLEBUFFER.
        visualInfo = impl_->glx.chooseVisual(impl_->display, screen, attributes);
    }
    if (visualInfo && visualInfo->visual) {
        const Colormap colormap = impl_->api.createColormap(impl_->display, root, visualInfo->visual, 0);
        XSetWindowAttributes attributes{};
        attributes.background_pixel = 0x111815;
        attributes.colormap = colormap;
        if (colormap) {
            impl_->window = impl_->api.createWindow(impl_->display, root, 0, 0, 480, 640, 0,
                visualInfo->depth, 1, visualInfo->visual, (1L << 1) | (1L << 13), &attributes);
        }
        if (impl_->window) {
            impl_->visual = visualInfo->visual;
            impl_->depth = visualInfo->depth;
            impl_->colormap = colormap;
            impl_->context = impl_->glx.createContext(impl_->display, visualInfo, nullptr, 1);
        } else if (colormap) {
            impl_->api.freeColormap(impl_->display, colormap);
        }
        impl_->api.free(visualInfo);
    }
    if (!impl_->window) {
        impl_->window = impl_->api.createSimpleWindow(impl_->display, root, 0, 0, 480, 640, 0, 0, 0x111815);
        impl_->visual = impl_->api.defaultVisual(impl_->display, screen);
        impl_->depth = impl_->api.defaultDepth(impl_->display, screen);
    }
    if (!impl_->window) {
        error = "Could not create the native 480x640 window";
        close();
        return false;
    }
    constexpr long masks = (1L << 0) | (1L << 1) | (1L << 2) | (1L << 3) |
                           (1L << 6) | (1L << 9) | (1L << 15) | (1L << 17);
    impl_->api.selectInput(impl_->display, impl_->window, masks);
    impl_->api.storeName(impl_->display, impl_->window, title.c_str());
    impl_->wmDelete = impl_->api.internAtom(impl_->display, "WM_DELETE_WINDOW", 0);
    impl_->api.setProtocols(impl_->display, impl_->window, &impl_->wmDelete, 1);
    impl_->gc = impl_->api.createGC(impl_->display, impl_->window, 0, nullptr);
    impl_->api.mapWindow(impl_->display, impl_->window);
    impl_->api.flush(impl_->display);
    impl_->bgra.resize(480U * 640U * 4U);
    impl_->open = true;
    if (impl_->context && impl_->glx.makeCurrent(impl_->display, impl_->window, impl_->context)) {
        impl_->gpu = impl_->renderer.load([this](const char* name) {
            return dlsym(impl_->glLibrary, name);
        });
        if (impl_->gpu) {
            impl_->disableVsync();
            activeGpu = impl_;
            setGpuRenderer(&renderGpu);
            const std::string gpuTitle = title + " - " + impl_->renderer.name();
            impl_->api.storeName(impl_->display, impl_->window, gpuTitle.c_str());
        } else {
            impl_->glx.makeCurrent(impl_->display, 0, nullptr);
        }
    }
    return true;
}

void NativeWindow::close() {
    if (impl_) {
        if (activeGpu == impl_) {
            setGpuRenderer(nullptr);
            activeGpu = nullptr;
        }
        impl_->shutdown();
        delete impl_;
        impl_ = nullptr;
    }
}

bool NativeWindow::poll(std::vector<WindowEvent>& events) {
    events.clear();
    if (!impl_ || !impl_->open) return false;
    while (impl_->api.pending(impl_->display) > 0) {
        XEvent event{};
        impl_->api.nextEvent(impl_->display, &event);
        if (event.type == 2 || event.type == 3) {
            bool repeat = false;
            if (event.type == 3 && impl_->api.pending(impl_->display) > 0) {
                XEvent next{};
                impl_->api.peekEvent(impl_->display, &next);
                if (next.type == 2 && next.xkey.keycode == event.xkey.keycode && next.xkey.time == event.xkey.time) {
                    impl_->api.nextEvent(impl_->display, &next);
                    repeat = true;
                }
            }
            WindowEvent key{event.type == 2 || repeat ? WindowEvent::Type::KeyDown : WindowEvent::Type::KeyUp,
                            keySymbol(impl_->api, event.xkey)};
            key.repeat = repeat;
            events.push_back(key);
        } else if (event.type == 6) {
            WindowEvent moved{WindowEvent::Type::MouseMove};
            moved.x = event.xmotion.x;
            moved.y = event.xmotion.y;
            if (impl_->captured) {
                if (impl_->skipMotion) {
                    impl_->skipMotion = false;
                } else {
                    moved.dx = moved.x - impl_->width / 2;
                    moved.dy = moved.y - impl_->height / 2;
                    if (moved.dx != 0 || moved.dy != 0) {
                        impl_->skipMotion = true;
                        impl_->api.warpPointer(impl_->display, 0, impl_->window, 0, 0, 0, 0,
                                               impl_->width / 2, impl_->height / 2);
                    }
                }
            } else {
                moved.dx = moved.x - impl_->lastX;
                moved.dy = moved.y - impl_->lastY;
            }
            impl_->lastX = moved.x;
            impl_->lastY = moved.y;
            events.push_back(moved);
        } else if (event.type == 4 || event.type == 5) {
            const int button = static_cast<int>(event.xbutton.button);
            if (event.type == 4 && (button == 4 || button == 5)) {
                WindowEvent wheel{WindowEvent::Type::Wheel};
                wheel.wheel = button == 4 ? 1 : -1;
                events.push_back(wheel);
            } else {
                WindowEvent click{event.type == 4 ? WindowEvent::Type::MouseDown : WindowEvent::Type::MouseUp};
                click.button = button;
                click.x = event.xbutton.x;
                click.y = event.xbutton.y;
                events.push_back(click);
            }
        } else if (event.type == 22) {
            impl_->width = std::max(1, event.xconfigure.width);
            impl_->height = std::max(1, event.xconfigure.height);
            if (impl_->captured) {
                impl_->skipMotion = true;
                impl_->api.warpPointer(impl_->display, 0, impl_->window, 0, 0, 0, 0,
                                       impl_->width / 2, impl_->height / 2);
            }
        } else if (event.type == 9) {
            events.push_back({WindowEvent::Type::FocusLoss});
        } else if (event.type == 33 && event.xclient.data.l[0] == static_cast<long>(impl_->wmDelete)) {
            events.push_back({WindowEvent::Type::Close});
            impl_->open = false;
        } else if (event.type == 17) {
            events.push_back({WindowEvent::Type::Close});
            impl_->open = false;
        }
    }
    return impl_->open;
}

bool NativeWindow::present(const NativeFrame& frame) {
    if (!impl_ || !impl_->open || frame.width <= 0 || frame.height <= 0 || frame.rgb.empty()) return false;
    if (frame.width != impl_->width || frame.height != impl_->height) return true; // Resize is applied next frame.
    if (impl_->gpu) {
        if (!impl_->glx.makeCurrent(impl_->display, impl_->window, impl_->context)) return false;
        if (!impl_->renderer.present(frame)) return false;
        impl_->glx.swapBuffers(impl_->display, impl_->window);
        impl_->api.flush(impl_->display);
        return true;
    }
    const std::size_t count = static_cast<std::size_t>(frame.width) * frame.height;
    impl_->bgra.resize(count * 4);
    for (std::size_t i = 0; i < count; ++i) {
        impl_->bgra[i * 4] = frame.rgb[i * 3 + 2];
        impl_->bgra[i * 4 + 1] = frame.rgb[i * 3 + 1];
        impl_->bgra[i * 4 + 2] = frame.rgb[i * 3];
        impl_->bgra[i * 4 + 3] = 0;
    }
    XImage* image = impl_->api.createImage(impl_->display, impl_->visual,
        static_cast<unsigned int>(impl_->depth), 2, 0,
        reinterpret_cast<char*>(impl_->bgra.data()), frame.width, frame.height, 32, frame.width * 4);
    if (!image) return false;
    const bool ok = impl_->api.putImage(impl_->display, impl_->window, impl_->gc, image,
                                        0, 0, 0, 0, frame.width, frame.height) != 0;
    impl_->api.free(image);
    impl_->api.flush(impl_->display);
    return ok;
}

bool NativeWindow::save(const NativeFrame& frame, const std::string& path) {
    if (!impl_ || !impl_->open) return false;
    if (impl_->gpu) {
        if (!impl_->glx.makeCurrent(impl_->display, impl_->window, impl_->context)) return false;
        return impl_->renderer.capture(impl_->width, impl_->height, path);
    }
    return frame.save(path);
}

void NativeWindow::captureMouse(bool capture) {
    if (!impl_ || !impl_->display || impl_->captured == capture) return;
    impl_->captured = capture;
    if (capture) {
        impl_->api.grabPointer(impl_->display, impl_->window, 1, (1U << 2) | (1U << 3) | (1U << 6),
                               1, 1, impl_->window, 0, 0);
        impl_->skipMotion = true;
        impl_->api.warpPointer(impl_->display, 0, impl_->window, 0, 0, 0, 0,
                               impl_->width / 2, impl_->height / 2);
    } else {
        impl_->skipMotion = false;
        impl_->api.ungrabPointer(impl_->display, 0);
    }
    impl_->api.flush(impl_->display);
}

void NativeWindow::size(int& width, int& height) const {
    width = impl_ ? impl_->width : 480;
    height = impl_ ? impl_->height : 640;
}

bool NativeWindow::gpu() const { return impl_ && impl_->gpu; }

bool NativeWindow::opened() const { return impl_ && impl_->open; }

} // namespace gtc::native
