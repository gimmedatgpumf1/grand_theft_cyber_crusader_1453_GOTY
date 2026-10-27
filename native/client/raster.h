#pragma once

#include "native.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gtc::native {

struct Color {
    double r = 0;
    double g = 0;
    double b = 0;
};

Color operator+(Color a, Color b);
Color operator*(Color a, double b);
Color colorHex(const char* value);
Color mix(Color a, Color b, double amount);

struct Texture {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

const Texture& loadTexture(const std::string& path);

struct NativeFrame {
    int width = 480;
    int height = 640;
    std::vector<unsigned char> rgb;

    bool save(const std::string& path) const;
    void rectangle(int x, int y, int w, int h, Color color);
};

struct Material {
    enum class Kind { Solid, Skin, Hair, Floor, Puddle } kind = Kind::Solid;
    Color color{0.5, 0.5, 0.5};
    Color second{0.5, 0.5, 0.5};
    const Texture* texture = nullptr;
    const Texture* young = nullptr;
    const Texture* old = nullptr;
    const Texture* normalYoung = nullptr;
    const Texture* normalOld = nullptr;
    double age = 25;
    double specular = 0.06;
};

struct Camera {
    Vec3 position;
    Vec3 target;
    Vec3 up{0, 1, 0};
    double fovDegrees = 50;
    int width = 480;
    int height = 640;

    Vec3 view(Vec3 point) const;
    Vec3 viewDirection(Vec3 direction) const;
    double focal() const;

private:
    void basis() const;
    mutable bool basisReady = false;
    mutable Vec3 forward;
    mutable Vec3 right;
    mutable Vec3 vertical;
};

struct RenderVertex {
    Vec3 view;
    Vec3 normal;
    Vec3 source;
    Vec2 uv;
};

RenderVertex cameraVertex(const Camera& camera, Vec3 point, Vec3 normal, Vec3 source, Vec2 uv = {});

class Rasterizer {
public:
    Rasterizer(int width, int height, Color background, double focal);

    void triangle(const std::array<RenderVertex, 3>& vertices, const Material& material);
    NativeFrame finish() const;

private:
    int width_;
    int height_;
    double focal_;
    std::vector<double> depth_;
    std::vector<Color> pixels_;
};

void drawBox(Rasterizer& rasterizer, const Camera& camera, Vec3 center, Vec3 size,
             const Material& material);
void drawEllipsoid(Rasterizer& rasterizer, const Camera& camera, Vec3 center, Vec3 radii,
                   const Material& material, int segments = 16, int rings = 10);
void drawText(NativeFrame& frame, int x, int y, const std::string& text, Color color, int scale = 3);

} // namespace gtc::native
