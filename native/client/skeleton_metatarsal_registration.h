#pragma once

#include "client/skeleton_finger_frame.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace gtc::native {
// Register the two scanned feet before their surfaces are averaged. The shaft
// directions are measured in 3D; their halfway direction keeps the individual
// metatarsals' arch and splay rather than forcing them parallel or horizontal.
// Each correction pivots at the tarsometatarsal contact and carries that ray's
// toe and sesamoids with it. The white source meshes and pose are untouched.
template<class Part>
bool registerComparisonMetatarsals(std::vector<Part>& parts, std::vector<JointRigidTransform>& frames) {
    frames.assign(parts.size(),{});
    auto find=[&](const std::string& name)->Part* {
        auto it=std::find_if(parts.begin(),parts.end(),[&](const Part& p){return p.name==name;});
        return it==parts.end() ? nullptr : &*it;
    };
    const auto centroid=[](const std::vector<Vec3>& points) {
        Vec3 c{}; for (Vec3 p:points) c+=p;
        return c/static_cast<double>(points.size());
    };
    const auto contact=[](const std::vector<Vec3>& a,const std::vector<Vec3>& b) {
        Vec3 p{}; double best=1e100;
        const auto as=std::max<std::size_t>(1,a.size()/192),bs=std::max<std::size_t>(1,b.size()/192);
        for (std::size_t i=0;i<a.size();i+=as) for (std::size_t j=0;j<b.size();j+=bs) {
            const Vec3 d=a[i]-b[j]; const double distance=dot(d,d);
            if (distance<best) {best=distance;p=(a[i]+b[j])*.5;}
        }
        return p;
    };
    for (int digit=1;digit<=5;++digit) {
        const std::string n=std::to_string(digit);
        const std::string tarsal=digit==1 ? "MedCun" : digit==2 ? "InterCun" : digit==3 ? "LatCun" : "Cub";
        std::array<Part*,2> meta{},base{}; std::array<Vec3,2> axes{},pivots{};
        for (int side=0;side<2;++side) {
            const std::string suffix=side==0 ? "_L" : "_R";
            meta[side]=find("FootMeta"+n+suffix); base[side]=find(tarsal+suffix);
            if (!meta[side] || !base[side] || meta[side]->points.empty() || base[side]->points.empty()) return false;
            const auto& points=meta[side]->points;
            axes[side]=FingerReferenceFrames::shaft(points.data(),points.size(),centroid(points),{0,0,1});
            pivots[side]=contact(points,base[side]->points);
        }
        // Reflect only the direction; the position reflection plane is handled
        // later by the existing bilateral fit.
        Vec3 reflected=axes[1]; reflected.x=-reflected.x;
        Vec3 mean=normalized(axes[0]+reflected);
        if (length(mean)<1e-8) return false;
        for (int side=0;side<2;++side) {
            Vec3 target=mean; if (side==1) target.x=-target.x;
            Vec3 axis=cross(axes[side],target); const double sine=length(axis);
            if (sine<1e-10) continue;
            axis=axis/sine;
            const double angle=std::atan2(sine,std::clamp(dot(axes[side],target),-1.0,1.0));
            auto turn=joint_detail::rotate(axis,angle);
            turn.translation=pivots[side]-frameVector(turn,pivots[side]);
            const std::string suffix=side==0 ? "_L" : "_R";
            const auto apply=[&](Part* part) {
                if (!part) return;
                frames[static_cast<std::size_t>(part-parts.data())]=turn;
                for (Vec3& p:part->points) p=frameVector(turn,p)+turn.translation;
            };
            apply(meta[side]);
            for (const char* prefix:{"FootProxP","FootMidP","FootDistP"}) apply(find(std::string(prefix)+n+suffix));
            if (digit==1) for (const char* sesamoid:{"FootSesaMed","FootSesaLat"}) apply(find(std::string(sesamoid)+suffix));
        }
    }
    return true;
}
// Transfer a source toe's rigid delta into its registered metatarsal frame.
// The centre mapping retains the comparison figure's existing scale field.
template<class Map>
std::array<double,16> registeredToeDelta(const std::array<double,16>& a, const std::array<double,16>& b,
                                         const JointRigidTransform& frame, Vec3 center, Map map) {
    const auto inv=joint_detail::inverse(frame);
    const auto sourceRotate=[&](Vec3 v) {
        Vec3 q{b[0]*v.x+b[1]*v.y+b[2]*v.z,b[4]*v.x+b[5]*v.y+b[6]*v.z,b[8]*v.x+b[9]*v.y+b[10]*v.z};
        return Vec3{a[0]*q.x+a[4]*q.y+a[8]*q.z,a[1]*q.x+a[5]*q.y+a[9]*q.z,a[2]*q.x+a[6]*q.y+a[10]*q.z};
    };
    const auto registeredRotate=[&](Vec3 v) {return frameVector(frame,sourceRotate(frameVector(inv,v)));};
    std::array<double,16> out{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    for (int col=0;col<3;++col) {
        Vec3 basis{}; if (col==0) basis.x=1; else if (col==1) basis.y=1; else basis.z=1;
        const Vec3 v=registeredRotate(basis); out[col*4]=v.x; out[col*4+1]=v.y; out[col*4+2]=v.z;
    }
    const Vec3 sourceCenter=frameVector(inv,center)+inv.translation;
    const Vec3 sourceOrigin{b[12],b[13],b[14]}, currentOrigin{a[12],a[13],a[14]};
    const Vec3 posed=currentOrigin+sourceRotate(sourceCenter-sourceOrigin);
    const Vec3 moved=frameVector(frame,posed)+frame.translation;
    const Vec3 offset=map(moved)-registeredRotate(map(center));
    out[12]=offset.x; out[13]=offset.y; out[14]=offset.z;
    return out;
}
} // namespace gtc::native
