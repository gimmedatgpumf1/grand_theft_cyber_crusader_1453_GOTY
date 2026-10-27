#include "client/window.h"
#include "client/opengl_shared.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>

#include <algorithm>
#include <cctype>

namespace gtc::native {

struct NativeWindow::Impl {
    HWND window = nullptr;
    HDC gpuDevice = nullptr;
    HGLRC gpuContext = nullptr;
    bool gpu = false;
    bool open = false;
    bool captured = false;
    double lastX = kDefaultWindowWidth * 0.5;
    double lastY = kDefaultWindowHeight * 0.5;
    std::vector<WindowEvent> events;
    std::vector<unsigned char> bgra;
    OpenGlRenderer renderer;
    HMODULE openGl = nullptr;
    int width = kDefaultWindowWidth;
    int height = kDefaultWindowHeight;

    static int key(WPARAM value) {
        if (value >= 'A' && value <= 'Z') return static_cast<int>(value - 'A' + 'a');
        if (value >= '0' && value <= '9') return static_cast<int>(value);
        switch (value) {
            case VK_ESCAPE: return kKeyEscape;
            case VK_F1: return kKeyF1;
            case VK_F12: return kKeyF12;
            case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return kKeyShift;
            case VK_CONTROL: case VK_LCONTROL: return kKeyLeftControl;
            case VK_MENU: case VK_LMENU: case VK_RMENU: return kKeyAlt;
            case VK_CAPITAL: return kKeyCapsLock;
            case VK_RETURN: return kKeyEnter;
            case VK_LEFT: return kKeyLeft;
            case VK_RIGHT: return kKeyRight;
            case VK_UP: return kKeyUp;
            case VK_DOWN: return kKeyDown;
            case VK_PRIOR: return kKeyPageUp;
            case VK_NEXT: return kKeyPageDown;
            case VK_TAB: return kKeyTab;
            case VK_SPACE: return ' ';
            default: return 0;
        }
    }

    void centerMouse() {
        POINT point{width / 2, height / 2};
        ClientToScreen(window, &point);
        SetCursorPos(point.x, point.y);
    }

    bool initializeGpu() {
        gpuDevice = GetDC(window);
        if (!gpuDevice) return false;
        PIXELFORMATDESCRIPTOR format{};
        format.nSize = sizeof(format);
        format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA;
        format.cColorBits = 32;
        format.cDepthBits = 24;
        format.iLayerType = PFD_MAIN_PLANE;
        const int selected = ChoosePixelFormat(gpuDevice, &format);
        if (!selected || !SetPixelFormat(gpuDevice, selected, &format)) return false;
        gpuContext = wglCreateContext(gpuDevice);
        if (!gpuContext || !wglMakeCurrent(gpuDevice, gpuContext)) return false;
        openGl = LoadLibraryW(L"opengl32.dll");
        if (!openGl) return false;
        gpu = renderer.load([this](const char* name) -> void* {
            if (void* address = reinterpret_cast<void*>(GetProcAddress(openGl, name))) return address;
            return reinterpret_cast<void*>(wglGetProcAddress(name));
        });
        if (gpu) {
            using SwapInterval = BOOL (WINAPI*)(int);
            if (const PROC address = wglGetProcAddress("wglSwapIntervalEXT")) {
                reinterpret_cast<SwapInterval>(address)(0);
            }
        }
        return gpu;
    }

