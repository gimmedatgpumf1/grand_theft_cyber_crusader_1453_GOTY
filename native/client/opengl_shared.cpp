#include "client/opengl_shared.h"

#include <algorithm>
#include <cmath>

namespace gtc::native {
namespace {

float displayChannel(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return static_cast<float>(value <= 0.0031308 ? value * 12.92 :
        1.055 * std::pow(value, 1.0 / 2.4) - 0.055);
}

namespace c {
constexpr gl::Enum noError = 0;
constexpr gl::Enum triangles = 0x0004;
constexpr gl::Enum less = 0x0201;
constexpr gl::Enum greater = 0x0204;
constexpr gl::Enum back = 0x0405;
constexpr gl::Enum frontAndBack = 0x0408;
constexpr gl::Enum depthBufferBit = 0x00000100;
constexpr gl::Enum colorBufferBit = 0x00004000;
constexpr gl::Enum floatType = 0x1406;
constexpr gl::Enum unsignedByte = 0x1401;
constexpr gl::Enum shininess = 0x1601;
constexpr gl::Enum ambientAndDiffuse = 0x1602;
constexpr gl::Enum modelView = 0x1700;
constexpr gl::Enum projection = 0x1701;
constexpr gl::Enum textureMatrix = 0x1702;
constexpr gl::Enum rgb = 0x1907;
constexpr gl::Enum rgba = 0x1908;
constexpr gl::Enum smooth = 0x1D01;
constexpr gl::Enum modulate = 0x2100;
constexpr gl::Enum textureEnv = 0x2300;
constexpr gl::Enum textureEnvMode = 0x2200;
constexpr gl::Enum linear = 0x2601;
constexpr gl::Enum textureMagFilter = 0x2800;
constexpr gl::Enum textureMinFilter = 0x2801;
constexpr gl::Enum textureWrapS = 0x2802;
constexpr gl::Enum textureWrapT = 0x2803;
constexpr gl::Enum repeat = 0x2901;
constexpr gl::Enum light0 = 0x4000;
constexpr gl::Enum light1 = 0x4001;
constexpr gl::Enum vertexArray = 0x8074;
constexpr gl::Enum normalArray = 0x8075;
constexpr gl::Enum textureCoordArray = 0x8078;
constexpr gl::Enum unpackAlignment = 0x0CF5;
constexpr gl::Enum packAlignment = 0x0D05;
constexpr gl::Enum cullFace = 0x0B44;
constexpr gl::Enum lighting = 0x0B50;
constexpr gl::Enum lightModelTwoSide = 0x0B52;
constexpr gl::Enum lightModelAmbient = 0x0B53;
constexpr gl::Enum colorMaterial = 0x0B57;
constexpr gl::Enum depthTest = 0x0B71;
constexpr gl::Enum alphaTest = 0x0BC0;
constexpr gl::Enum blend = 0x0BE2;
constexpr gl::Enum normalize = 0x0BA1;
constexpr gl::Enum texture2D = 0x0DE1;
constexpr gl::Enum ambient = 0x1200;
constexpr gl::Enum diffuse = 0x1201;
constexpr gl::Enum specular = 0x1202;
constexpr gl::Enum position = 0x1203;
} // namespace c

} // namespace

namespace gl {

bool loadApi(Api& api, const std::function<void*(const char*)>& symbol) {
    if (!symbol) return false;
#define GTC_LOAD_GL(member, name) \
    do { api.member = reinterpret_cast<decltype(api.member)>(symbol(name)); \
         if (!api.member) { api = {}; return false; } } while (false)
    GTC_LOAD_GL(viewport, "glViewport");
    GTC_LOAD_GL(drawBuffer, "glDrawBuffer");
    GTC_LOAD_GL(readBuffer, "glReadBuffer");
    GTC_LOAD_GL(clearColor, "glClearColor");
    GTC_LOAD_GL(clear, "glClear");
    GTC_LOAD_GL(enable, "glEnable");
    GTC_LOAD_GL(disable, "glDisable");
    GTC_LOAD_GL(depthMask, "glDepthMask");
    GTC_LOAD_GL(depthFunc, "glDepthFunc");
    GTC_LOAD_GL(shadeModel, "glShadeModel");
    GTC_LOAD_GL(matrixMode, "glMatrixMode");
    GTC_LOAD_GL(loadIdentity, "glLoadIdentity");
    GTC_LOAD_GL(frustum, "glFrustum");
    GTC_LOAD_GL(scalef, "glScalef");
    GTC_LOAD_GL(lightModelfv, "glLightModelfv");
    GTC_LOAD_GL(lightModeli, "glLightModeli");
    GTC_LOAD_GL(lightfv, "glLightfv");
    GTC_LOAD_GL(colorMaterial, "glColorMaterial");
    GTC_LOAD_GL(materialfv, "glMaterialfv");
    GTC_LOAD_GL(materialf, "glMaterialf");
    GTC_LOAD_GL(enableClientState, "glEnableClientState");
    GTC_LOAD_GL(disableClientState, "glDisableClientState");
    GTC_LOAD_GL(vertexPointer, "glVertexPointer");
    GTC_LOAD_GL(normalPointer, "glNormalPointer");
    GTC_LOAD_GL(texCoordPointer, "glTexCoordPointer");
    GTC_LOAD_GL(drawArrays, "glDrawArrays");
    GTC_LOAD_GL(genTextures, "glGenTextures");
    GTC_LOAD_GL(deleteTextures, "glDeleteTextures");
    GTC_LOAD_GL(bindTexture, "glBindTexture");
    GTC_LOAD_GL(texParameteri, "glTexParameteri");
    GTC_LOAD_GL(texImage2D, "glTexImage2D");
    GTC_LOAD_GL(texEnvi, "glTexEnvi");
    GTC_LOAD_GL(pixelStorei, "glPixelStorei");
    GTC_LOAD_GL(readPixels, "glReadPixels");
    GTC_LOAD_GL(getError, "glGetError");
    GTC_LOAD_GL(color3f, "glColor3f");
    GTC_LOAD_GL(alphaFunc, "glAlphaFunc");
    GTC_LOAD_GL(rasterPos2i, "glRasterPos2i");
    GTC_LOAD_GL(drawPixels, "glDrawPixels");
#undef GTC_LOAD_GL
    return true;
}

} // namespace gl

bool OpenGlRenderer::load(const std::function<void*(const char*)>& symbol) {
    loaded_ = false;
    textures_.clear();
    if (!gl::loadApi(api_, symbol)) return false;
    loaded_ = true;
    api_.enable(c::depthTest);
    api_.depthFunc(c::less);
    api_.shadeModel(c::smooth);
    api_.enable(c::normalize);
    api_.disable(c::cullFace);
    return true;
}

void OpenGlRenderer::shutdown() {
    if (loaded_ && !textures_.empty()) {
        for (const auto& [source, id] : textures_) api_.deleteTextures(1, &id);
    }
    textures_.clear();
    bottomUp_.clear();
    api_ = {};
    loaded_ = false;
}

gl::UInt OpenGlRenderer::texture(const Texture& source) {
    const auto found = textures_.find(&source);
    if (found != textures_.end()) return found->second;
    gl::UInt id = 0;
    api_.genTextures(1, &id);
    api_.bindTexture(c::texture2D, id);
    api_.texParameteri(c::texture2D, c::textureMinFilter, c::linear);
    api_.texParameteri(c::texture2D, c::textureMagFilter, c::linear);
    api_.texParameteri(c::texture2D, c::textureWrapS, c::repeat);
    api_.texParameteri(c::texture2D, c::textureWrapT, c::repeat);
    api_.pixelStorei(c::unpackAlignment, 1);
    api_.texImage2D(c::texture2D, 0, c::rgba, source.width, source.height, 0,
                    c::rgba, c::unsignedByte, source.pixels.data());
    textures_.emplace(&source, id);
    return id;
}

bool OpenGlRenderer::render(const GpuScene& scene, NativeFrame& frame) {
    if (!loaded_ || scene.width <= 0 || scene.height <= 0) return false;
    api_.viewport(0, 0, scene.width, scene.height);
    api_.drawBuffer(c::back);
    api_.clearColor(displayChannel(scene.background.r), displayChannel(scene.background.g),
                    displayChannel(scene.background.b), 1);
    api_.clear(c::colorBufferBit | c::depthBufferBit);
    api_.enable(c::depthTest);
    api_.depthMask(1);
    api_.disable(c::blend);
    api_.disable(c::alphaTest);
    api_.disable(c::texture2D);
    api_.matrixMode(c::projection);
    api_.loadIdentity();
    constexpr double nearPlane = 0.035;
    const double horizontal = nearPlane * scene.width / (2 * scene.focal);
    const double vertical = nearPlane * scene.height / (2 * scene.focal);
    api_.frustum(-horizontal, horizontal, -vertical, vertical, nearPlane, 70.0);
    api_.matrixMode(c::modelView);
    api_.loadIdentity();
    const gl::Float ambient[] = {0.38F, 0.38F, 0.38F, 1.0F};
    const gl::Float keyDiffuse[] = {0.78F, 0.78F, 0.78F, 1.0F};
    const gl::Float fillDiffuse[] = {0.24F, 0.24F, 0.24F, 1.0F};
    const gl::Float keyPosition[] = {-0.45F, 0.78F, -0.55F, 0.0F};
    const gl::Float fillPosition[] = {0.65F, 0.25F, -0.35F, 0.0F};
    api_.lightModelfv(c::lightModelAmbient, ambient);
    api_.lightModeli(c::lightModelTwoSide, 1);
    api_.lightfv(c::light0, c::diffuse, keyDiffuse);
    api_.lightfv(c::light0, c::position, keyPosition);
    api_.lightfv(c::light1, c::diffuse, fillDiffuse);
    api_.lightfv(c::light1, c::position, fillPosition);
    api_.colorMaterial(c::frontAndBack, c::ambientAndDiffuse);
    api_.enable(c::colorMaterial);
    api_.scalef(1, 1, -1);
    api_.enableClientState(c::vertexArray);
    api_.enableClientState(c::normalArray);
    api_.enableClientState(c::textureCoordArray);
    for (const GpuBatch& batch : scene.batches) {
        if (batch.vertices.empty()) continue;
        const Material& material = batch.material;
        api_.matrixMode(c::textureMatrix);
        api_.loadIdentity();
        if (material.kind == Material::Kind::Cloth) api_.scalef(5, 5, 1);
        api_.matrixMode(c::modelView);
        const bool textured = material.texture && !material.texture->pixels.empty() &&
            (material.kind == Material::Kind::Skin || material.kind == Material::Kind::Hair ||
             material.kind == Material::Kind::Cloth || material.kind == Material::Kind::Floor ||
             material.kind == Material::Kind::Textured);
        if (textured) {
            api_.enable(c::texture2D);
            api_.bindTexture(c::texture2D, texture(*material.texture));
            api_.texEnvi(c::textureEnv, c::textureEnvMode, c::modulate);
        } else {
            api_.disable(c::texture2D);
        }
        if (material.kind == Material::Kind::Floor && textured) api_.color3f(1, 1, 1);
        else api_.color3f(displayChannel(material.color.r), displayChannel(material.color.g), displayChannel(material.color.b));
        const gl::Float specular[] = {static_cast<float>(material.specular), static_cast<float>(material.specular),
                                      static_cast<float>(material.specular), 1.0F};
        api_.materialfv(c::frontAndBack, c::specular, specular);
        api_.materialf(c::frontAndBack, c::shininess, 42);
        if (material.unlit) api_.disable(c::lighting);
        else {
            api_.enable(c::lighting);
            api_.enable(c::light0);
            api_.enable(c::light1);
        }
        if (material.kind == Material::Kind::Hair) {
            api_.enable(c::alphaTest);
            api_.alphaFunc(c::greater, 0.32F);
        } else {
            api_.disable(c::alphaTest);
        }
        const GpuVertex* vertices = batch.vertices.data();
        api_.vertexPointer(3, c::floatType, sizeof(GpuVertex), &vertices->x);
        api_.normalPointer(c::floatType, sizeof(GpuVertex), &vertices->nx);
        api_.texCoordPointer(2, c::floatType, sizeof(GpuVertex), &vertices->u);
        api_.drawArrays(c::triangles, 0, static_cast<gl::Sizei>(batch.vertices.size()));
    }
    api_.disableClientState(c::vertexArray);
    api_.disableClientState(c::normalArray);
    api_.disableClientState(c::textureCoordArray);
    api_.disable(c::lighting);
    api_.disable(c::texture2D);
    api_.disable(c::alphaTest);
    frame.width = scene.width;
    frame.height = scene.height;
    frame.rgb.resize(static_cast<std::size_t>(scene.width) * scene.height * 3);
    bottomUp_.resize(frame.rgb.size());
    api_.pixelStorei(c::packAlignment, 1);
    api_.readBuffer(c::back);
    api_.readPixels(0, 0, scene.width, scene.height, c::rgb, c::unsignedByte, bottomUp_.data());
    for (int y = 0; y < scene.height; ++y) {
        const unsigned char* source = bottomUp_.data() + static_cast<std::size_t>(scene.height - 1 - y) * scene.width * 3;
        std::copy(source, source + static_cast<std::size_t>(scene.width) * 3,
                  frame.rgb.begin() + static_cast<std::size_t>(y) * scene.width * 3);
    }
    return api_.getError() == c::noError;
}

bool OpenGlRenderer::present(const NativeFrame& frame) {
    if (!loaded_ || frame.width <= 0 || frame.height <= 0 ||
        frame.rgb.size() < static_cast<std::size_t>(frame.width) * frame.height * 3) return false;
    api_.viewport(0, 0, frame.width, frame.height);
    api_.disable(c::depthTest);
    api_.disable(c::lighting);
    api_.disable(c::texture2D);
    api_.disable(c::alphaTest);
    api_.matrixMode(c::projection);
    api_.loadIdentity();
    api_.matrixMode(c::modelView);
    api_.loadIdentity();
    api_.rasterPos2i(-1, -1);
    bottomUp_.resize(frame.rgb.size());
    for (int y = 0; y < frame.height; ++y) {
        const unsigned char* source = frame.rgb.data() + static_cast<std::size_t>(frame.height - 1 - y) * frame.width * 3;
        std::copy(source, source + static_cast<std::size_t>(frame.width) * 3,
                  bottomUp_.begin() + static_cast<std::size_t>(y) * frame.width * 3);
    }
    api_.pixelStorei(c::unpackAlignment, 1);
    api_.drawBuffer(c::back);
    api_.drawPixels(frame.width, frame.height, c::rgb, c::unsignedByte, bottomUp_.data());
    return api_.getError() == c::noError;
}

} // namespace gtc::native
