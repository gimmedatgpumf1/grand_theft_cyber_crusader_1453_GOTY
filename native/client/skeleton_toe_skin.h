#pragma once

// Construct a single connected exterior for one toe. Rings on either side of
// a joint belong to different bones; the triangles between them deform on the
// GPU. The buried prism caps and rear half of the tip are not emitted.
#include "client/raster.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace gtc::native {
struct ToeSkinMesh {
    std::vector<Vec3> points;
    std::vector<std::array<int,3>> faces;
    std::vector<int> owners;
    std::vector<Vec3> nailPoints;
    std::vector<std::array<int,3>> nailFaces;

    int vertex(Vec3 p, int owner) {
        points.push_back(p); owners.push_back(owner);
        return static_cast<int>(points.size()-1);
    }
    void triangle(int a,int b,int c) { if (a!=b && b!=c && c!=a) faces.push_back({a,b,c}); }
};

// Retain the original facets outside the nail footprint. Only the part above
// the nail plane and inside the plate is removed; the narrow cut edge is joined
// to the plate rather than flattening the whole tip.
inline void restoreNailTip(ToeSkinMesh& mesh, const std::vector<std::array<int,3>>& original,
                           Vec3 lateral, Vec3 forward, Vec3 normal, int owner) {
    const auto side=[&](Vec3 p,Vec3 a,Vec3 b) {
        const Vec3 ab=b-a, ap=p-a;
        return dot(ab,lateral)*dot(ap,forward)-dot(ab,forward)*dot(ap,lateral);
    };
    const auto clip=[&](const std::vector<Vec3>& polygon,Vec3 a,Vec3 b,bool inside) {
        std::vector<Vec3> result;
        if (polygon.empty()) return result;
        for (std::size_t i=0;i<polygon.size();++i) {
            const Vec3 p=polygon[i], q=polygon[(i+1)%polygon.size()];
            const double d1=side(p,a,b), d2=side(q,a,b);
            const bool keep1=inside ? d1>=0 : d1<=0, keep2=inside ? d2>=0 : d2<=0;
            if (keep1) result.push_back(p);
            if (keep1!=keep2) result.push_back(p+(q-p)*(d1/(d1-d2)));
        }
        return result;
    };
    Vec3 nailCenter{};
    for (Vec3 p:mesh.nailPoints) nailCenter+=p;
    nailCenter=nailCenter/static_cast<double>(mesh.nailPoints.size());
    const auto height=[&](Vec3 p) { return dot(p-nailCenter,normal); };
    const auto emitPolygon=[&](const std::vector<Vec3>& polygon) {
        if (polygon.size()<3) return;
        const int root=mesh.vertex(polygon[0],owner);
        for (std::size_t j=1;j+1<polygon.size();++j) {
            if (length(cross(polygon[j]-polygon[0],polygon[j+1]-polygon[0]))>1e-14) {
                const int v1=mesh.vertex(polygon[j],owner), v2=mesh.vertex(polygon[j+1],owner);
                mesh.triangle(root,v1,v2);
            }
        }
    };
    for (const auto& face:original) {
        std::vector<Vec3> upper;
        for (int k=0;k<3;++k) {
            const Vec3 p=mesh.points[std::size_t(face[k])], q=mesh.points[std::size_t(face[(k+1)%3])];
            const double h1=height(p), h2=height(q);
            if (h1>=0) upper.push_back(p);
            if ((h1>=0)!=(h2>=0)) upper.push_back(p+(q-p)*(h1/(h1-h2)));
        }
        if (upper.size()<3) continue;
        std::vector<std::vector<Vec3>> pieces{upper};
        for (std::size_t k=0;k<mesh.nailPoints.size();++k) {
            const Vec3 a=mesh.nailPoints[k], b=mesh.nailPoints[(k+1)%mesh.nailPoints.size()];
            const std::array<Vec3,3> wedge{nailCenter,a,b};
            // At the outer wedge edge, join the retained icosahedron surface to
            // the nail plane. Radial wedge edges are internal and get no wall.
            auto overlap=upper;
            for (int edge=0;edge<3 && !overlap.empty();++edge)
                overlap=clip(overlap,wedge[edge],wedge[(edge+1)%3],true);
            for (std::size_t j=0;j<overlap.size();++j) {
                const Vec3 p=overlap[j], q=overlap[(j+1)%overlap.size()];
                if (std::abs(side(p,a,b))<1e-12 && std::abs(side(q,a,b))<1e-12 && length(q-p)>1e-9) {
                    const Vec3 p0=p-normal*height(p), q0=q-normal*height(q);
                    emitPolygon({p,q,q0,p0});
                }
            }
            std::vector<std::vector<Vec3>> outsidePieces;
            for (auto remainder:pieces) {
                for (int edge=0;edge<3 && !remainder.empty();++edge) {
                    auto outside=clip(remainder,wedge[edge],wedge[(edge+1)%3],false);
                    if (outside.size()>=3) outsidePieces.push_back(std::move(outside));
                    remainder=clip(remainder,wedge[edge],wedge[(edge+1)%3],true);
                }
            }
            pieces=std::move(outsidePieces);
        }
        for (const auto& polygon:pieces) emitPolygon(polygon);
    }
}