    void shutdownGpu() {
        if (gpuDevice && gpuContext) {
            wglMakeCurrent(gpuDevice, gpuContext);
            renderer.shutdown();
            wglMakeCurrent(gpuDevice, nullptr);
        }
        if (gpuContext) wglDeleteContext(gpuContext);
        if (gpuDevice && window) ReleaseDC(window, gpuDevice);
        gpuContext = nullptr;
        gpuDevice = nullptr;
        gpu = false;
        if (openGl) FreeLibrary(openGl);
        openGl = nullptr;
    }


    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProc(hwnd, message, wParam, lParam);
        switch (message) {
            case WM_KEYDOWN: case WM_SYSKEYDOWN: {
                WindowEvent pressed{WindowEvent::Type::KeyDown, key(wParam)};
                pressed.repeat = (lParam & (1LL << 30)) != 0;
                self->events.push_back(pressed);
                return 0;
            }
            case WM_KEYUP: case WM_SYSKEYUP:
                self->events.push_back({WindowEvent::Type::KeyUp, key(wParam)});
                return 0;
            case WM_MOUSEMOVE: {
                WindowEvent moved{WindowEvent::Type::MouseMove};
                moved.x = GET_X_LPARAM(lParam);
                moved.y = GET_Y_LPARAM(lParam);
                if (self->captured) {
                    // A synthetic recenter has exactly zero delta. Do not skip an
                    // arbitrary queued event after a slow frame.
                    moved.dx = moved.x - self->width / 2;
                    moved.dy = moved.y - self->height / 2;
                    if (moved.dx != 0 || moved.dy != 0) self->centerMouse();
                } else {
                    moved.dx = moved.x - self->lastX;
                    moved.dy = moved.y - self->lastY;
                }
                self->lastX = moved.x;
                self->lastY = moved.y;
                self->events.push_back(moved);
                return 0;
            }
            case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
            case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
                const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
                WindowEvent click{down ? WindowEvent::Type::MouseDown : WindowEvent::Type::MouseUp};
                click.button = message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ? 1 :
                               message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ? 3 : 2;
                click.x = GET_X_LPARAM(lParam);
                click.y = GET_Y_LPARAM(lParam);
                self->events.push_back(click);
                return 0;
            }
            case WM_MOUSEWHEEL: {
                WindowEvent wheel{WindowEvent::Type::Wheel};
                wheel.wheel = GET_WHEEL_DELTA_WPARAM(wParam) / 120.0;
                self->events.push_back(wheel);
                return 0;
            }
            case WM_SIZE:
                self->width = std::max(1, static_cast<int>(LOWORD(lParam)));
                self->height = std::max(1, static_cast<int>(HIWORD(lParam)));
                if (self->captured) self->centerMouse();
                return 0;
            case WM_KILLFOCUS:
                self->events.push_back({WindowEvent::Type::FocusLoss});
                return 0;
            case WM_CLOSE:
                self->events.push_back({WindowEvent::Type::Close});
                self->open = false;
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                self->open = false;
                return 0;
            default:
                return DefWindowProc(hwnd, message, wParam, lParam);
        }
    }
};

namespace {

NativeWindow::Impl* activeGpu = nullptr;

bool renderGpu(const GpuScene& scene, NativeFrame& frame) {
    if (!activeGpu || !activeGpu->gpu) return false;
    if (!wglMakeCurrent(activeGpu->gpuDevice, activeGpu->gpuContext)) return false;
    return activeGpu->renderer.render(scene, frame);
}

} // namespace

NativeWindow::NativeWindow() = default;
NativeWindow::~NativeWindow() { close(); }

