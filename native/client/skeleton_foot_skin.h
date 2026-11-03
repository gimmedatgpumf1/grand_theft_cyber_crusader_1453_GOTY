#pragma once

#include "client/skeleton_toe_skin.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace gtc::native {
// A single loft from the five individually owned toe roots to the heel. The
// first row is the exact open edge of each toe; the intervening faces form the
// webs. Subsequent rows belong to the rigid hindfoot and deform at the roots.
inline ToeSkinMesh makeFootSkin(const std::array<std::vector<Vec3>,5>& rings,
                                const std::array<int,5>& owners,
                                const std::vector<Vec3>& bones,
                                const std::vector<Vec3>& heel, int heelOwner) {
    ToeSkinMesh out;
    if (bones.empty() || heel.empty()) return out;
    for (const auto& r:rings) if (r.size()!=16) return out;
    Vec3 heelCenter{},toeCenter{};
    for (Vec3 p:heel) heelCenter+=p;
    heelCenter=heelCenter/double(heel.size());
    for (const auto& r:rings) for (int k=0;k<8;++k) toeCenter+=r[k];
    toeCenter=toeCenter/40.0;
    Vec3 forward=toeCenter-heelCenter; forward.y=0; forward=normalized(forward);
    const Vec3 lateral=normalized(cross(Vec3{0,1,0},forward));
    if (length(forward)<1e-8 || length(lateral)<1e-8) return out;
    const auto side=[&](Vec3 p) { return dot(p,lateral); };
    const auto along=[&](Vec3 p) { return dot(p,forward); };
    const auto world=[&](double x,double y,double z) { return lateral*x+Vec3{0,y,0}+forward*z; };
    std::array<int,5> order{0,1,2,3,4};
    const auto ringSide=[&](int n) { double x=0;for(int k=0;k<8;++k)x+=side(rings[n][k]);return x/8.0; };
    std::sort(order.begin(),order.end(),[&](int a,int b){ return ringSide(a)<ringSide(b); });
    constexpr int columns=20, rows=10;
    std::array<std::array<int,columns>,rows> top{},bottom{};
    std::array<std::array<int,2>,rows> sides{};
    for (int n=0;n<5;++n) {
        const int toe=order[n];
        std::array<int,4> upper{0,1,2,3},lower{4,5,6,7};
        auto less=[&](int a,int b) { return side(rings[toe][a])<side(rings[toe][b]); };
        std::sort(upper.begin(),upper.end(),less); std::sort(lower.begin(),lower.end(),less);
        for (int j=0;j<4;++j) {
            top[0][n*4+j]=out.vertex(rings[toe][upper[j]],owners[toe]);
            bottom[0][n*4+j]=out.vertex(rings[toe][lower[j]],owners[toe]);
        }
    }
    const double frontZ=along(toeCenter);
    double heelZ=along(heel.front());
    for (Vec3 p:heel) heelZ=std::min(heelZ,along(p));
    const double footLength=frontZ-heelZ;
    if (footLength<1e-5) return {};
    heelZ-=footLength*0.035;
    const double pad=footLength*0.045;
    double sole=heel.front().y;
    for (Vec3 p:bones) sole=std::min(sole,p.y);
    sole-=pad*.7;
    const double rootLeft=side(out.points[top[0][0]]),rootRight=side(out.points[top[0][columns-1]]);
    for (int edge=0;edge<2;++edge) {
        const int col=edge ? columns-1 : 0;
        sides[0][edge]=out.vertex((out.points[top[0][col]]+out.points[bottom[0][col]])*.5,owners[order[edge ? 4 : 0]]);
    }
    if (rootRight-rootLeft<1e-6) return {};
    const double fractions[rows]={0,.15,.25,.36,.48,.60,.72,.84,.93,1};
    for (int row=1;row<rows;++row) {
        const double t=fractions[row], z=frontZ+(heelZ-frontZ)*t;
        double loX=1e9,hiX=-1e9,loY=1e9,hiY=-1e9;
        const double window=footLength*(t>.80 ? .085 : .04);
        for (Vec3 p:bones) if (std::abs(along(p)-z)<window) {
            loX=std::min(loX,side(p)); hiX=std::max(hiX,side(p));
            loY=std::min(loY,p.y); hiY=std::max(hiY,p.y);
        }
        if (hiX<loX) for (Vec3 p:heel) {
            loX=std::min(loX,side(p)); hiX=std::max(hiX,side(p));
            loY=std::min(loY,p.y); hiY=std::max(hiY,p.y);
        }
        loX-=pad; hiX+=pad; loY=sole; hiY+=pad*1.5;
        // Round the heel behind the calcaneus instead of truncating it.
        if (row==rows-1) {
            const double middle=(loX+hiX)*.5;
            loX=middle+(loX-middle)*.80; hiX=middle+(hiX-middle)*.80;
            const double center=(loY+hiY)*.5;
            loY=center+(loY-center)*.65; hiY=center+(hiY-center)*.65;
        }
        for (int col=0;col<columns;++col) {
            const double u=double(col)/double(columns-1);
            const double edge=std::pow(std::abs(2*u-1),4.0);
            const double x=loX+(hiX-loX)*(.06+.88*u);
            const double h=hiY-loY;
            const double towardLittle=order[0]==0 ? u : 1.0-u;
            // The instep is higher on the great-toe/medial side, easing
            // toward a more even heel cross-section.
            const double roof=hiY-h*(.25-.12*t)*towardLittle;
            top[row][col]=out.vertex(world(x,roof-edge*h*.04,z),heelOwner);
            bottom[row][col]=out.vertex(world(x,loY+edge*h*.08,z),heelOwner);
        }
        for (int edge=0;edge<2;++edge) {
            const int col=edge ? columns-1 : 0;
            const double y=(out.points[top[row][col]].y+out.points[bottom[row][col]].y)*.5;
            sides[row][edge]=out.vertex(world(edge ? hiX : loX,y,z),heelOwner);
        }
    }
    for (int r=0;r<rows-1;++r) for (int c=0;c<columns-1;++c) {
        out.triangle(top[r][c],top[r+1][c],top[r+1][c+1]);
        out.triangle(top[r][c],top[r+1][c+1],top[r][c+1]);
        out.triangle(bottom[r][c],bottom[r+1][c+1],bottom[r+1][c]);
        out.triangle(bottom[r][c],bottom[r][c+1],bottom[r+1][c+1]);
    }
    for (int r=0;r<rows-1;++r) for (int edge=0;edge<2;++edge) {
        const int c=edge ? columns-1 : 0;
        out.triangle(top[r][c],sides[r][edge],sides[r+1][edge]);
        out.triangle(top[r][c],sides[r+1][edge],top[r+1][c]);
        out.triangle(sides[r][edge],bottom[r][c],bottom[r+1][c]);
        out.triangle(sides[r][edge],bottom[r+1][c],sides[r+1][edge]);
    }
    // Close only the webs between adjacent toes, never the open toe roots.
    for (int n=0;n<4;++n) {
        const int a=n*4+3,b=a+1;
        out.triangle(top[0][a],bottom[0][a],bottom[0][b]);
        out.triangle(top[0][a],bottom[0][b],top[0][b]);
    }
    for (int c=0;c<columns-1;++c) {
        out.triangle(top[rows-1][c],bottom[rows-1][c],bottom[rows-1][c+1]);
        out.triangle(top[rows-1][c],bottom[rows-1][c+1],top[rows-1][c+1]);
    }
    for (int edge=0;edge<2;++edge) {
        const int col=edge ? columns-1 : 0;
        out.triangle(top[rows-1][col],sides[rows-1][edge],bottom[rows-1][col]);
    }
    return out;
}
template<class Comparison>
ToeSkinMesh comparisonFootSkin(const Comparison& comparison, const std::string& suffix, Vec3 origin) {
    const auto& parts=comparison.parts();
    std::array<std::vector<Vec3>,5> rings;
    std::array<int,5> owners{}; owners.fill(-1);
    std::vector<Vec3> bones,heel;
    int heelOwner=-1;
    for (std::size_t i=0;i<parts.size();++i) {
        const auto& p=parts[i];
        if (!p.name.ends_with(suffix)) continue;
        if (p.name.rfind("FootProxP",0)==0 && p.name.size()>9 && p.name[9]>='1' && p.name[9]<='5') {
            const int n=p.name[9]-'1';
            rings[n]=comparison.toePrism(i,origin);
            comparison.resizeGreatToe(i,origin,rings[n]); owners[n]=int(i);
            continue;
        }
        const bool calcaneus=p.name=="Cal"+suffix;
        const bool foot=calcaneus || p.name.rfind("FootMeta",0)==0 || p.name.rfind("Tal_",0)==0 ||
            p.name.rfind("Nav_",0)==0 || p.name.rfind("Cub_",0)==0 || p.name.rfind("MedCun_",0)==0 ||
            p.name.rfind("InterCun_",0)==0 || p.name.rfind("LatCun_",0)==0;
        if (!foot) continue;
        if (calcaneus) heelOwner=int(i);
        for (Vec3 v:p.points) {
            const Vec3 mapped=comparison.mapPart(v,p)+origin;
            bones.push_back(mapped); if (calcaneus) heel.push_back(mapped);
        }
    }
    if (heelOwner<0) return {};
    return makeFootSkin(rings,owners,bones,heel,heelOwner);
}
} // namespace gtc::native