// When a straight free edge extends past the tapered cut, connect it to the
// closest original tip facets. This keeps the flat plate supported instead of
// leaving its outer corners floating above the pointed icosahedron.
inline void joinNailEdge(ToeSkinMesh& mesh, const std::vector<std::array<int,3>>& original,
                         const std::vector<int>& cut, Vec3 lateral, Vec3 forward,
                         Vec3 normal, int owner) {
    const auto cross2=[&](Vec3 a,Vec3 b) {
        return dot(a,lateral)*dot(b,forward)-dot(a,forward)*dot(b,lateral);
    };
    const auto side=[&](Vec3 p,Vec3 a,Vec3 b) { return cross2(b-a,p-a); };
    const auto support=[&](Vec3 p) {
        double best=1e100, bestHeight=-1e100;
        Vec3 nearest=p;
        const auto consider=[&](Vec3 q) {
            const Vec3 d=q-p;
            const double x=dot(d,lateral), f=dot(d,forward);
            const double distance=x*x+f*f, height=dot(q,normal);
            if (distance<best-1e-15 || (std::abs(distance-best)<=1e-15 && height>bestHeight)) {
                best=distance; bestHeight=height; nearest=q;
            }
        };
        for (const auto& face:original) {
            const Vec3 a=mesh.points[std::size_t(face[0])], b=mesh.points[std::size_t(face[1])],
                       c=mesh.points[std::size_t(face[2])];
            const double denom=cross2(b-a,c-a);
            if (std::abs(denom)>1e-20) {
                const double u=cross2(p-a,c-a)/denom, v=cross2(b-a,p-a)/denom;
                if (u>=0 && v>=0 && u+v<=1) consider(a+(b-a)*u+(c-a)*v);
            }
            const std::array<Vec3,3> vertices{a,b,c};
            for (int i=0;i<3;++i) {
                const Vec3 v0=vertices[i], d=vertices[(i+1)%3]-v0, rel=p-v0;
                const double dx=dot(d,lateral), dy=dot(d,forward), norm=dx*dx+dy*dy;
                if (norm>1e-20) {
                    const double t=std::clamp((dot(rel,lateral)*dx+dot(rel,forward)*dy)/norm,0.0,1.0);
                    consider(v0+d*t);
                }
            }
        }
        return nearest;
    };
    for (std::size_t k=0;k<mesh.nailPoints.size();++k) {
        const Vec3 a=mesh.nailPoints[k], b=mesh.nailPoints[(k+1)%mesh.nailPoints.size()];
        std::vector<double> breaks{0.0,1.0};
        for (std::size_t j=0;j<cut.size();++j) {
            const Vec3 c=mesh.points[std::size_t(cut[j])], d=mesh.points[std::size_t(cut[(j+1)%cut.size()])];
            const double s1=side(a,c,d), s2=side(b,c,d);
            if ((s1<0)!=(s2<0) && std::abs(s1-s2)>1e-20)
                breaks.push_back(std::clamp(s1/(s1-s2),0.0,1.0));
        }
        std::sort(breaks.begin(),breaks.end());
        for (std::size_t j=0;j+1<breaks.size();++j) {
            const double lo=breaks[j], hi=breaks[j+1];
            if (hi-lo<1e-9) continue;
            const Vec3 middle=a+(b-a)*((lo+hi)*0.5);
            bool inside=true;
            for (std::size_t m=0;m<cut.size();++m) {
                const Vec3 c=mesh.points[std::size_t(cut[m])], d=mesh.points[std::size_t(cut[(m+1)%cut.size()])];
                if (side(middle,c,d)<-1e-14) { inside=false; break; }
            }
            if (inside) continue;
            // Subdivide to follow the changing nearest tip facet.
            for (int part=0;part<4;++part) {
                const double t0=lo+(hi-lo)*part/4.0, t1=lo+(hi-lo)*(part+1)/4.0;
                const Vec3 p=a+(b-a)*t0, q=a+(b-a)*t1, p0=support(p), q0=support(q);
                if (length(cross(q-p,p0-p))>1e-14) {
                    const int i0=mesh.vertex(p,owner), i1=mesh.vertex(q,owner);
                    const int i2=mesh.vertex(q0,owner), i3=mesh.vertex(p0,owner);
                    mesh.triangle(i0,i1,i2); mesh.triangle(i0,i2,i3);
                }
            }
        }
    }
}

