#pragma once

#include "client/planar_gpu_scene.h"
#include <algorithm>
#include <cmath>

namespace gtc::native::seesaw {
// Playground dimensions are metres; its moving parts use a separate planar GPU simulation in 2D mode.
inline constexpr Vec3 pivot{0,0.75,0};
inline constexpr Vec3 beamSize{2.0,0.10,0.20};
inline constexpr Vec3 smallSize{0.24,0.24,0.24};
inline constexpr Vec3 largeSize{0.40,0.40,0.40};
inline constexpr Vec3 smallCenter{pivot.x-0.78,pivot.y+0.05+0.12+0.015,pivot.z};
inline constexpr Vec3 largeCenter{pivot.x+0.78,pivot.y+0.05+0.20+0.015,pivot.z};
inline constexpr std::uint32_t beamId=50000,smallId=50001,largeId=50002;
inline constexpr double density=500.0; // kg/m³, the same density for the three boxes

inline void bind(GpuPlanarRig& rig,std::uint32_t id,std::uint32_t body,Vec3 center) {
    GpuPlanarBinding b; b.objectId=id; b.body=body;
    b.localFromMesh={1,0,0,0,0,1,0,0,0,0,1,0,float(-center.x),float(-center.y),float(-center.z),1};
    rig.bindings.push_back(b);
}
inline void drawBoxMesh(Rasterizer& raster,Vec3 center,Vec3 size,const Material& material) {
    Vec3 h=size*0.5;
    std::vector<Vec3> p={center+Vec3{-h.x,-h.y,-h.z},center+Vec3{h.x,-h.y,-h.z},
        center+Vec3{h.x,h.y,-h.z},center+Vec3{-h.x,h.y,-h.z},center+Vec3{-h.x,-h.y,h.z},
        center+Vec3{h.x,-h.y,h.z},center+Vec3{h.x,h.y,h.z},center+Vec3{-h.x,h.y,h.z}};
    const std::vector<std::array<int,3>> faces={{0,3,2},{0,2,1},{4,5,6},{4,6,7},
        {0,4,7},{0,7,3},{1,2,6},{1,6,5},{0,1,5},{0,5,4},{3,7,6},{3,6,2}};
    raster.mesh(p,faces,material);
}
inline void draw(Rasterizer& raster,const Camera& camera) {
    Material support; support.color=colorHex("#78827c"); support.constantLighting=true;
    // Triangular prism with its base on the floor and the board axle above it.
    Vec3 p[6]={{pivot.x-0.33,0,pivot.z-0.14},{pivot.x+0.33,0,pivot.z-0.14},{pivot.x,0.70,pivot.z-0.14},
               {pivot.x-0.33,0,pivot.z+0.14},{pivot.x+0.33,0,pivot.z+0.14},{pivot.x,0.70,pivot.z+0.14}};
    constexpr int faces[8][3]={{0,2,1},{3,4,5},{0,1,4},{0,4,3},{0,3,5},{0,5,2},{1,2,5},{1,5,4}};
    raster.setObject(0,{0,0},0);
    for (const auto& face:faces) {
        Vec3 a=p[face[0]],b=p[face[1]],c=p[face[2]],n=normalized(cross(b-a,c-a));
        raster.triangle({cameraVertex(camera,a,n,a),cameraVertex(camera,b,n,b),cameraVertex(camera,c,n,c)},support);
    }
    Material beam; beam.color=colorHex("#c8a56b");
    Material smallBox; smallBox.color=colorHex("#66aed2");
    Material largeBox; largeBox.color=colorHex("#df765a");
    raster.setObject(beamId,{0,0},0); drawBoxMesh(raster,pivot,beamSize,beam);
    raster.setObject(smallId,{0,0},0); drawBoxMesh(raster,smallCenter,smallSize,smallBox);
    raster.setObject(largeId,{0,0},0); drawBoxMesh(raster,largeCenter,largeSize,largeBox);
    raster.setObject(0,{0,0},0);
}
} // namespace gtc::native::seesaw
