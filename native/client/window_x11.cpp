#include "client/window.h"

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
using Atom = unsigned long;
using Bool = int;
using Time = unsigned long;
using KeySym = unsigned long;

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
    int x, y, x_root, y_root;
    unsigned int state;
    char is_hint;
    Bool same_screen;
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
    XClientMessageEvent xclient;
    long pad[24];
};

struct XImage;

struct Api {
    void* library = nullptr;
    Display* (*openDisplay)(const char*) = nullptr;
    int (*closeDisplay)(Display*) = nullptr;
    int (*defaultScreen)(Display*) = nullptr;
    Window (*rootWindow)(Display*, int) = nullptr;
    Visual* (*defaultVisual)(Display*, int) = nullptr;
    int (*defaultDepth)(Display*, int) = nullptr;
    Window (*createWindow)(Display*, Window, int, int, unsigned int, unsigned int,
                           unsigned int, unsigned long, unsigned long) = nullptr;
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
    KeySym (*lookupKeysym)(XKeyEvent*, int) = nullptr;
    Atom (*internAtom)(Display*, const char*, Bool) = nullptr;
    int (*setProtocols)(Display*, Window, Atom*, int) = nullptr;
    int (*grabPointer)(Display*, Window, Bool, unsigned int, int, int, Window, unsigned long, Time) = nullptr;
    int (*ungrabPointer)(Display*, Time) = nullptr;
    int (*warpPointer)(Display*, Window, Window, int, int, unsigned int, unsigned int, int, int) = nullptr;
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
    Display* display = nullptr;
    Window window = 0;
    GC gc = nullptr;
    Atom wmDelete = 0;
    bool open = false;
    bool captured = false;
    double lastX = 240;
    double lastY = 320;
    std::vector<unsigned char> bgra;

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
        GTC_LOAD_X11(createWindow, "XCreateSimpleWindow");
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
        GTC_LOAD_X11(lookupKeysym, "XLookupKeysym");
        GTC_LOAD_X11(internAtom, "XInternAtom");
        GTC_LOAD_X11(setProtocols, "XSetWMProtocols");
        GTC_LOAD_X11(grabPointer, "XGrabPointer");
        GTC_LOAD_X11(ungrabPointer, "XUngrabPointer");
        GTC_LOAD_X11(warpPointer, "XWarpPointer");
#undef GTC_LOAD_X11
        return true;
    }

    void shutdown() {
        if (display) {
            if (captured) api.ungrabPointer(display, 0);
            if (gc) api.freeGC(display, gc);
            api.closeDisplay(display);
        }
        if (api.library) dlclose(api.library);
        display = nullptr;
        gc = nullptr;
        api = {};
        open = false;
        captured = false;
    }
};

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
    impl_->window = impl_->api.createWindow(impl_->display, root, 0, 0, 480, 640, 0, 0, 0x111815);
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
    return true;
}

void NativeWindow::close() {
    if (impl_) {
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
            events.push_back({event.type == 2 ? WindowEvent::Type::KeyDown : WindowEvent::Type::KeyUp,
                              keySymbol(impl_->api, event.xkey)});
        } else if (event.type == 6) {
            WindowEvent moved{WindowEvent::Type::MouseMove};
            moved.x = event.xmotion.x;
            moved.y = event.xmotion.y;
            if (impl_->captured) {
                moved.dx = moved.x - 240;
                moved.dy = moved.y - 320;
                if (moved.dx != 0 || moved.dy != 0) {
                    impl_->api.warpPointer(impl_->display, 0, impl_->window, 0, 0, 0, 0, 240, 320);
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
    if (!impl_ || !impl_->open || frame.width != 480 || frame.height != 640) return false;
    for (std::size_t i = 0; i < 480U * 640U; ++i) {
        impl_->bgra[i * 4] = frame.rgb[i * 3 + 2];
        impl_->bgra[i * 4 + 1] = frame.rgb[i * 3 + 1];
        impl_->bgra[i * 4 + 2] = frame.rgb[i * 3];
        impl_->bgra[i * 4 + 3] = 0;
    }
    const int screen = impl_->api.defaultScreen(impl_->display);
    XImage* image = impl_->api.createImage(impl_->display, impl_->api.defaultVisual(impl_->display, screen),
        static_cast<unsigned int>(impl_->api.defaultDepth(impl_->display, screen)), 2, 0,
        reinterpret_cast<char*>(impl_->bgra.data()), 480, 640, 32, 480 * 4);
    if (!image) return false;
    const bool ok = impl_->api.putImage(impl_->display, impl_->window, impl_->gc, image,
                                        0, 0, 0, 0, 480, 640) != 0;
    impl_->api.free(image);
    impl_->api.flush(impl_->display);
    return ok;
}

void NativeWindow::captureMouse(bool capture) {
    if (!impl_ || !impl_->display || impl_->captured == capture) return;
    impl_->captured = capture;
    if (capture) {
        impl_->api.grabPointer(impl_->display, impl_->window, 1, (1U << 2) | (1U << 3) | (1U << 6),
                               1, 1, impl_->window, 0, 0);
        impl_->api.warpPointer(impl_->display, 0, impl_->window, 0, 0, 0, 0, 240, 320);
    } else {
        impl_->api.ungrabPointer(impl_->display, 0);
    }
    impl_->api.flush(impl_->display);
}

bool NativeWindow::opened() const { return impl_ && impl_->open; }

} // namespace gtc::native