// Trim the distal icosahedron and fit an eight-sided, recessed flat nail.
inline void cutToeNail(ToeSkinMesh& mesh, std::size_t firstFace, const std::vector<Vec3>& tip,
                       const std::vector<Vec3>& bone, Vec3 axis, Vec3 basePlane,
                       int owner) {
    const Vec3 lateral=normalized(cross(Vec3{0,1,0},axis));
    const Vec3 up=normalized(cross(axis,lateral));
    if (length(lateral)<1e-8 || length(up)<1e-8) return;
    double lowY=dot(tip[0],up), highY=lowY, lowZ=dot(tip[0],axis), highZ=lowZ;
    for (Vec3 p:tip) {
        lowY=std::min(lowY,dot(p,up)); highY=std::max(highY,dot(p,up));
        lowZ=std::min(lowZ,dot(p,axis)); highZ=std::max(highZ,dot(p,axis));
    }
    const double height=highY-lowY, lengthZ=highZ-lowZ;
    if (height<1e-8 || lengthZ<1e-8) return;
    // A gentle downward slope towards the free edge, sized to the tip.
    const double slope=0.22*height/lengthZ;
    const Vec3 normal=normalized(up+axis*slope);
    double level=lowY+0.64*height+slope*(lowZ+highZ)*0.5;
    // Keep the plane above the distal bone, even with different foot scales.
    for (Vec3 p:bone) level=std::max(level,dot(p,up)+slope*dot(p,axis)+0.012*height);
    const auto distance=[&](Vec3 p) { return dot(p,up)+slope*dot(p,axis)-level; };
    constexpr double epsilon=1e-9;
    std::map<std::pair<int,int>,int> cutEdges;
    const auto crossing=[&](int a,int b,double da,double db) {
        if (std::abs(da)<epsilon) return a;
        if (std::abs(db)<epsilon) return b;
        const auto mm=std::minmax(a,b);
        const std::pair<int,int> key{mm.first,mm.second};
        const auto found=cutEdges.find(key);
        if (found!=cutEdges.end()) return found->second;
        const double t=da/(da-db);
        const Vec3 point=mesh.points[std::size_t(a)]+(mesh.points[std::size_t(b)]-mesh.points[std::size_t(a)])*t;
        const int index=mesh.vertex(point,owner); cutEdges.emplace(key,index); return index;
    };
    const std::vector<std::array<int,3>> original(mesh.faces.begin()+firstFace,mesh.faces.end());
    std::vector<std::array<int,3>> clipped;
    for (std::size_t f=firstFace;f<mesh.faces.size();++f) {
        const auto& face=mesh.faces[f]; std::vector<int> polygon;
        for (int k=0;k<3;++k) {
            const int a=face[k], b=face[(k+1)%3];
            const double da=distance(mesh.points[std::size_t(a)]), db=distance(mesh.points[std::size_t(b)]);
            const bool inA=da<=epsilon, inB=db<=epsilon;
            if (inA) polygon.push_back(a);
            if (inA!=inB) polygon.push_back(crossing(a,b,da,db));
        }
        for (std::size_t k=1;k+1<polygon.size();++k)
            if (polygon[0]!=polygon[k] && polygon[k]!=polygon[k+1] && polygon[k+1]!=polygon[0])
                clipped.push_back({polygon[0],polygon[k],polygon[k+1]});
    }
    std::vector<int> nailBoundary, clippedBase;
    for (const auto& face:clipped) for (int index:face) {
        const Vec3 p=mesh.points[std::size_t(index)];
        if (std::abs(dot(p-basePlane,axis))<epsilon) clippedBase.push_back(index);
        if (std::abs(distance(p))<epsilon) nailBoundary.push_back(index);
    }
    const auto unique=[](std::vector<int>& ids) {
        std::sort(ids.begin(),ids.end()); ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    };
    unique(clippedBase); unique(nailBoundary);
    if (clippedBase.size()<3 || nailBoundary.size()<3) return;
    // The restored upper facets reach the original rear boundary too. Keep
    // that complete loop for stitching to the prism, not the shortened cut.
    mesh.faces.resize(firstFace); mesh.faces.insert(mesh.faces.end(),clipped.begin(),clipped.end());
    Vec3 center{}; for (int index:nailBoundary) center+=mesh.points[std::size_t(index)];
    center=center/static_cast<double>(nailBoundary.size());
    const Vec3 nailForward=normalized(axis-normal*dot(axis,normal));
    const auto angle=[&](int index) {
        const Vec3 q=mesh.points[std::size_t(index)]-center;
        return std::atan2(dot(q,nailForward),dot(q,lateral));
    };
    std::sort(nailBoundary.begin(),nailBoundary.end(),[&](int a,int b) {return angle(a)<angle(b);});
    double minX=1e30,maxX=-1e30,minF=1e30,maxF=-1e30;
    for (int index:nailBoundary) {
        const Vec3 p=mesh.points[std::size_t(index)]-center;
        const double x=dot(p,lateral), f=dot(p,nailForward);
        minX=std::min(minX,x); maxX=std::max(maxX,x); minF=std::min(minF,f); maxF=std::max(maxF,f);
    }
    const double width=maxX-minX, depth=maxF-minF;
    if (width<1e-8 || depth<1e-8) return;
    // The user-specified eight-point outline has a shallow inverted root,
    // almost straight sides and a broad, straight free edge with chamfers.
    // Size it from this icosahedron's cut, not a uniformly shrunken rectangle.
    const double midX=(minX+maxX)*0.5, halfWidth=width*0.37;
    const double back=minF+depth*0.10, front=maxF-depth*0.005;
    const double notch=(front-back)*0.075, chamfer=(front-back)*0.18;
    const double inner=halfWidth*0.37;
    const std::array<std::array<double,2>,8> outline={{{midX-inner,back+notch},
        {midX+inner,back+notch},{midX+halfWidth,back},
        {midX+halfWidth,front-chamfer},{midX+inner,front},
        {midX-inner,front},{midX-halfWidth,front-chamfer},
        {midX-halfWidth,back}}};
    for (const auto& p:outline) mesh.nailPoints.push_back(center+lateral*p[0]+nailForward*p[1]);
    restoreNailTip(mesh,original,lateral,nailForward,normal,owner);
    joinNailEdge(mesh,original,nailBoundary,lateral,nailForward,normal,owner);
    Vec3 nailCenter{};
    for (Vec3 p:mesh.nailPoints) nailCenter+=p;
    nailCenter=nailCenter/static_cast<double>(mesh.nailPoints.size());
    const int centerIndex=static_cast<int>(mesh.nailPoints.size());
    mesh.nailPoints.push_back(nailCenter);
    for (int k=0;k<8;++k) mesh.nailFaces.push_back({centerIndex,k,(k+1)%8});
}

