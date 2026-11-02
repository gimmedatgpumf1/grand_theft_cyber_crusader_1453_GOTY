#pragma once

// Toe flesh is derived from the comparison's own phalanx vertices. There is
// no external foot mesh. The soft-tissue offsets are modelling estimates, not
// measurements of the source CT skin.
#include "client/skeleton_skinning.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace gtc::native {
struct ToeBoneSample { std::size_t structure=0; std::vector<Vec3> points; };
struct ToeBodyMesh {
    std::vector<SkinVertex> skin,nails;
    std::vector<std::array<int,3>> skinFaces,nailFaces,nailBedFaces,nailTipFaces;
    bool valid=false;
};
namespace toe_body {
struct FieldPart { Vec3 center,radius; int bone=-1; int digit=0; bool distal=false; };
inline double component(Vec3 p,int i) { return i==0?p.x:i==1?p.y:p.z; }
inline double fieldPart(Vec3 p,const FieldPart& v) {
    const Vec3 d=p-v.center;
    const double r=std::sqrt(d.x*d.x/(v.radius.x*v.radius.x)+d.y*d.y/(v.radius.y*v.radius.y)+d.z*d.z/(v.radius.z*v.radius.z));
    return (r-1)*std::min({v.radius.x,v.radius.y,v.radius.z});
}
struct Field {
    struct Cleft { double x0,x1,z0,z1,width,floor; };
    struct Crease { double x,z,width,height,spacing,depth; };
    std::vector<FieldPart> parts;
    std::vector<Cleft> clefts;
    std::vector<Crease> creases;
    double smoothing=0;
    double operator()(Vec3 p) const {
        std::array<double,6> digits; digits.fill(1e9);
        const auto merge=[](double a,double b,double k) {
            const double h=std::max(0.0,k-std::abs(a-b))/k;
            return std::min(a,b)-h*h*k*.25;
        };
        for (const auto& part:parts) digits[part.digit]=merge(digits[part.digit],fieldPart(p,part),smoothing);
        // The dorsal forefoot is one continuous surface. Blend each toe root
        // into it, but retain distinct clefts where the toes extend beyond it.
        double toes=digits[1];
        for (int digit=2;digit<=5;++digit) toes=merge(toes,digits[digit],smoothing*.16);
        double distance=merge(digits[0],toes,smoothing*1.25);
        // Gentle transverse folds at the phalanx joints are actual shallow
        // surface relief, not painted lines or separate floating geometry.
        for (const auto& crease:creases) {
            const double x=std::abs((p.x-crease.x)/crease.width);
            const double z=std::abs((p.z-crease.z)/crease.spacing);
            const double dorsal=std::clamp((p.y-crease.height)/crease.spacing,0.0,1.0);
            if (x<1 && z<1) distance+=crease.depth*(1-x*x)*(1-z*z)*dorsal;
        }
        // A soft web closes each cleft at its root. Beyond it, the narrow
        // channel defines the contact line rather than a fused mitten.
        for (const auto& cleft:clefts) {
            const double t=std::clamp((p.z-cleft.z0)/(cleft.z1-cleft.z0),0.0,1.0);
            const double x=cleft.x0+(cleft.x1-cleft.x0)*t;
            const double width=cleft.width*std::min(1.0,t*5.0);
            const double groove=std::max({std::abs(p.x-x)-width,cleft.z0-p.z,cleft.floor-p.y});
            distance=-merge(-distance,groove,smoothing*.10);
        }
        return distance;
    }
    SkinVertex vertex(Vec3 p) const {
        SkinVertex v; v.position=p;
        // Try to keep the dorsal nail bed with the distal plate. This is not
        // sufficient to prevent all posed nail/skin contacts; see the audit.
        for (const auto& part:parts) if (part.distal && p.y>part.center.y-part.radius.y*.2 &&
            std::abs(p.x-part.center.x)<part.radius.x*1.1 &&
            p.z>part.center.z-part.radius.z*.70 && p.z<part.center.z+part.radius.z*1.2) {
            v.count=1; v.influences[0]={static_cast<std::size_t>(part.bone),1}; return v;
        }
        // Blend nearest bone fields over a short transition at joint surfaces.
        // The web and forefoot have their own metatarsal owner.
        std::array<SkinInfluence,4> nearest{}; std::array<double,4> score{};
        score.fill(std::numeric_limits<double>::infinity());
        for (const auto& part:parts) {
            if (part.bone<0) continue;
            const double d=std::abs(fieldPart(p,part));
            bool duplicate=false;
            for (std::size_t i=0;i<v.count;++i) if (nearest[i].bone==static_cast<std::size_t>(part.bone)) {
                score[i]=std::min(score[i],d); duplicate=true; break;
            }
            if (duplicate) continue;
            std::size_t slot=v.count;
            if (slot<4) ++v.count;
            else { slot=static_cast<std::size_t>(std::max_element(score.begin(),score.end())-score.begin()); if (d>=score[slot]) continue; }
            nearest[slot].bone=static_cast<std::size_t>(part.bone); score[slot]=d;
        }
        const double minimum=*std::min_element(score.begin(),score.begin()+v.count);
        double sum=0; const std::size_t count=v.count; v.count=0;
        for (std::size_t i=0;i<count;++i) {
            const double weight=std::exp(-(score[i]-minimum)/(smoothing*.18));
            if (weight<1e-10) continue;
            nearest[i].weight=weight; sum+=weight; v.influences[v.count++]=nearest[i];
        }
        for (std::size_t i=0;i<v.count;++i) v.influences[i].weight/=sum;
        return v;
    }
};
inline FieldPart enclose(const ToeBoneSample& bone,double scale) {
    Vec3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
    for (Vec3 p:bone.points) {
        lo.x=std::min(lo.x,p.x); lo.y=std::min(lo.y,p.y); lo.z=std::min(lo.z,p.z);
        hi.x=std::max(hi.x,p.x); hi.y=std::max(hi.y,p.y); hi.z=std::max(hi.z,p.z);
    }
    const Vec3 c=(lo+hi)*.5;
    Vec3 r=(hi-lo)*.5+Vec3{.0025*scale,.0025*scale,.0025*scale};
    double factor=1;
    for (Vec3 p:bone.points) {
        Vec3 q=p-c; factor=std::max(factor,std::sqrt(q.x*q.x/(r.x*r.x)+q.y*q.y/(r.y*r.y)+q.z*q.z/(r.z*r.z)));
    }
    r=r*(factor*1.06);
    // The scan bones are side-by-side: a single enclosing sphere around each
    // oblique bone makes the toes overlap and destroys the natural clefts.
    r.x=(hi.x-lo.x)*.5+.0014*scale;
    return {c,r,static_cast<int>(bone.structure)};
}
inline void appendTriangle(ToeBodyMesh& mesh,int a,int b,int c,Vec3 outward,double step) {
    const Vec3 p=mesh.skin[a].position,q=mesh.skin[b].position,r=mesh.skin[c].position;
    const Vec3 normal=cross(q-p,r-p); if (length(normal)<1e-12*step*step) return;
    if (dot(normal,outward)<0) std::swap(b,c);
    mesh.skinFaces.push_back({a,b,c});
}
// A small regular grid samples only the forefoot region. Marching tetrahedra
// shares edge vertices; no triangle contains a copied external body mesh.
inline void triangulate(ToeBodyMesh& mesh,const Field& field,Vec3 lo,Vec3 hi,double step) {
    const int nx=static_cast<int>(std::ceil((hi.x-lo.x)/step))+1;
    const int ny=static_cast<int>(std::ceil((hi.y-lo.y)/step))+1;
    const int nz=static_cast<int>(std::ceil((hi.z-lo.z)/step))+1;
    const auto index=[&](int x,int y,int z) { return (x*ny+y)*nz+z; };
    const auto position=[&](int i) {
        const int z=i%nz,y=(i/nz)%ny,x=i/(ny*nz);
        return lo+Vec3{x*step,y*step,z*step};
    };
    std::vector<double> values(static_cast<std::size_t>(nx)*ny*nz);
    for (int x=0;x<nx;++x) for (int y=0;y<ny;++y) for (int z=0;z<nz;++z) {
        const int i=index(x,y,z); values[i]=field(position(i));
        if (std::abs(values[i])<step*.0001) values[i]=0;
    }
    std::unordered_map<std::uint64_t,int> edges;
    const auto edge=[&](int a,int b) {
        const int endpoint=std::abs(values[a])<1e-14 ? a : std::abs(values[b])<1e-14 ? b : -1;
        const auto small=static_cast<std::uint32_t>(endpoint>=0?endpoint:std::min(a,b));
        const auto large=static_cast<std::uint32_t>(endpoint>=0?endpoint:std::max(a,b));
        const std::uint64_t key=(std::uint64_t(small)<<32)|large;
        const auto found=edges.find(key); if (found!=edges.end()) return found->second;
        const double t=values[a]/(values[a]-values[b]);
        const Vec3 p=position(a)+(position(b)-position(a))*std::clamp(t,0.0,1.0);
        const int result=static_cast<int>(mesh.skin.size()); mesh.skin.push_back(field.vertex(p)); edges.emplace(key,result); return result;
    };
    static constexpr int corner[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    static constexpr int tetra[6][4]={{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}};
    for (int x=0;x<nx-1;++x) for (int y=0;y<ny-1;++y) for (int z=0;z<nz-1;++z) {
        int cube[8]; for (int j=0;j<8;++j) cube[j]=index(x+corner[j][0],y+corner[j][1],z+corner[j][2]);
        for (const auto& t:tetra) {
            int in[4],out[4],ni=0,no=0;
            for (int j:t) { const int k=cube[j]; if (values[k]<0) in[ni++]=k; else out[no++]=k; }
            if (ni==0 || ni==4) continue;
            Vec3 inside,outside;
            for (int j=0;j<ni;++j) inside+=position(in[j]);
            for (int j=0;j<no;++j) outside+=position(out[j]);
            const Vec3 direction=outside/double(no)-inside/double(ni);
            if (ni==1) appendTriangle(mesh,edge(in[0],out[0]),edge(in[0],out[1]),edge(in[0],out[2]),direction,step);
            else if (ni==3) appendTriangle(mesh,edge(out[0],in[0]),edge(out[0],in[1]),edge(out[0],in[2]),direction,step);
            else {
                const int a=edge(in[0],out[0]),b=edge(in[0],out[1]),c=edge(in[1],out[0]),d=edge(in[1],out[1]);
                appendTriangle(mesh,a,b,c,direction,step); appendTriangle(mesh,b,d,c,direction,step);
            }
        }
    }
}
// A nail is a curved closed plate, with physical thickness and side walls.
// The lower surface is sampled against the generated dorsal skin.
inline bool nail(ToeBodyMesh& mesh,const FieldPart& distal,double scale) {
    constexpr int columns=17,rows=15;
    const double width=distal.radius.x*.72;
    const double start=distal.center.z-distal.radius.z*.55,end=distal.center.z+distal.radius.z*.55;
    const int base=static_cast<int>(mesh.nails.size());
    for (int layer=0;layer<2;++layer) for (int j=0;j<rows;++j) for (int i=0;i<columns;++i) {
        const double u=2.0*i/(columns-1)-1.0,v=double(j)/(rows-1);
        const double taper=.74+.26*std::sqrt(std::sin(v*3.141592653589793));
        Vec3 p{distal.center.x+u*width*taper,0,start+(end-start)*v};
        // Project onto the actual tessellated skin, not the continuous
        // implicit surface: the latter can sit below its coarse triangles.
        double top=-1e30;
        for (const auto face:mesh.skinFaces) {
            const Vec3 a=mesh.skin[face[0]].position,b=mesh.skin[face[1]].position,c=mesh.skin[face[2]].position;
            const double det=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);
            if (std::abs(det)<1e-20) continue;
            const double u=((b.z-c.z)*(p.x-c.x)+(c.x-b.x)*(p.z-c.z))/det;
            const double v=((c.z-a.z)*(p.x-c.x)+(a.x-c.x)*(p.z-c.z))/det;
            if (u>=-1e-9 && v>=-1e-9 && u+v<=1+1e-9) top=std::max(top,u*a.y+v*b.y+(1-u-v)*c.y);
        }
        if (!std::isfinite(top) || top<-1e20) return false;
        p.y=top+scale*(layer?.0009:.00015);
        SkinVertex vertex; vertex.position=p; vertex.count=1;
        vertex.influences[0]={static_cast<std::size_t>(distal.bone),1}; mesh.nails.push_back(vertex);
    }
    const int layer=columns*rows;
    for (int y=0;y<rows-1;++y) for (int x=0;x<columns-1;++x) {
        const int a=base+y*columns+x,b=a+1,c=a+columns,d=c+1;
        for (auto face:{std::array<int,3>{a,b,c},std::array<int,3>{b,d,c}}) {
            mesh.nailFaces.push_back(face); mesh.nailBedFaces.push_back(face);
        }
        auto& top=(y==rows-2?mesh.nailTipFaces:mesh.nailBedFaces);
        for (auto face:{std::array<int,3>{a+layer,c+layer,b+layer},std::array<int,3>{b+layer,c+layer,d+layer}}) {
            mesh.nailFaces.push_back(face); top.push_back(face);
        }
    }
    const auto wall=[&](int a,int b) {
        for (auto face:{std::array<int,3>{a,b,a+layer},std::array<int,3>{b,b+layer,a+layer}}) {
            mesh.nailFaces.push_back(face); mesh.nailBedFaces.push_back(face);
        }
    };
    for (int i=0;i<columns-1;++i) { wall(base+i+1,base+i); wall(base+(rows-1)*columns+i,base+(rows-1)*columns+i+1); }
    for (int j=0;j<rows-1;++j) { wall(base+j*columns,base+(j+1)*columns); wall(base+(j+1)*columns+columns-1,base+j*columns+columns-1); }
    return true;
}
} // namespace toe_body

