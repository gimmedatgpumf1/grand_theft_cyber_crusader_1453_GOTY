#include "client/opengl_shared.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace gtc::native {
namespace {

float displayChannel(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return static_cast<float>(value <= 0.0031308 ? value * 12.92 :
        1.055 * std::pow(value, 1.0 / 2.4) - 0.055);
}

float litDisplayChannel(double value) {
    const double x = std::max(0.0, value);
    const double tone = std::clamp((x * (2.51 * x + 0.03)) /
        (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
    return displayChannel(tone);
}

namespace c {
constexpr gl::Enum noError = 0;
constexpr gl::Enum triangles = 0x0004;
constexpr gl::Enum triangleStrip = 0x0005;
constexpr gl::Enum less = 0x0201;
constexpr gl::Enum greater = 0x0204;
constexpr gl::Enum srcAlpha = 0x0302;
constexpr gl::Enum oneMinusSrcAlpha = 0x0303;
constexpr gl::Enum front = 0x0404;
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
constexpr gl::Enum nearest = 0x2600;
constexpr gl::Enum linear = 0x2601;
constexpr gl::Enum clamp = 0x2900;
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
constexpr gl::Enum arrayBuffer = 0x8892;
constexpr gl::Enum dynamicDraw = 0x88E8;
constexpr gl::Enum vertexShader = 0x8B31;
constexpr gl::Enum fragmentShader = 0x8B30;
constexpr gl::Enum compileStatus = 0x8B81;
constexpr gl::Enum linkStatus = 0x8B82;
constexpr gl::Enum texture0 = 0x84C0;
constexpr gl::Enum texture1 = 0x84C1;
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
constexpr gl::Enum vendor = 0x1F00;
constexpr gl::Enum renderer = 0x1F01;
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
    GTC_LOAD_GL(loadMatrixd, "glLoadMatrixd");
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
    GTC_LOAD_GL(texSubImage2D, "glTexSubImage2D");
    GTC_LOAD_GL(blendFunc, "glBlendFunc");
    GTC_LOAD_GL(pixelStorei, "glPixelStorei");
    GTC_LOAD_GL(readPixels, "glReadPixels");
    GTC_LOAD_GL(getError, "glGetError");
    GTC_LOAD_GL(getString, "glGetString");
    GTC_LOAD_GL(color3f, "glColor3f");
    GTC_LOAD_GL(alphaFunc, "glAlphaFunc");
    GTC_LOAD_GL(rasterPos2i, "glRasterPos2i");
    GTC_LOAD_GL(drawPixels, "glDrawPixels");
#undef GTC_LOAD_GL
    return true;
}

bool loadBufferApi(BufferApi& api, const std::function<void*(const char*)>& symbol) {
    if (!symbol) return false;
#define GTC_LOAD_BUFFER(member, name) \
    do { api.member = reinterpret_cast<decltype(api.member)>(symbol(name)); \
         if (!api.member) { api = {}; return false; } } while (false)
    GTC_LOAD_BUFFER(genBuffers, "glGenBuffers");
    GTC_LOAD_BUFFER(deleteBuffers, "glDeleteBuffers");
    GTC_LOAD_BUFFER(bindBuffer, "glBindBuffer");
    GTC_LOAD_BUFFER(bufferData, "glBufferData");
#undef GTC_LOAD_BUFFER
    return true;
}

bool loadShaderApi(ShaderApi& api, const std::function<void*(const char*)>& symbol) {
    if (!symbol) return false;
#define GTC_LOAD_SHADER(member, name) \
    do { api.member = reinterpret_cast<decltype(api.member)>(symbol(name)); \
         if (!api.member) { api = {}; return false; } } while (false)
    GTC_LOAD_SHADER(createShader, "glCreateShader");
    GTC_LOAD_SHADER(shaderSource, "glShaderSource");
    GTC_LOAD_SHADER(compileShader, "glCompileShader");
    GTC_LOAD_SHADER(getShaderiv, "glGetShaderiv");
    GTC_LOAD_SHADER(getShaderInfoLog, "glGetShaderInfoLog");
    GTC_LOAD_SHADER(deleteShader, "glDeleteShader");
    GTC_LOAD_SHADER(createProgram, "glCreateProgram");
    GTC_LOAD_SHADER(attachShader, "glAttachShader");
    GTC_LOAD_SHADER(linkProgram, "glLinkProgram");
    GTC_LOAD_SHADER(getProgramiv, "glGetProgramiv");
    GTC_LOAD_SHADER(getProgramInfoLog, "glGetProgramInfoLog");
    GTC_LOAD_SHADER(deleteProgram, "glDeleteProgram");
    GTC_LOAD_SHADER(useProgram, "glUseProgram");
    GTC_LOAD_SHADER(getUniformLocation, "glGetUniformLocation");
    GTC_LOAD_SHADER(uniform1i, "glUniform1i");
    GTC_LOAD_SHADER(uniform1f, "glUniform1f");
    GTC_LOAD_SHADER(uniform3f, "glUniform3f");
    GTC_LOAD_SHADER(activeTexture, "glActiveTexture");
#undef GTC_LOAD_SHADER
    return true;
}

} // namespace gl

bool OpenGlRenderer::load(const std::function<void*(const char*)>& symbol) {
    loaded_ = false;
    debug_ = std::getenv("GTC_GL_DEBUG") != nullptr;
    textures_.clear();
    if (!gl::loadApi(api_, symbol)) return false;
    vbo_ = gl::loadBufferApi(buffers_, symbol);
    shaders_ = gl::loadShaderApi(shaderApi_, symbol) && buildShader();
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
    if (loaded_ && vbo_ && !bufferIds_.empty()) {
        buffers_.deleteBuffers(static_cast<gl::Sizei>(bufferIds_.size()), bufferIds_.data());
    }
    bufferIds_.clear();
    uploadedScene_ = nullptr;
    uploadedRevision_ = 0;
    vbo_ = false;
    if (loaded_ && shaderProgram_) shaderApi_.deleteProgram(shaderProgram_);
    shaderProgram_ = 0;
    shaders_ = false;
    if (loaded_) {
        for (gl::UInt& texture : overlayTextures_) {
            if (texture) api_.deleteTextures(1, &texture);
            texture = 0;
        }
    }
    textures_.clear();
    bottomUp_.clear();
    overlay_.clear();
    overlayIndex_ = 0;
    overlayWidth_ = overlayHeight_ = 0;
    api_ = {};
    loaded_ = false;
}

bool OpenGlRenderer::checkError() {
    return !debug_ || api_.getError() == c::noError;
}

bool OpenGlRenderer::buildShader() {
    static const char vertexSource[] = R"GLSL(#version 110
varying vec3 vPosition;
varying vec3 vNormal;
varying vec2 vUv;
varying vec3 vColor;
void main() {
    vPosition = (gl_ModelViewMatrix * gl_Vertex).xyz;
    vNormal = gl_NormalMatrix * gl_Normal;
    vUv = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;
    vColor = gl_Color.rgb;
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
)GLSL";
    static const char fragmentSource[] = R"GLSL(#version 110
uniform sampler2D baseMap;
uniform sampler2D normalMap;
uniform int textured;
uniform int normalMapped;
uniform int unlit;
uniform int hair;
uniform float specularAmount;
uniform vec3 fogColor;
varying vec3 vPosition;
varying vec3 vNormal;
varying vec2 vUv;
varying vec3 vColor;
vec3 toLinear(vec3 value) {
    vec3 usePower = step(vec3(0.04045), value);
    return mix(value / 12.92, pow((value + 0.055) / 1.055, vec3(2.4)), usePower);
}
vec3 toSrgb(vec3 value) {
    vec3 usePower = step(vec3(0.0031308), value);
    return mix(value * 12.92, 1.055 * pow(value, vec3(1.0 / 2.4)) - 0.055, usePower);
}
vec3 toneMap(vec3 value) {
    return clamp((value * (2.51 * value + 0.03)) /
                 (value * (2.43 * value + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec4 detail = textured != 0 ? texture2D(baseMap, vUv) : vec4(1.0);
    if (hair != 0 && detail.a < 0.32) discard;
    vec3 base = vColor * (textured != 0 ? toLinear(detail.rgb) : vec3(1.0));
    vec3 normal = normalize(vNormal);
    if (normalMapped != 0) {
        vec3 edge1 = dFdx(vPosition), edge2 = dFdy(vPosition);
        vec2 uv1 = dFdx(vUv), uv2 = dFdy(vUv);
        float denominator = uv1.x * uv2.y - uv2.x * uv1.y;
        if (abs(denominator) > 0.00000001) {
            vec3 tangent = normalize((edge1 * uv2.y - edge2 * uv1.y) / denominator);
            vec3 bitangent = normalize((edge2 * uv1.x - edge1 * uv2.x) / denominator);
            vec3 mapped = texture2D(normalMap, vUv).xyz * 2.0 - 1.0;
            normal = normalize(normal + tangent * (mapped.x * 0.32) + bitangent * (mapped.y * 0.32));
        }
    }
    if (dot(normal, vPosition) > 0.0) normal = -normal;
    vec3 color = base;
    if (unlit == 0) {
        vec3 key = normalize(vec3(-0.45, 0.78, -0.55));
        vec3 fill = normalize(vec3(0.65, 0.25, -0.35));
        vec3 toward = normalize(-vPosition);
        float diffuse = 0.38 + 0.78 * max(0.0, dot(normal, key)) +
                        0.24 * max(0.0, dot(normal, fill));
        float rim = 0.10 * pow(1.0 - max(0.0, dot(normal, toward)), 3.0);
        float specular = specularAmount * pow(max(0.0, dot(normal, normalize(key + toward))), 42.0);
        color = base * (diffuse + rim) + vec3(specular);
        float fog = clamp((-vPosition.z - 20.0) / 18.0, 0.0, 1.0);
        color = mix(color, fogColor, fog);
    }
    gl_FragColor = vec4(toSrgb(toneMap(color)), 1.0);
}
)GLSL";
    auto compile = [&](gl::Enum type, const char* source) {
        const gl::UInt shader = shaderApi_.createShader(type);
        const gl::Int length = static_cast<gl::Int>(std::strlen(source));
        shaderApi_.shaderSource(shader, 1, &source, &length);
        shaderApi_.compileShader(shader);
        gl::Int status = 0;
        shaderApi_.getShaderiv(shader, c::compileStatus, &status);
        if (!status && debug_) {
            char log[2048]{};
            shaderApi_.getShaderInfoLog(shader, static_cast<gl::Sizei>(sizeof(log)), nullptr, log);
            std::cerr << "Native GLSL compile: " << log << '\n';
        }
        if (!status) {
            shaderApi_.deleteShader(shader);
            return gl::UInt{0};
        }
        return shader;
    };
    const gl::UInt vertex = compile(c::vertexShader, vertexSource);
    const gl::UInt fragment = compile(c::fragmentShader, fragmentSource);
    if (!vertex || !fragment) {
        if (vertex) shaderApi_.deleteShader(vertex);
        if (fragment) shaderApi_.deleteShader(fragment);
        return false;
    }
    shaderProgram_ = shaderApi_.createProgram();
    shaderApi_.attachShader(shaderProgram_, vertex);
    shaderApi_.attachShader(shaderProgram_, fragment);
    shaderApi_.linkProgram(shaderProgram_);
    shaderApi_.deleteShader(vertex);
    shaderApi_.deleteShader(fragment);
    gl::Int status = 0;
    shaderApi_.getProgramiv(shaderProgram_, c::linkStatus, &status);
    if (!status) {
        if (debug_) {
            char log[2048]{};
            shaderApi_.getProgramInfoLog(shaderProgram_, static_cast<gl::Sizei>(sizeof(log)), nullptr, log);
            std::cerr << "Native GLSL link: " << log << '\n';
        }
        shaderApi_.deleteProgram(shaderProgram_);
        shaderProgram_ = 0;
        return false;
    }
    shaderApi_.useProgram(shaderProgram_);
    shaderApi_.uniform1i(shaderApi_.getUniformLocation(shaderProgram_, "baseMap"), 0);
    shaderApi_.uniform1i(shaderApi_.getUniformLocation(shaderProgram_, "normalMap"), 1);
    shaderUniforms_.textured = shaderApi_.getUniformLocation(shaderProgram_, "textured");
    shaderUniforms_.normalMapped = shaderApi_.getUniformLocation(shaderProgram_, "normalMapped");
    shaderUniforms_.unlit = shaderApi_.getUniformLocation(shaderProgram_, "unlit");
    shaderUniforms_.hair = shaderApi_.getUniformLocation(shaderProgram_, "hair");
    shaderUniforms_.specular = shaderApi_.getUniformLocation(shaderProgram_, "specularAmount");
    shaderUniforms_.fogColor = shaderApi_.getUniformLocation(shaderProgram_, "fogColor");
    shaderApi_.useProgram(0);
    return true;
}

std::string OpenGlRenderer::name() const {
    if (!loaded_) return {};
    const gl::UByte* vendorName = api_.getString(c::vendor);
    const gl::UByte* rendererName = api_.getString(c::renderer);
    const std::string vendorText = vendorName ? reinterpret_cast<const char*>(vendorName) : "Unknown vendor";
    const std::string rendererText = rendererName ? reinterpret_cast<const char*>(rendererName) : "Unknown renderer";
    return vendorText + " / " + rendererText + (shaders_ ? " / GLSL" : " / fixed-function");
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
    const bool refresh = scene.revision == 0 || uploadedScene_ != &scene || uploadedRevision_ != scene.revision;
    api_.viewport(0, 0, scene.width, scene.height);
    api_.drawBuffer(c::back);
    const auto sceneChannel = [&](double value) {
        return shaders_ ? litDisplayChannel(value) : displayChannel(value);
    };
    api_.clearColor(sceneChannel(scene.background.r), sceneChannel(scene.background.g),
                    sceneChannel(scene.background.b), 1);
    api_.clear(c::colorBufferBit | c::depthBufferBit);
    if (shaderApi_.activeTexture) shaderApi_.activeTexture(c::texture0);
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
    api_.loadMatrixd(scene.viewMatrix.data());
    api_.scalef(1, 1, -1);
    api_.enableClientState(c::vertexArray);
    api_.enableClientState(c::normalArray);
    api_.enableClientState(c::textureCoordArray);
    if (shaders_) {
        shaderApi_.useProgram(shaderProgram_);
        shaderApi_.uniform3f(shaderUniforms_.fogColor, static_cast<float>(scene.background.r),
            static_cast<float>(scene.background.g), static_cast<float>(scene.background.b));
    } else if (shaderApi_.useProgram) {
        shaderApi_.useProgram(0);
    }
    for (std::size_t batchIndex = 0; batchIndex < scene.batches.size(); ++batchIndex) {
        const GpuBatch& batch = scene.batches[batchIndex];
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
        else api_.color3f(sceneChannel(material.color.r), sceneChannel(material.color.g), sceneChannel(material.color.b));
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
        if (shaders_) {
            const bool normalMapped = material.kind == Material::Kind::Skin && material.normalYoung &&
                !material.normalYoung->pixels.empty();
            if (normalMapped) {
                shaderApi_.activeTexture(c::texture1);
                api_.bindTexture(c::texture2D, texture(*material.normalYoung));
                shaderApi_.activeTexture(c::texture0);
            }
            shaderApi_.uniform1i(shaderUniforms_.textured, textured ? 1 : 0);
            shaderApi_.uniform1i(shaderUniforms_.normalMapped, normalMapped ? 1 : 0);
            shaderApi_.uniform1i(shaderUniforms_.unlit, material.unlit ? 1 : 0);
            shaderApi_.uniform1i(shaderUniforms_.hair, material.kind == Material::Kind::Hair ? 1 : 0);
            shaderApi_.uniform1f(shaderUniforms_.specular, static_cast<float>(material.specular));
        }
        const GpuVertex* vertices = batch.vertices.data();
        bool uploaded = false;
        if (vbo_) {
            bool upload = refresh;
            while (bufferIds_.size() <= batchIndex) {
                gl::UInt id = 0;
                buffers_.genBuffers(1, &id);
                bufferIds_.push_back(id);
                upload = true;
            }
            const gl::UInt id = bufferIds_[batchIndex];
            if (id) {
                buffers_.bindBuffer(c::arrayBuffer, id);
                if (upload) buffers_.bufferData(c::arrayBuffer,
                    static_cast<std::ptrdiff_t>(batch.vertices.size() * sizeof(GpuVertex)), vertices, c::dynamicDraw);
                api_.vertexPointer(3, c::floatType, sizeof(GpuVertex), nullptr);
                api_.normalPointer(c::floatType, sizeof(GpuVertex), reinterpret_cast<const void*>(sizeof(gl::Float) * 3));
                api_.texCoordPointer(2, c::floatType, sizeof(GpuVertex), reinterpret_cast<const void*>(sizeof(gl::Float) * 6));
                uploaded = true;
            }
        }
        if (!uploaded) {
            api_.vertexPointer(3, c::floatType, sizeof(GpuVertex), &vertices->x);
            api_.normalPointer(c::floatType, sizeof(GpuVertex), &vertices->nx);
            api_.texCoordPointer(2, c::floatType, sizeof(GpuVertex), &vertices->u);
        }
        api_.drawArrays(c::triangles, 0, static_cast<gl::Sizei>(batch.vertices.size()));
    }
    if (vbo_) buffers_.bindBuffer(c::arrayBuffer, 0);
    if (shaders_) shaderApi_.useProgram(0);
    if (shaderApi_.activeTexture) shaderApi_.activeTexture(c::texture0);
    api_.disableClientState(c::vertexArray);
    api_.disableClientState(c::normalArray);
    api_.disableClientState(c::textureCoordArray);
    api_.disable(c::lighting);
    api_.disable(c::texture2D);
    api_.disable(c::alphaTest);
    uploadedScene_ = &scene;
    uploadedRevision_ = scene.revision;
    frame.width = scene.width;
    frame.height = scene.height;
    const std::size_t pixels = static_cast<std::size_t>(scene.width) * scene.height;
    frame.rgb.assign(pixels * 3, 0);
    frame.alpha.assign(pixels, 0);
    frame.uiScale = 1.0;
    frame.uiOffsetX = frame.uiOffsetY = 0.0;
    return checkError();
}

bool OpenGlRenderer::present(const NativeFrame& frame) {
    const std::size_t pixels = static_cast<std::size_t>(frame.width) * frame.height;
    if (!loaded_ || frame.width <= 0 || frame.height <= 0 ||
        frame.rgb.size() < pixels * 3 || frame.alpha.size() < pixels) return false;
    if (shaders_) shaderApi_.useProgram(0);
    overlay_.resize(pixels * 4);
    for (std::size_t i = 0; i < pixels; ++i) {
        overlay_[i * 4] = frame.rgb[i * 3];
        overlay_[i * 4 + 1] = frame.rgb[i * 3 + 1];
        overlay_[i * 4 + 2] = frame.rgb[i * 3 + 2];
        overlay_[i * 4 + 3] = frame.alpha[i];
    }
    api_.drawBuffer(c::back);
    api_.viewport(0, 0, frame.width, frame.height);
    api_.disable(c::depthTest);
    api_.depthMask(0);
    api_.disable(c::lighting);
    api_.disable(c::alphaTest);
    api_.enable(c::blend);
    api_.blendFunc(c::srcAlpha, c::oneMinusSrcAlpha);
    api_.matrixMode(c::projection);
    api_.loadIdentity();
    api_.matrixMode(c::modelView);
    api_.loadIdentity();
    api_.matrixMode(c::textureMatrix);
    api_.loadIdentity();
    api_.matrixMode(c::modelView);
    api_.enable(c::texture2D);
    if (overlayWidth_ != frame.width || overlayHeight_ != frame.height) {
        for (gl::UInt& texture : overlayTextures_) {
            if (texture) api_.deleteTextures(1, &texture);
            texture = 0;
        }
        overlayWidth_ = frame.width;
        overlayHeight_ = frame.height;
        overlayIndex_ = 0;
    }
    gl::UInt& overlayTexture = overlayTextures_[overlayIndex_];
    overlayIndex_ = (overlayIndex_ + 1) % 3;
    const bool allocate = !overlayTexture;
    if (allocate) {
        api_.genTextures(1, &overlayTexture);
        api_.bindTexture(c::texture2D, overlayTexture);
        api_.texParameteri(c::texture2D, c::textureMinFilter, c::nearest);
        api_.texParameteri(c::texture2D, c::textureMagFilter, c::nearest);
        api_.texParameteri(c::texture2D, c::textureWrapS, c::clamp);
        api_.texParameteri(c::texture2D, c::textureWrapT, c::clamp);
    } else {
        api_.bindTexture(c::texture2D, overlayTexture);
    }
    api_.texEnvi(c::textureEnv, c::textureEnvMode, c::modulate);
    api_.color3f(1, 1, 1);
    api_.pixelStorei(c::unpackAlignment, 1);
    if (allocate) {
        api_.texImage2D(c::texture2D, 0, c::rgba, frame.width, frame.height, 0,
                        c::rgba, c::unsignedByte, overlay_.data());
    } else {
        api_.texSubImage2D(c::texture2D, 0, 0, 0, frame.width, frame.height,
                           c::rgba, c::unsignedByte, overlay_.data());
    }
    static const gl::Float positions[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    static const gl::Float coordinates[] = {0, 1, 1, 1, 0, 0, 1, 0};
    api_.enableClientState(c::vertexArray);
    api_.enableClientState(c::textureCoordArray);
    api_.disableClientState(c::normalArray);
    api_.vertexPointer(2, c::floatType, 0, positions);
    api_.texCoordPointer(2, c::floatType, 0, coordinates);
    api_.drawArrays(c::triangleStrip, 0, 4);
    api_.disableClientState(c::vertexArray);
    api_.disableClientState(c::textureCoordArray);
    api_.disable(c::blend);
    api_.disable(c::texture2D);
    api_.depthMask(1);
    return checkError();
}

bool OpenGlRenderer::capture(int width, int height, const std::string& path) {
    if (!loaded_ || width <= 0 || height <= 0) return false;
    NativeFrame frame;
    frame.width = width;
    frame.height = height;
    frame.rgb.resize(static_cast<std::size_t>(width) * height * 3);
    bottomUp_.resize(frame.rgb.size());
    api_.readBuffer(c::front);
    api_.pixelStorei(c::packAlignment, 1);
    api_.readPixels(0, 0, width, height, c::rgb, c::unsignedByte, bottomUp_.data());
    for (int y = 0; y < height; ++y) {
        const unsigned char* source = bottomUp_.data() + static_cast<std::size_t>(height - 1 - y) * width * 3;
        std::copy(source, source + static_cast<std::size_t>(width) * 3,
                  frame.rgb.begin() + static_cast<std::size_t>(y) * width * 3);
    }
    return api_.getError() == c::noError && frame.save(path);
}

} // namespace gtc::native
