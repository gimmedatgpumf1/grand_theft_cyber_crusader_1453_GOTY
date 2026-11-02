#pragma once

// Low-poly toe surfaces for the static comparison bodies. Their soft-tissue
// thickness is an artistic estimate around the mapped bones, not CT data.
#include "client/raster.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace gtc::native {
struct ToeSurfaceMesh {
    std::vector<Vec3> skin, nails;
    std::vector<std::array<int,3>> skinFaces, nailFaces;
};
struct ToeBoneBounds {
    Vec3 low,high,distalLow,distalHigh;
    std::vector<Vec3> points;
};

// Build five individually fitted left toes. The right side is an exact
// reflection about the comparison body's common midplane.
inline ToeSurfaceMesh comparisonToeSurface(const std::array<ToeBoneBounds,5>& bones,double scale) {
    ToeSurfaceMesh out;
    const auto face=[](std::vector<std::array<int,3>>& faces,int a,int b,int c) { faces.push_back({a,b,c}); };
    for (int digit=0;digit<5;++digit) {
        const auto& bone=bones[digit];
        const double pad=.0012*scale, extension=.0045*scale;
        double inner=pad,outer=pad;
        if (digit>0) inner=std::min(inner,std::max(0.0,(bone.low.x-bones[digit-1].high.x)*.4));
        if (digit<4) outer=std::min(outer,std::max(0.0,(bones[digit+1].low.x-bone.high.x)*.4));
        const double backLeft=bone.low.x-inner,backRight=bone.high.x+outer;
        const double frontLeft=bone.distalLow.x-inner,frontRight=bone.distalHigh.x+outer;
        const double bottom=std::max(0.0,bone.low.y-.0015*scale);
        const double z1=bone.low.z,z2=bone.high.z,range=z2-z1;
        const std::array<double,8> z={z1-extension,z1,z1+range*.20,z1+range*.40,
                                     z1+range*.60,z1+range*.80,z2,z2+extension};
        std::array<double,8> left{},right{},top{};
        const double backTop=bone.high.y+.0025*scale,frontTop=bone.distalHigh.y+.003*scale;
        for (int i=1;i<=6;++i) {
            const double t=(z[i]-z1)/range;
            const double taper=t*t*(3-2*t);
            left[i]=backLeft+(frontLeft-backLeft)*taper;
            right[i]=backRight+(frontRight-backRight)*taper;
            top[i]=backTop+(frontTop-backTop)*taper;
        }
        // Keep the sketched contour outside the actual toe bones. The extra
        // top allowance also accounts for the bevelled cross-section.
        for (Vec3 p:bone.points) {
            int i=1; while (i<5 && p.z>z[i+1]) ++i;
            const double t=std::clamp((p.z-z[i])/(z[i+1]-z[i]),0.0,1.0);
            const auto lerp=[&](const std::array<double,8>& a){return a[i]*(1-t)+a[i+1]*t;};
            const double shortLeft=std::max(0.0,lerp(left)-(p.x-inner));
            const double shortRight=std::max(0.0,(p.x+outer)-lerp(right));
            const double shortTop=std::max(0.0,bottom+(p.y-bottom)*1.12+.0012*scale-lerp(top));
            left[i]-=shortLeft; left[i+1]-=shortLeft;
            right[i]+=shortRight; right[i+1]+=shortRight;
            top[i]+=shortTop; top[i+1]+=shortTop;
        }
        // Round both ends beyond the bone rather than exposing a square cut.
        for (int end:{0,7}) {
            const int neighbour=end==0?1:6;
            const double mid=(left[neighbour]+right[neighbour])*.5;
            const double half=(right[neighbour]-left[neighbour])*.5*.55;
            left[end]=mid-half; right[end]=mid+half;
            top[end]=bottom+(top[neighbour]-bottom)*.74;
        }
        const int base=static_cast<int>(out.skin.size());
        for (int i=0;i<8;++i) {
            const double mid=(left[i]+right[i])*.5,w=(right[i]-left[i])*.5,h=top[i]-bottom;
            // Flattened sole, fuller sides, and a gently bevelled dorsal face.
            const std::array<Vec3,10> ring={
                Vec3{mid-w*.98,bottom,z[i]},Vec3{mid+w*.98,bottom,z[i]},
                Vec3{mid+w,bottom+h*.13,z[i]},Vec3{mid+w,bottom+h*.56,z[i]},
                Vec3{mid+w*.98,bottom+h*.86,z[i]},Vec3{mid+w*.80,top[i],z[i]},
                Vec3{mid-w*.80,top[i],z[i]},Vec3{mid-w*.98,bottom+h*.86,z[i]},
                Vec3{mid-w,bottom+h*.56,z[i]},Vec3{mid-w,bottom+h*.13,z[i]}};
            out.skin.insert(out.skin.end(),ring.begin(),ring.end());
        }
        for (int ring=0;ring<7;++ring) for (int j=0;j<10;++j) {
            const int a=base+ring*10+j,b=base+ring*10+(j+1)%10,c=b+10,d=a+10;
            face(out.skinFaces,a,b,c); face(out.skinFaces,a,c,d);
        }
        for (int j=1;j<9;++j) {
            face(out.skinFaces,base,base+j+1,base+j);
            const int tip=base+70; face(out.skinFaces,tip,tip+j,tip+j+1);
        }
        // A gently bevelled nail plate, fitted to the slope of the toe top.
        // Keep the plate on one planar dorsal strip, so it cannot cut through
        // a bend between rings or float above the neighbouring face.
        const double nailLength=std::min(range*.18,(digit==0?.012:.007)*scale);
        const double nailEnd=z2,nailStart=nailEnd-nailLength;
        const double startT=(nailStart-z[5])/(z[6]-z[5]);
        const double startWidth=(right[5]-left[5])*(1-startT)+(right[6]-left[6])*startT;
        const double nailWidth=std::min(startWidth,right[6]-left[6])*(digit==0?.24:.22);
        const auto roof=[&](double zz) {
            int i=1; while(i<6 && zz>z[i+1]) ++i;
            const double t=std::clamp((zz-z[i])/(z[i+1]-z[i]),0.0,1.0);
            return std::array<double,2>{(left[i]+right[i])*.5*(1-t)+(left[i+1]+right[i+1])*.5*t,
                                        top[i]*(1-t)+top[i+1]*t+.00012*scale};
        };
        const auto a=roof(nailStart),b=roof(nailStart+nailLength*.23),c=roof(nailEnd);
        const int nail=static_cast<int>(out.nails.size());
        out.nails.insert(out.nails.end(),{
            Vec3{a[0]-nailWidth*.72,a[1],nailStart},Vec3{a[0]+nailWidth*.72,a[1],nailStart},
            Vec3{b[0]+nailWidth,b[1],nailStart+nailLength*.23},Vec3{c[0]+nailWidth*.85,c[1],nailEnd},
            Vec3{c[0]-nailWidth*.85,c[1],nailEnd},Vec3{b[0]-nailWidth,b[1],nailStart+nailLength*.23}});
        for (int j=1;j<5;++j) face(out.nailFaces,nail,nail+j+1,nail+j);
    }
    const auto reflect=[&](std::vector<Vec3>& vertices,std::vector<std::array<int,3>>& faces) {
        const int n=static_cast<int>(vertices.size()); const auto original=faces;
        for (int i=0;i<n;++i) { Vec3 p=vertices[i]; p.x=-p.x; vertices.push_back(p); }
        for (auto f:original) faces.push_back({f[0]+n,f[2]+n,f[1]+n});
    };
    reflect(out.skin,out.skinFaces); reflect(out.nails,out.nailFaces);
    return out;
}
} // namespace gtc::native