// `bones[digit][0..2]` contains proximal, middle (empty for the big toe),
// and distal phalanx geometry in mapped comparison coordinates. Metatarsal
// owners connect the webbed roots to the actual foot bones.
inline ToeBodyMesh buildToeBody(const std::array<std::array<ToeBoneSample,3>,5>& bones,
                               const std::array<std::size_t,5>& metatarsals,double scale,
                               const std::array<ToeBoneSample,5>& supports={}) {
    using namespace toe_body; ToeBodyMesh mesh;
    if (!std::isfinite(scale) || scale<=0) return mesh;
    for (int d=0;d<5;++d) for (int j=0;j<3;++j) {
        if (bones[d][j].points.empty() && !(d==0 && j==1)) return mesh;
        for (Vec3 p:bones[d][j].points)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return mesh;
    }
    Field field; field.smoothing=.010*scale;
    std::array<FieldPart,5> proximal,distal;
    for (int d=0;d<5;++d) for (int j=0;j<3;++j) if (!bones[d][j].points.empty()) {
        auto part=enclose(bones[d][j],scale); part.digit=d+1; part.distal=(j==2); field.parts.push_back(part);
        if (j==0) proximal[d]=part;
        if (j==2) distal[d]=part;
    }
    for (int d=0;d<5;++d) {
        const auto& a=proximal[d]; const auto& b=distal[d];
        const double z=a.center.z+(b.center.z-a.center.z)*.42;
        const double x=a.center.x+(b.center.x-a.center.x)*.42;
        const double width=std::min(a.radius.x,b.radius.x)*.85;
        for (int line=-1;line<=1;++line)
            field.creases.push_back({x,z+line*.0014*scale,width,(a.center.y+b.center.y)*.5,
                                     .001*scale,.00028*scale});
    }
    for (int d=0;d<4;++d) {
        const auto& a0=proximal[d]; const auto& b0=proximal[d+1];
        const auto& a1=distal[d]; const auto& b1=distal[d+1];
        const double z0=(a0.center.z+b0.center.z)*.5+.004*scale;
        const double z1=std::max(a1.center.z,b1.center.z);
        field.clefts.push_back({(a0.center.x+b0.center.x)*.5,(a1.center.x+b1.center.x)*.5,
                               z0,std::max(z0+.001*scale,z1),.0011*scale,.0025*scale});
        Vec3 a=proximal[d].center,b=proximal[d+1].center;
        // The web lies near the middle of each proximal phalanx, not at
        // its metatarsal joint or halfway down the distal toe.
        a.z+=proximal[d].radius.z*.05; b.z+=proximal[d+1].radius.z*.05;
        const Vec3 center=(a+b)*.5;
        const Vec3 radius{std::abs(b.x-a.x)*.60+.001*scale,
                          std::max(proximal[d].radius.y,proximal[d+1].radius.y)*.78,.008*scale};
        field.parts.push_back({center,radius,static_cast<int>(metatarsals[d])});
    }
    // Extend the visible toe roots over the actual metatarsal shafts. This
    // keeps the transition from looking like detached toes on a round pad.
    Vec3 supportLo{1e30,1e30,1e30},supportHi{-1e30,-1e30,-1e30};
    bool haveSupport=false;
    for (const auto& bone:supports) if (!bone.points.empty()) {
        haveSupport=true;
        for (Vec3 p:bone.points) {
            supportLo.x=std::min(supportLo.x,p.x); supportLo.y=std::min(supportLo.y,p.y); supportLo.z=std::min(supportLo.z,p.z);
            supportHi.x=std::max(supportHi.x,p.x); supportHi.y=std::max(supportHi.y,p.y); supportHi.z=std::max(supportHi.z,p.z);
        }
        auto part=enclose(bone,scale); part.radius.x+=.004*scale;
        part.radius.y+=.004*scale; part.radius.z+=.003*scale;
        field.parts.push_back(part);
    }
    if (haveSupport) {
        const Vec3 center=(supportLo+supportHi)*.5;
        const Vec3 radius=(supportHi-supportLo)*.5+Vec3{.006*scale,.008*scale,.004*scale};
        field.parts.push_back({center,radius,static_cast<int>(metatarsals[2])});
    }
    Vec3 center=(proximal[0].center+proximal[4].center)*.5;
    center.z-=.045*scale; center.y=std::max(.012*scale,center.y);
    Vec3 radius{std::abs(proximal[4].center.x-proximal[0].center.x)*.5+.012*scale,
                std::max(proximal[0].radius.y,proximal[4].radius.y)*1.1,.048*scale};
    field.parts.push_back({center,radius,static_cast<int>(metatarsals[2])});
    Vec3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
    for (const auto& part:field.parts) {
        lo.x=std::min(lo.x,part.center.x-part.radius.x-.005*scale);
        lo.y=std::min(lo.y,part.center.y-part.radius.y-.008*scale);
        lo.z=std::min(lo.z,part.center.z-part.radius.z-.005*scale);
        hi.x=std::max(hi.x,part.center.x+part.radius.x+.005*scale);
        hi.y=std::max(hi.y,part.center.y+part.radius.y+.005*scale);
        hi.z=std::max(hi.z,part.center.z+part.radius.z+.005*scale);
    }
    triangulate(mesh,field,lo,hi,.0015*scale);
    // Preserve curved toe pulp rather than slicing it flat at ground level.
    // This is a visual placement of the skin; bone and joint fitting comes later.
    double bottom=1e30;
    for (const auto& v:mesh.skin) bottom=std::min(bottom,v.position.y);
    for (auto& v:mesh.skin) v.position.y-=bottom;
    for (const auto& tip:distal) if (!nail(mesh,tip,scale)) return mesh;
    mesh.valid=true; return mesh;
}
} // namespace gtc::native
