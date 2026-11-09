#pragma once

#include "client/skeleton_gpu_rig.h"
#include <algorithm>
#include <cmath>

namespace gtc::native::seesaw {
// Playground dimensions are metres; its moving parts use the same GPU
// bodies, contacts, gravity and joint solver as the skeleton room.
inline constexpr Vec3 pivot{0,0.75,0};
inline constexpr Vec3 beamSize{2.0,0.10,0.20};
inline constexpr Vec3 smallSize{0.24,0.24,0.24};
inline constexpr Vec3 largeSize{0.40,0.40,0.40};
inline constexpr Vec3 smallCenter{pivot.x-0.78,pivot.y+0.05+0.12+0.015,pivot.z};
inline constexpr Vec3 largeCenter{pivot.x+0.78,pivot.y+0.05+0.20+0.015,pivot.z};
inline constexpr std::uint32_t beamId=50000,smallId=50001,largeId=50002;
inline constexpr double density=500.0; // kg/m³, the same density for the three boxes

inline std::array<float,4> pack(Vec3 p,float w=0) { return {float(p.x),float(p.y),float(p.z),w}; }
inline gpu_physics::Body box(GpuSkeletonRig& rig,Vec3 center,Vec3 size) {
    const double mass=density*size.x*size.y*size.z;
    gpu_physics::Body b; b.positionMass=pack(center,float(1.0/mass));
    b.inverseInertia={float(12.0/(mass*(size.y*size.y+size.z*size.z))),
                      float(12.0/(mass*(size.x*size.x+size.z*size.z))),
                      float(12.0/(mass*(size.x*size.x+size.y*size.y))),0};
    // The existing rigid solver uses sphere proxies, not actual box contacts.
    // Fill each box with overlapping spheres whose outside bounds match it.
    constexpr double radius=0.05;
    const int nx=std::max(1,int(std::ceil((size.x-2*radius)/(2*radius)))+1);
    const int ny=std::max(1,int(std::ceil((size.y-2*radius)/(2*radius)))+1);
    const int nz=std::max(1,int(std::ceil((size.z-2*radius)/(2*radius)))+1);
    const auto coord=[](int i,int n,double extent,double r) { return n==1 ? 0.0 : -extent*0.5+r+(extent-2*r)*i/(n-1); };
    b.samples[0]=static_cast<std::uint32_t>(rig.spheres.size());
    for (int x=0;x<nx;++x) for (int y=0;y<ny;++y) for (int z=0;z<nz;++z) {
        Vec3 p{coord(x,nx,size.x,radius),coord(y,ny,size.y,radius),coord(z,nz,size.z,radius)};
        rig.spheres.push_back(pack(p,float(radius)));
        b.inverseInertia[3]=std::max(b.inverseInertia[3],float(length(p)+radius+0.0001));
        b.bounds[0]=std::max(b.bounds[0],float(std::abs(p.x)+radius));
        b.bounds[1]=std::max(b.bounds[1],float(std::abs(p.y)+radius));
        b.bounds[2]=std::max(b.bounds[2],float(std::abs(p.z)+radius));
    }
    b.samples[1]=static_cast<std::uint32_t>(rig.spheres.size()-b.samples[0]);
    return b;
}
inline void bind(GpuSkeletonRig& rig,std::uint32_t id,std::uint32_t body,Vec3 center) {
    GpuSkeletonBinding b; b.objectId=id; b.body=body;
    b.localFromMesh={1,0,0,0,0,1,0,0,0,0,1,0,float(-center.x),float(-center.y),float(-center.z),1};
    rig.bindings.push_back(b);
}
inline void append(GpuSkeletonRig& rig) {
    // The support is an immovable body. Its sphere proxies approximate the
    // triangular prism; the hinge keeps the board attached at the apex.
    gpu_physics::Body base; base.positionMass=pack({pivot.x,0.35,pivot.z});
    base.samples[0]=static_cast<std::uint32_t>(rig.spheres.size());
    constexpr float r=0.075f;
    for (int y=0;y<5;++y) {
        const float h=0.075f+0.13f*y, width=0.33f*(1.0f-h/0.75f);
        for (int x=-2;x<=2;++x) for (int z=-1;z<=1;++z) {
            const float xx=0.13f*x,zz=0.075f*z;
            if (std::abs(xx)>width) continue;
            Vec3 p{xx,h-0.35,zz}; rig.spheres.push_back(pack(p,r));
            base.inverseInertia[3]=std::max(base.inverseInertia[3],float(length(p)+r+0.0001));
            base.bounds[0]=std::max(base.bounds[0],float(std::abs(p.x)+r));
            base.bounds[1]=std::max(base.bounds[1],float(std::abs(p.y)+r));
            base.bounds[2]=std::max(base.bounds[2],float(std::abs(p.z)+r));
        }
    }
    base.samples[1]=static_cast<std::uint32_t>(rig.spheres.size()-base.samples[0]);
    const auto root=static_cast<std::uint32_t>(rig.bodies.size()); rig.bodies.push_back(base);
    const auto plank=static_cast<std::uint32_t>(rig.bodies.size()); rig.bodies.push_back(box(rig,pivot,beamSize));
    gpu_physics::Joint hinge; hinge.bodies={root,plank,1,0};
    hinge.parentAnchor=pack({0,0.40,0}); hinge.childAnchor=pack({0,0,0});
    hinge.parentAxis=pack({0,0,1},-3.14159265f); hinge.childAxis=pack({0,0,1},3.14159265f);
    hinge.parentTangent=hinge.childTangent=pack({1,0,0}); rig.joints.push_back(hinge);
    bind(rig,beamId,plank,pivot);
    const auto smallBox=static_cast<std::uint32_t>(rig.bodies.size()); rig.bodies.push_back(box(rig,smallCenter,smallSize));
    const auto largeBox=static_cast<std::uint32_t>(rig.bodies.size()); rig.bodies.push_back(box(rig,largeCenter,largeSize));
    bind(rig,smallId,smallBox,smallCenter); bind(rig,largeId,largeBox,largeCenter);
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
