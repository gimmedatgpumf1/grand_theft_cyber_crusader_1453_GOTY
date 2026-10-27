#include "client/window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>

namespace gtc::native {
namespace {

float displayChannel(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return static_cast<float>(value <= 0.0031308 ? value * 12.92 :
        1.055 * std::pow(value, 1.0 / 2.4) - 0.055);
}

} // namespace

struct NativeWindow::Impl {
    HWND window = nullptr;
    HDC gpuDevice = nullptr;
    HGLRC gpuContext = nullptr;
    bool gpu = false;
    bool open = false;
    bool captured = false;
    double lastX = 240;
    double lastY = 320;
    std::vector<WindowEvent> events;
    std::vector<unsigned char> bgra;
    std::vector<unsigned char> bottomUp;
    std::map<const Texture*, unsigned> textures;
    int width = 480;
    int height = 640;

    static int key(WPARAM value) {
        if (value >= 'A' && value <= 'Z') return static_cast<int>(value - 'A' + 'a');
        if (value >= '0' && value <= '9') return static_cast<int>(value);
        switch (value) {
            case VK_ESCAPE: return kKeyEscape;
            case VK_F1: return kKeyF1;
            case VK_F12: return kKeyF12;
            case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return kKeyShift;
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
        gpu = true;
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glShadeModel(GL_SMOOTH);
        glEnable(GL_NORMALIZE);
        glDisable(GL_CULL_FACE);
        return true;
    }

    void shutdownGpu() {
        if (gpuDevice && gpuContext) {
            wglMakeCurrent(gpuDevice, gpuContext);
            for (const auto& [source, id] : textures) {
                const unsigned texture = id;
                glDeleteTextures(1, &texture);
            }
            wglMakeCurrent(gpuDevice, nullptr);
        }
        if (gpuContext) wglDeleteContext(gpuContext);
        if (gpuDevice && window) ReleaseDC(window, gpuDevice);
        textures.clear();
        gpuContext = nullptr;
        gpuDevice = nullptr;
        gpu = false;
    }

    unsigned texture(const Texture& source) {
        const auto found = textures.find(&source);
        if (found != textures.end()) return found->second;
        unsigned id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source.width, source.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, source.pixels.data());
        textures.emplace(&source, id);
        return id;
    }

    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProc(hwnd, message, wParam, lParam);
        switch (message) {
            case WM_KEYDOWN: case WM_SYSKEYDOWN:
                self->events.push_back({WindowEvent::Type::KeyDown, key(wParam)});
                return 0;
            case WM_KEYUP: case WM_SYSKEYUP:
                self->events.push_back({WindowEvent::Type::KeyUp, key(wParam)});
                return 0;
            case WM_MOUSEMOVE: {
                WindowEvent moved{WindowEvent::Type::MouseMove};
                moved.x = GET_X_LPARAM(lParam);
                moved.y = GET_Y_LPARAM(lParam);
                if (self->captured) {
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
                               message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ? 2 : 3;
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
                if (self->gpu) glViewport(0, 0, self->width, self->height);
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
    NativeWindow::Impl& gpu = *activeGpu;
    if (!wglMakeCurrent(gpu.gpuDevice, gpu.gpuContext)) return false;
    glViewport(0, 0, scene.width, scene.height);
    glDrawBuffer(GL_BACK);
    glClearColor(displayChannel(scene.background.r), displayChannel(scene.background.g),
                 displayChannel(scene.background.b), 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    constexpr double near = 0.035;
    const double horizontal = near * scene.width / (2 * scene.focal);
    const double vertical = near * scene.height / (2 * scene.focal);
    glFrustum(-horizontal, horizontal, -vertical, vertical, near, 70.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    const GLfloat ambient[] = {0.38F, 0.38F, 0.38F, 1.0F};
    const GLfloat keyDiffuse[] = {0.78F, 0.78F, 0.78F, 1.0F};
    const GLfloat fillDiffuse[] = {0.24F, 0.24F, 0.24F, 1.0F};
    const GLfloat keyPosition[] = {-0.45F, 0.78F, -0.55F, 0.0F};
    const GLfloat fillPosition[] = {0.65F, 0.25F, -0.35F, 0.0F};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, keyDiffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, keyPosition);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, fillDiffuse);
    glLightfv(GL_LIGHT1, GL_POSITION, fillPosition);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glScalef(1, 1, -1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    for (const GpuBatch& batch : scene.batches) {
        if (batch.vertices.empty()) continue;
        const Material& material = batch.material;
        const bool textured = material.texture && !material.texture->pixels.empty() &&
            (material.kind == Material::Kind::Skin || material.kind == Material::Kind::Hair ||
             material.kind == Material::Kind::Floor || material.kind == Material::Kind::Textured);
        if (textured) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, gpu.texture(*material.texture));
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        if (material.kind == Material::Kind::Floor && textured) glColor3f(1, 1, 1);
        else glColor3f(displayChannel(material.color.r), displayChannel(material.color.g), displayChannel(material.color.b));
        const GLfloat specular[] = {static_cast<float>(material.specular), static_cast<float>(material.specular),
                                    static_cast<float>(material.specular), 1.0F};
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 42);
        if (material.unlit) glDisable(GL_LIGHTING);
        else {
            glEnable(GL_LIGHTING);
            glEnable(GL_LIGHT0);
            glEnable(GL_LIGHT1);
        }
        if (material.kind == Material::Kind::Hair) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.32F);
        } else {
            glDisable(GL_ALPHA_TEST);
        }
        const GpuVertex* vertices = batch.vertices.data();
        glVertexPointer(3, GL_FLOAT, sizeof(GpuVertex), &vertices->x);
        glNormalPointer(GL_FLOAT, sizeof(GpuVertex), &vertices->nx);
        glTexCoordPointer(2, GL_FLOAT, sizeof(GpuVertex), &vertices->u);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    frame.width = scene.width;
    frame.height = scene.height;
    frame.rgb.resize(static_cast<std::size_t>(scene.width) * scene.height * 3);
    gpu.bottomUp.resize(frame.rgb.size());
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, scene.width, scene.height, GL_RGB, GL_UNSIGNED_BYTE, gpu.bottomUp.data());
    for (int y = 0; y < scene.height; ++y) {
        const unsigned char* source = gpu.bottomUp.data() + static_cast<std::size_t>(scene.height - 1 - y) * scene.width * 3;
        std::copy(source, source + static_cast<std::size_t>(scene.width) * 3,
                  frame.rgb.begin() + static_cast<std::size_t>(y) * scene.width * 3);
    }
    return glGetError() == GL_NO_ERROR;
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
    RECT rectangle{0, 0, 480, 640};
    AdjustWindowRect(&rectangle, style, false);
    impl_->window = CreateWindowEx(0, description.lpszClassName, title.c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
        nullptr, nullptr, instance, impl_);
    if (!impl_->window) {
        error = "Could not create the resizable native Win32 window";
        close();
        return false;
    }
    impl_->bgra.resize(480U * 640U * 4U);
    impl_->open = true;
    if (impl_->initializeGpu()) {
        activeGpu = impl_;
        setGpuRenderer(&renderGpu);
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
        glViewport(0, 0, frame.width, frame.height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glRasterPos2i(-1, -1);
        impl_->bottomUp.resize(frame.rgb.size());
        for (int y = 0; y < frame.height; ++y) {
            const unsigned char* source = frame.rgb.data() + static_cast<std::size_t>(frame.height - 1 - y) * frame.width * 3;
            std::copy(source, source + static_cast<std::size_t>(frame.width) * 3,
                      impl_->bottomUp.begin() + static_cast<std::size_t>(y) * frame.width * 3);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glDrawBuffer(GL_BACK);
        glDrawPixels(frame.width, frame.height, GL_RGB, GL_UNSIGNED_BYTE, impl_->bottomUp.data());
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
    width = impl_ ? impl_->width : 480;
    height = impl_ ? impl_->height : 640;
}

bool NativeWindow::gpu() const { return impl_ && impl_->gpu; }

bool NativeWindow::opened() const { return impl_ && impl_->open; }

} // namespace gtc::native
