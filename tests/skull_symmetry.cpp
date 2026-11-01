// Verify the actual comparison skull/jaw meshes, including their triangle
// winding, and prove that the white reference mesh remains the source mesh.
#include "client/skeleton_room.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
using namespace gtc::native;

static Vec3 transform(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
using PointKey=std::array<long long,3>;
using FaceKey=std::array<PointKey,3>;
static FaceKey canonical(FaceKey f) {
    const FaceKey a={f[1],f[2],f[0]}, b={f[2],f[0],f[1]};
    return std::min({f,a,b});
}
static void mirroredTriangles(const SkeletonComparison& copy,const SkeletonComparison::Part& part,
                              SkullMidplane plane,bool mapped) {
    if (mapped) plane.origin=copy.map(plane.origin,0,true);
    std::vector<PointKey> vertices; vertices.reserve(part.points.size());
    for (Vec3 p:part.points) {
        if (mapped) p=copy.map(p,part.armSide,part.cranial,part.girdle,part.humerus);
        p=plane.local(p);
        vertices.push_back({std::llround(p.x*1e8),std::llround(p.y*1e8),std::llround(p.z*1e8)});
    }
    std::map<FaceKey,int> faces;
    for (auto f:part.faces) {
        for (int i:f) assert(i>=0 && static_cast<std::size_t>(i)<vertices.size());
        ++faces[canonical({vertices[f[0]],vertices[f[1]],vertices[f[2]]})];
    }
    for (auto f:part.faces) {
        auto a=vertices[f[0]],b=vertices[f[1]],c=vertices[f[2]];
        a[0]=-a[0]; b[0]=-b[0]; c[0]=-c[0];
        auto found=faces.find(canonical({a,c,b}));
        assert(found!=faces.end() && found->second>0);
        --found->second;
    }
    for (const auto& entry:faces) assert(entry.second==0);
}
// Independent exhaustive point-to-triangle distance for checking the BVH.
static Vec3 bruteTriangle(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    const Vec3 ab=b-a, ac=c-a, n=cross(ab,ac);
    const double nn=dot(n,n);
    if (nn>1e-30) {
        const Vec3 q=p-n*(dot(p-a,n)/nn), aq=q-a;
        const double aa=dot(ab,ab), bb=dot(ab,ac), cc=dot(ac,ac);
        const double d=aa*cc-bb*bb;
        const double u=(dot(aq,ab)*cc-dot(aq,ac)*bb)/d;
        const double v=(dot(aq,ac)*aa-dot(aq,ab)*bb)/d;
        if (u>=0 && v>=0 && u+v<=1) return q;
    }
    const auto segment=[&](Vec3 x,Vec3 y) {
        const Vec3 edge=y-x; const double d=dot(edge,edge);
        return d>1e-30 ? x+edge*std::clamp(dot(p-x,edge)/d,0.,1.) : x;
    };
    Vec3 best=segment(a,b);
    for (Vec3 q:{segment(b,c),segment(c,a)}) if (dot(p-q,p-q)<dot(p-best,p-best)) best=q;
    return best;
}
static void checkSurfaceQueries() {
    const std::vector<Vec3> p={{0,0,0},{1,0,0},{0,1,0},{2,0,0},{3,0,0},{4,0,0}};
    const std::vector<std::array<int,3>> f={{0,1,2},{3,4,5}};
    bilateral::TriangleSurfaceTree surface(p,f);
    assert(length(surface.nearest({0.25,0.25,1})-Vec3{0.25,0.25,0})<1e-12);
    assert(length(surface.nearest({1,1,0})-Vec3{0.5,0.5,0})<1e-12);
    assert(length(surface.nearest({2.5,1,0})-Vec3{2.5,0,0})<1e-12); // degenerate triangle
    // A closer, oppositely facing thin wall is not the correct match.
    const std::vector<Vec3> thin={{0,0,0},{1,0,0},{0,1,0}, {0,0,.0002},{1,0,.0002},{0,1,.0002}};
    const std::vector<std::array<int,3>> sheets={{0,1,2},{3,4,5}};
    const std::vector<Vec3> normals={{0,0,1},{0,0,1},{0,0,1},{0,0,-1},{0,0,-1},{0,0,-1}};
    const Vec3 direction{0,0,1};
    bilateral::TriangleSurfaceTree oriented(thin,sheets,&normals);
    assert(oriented.match({.25,.25,.00019},&direction).face==0);
}
static Vec3 rotate3(Vec3 p) {
    // Three independent nonzero rotations, not just a level horizontal scan.
    const auto rx=[](Vec3 v,double a) { return Vec3{v.x,v.y*std::cos(a)-v.z*std::sin(a),v.y*std::sin(a)+v.z*std::cos(a)}; };
    const auto ry=[](Vec3 v,double a) { return Vec3{v.x*std::cos(a)+v.z*std::sin(a),v.y,-v.x*std::sin(a)+v.z*std::cos(a)}; };
    const auto rz=[](Vec3 v,double a) { return Vec3{v.x*std::cos(a)-v.y*std::sin(a),v.x*std::sin(a)+v.y*std::cos(a),v.z}; };
    return rz(ry(rx(p,0.62),-0.91),1.17);
}
static std::array<double,16> rotationMatrix() {
    const Vec3 x=rotate3({1,0,0}),y=rotate3({0,1,0}),z=rotate3({0,0,1}),translation{0.2,-0.4,0.7};
    return {x.x,x.y,x.z,0,y.x,y.y,y.z,0,z.x,z.y,z.z,0,translation.x,translation.y,translation.z,1};
}
static void checkRotation(bool male) {
    const Vec3 translation{0.2,-0.4,0.7};
    const auto rotated=rotationMatrix();
    const std::array<double,16> identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const auto original=skullMidplane(male,identity), posed=skullMidplane(male,rotated);
    for (Vec3 local:std::vector<Vec3>{{0,0,0},{0.03,-0.04,0.05},{-0.06,0.02,-0.07}}) {
        const Vec3 raw=original.world(local), world=rotate3(raw)+translation;
        assert(length(posed.local(world)-local)<1e-12);
        assert(length(posed.world(local)-world)<1e-12);
    }
}
static void checkSkull(SkeletonRoom& room,bool male) {
    const auto& model=male?room.male():room.female();
    const auto& copy=room.comparison(male);
    const auto matrices=model.sourceMatrices({},0);
    const int headId=model.sourceStructure("Head"); assert(headId>=0);
    const SkullMidplane plane=skullMidplane(male,matrices[headId]);
    SkeletonModel source; std::string error;
    assert(source.load(std::string("tools/skeleton-source/skeleton-")+(male?"male":"female")+".obj",error));
    // Check the skull-only plane against the original skull and independent
    // eye landmarks, rather than comparing it with a body or hip midline.
    const std::array<double,16> identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const SkullMidplane rawPlane=skullMidplane(male,identity);
    const int rawHeadId=source.sourceStructure("Head"); assert(rawHeadId>=0);
    const auto& head=source.sourceStructures()[static_cast<std::size_t>(rawHeadId)];
    const auto& headGroup=source.bones()[head.group];
    std::ifstream maskFile(std::string("tools/skeleton-source/skeleton-")+(male?"male":"female")+".skull-exterior");
    assert(maskFile); std::string line; std::vector<bool> outside;
    while (std::getline(maskFile,line)) {
        if (!line.empty() && line[0]=='#') continue;
        for (char bit:line) { assert(bit=='0' || bit=='1' || bit=='\r'); if (bit!='\r') outside.push_back(bit=='1'); }
    }
    assert(outside.size()==head.triangleCount);
    std::vector<Vec3> skull(headGroup.points.begin()+head.firstPoint,
                            headGroup.points.begin()+head.firstPoint+head.pointCount);
    // The offline voxel regression checks the fit itself against selected
    // outer surface voxels; these independent eye estimates are diagnostic.
    const auto& eyes=*model.orbitalCenters();
    assert(std::abs(dot((eyes[0]+eyes[1])*0.5-rawPlane.origin,rawPlane.normal))<0.003);
    assert(std::abs(dot(normalized(eyes[1]-eyes[0]),rawPlane.normal))>std::cos(5.0*3.141592653589793/180));
    // Repeat the actual skull surface averaging after arbitrary pitch, yaw,
    // roll and translation. It must produce the same local geometry.
    const auto rotation=rotationMatrix(); const auto rotatedPlane=skullMidplane(male,rotation);
    std::vector<Vec3> unrotated,rotated;
    for (Vec3 p:skull) {
        unrotated.push_back(rawPlane.local(p));
        rotated.push_back(rotatedPlane.local(transform(rotation,p)));
    }
    std::vector<std::array<int,3>> headFaces;
    for (std::size_t i=0;i<head.triangleCount;++i) {
        auto f=headGroup.triangles[head.firstTriangle+i];
        for (int& v:f) v-=static_cast<int>(head.firstPoint);
        headFaces.push_back(f);
    }
    assert(bilateral::surfaceAverage(unrotated,headFaces,&outside,true));
    assert(bilateral::surfaceAverage(rotated,headFaces,&outside,true));
    for (std::size_t i=0;i<unrotated.size();++i) assert(length(unrotated[i]-rotated[i])<1e-9);
    for (const char* name:{"Head","Jaw"}) {
        const int id=model.sourceStructure(name), rawId=source.sourceStructure(name);
        assert(id>=0 && rawId>=0);
        const auto& s=model.sourceStructures()[id]; const auto& raw=source.sourceStructures()[rawId];
        const auto& group=model.bones()[s.group]; const auto& rawGroup=source.bones()[raw.group];
        assert(s.pointCount==raw.pointCount && s.triangleCount==raw.triangleCount);
        std::vector<Vec3> original,opposite;
        std::vector<std::array<int,3>> triangles;
        for (std::size_t i=0;i<s.pointCount;++i) {
            const Vec3 p=group.points[s.firstPoint+i], q=rawGroup.points[raw.firstPoint+i];
            assert(p.x==q.x && p.y==q.y && p.z==q.z);
            Vec3 posed=plane.local(transform(matrices[id],p)); original.push_back(posed);
            posed.x=-posed.x; opposite.push_back(posed);
        }
        for (std::size_t i=0;i<s.triangleCount;++i) {
            auto f=group.triangles[s.firstTriangle+i], rf=rawGroup.triangles[raw.firstTriangle+i];
            for (int j=0;j<3;++j) {
                f[j]-=static_cast<int>(s.firstPoint); rf[j]-=static_cast<int>(raw.firstPoint);
                assert(f[j]==rf[j]);
            }
            triangles.push_back(f);
        }
        const bool isHead=std::string(name)=="Head";
        auto averaged=original;
        assert(bilateral::surfaceAverage(averaged,triangles,isHead ? &outside : nullptr,true));
        // Independently verify the underlying nearest-triangle spatial tree.
        // Averaging also considers oriented and reverse correspondences.
        bilateral::TriangleSurfaceTree reflectedSurface(opposite,triangles);
        std::vector<std::array<int,3>> exteriorTriangles;
        std::vector<bool> exteriorVertex(original.size());
        if (isHead) for (std::size_t i=0;i<triangles.size();++i) if (outside[i]) {
            exteriorTriangles.push_back(triangles[i]);
            for (int v:triangles[i]) exteriorVertex[static_cast<std::size_t>(v)]=true;
        }
        bilateral::TriangleSurfaceTree exteriorSurface(opposite,exteriorTriangles);
        for (std::size_t sample=0;sample<32;++sample) {
            const std::size_t index=sample*original.size()/32;
            const Vec3 p=original[index]; const bool useExterior=isHead && exteriorVertex[index];
            const Vec3 nearest=useExterior ? exteriorSurface.nearest(p) : reflectedSurface.nearest(p);
            double brute=1e100;
            for (auto face:useExterior ? exteriorTriangles : triangles) {
                const Vec3 q=bruteTriangle(p,opposite[face[0]],opposite[face[1]],opposite[face[2]]);
                brute=std::min(brute,dot(p-q,p-q));
            }
            assert(std::abs(dot(p-nearest,p-nearest)-brute)<1e-13);
        }
        double maxDisplacement=0;
        for (std::size_t i=0;i<original.size();++i) {
            assert(std::isfinite(averaged[i].x) && std::isfinite(averaged[i].y) && std::isfinite(averaged[i].z));
            maxDisplacement=std::max(maxDisplacement,length(averaged[i]-original[i]));
        }
        assert(maxDisplacement>1e-5);
        // Compare each source side's area-weighted surface distance to the
        // result against an unguarded one-way nearest-midpoint baseline.
        auto oneWay=original; auto baselineFaces=triangles;
        for (std::size_t i=0;i<oneWay.size();++i) {
            const Vec3 nearest=isHead && exteriorVertex[i] ? exteriorSurface.nearest(original[i])
                                                            : reflectedSurface.nearest(original[i]);
            oneWay[i]=(original[i]+nearest)*0.5;
        }
        assert(bilateral::mirrorAxial(oneWay,baselineFaces,0));
        assert(bilateral::mirrorAxial(averaged,triangles,0));
        bilateral::TriangleSurfaceTree prior(oneWay,baselineFaces), revised(averaged,triangles);
        std::array<double,2> area{},oldDistance{},newDistance{};
        for (std::size_t i=0;i<s.triangleCount;++i) {
            auto f=group.triangles[s.firstTriangle+i];
            for (int& v:f) v-=static_cast<int>(s.firstPoint);
            if (isHead && !outside[i]) continue;
            const Vec3 a=original[f[0]],b=original[f[1]],c=original[f[2]];
            if (std::min({std::abs(a.x),std::abs(b.x),std::abs(c.x)})<.003) continue;
            Vec3 point=(a+b+c)/3; const int side=point.x>=0 ? 0 : 1;
            if (side) point.x=-point.x;
            const double weight=length(cross(b-a,c-a))*0.5;
            area[side]+=weight;
            oldDistance[side]+=weight*length(prior.nearest(point)-point);
            newDistance[side]+=weight*length(revised.nearest(point)-point);
        }
        for (int side=0;side<2;++side) {
            assert(area[side]>0); oldDistance[side]/=area[side]; newDistance[side]/=area[side];
        }
        if (!isHead) {
            const double oldBias=std::abs(oldDistance[0]-oldDistance[1]);
            const double newBias=std::abs(newDistance[0]-newDistance[1]);
            assert(newBias<oldBias*.8);
        }
        std::printf("%s %s source-side mean distances old %.3f/%.3f new %.3f/%.3f mm\n",male?"male":"female",name,
                    oldDistance[0]*1000,oldDistance[1]*1000,newDistance[0]*1000,newDistance[1]*1000);
        const auto& result=copy.parts()[static_cast<std::size_t>(id)];
        assert(result.points.size()==averaged.size() && result.faces==triangles);
        for (std::size_t i=0;i<averaged.size();++i) assert(length(result.points[i]-plane.world(averaged[i]))<1e-12);
        mirroredTriangles(copy,result,plane,false); mirroredTriangles(copy,result,plane,true);
        std::printf("%s %s: source unchanged; max average displacement %.3f mm, %zu mirrored triangles\n",
                    male?"male":"female",name,maxDisplacement*1000,result.faces.size());
    }
    for (double height:{50.,250.}) for (double headMultiplier:{0.8,1.2}) {
        auto profile=copy.profile(); profile.height=height; profile.multiplier[SkeletonComparison::Head]=headMultiplier;
        assert(room.setComparison(male,profile,error));
        for (const auto& part:copy.parts()) if (part.name=="Head" || part.name=="Jaw")
            mirroredTriangles(copy,part,plane,true);
    }
    room.resetComparison(male);
}
static void checkBalancedAverage() {
    // Matching identical mirrored surfaces must not distort them.
    std::vector<Vec3> p={{.01,-.01,-.01},{.01,.01,-.01},{.01,.01,.01},{.01,-.01,.01},
                         {-.01,-.01,-.01},{-.01,.01,-.01},{-.01,.01,.01},{-.01,-.01,.01}};
    std::vector<std::array<int,3>> f={{0,1,2},{0,2,3},{4,6,5},{4,7,6}};
    auto original=p;
    assert(bilateral::surfaceAverage(p,f));
    for (std::size_t i=0;i<p.size();++i) assert(length(p[i]-original[i])<1e-12);
    // A symmetric curved feature must not drift due to its vertex/face normals.
    auto curved=original; curved.push_back({.014,0,0}); curved.push_back({-.014,0,0});
    const std::vector<std::array<int,3>> curvedFaces={{0,1,8},{1,2,8},{2,3,8},{3,0,8},
                                                       {4,9,5},{5,9,6},{6,9,7},{7,9,4}};
    auto unchanged=curved; assert(bilateral::surfaceAverage(curved,curvedFaces));
    for (std::size_t i=0;i<curved.size();++i) assert(length(curved[i]-unchanged[i])<1e-12);
    // Parallel sheets displaced by different amounts meet at their 50/50 mean.
    p=original; for (int i=0;i<4;++i) p[i].x+=.002;
    for (int i=4;i<8;++i) p[i].x+=.002;
    assert(bilateral::surfaceAverage(p,f));
    for (int i=0;i<4;++i) assert(std::abs(p[i].x-.01)<1e-12);
    for (int i=4;i<8;++i) assert(std::abs(p[i].x+.01)<1e-12);
    // Different triangulations of the same planar surface must not drift.
    p=original; p.push_back({-.01,0,0});
    f={{0,1,2},{0,2,3},{4,8,5},{5,8,6},{6,8,7},{7,8,4}};
    auto retessellated=p; assert(bilateral::surfaceAverage(p,f));
    for (std::size_t i=0;i<p.size();++i) assert(length(p[i]-retessellated[i])<1e-12);
    // A detail only on a densely tessellated opposite side must influence the
    // coarse template, even though none of its vertices sits at the detail.
    p=retessellated; p[8].x=-.014;
    assert(bilateral::surfaceAverage(p,f));
    double shift=0; for (int i=0;i<4;++i) shift+=p[i].x-.01;
    assert(shift>1e-5);
    const std::vector<std::array<int,3>> coarse={{0,1,2},{0,2,3}};
    bilateral::TriangleSurfaceTree oldSurface(original,coarse), newSurface(p,coarse);
    double oldError=0,newError=0;
    // Samples of the known halfway surface: the corners stay put and the
    // opposite-side 4 mm bump contributes a 2 mm midpoint bump.
    for (int i=0;i<4;++i) {
        const Vec3 target=(original[i]+original[(i+1)%4]+Vec3{.012,0,0})/3;
        const Vec3 a=oldSurface.nearest(target)-target,b=newSurface.nearest(target)-target;
        oldError+=dot(a,a); newError+=dot(b,b);
    }
    assert(newError<oldError*.8);
    // Interior-only geometry may not add reverse constraints to exterior vertices.
    auto shell=original; shell.insert(shell.end(),{{.009,0,-.005},{.009,.005,.005},{.009,-.005,.005}});
    auto other=shell; for (int i=8;i<11;++i) other[i].x-=.003;
    const std::vector<std::array<int,3>> nested={{0,1,2},{0,2,3},{4,6,5},{4,7,6},{8,9,10}};
    const std::vector<bool> exterior={true,true,true,true,false};
    assert(bilateral::surfaceAverage(shell,nested,&exterior));
    assert(bilateral::surfaceAverage(other,nested,&exterior));
    for (int i=0;i<8;++i) assert(length(shell[i]-other[i])<1e-12);
    std::puts("Balanced skull averaging: PASS (unchanged symmetry, true midpoint, opposite-side detail)");
}
int main() {
    checkSurfaceQueries(); checkBalancedAverage(); checkRotation(true); checkRotation(false);
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    checkSkull(room,true); checkSkull(room,false);
    std::puts("Skull and jaw bilateral triangle symmetry: PASS");
}