bool NativeWindow::open(const std::string& title, std::string& error) {
    close();
    impl_ = new Impl;
    const HINSTANCE instance = GetModuleHandle(nullptr);
    WNDCLASSEX description{};
    description.cbSize = sizeof(description);
    description.hInstance = instance;
    description.lpfnWndProc = Impl::procedure;
    description.lpszClassName = "GTC1453NativeWindow";
    description.hCursor = LoadCursor(nullptr, IDC_ARROW);
    description.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    if (!RegisterClassEx(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = "Could not register the native Win32 window class";
        close();
        return false;
    }
    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rectangle{0, 0, kDefaultWindowWidth, kDefaultWindowHeight};
    AdjustWindowRect(&rectangle, style, false);
    impl_->window = CreateWindowEx(0, description.lpszClassName, title.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        nullptr, nullptr, instance, impl_);
    if (!impl_->window) {
        error = "Could not create the resizable native Win32 window";
        close();
        return false;
    }
    impl_->bgra.resize(static_cast<std::size_t>(kDefaultWindowWidth) * kDefaultWindowHeight * 4U);
    impl_->open = true;
    if (impl_->initializeGpu()) {
        activeGpu = impl_;
        setGpuRenderer(&renderGpu);
        const std::string gpuTitle = title + " - " + impl_->renderer.name();
        SetWindowTextA(impl_->window, gpuTitle.c_str());
    }
    ShowWindow(impl_->window, SW_SHOW);
    UpdateWindow(impl_->window);
    return true;
}

void NativeWindow::close() {
    if (impl_) {
        if (activeGpu == impl_) {
            setGpuRenderer(nullptr);
            activeGpu = nullptr;
        }
        impl_->shutdownGpu();
        if (impl_->window) DestroyWindow(impl_->window);
        delete impl_;
        impl_ = nullptr;
    }
}

bool NativeWindow::poll(std::vector<WindowEvent>& events) {
    events.clear();
    if (!impl_ || !impl_->open) return false;
    MSG message;
    while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) impl_->open = false;
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    events.swap(impl_->events);
    impl_->events.clear();
    return impl_->open;
}

bool NativeWindow::present(const NativeFrame& frame) {
    if (!impl_ || !impl_->open || frame.width <= 0 || frame.height <= 0 || frame.rgb.empty()) return false;
    RECT client{};
    GetClientRect(impl_->window, &client);
    impl_->width = std::max(1, static_cast<int>(client.right - client.left));
    impl_->height = std::max(1, static_cast<int>(client.bottom - client.top));
    const std::size_t count = static_cast<std::size_t>(frame.width) * frame.height;
    if (impl_->gpu) {
        if (!wglMakeCurrent(impl_->gpuDevice, impl_->gpuContext)) return false;
        if (frame.width != impl_->width || frame.height != impl_->height) return true;
        if (!impl_->renderer.present(frame)) return false;
        return SwapBuffers(impl_->gpuDevice) != 0;
    }
    impl_->bgra.resize(count * 4);
    for (std::size_t i = 0; i < count; ++i) {
        impl_->bgra[i * 4] = frame.rgb[i * 3 + 2];
        impl_->bgra[i * 4 + 1] = frame.rgb[i * 3 + 1];
        impl_->bgra[i * 4 + 2] = frame.rgb[i * 3];
        impl_->bgra[i * 4 + 3] = 255;
    }
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = frame.width;
    bitmap.bmiHeader.biHeight = -frame.height;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;
    const HDC device = GetDC(impl_->window);
    SetStretchBltMode(device, HALFTONE);
    const int result = StretchDIBits(device, 0, 0, impl_->width, impl_->height, 0, 0, frame.width, frame.height,
                                     impl_->bgra.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(impl_->window, device);
    return result != 0;
}

bool NativeWindow::save(const NativeFrame& frame, const std::string& path) {
    if (!impl_ || !impl_->open) return false;
    if (impl_->gpu) {
        if (!wglMakeCurrent(impl_->gpuDevice, impl_->gpuContext)) return false;
        return impl_->renderer.capture(impl_->width, impl_->height, path);
    }
    return frame.save(path);
}

void NativeWindow::captureMouse(bool capture) {
    if (!impl_ || !impl_->window || impl_->captured == capture) return;
    impl_->captured = capture;
    if (capture) {
        SetCapture(impl_->window);
        ShowCursor(false);
        impl_->centerMouse();
    } else {
        ReleaseCapture();
        ShowCursor(true);
    }
}

void NativeWindow::size(int& width, int& height) const {
    width = impl_ ? impl_->width : kDefaultWindowWidth;
    height = impl_ ? impl_->height : kDefaultWindowHeight;
}

bool NativeWindow::gpu() const { return impl_ && impl_->gpu; }

bool NativeWindow::opened() const { return impl_ && impl_->open; }

} // namespace gtc::native
