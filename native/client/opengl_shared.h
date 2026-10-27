#pragma once

#include "client/raster.h"

#include <functional>
#include <map>
#include <vector>

#if defined(_WIN32)
#define GTC_GL_CALL __stdcall
#else
#define GTC_GL_CALL
#endif

namespace gtc::native {
namespace gl {

using Enum = unsigned int;
using Bitfield = unsigned int;
using Boolean = unsigned char;
using Int = int;
using UInt = unsigned int;
using Sizei = int;
using Float = float;
using Double = double;

struct Api {
    void (GTC_GL_CALL *viewport)(Int, Int, Sizei, Sizei) = nullptr;
    void (GTC_GL_CALL *drawBuffer)(Enum) = nullptr;
    void (GTC_GL_CALL *readBuffer)(Enum) = nullptr;
    void (GTC_GL_CALL *clearColor)(Float, Float, Float, Float) = nullptr;
    void (GTC_GL_CALL *clear)(Bitfield) = nullptr;
    void (GTC_GL_CALL *enable)(Enum) = nullptr;
    void (GTC_GL_CALL *disable)(Enum) = nullptr;
    void (GTC_GL_CALL *depthMask)(Boolean) = nullptr;
    void (GTC_GL_CALL *depthFunc)(Enum) = nullptr;
    void (GTC_GL_CALL *shadeModel)(Enum) = nullptr;
    void (GTC_GL_CALL *matrixMode)(Enum) = nullptr;
    void (GTC_GL_CALL *loadIdentity)() = nullptr;
    void (GTC_GL_CALL *frustum)(Double, Double, Double, Double, Double, Double) = nullptr;
    void (GTC_GL_CALL *scalef)(Float, Float, Float) = nullptr;
    void (GTC_GL_CALL *lightModelfv)(Enum, const Float*) = nullptr;
    void (GTC_GL_CALL *lightModeli)(Enum, Int) = nullptr;
    void (GTC_GL_CALL *lightfv)(Enum, Enum, const Float*) = nullptr;
    void (GTC_GL_CALL *colorMaterial)(Enum, Enum) = nullptr;
    void (GTC_GL_CALL *materialfv)(Enum, Enum, const Float*) = nullptr;
    void (GTC_GL_CALL *materialf)(Enum, Enum, Float) = nullptr;
    void (GTC_GL_CALL *enableClientState)(Enum) = nullptr;
    void (GTC_GL_CALL *disableClientState)(Enum) = nullptr;
    void (GTC_GL_CALL *vertexPointer)(Int, Enum, Sizei, const void*) = nullptr;
    void (GTC_GL_CALL *normalPointer)(Enum, Sizei, const void*) = nullptr;
    void (GTC_GL_CALL *texCoordPointer)(Int, Enum, Sizei, const void*) = nullptr;
    void (GTC_GL_CALL *drawArrays)(Enum, Int, Sizei) = nullptr;
    void (GTC_GL_CALL *genTextures)(Sizei, UInt*) = nullptr;
    void (GTC_GL_CALL *deleteTextures)(Sizei, const UInt*) = nullptr;
    void (GTC_GL_CALL *bindTexture)(Enum, UInt) = nullptr;
    void (GTC_GL_CALL *texParameteri)(Enum, Enum, Int) = nullptr;
    void (GTC_GL_CALL *texImage2D)(Enum, Int, Int, Sizei, Sizei, Int, Enum, Enum, const void*) = nullptr;
    void (GTC_GL_CALL *texEnvi)(Enum, Enum, Int) = nullptr;
    void (GTC_GL_CALL *texSubImage2D)(Enum, Int, Int, Int, Sizei, Sizei, Enum, Enum, const void*) = nullptr;
    void (GTC_GL_CALL *blendFunc)(Enum, Enum) = nullptr;
    void (GTC_GL_CALL *pixelStorei)(Enum, Int) = nullptr;
    void (GTC_GL_CALL *readPixels)(Int, Int, Sizei, Sizei, Enum, Enum, void*) = nullptr;
    Enum (GTC_GL_CALL *getError)() = nullptr;
    void (GTC_GL_CALL *color3f)(Float, Float, Float) = nullptr;
    void (GTC_GL_CALL *alphaFunc)(Enum, Float) = nullptr;
    void (GTC_GL_CALL *rasterPos2i)(Int, Int) = nullptr;
    void (GTC_GL_CALL *drawPixels)(Sizei, Sizei, Enum, Enum, const void*) = nullptr;
};

bool loadApi(Api& api, const std::function<void*(const char*)>& symbol);

} // namespace gl

class OpenGlRenderer {
public:
    bool load(const std::function<void*(const char*)>& symbol);
    void shutdown();
    bool loaded() const { return loaded_; }
    bool render(const GpuScene& scene, NativeFrame& frame);
    bool present(const NativeFrame& frame);
    bool capture(int width, int height, const std::string& path);

private:
    gl::UInt texture(const Texture& source);

    gl::Api api_;
    std::map<const Texture*, gl::UInt> textures_;
    std::vector<unsigned char> bottomUp_;
    std::vector<unsigned char> overlay_;
    gl::UInt overlayTexture_ = 0;
    int overlayWidth_ = 0;
    int overlayHeight_ = 0;
    bool loaded_ = false;
};

} // namespace gtc::native

#undef GTC_GL_CALL