inline ToeSkinMesh makeToeSkin(const std::vector<std::vector<Vec3>>& prisms,
                               const std::vector<int>& boneIds,
                               const std::vector<Vec3>& tip, const std::vector<Vec3>& tipBone, int tipId) {
    ToeSkinMesh out;
    if (prisms.empty() || prisms.size()!=boneIds.size() || tip.size()!=12) return out;
    for (const auto& prism:prisms) if (prism.size()!=16) return {};
    auto ring=[&](const std::vector<Vec3>& prism, double t, int owner) {
        std::array<int,8> result{};
        for (int k=0;k<8;++k) result[k]=out.vertex(prism[k]*(1-t)+prism[k+8]*t,owner);
        return result;
    };
    auto join=[&](const std::array<int,8>& a,const std::array<int,8>& b) {
        for (int k=0;k<8;++k) {
            const int j=(k+1)%8;
            out.triangle(a[k],a[j],b[j]); out.triangle(a[k],b[j],b[k]);
        }
    };
    std::array<int,8> last{};
    for (std::size_t i=0;i<prisms.size();++i) {
        const auto rear=ring(prisms[i],i==0 ? 0.0 : 0.12,boneIds[i]);
        const auto front=ring(prisms[i],i+1==prisms.size() ? 0.88 : 0.85,boneIds[i]);
        if (i==0) for (int k=1;k<7;++k) out.triangle(rear[0],rear[k+1],rear[k]);
        else join(last,rear);
        join(rear,front);
        last=front;
    }
    // Clip the icosahedron at its centre, retaining its visible forward half.
    // Shared edge intersections ensure adjacent triangles use the same vertex.
    constexpr std::array<std::array<int,3>,20> ico={{{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},
        {3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}}};
    Vec3 rear{},plane{};
    for (int k=0;k<8;++k) { rear+=prisms.back()[k]; plane+=prisms.back()[k+8]; }
    rear=rear/8.0; plane=plane/8.0;
    const Vec3 axis=normalized(plane-rear);
    if (length(axis)<1e-8) return {};
    const Vec3 u=normalized(prisms.back()[8]-plane);
    const Vec3 v=normalized(cross(axis,u));
    std::array<double,12> distance{};
    std::array<int,12> originals{}; originals.fill(-1);
    for (int i=0;i<12;++i) distance[i]=dot(tip[i]-plane,axis);
    std::map<std::pair<int,int>,int> edges;
    std::vector<int> boundary;
    const double epsilon=1e-9;
    auto original=[&](int i) {
        if (originals[i]<0) originals[i]=out.vertex(tip[i],tipId);
        if (std::abs(distance[i])<epsilon) boundary.push_back(originals[i]);
        return originals[i];
    };
    auto crossing=[&](int a,int b) {
        if (std::abs(distance[a])<epsilon) return original(a);
        if (std::abs(distance[b])<epsilon) return original(b);
        const auto key=std::minmax(a,b);
        const std::pair<int,int> edge{key.first,key.second};
        auto found=edges.find(edge);
        if (found!=edges.end()) return found->second;
        const double t=distance[a]/(distance[a]-distance[b]);
        const int id=out.vertex(tip[a]+(tip[b]-tip[a])*t,tipId);
        edges.emplace(edge,id); boundary.push_back(id); return id;
    };
    const std::size_t tipFaceStart=out.faces.size();
    for (const auto& face:ico) {
        std::vector<int> polygon;
        for (int k=0;k<3;++k) {
            const int a=face[k], b=face[(k+1)%3];
            const bool insideA=distance[a]>=-epsilon, insideB=distance[b]>=-epsilon;
            if (insideA) polygon.push_back(original(a));
            if (insideA!=insideB) polygon.push_back(crossing(a,b));
        }
        for (std::size_t k=1;k+1<polygon.size();++k) out.triangle(polygon[0],polygon[k],polygon[k+1]);
    }
    cutToeNail(out,tipFaceStart,tip,tipBone,axis,plane,tipId);
    std::sort(boundary.begin(),boundary.end());
    boundary.erase(std::unique(boundary.begin(),boundary.end()),boundary.end());
    if (boundary.size()<3 || length(u)<1e-8 || length(v)<1e-8) return {};
    constexpr double twoPi=6.28318530717958647692;
    auto angle=[&](int id) {
        const Vec3 p=out.points[std::size_t(id)]-plane;
        double a=std::atan2(dot(p,v),dot(p,u));
        return a<0 ? a+twoPi : a;
    };
    std::sort(boundary.begin(),boundary.end(),[&](int a,int b) {return angle(a)<angle(b);});
    std::vector<int> back(last.begin(),last.end());
    std::sort(back.begin(),back.end(),[&](int a,int b) {return angle(a)<angle(b);});
    // Stitch two loops, advancing whichever next vertex has the smaller angle.
    std::size_t a=0,b=0;
    while (a<back.size() || b<boundary.size()) {
        const double nextA=a+1<back.size() ? angle(back[a+1]) : twoPi+angle(back[0]);
        const double nextB=b+1<boundary.size() ? angle(boundary[b+1]) : twoPi+angle(boundary[0]);
        const int av=back[a%back.size()], bv=boundary[b%boundary.size()];
        if (a<back.size() && (b==boundary.size() || nextA<=nextB)) {
            out.triangle(av,back[(a+1)%back.size()],bv); ++a;
        } else {
            out.triangle(av,boundary[(b+1)%boundary.size()],bv); ++b;
        }
    }
    return out;
}
} // namespace gtc::native
