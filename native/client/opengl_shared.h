#pragma once

#include "client/raster.h"

#include <cstddef>
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
using UByte = unsigned char;
using Sizei = int;
using Float = float;
using Double = double;
using Char = char;

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
    void (GTC_GL_CALL *loadMatrixd)(const Double*) = nullptr;
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
    const UByte* (GTC_GL_CALL *getString)(Enum) = nullptr;
    void (GTC_GL_CALL *color3f)(Float, Float, Float) = nullptr;
    void (GTC_GL_CALL *alphaFunc)(Enum, Float) = nullptr;
    void (GTC_GL_CALL *rasterPos2i)(Int, Int) = nullptr;
    void (GTC_GL_CALL *drawPixels)(Sizei, Sizei, Enum, Enum, const void*) = nullptr;
};

struct BufferApi {
    void (GTC_GL_CALL *genBuffers)(Sizei, UInt*) = nullptr;
    void (GTC_GL_CALL *deleteBuffers)(Sizei, const UInt*) = nullptr;
    void (GTC_GL_CALL *bindBuffer)(Enum, UInt) = nullptr;
    void (GTC_GL_CALL *bufferData)(Enum, std::ptrdiff_t, const void*, Enum) = nullptr;
};

struct ShaderApi {
    UInt (GTC_GL_CALL *createShader)(Enum) = nullptr;
    void (GTC_GL_CALL *shaderSource)(UInt, Sizei, const Char* const*, const Int*) = nullptr;
    void (GTC_GL_CALL *compileShader)(UInt) = nullptr;
    void (GTC_GL_CALL *getShaderiv)(UInt, Enum, Int*) = nullptr;
    void (GTC_GL_CALL *getShaderInfoLog)(UInt, Sizei, Sizei*, Char*) = nullptr;
    void (GTC_GL_CALL *deleteShader)(UInt) = nullptr;
    UInt (GTC_GL_CALL *createProgram)() = nullptr;
    void (GTC_GL_CALL *attachShader)(UInt, UInt) = nullptr;
    void (GTC_GL_CALL *linkProgram)(UInt) = nullptr;
    void (GTC_GL_CALL *getProgramiv)(UInt, Enum, Int*) = nullptr;
    void (GTC_GL_CALL *getProgramInfoLog)(UInt, Sizei, Sizei*, Char*) = nullptr;
    void (GTC_GL_CALL *deleteProgram)(UInt) = nullptr;
    void (GTC_GL_CALL *useProgram)(UInt) = nullptr;
    Int (GTC_GL_CALL *getUniformLocation)(UInt, const Char*) = nullptr;
    void (GTC_GL_CALL *uniform1i)(Int, Int) = nullptr;
    void (GTC_GL_CALL *uniform1f)(Int, Float) = nullptr;
    void (GTC_GL_CALL *uniform3f)(Int, Float, Float, Float) = nullptr;
    void (GTC_GL_CALL *activeTexture)(Enum) = nullptr;
};

bool loadApi(Api& api, const std::function<void*(const char*)>& symbol);
bool loadBufferApi(BufferApi& api, const std::function<void*(const char*)>& symbol);
bool loadShaderApi(ShaderApi& api, const std::function<void*(const char*)>& symbol);

} // namespace gl

class OpenGlRenderer {
public:
    bool load(const std::function<void*(const char*)>& symbol);
    void shutdown();
    bool loaded() const { return loaded_; }
    bool render(const GpuScene& scene, NativeFrame& frame);
    bool present(const NativeFrame& frame);
    bool capture(int width, int height, const std::string& path);
    std::string name() const;

private:
    gl::UInt texture(const Texture& source);
    bool checkError();
    bool buildShader();

    struct ShaderUniforms {
        gl::Int textured = -1, normalMapped = -1, unlit = -1, hair = -1;
        gl::Int specular = -1, fogColor = -1;
    };

    gl::Api api_;
    gl::BufferApi buffers_;
    gl::ShaderApi shaderApi_;
    std::vector<gl::UInt> bufferIds_;
    const GpuScene* uploadedScene_ = nullptr;
    std::uint64_t uploadedRevision_ = 0;
    bool vbo_ = false;
    std::map<const Texture*, gl::UInt> textures_;
    std::vector<unsigned char> bottomUp_;
    std::vector<unsigned char> overlay_;
    gl::UInt overlayTextures_[3] = {0, 0, 0};
    int overlayIndex_ = 0;
    int overlayWidth_ = 0;
    int overlayHeight_ = 0;
    gl::UInt shaderProgram_ = 0;
    ShaderUniforms shaderUniforms_;
    bool shaders_ = false;
    bool loaded_ = false;
    bool debug_ = false;
};

} // namespace gtc::native

#undef GTC_GL_CALL
