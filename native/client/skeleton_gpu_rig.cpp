#include "client/skeleton_gpu_rig.h"
#include "client/skeleton_body.h"
#include "client/skeleton_comparison.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <limits>
#include <set>
#include <utility>

namespace gtc::native {
namespace {
using gpu_physics::Body;
using gpu_physics::Joint;

Vec3 vectorOf(const std::array<float,4>& a) { return {a[0],a[1],a[2]}; }
std::array<float,4> packed(Vec3 v, double w=0) { return {float(v.x),float(v.y),float(v.z),float(w)}; }
Vec3 apply(const std::array<double,16>& m, Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
            m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
Vec3 direction(const JointRigidTransform& t, Vec3 p) {
    return {t.rotation[0]*p.x+t.rotation[1]*p.y+t.rotation[2]*p.z,
            t.rotation[3]*p.x+t.rotation[4]*p.y+t.rotation[5]*p.z,
            t.rotation[6]*p.x+t.rotation[7]*p.y+t.rotation[8]*p.z};
}

// Approximate a body's collision mesh by small spheres covering occupied cells.
// These are deliberately proxies, not a triangle-accurate collision model.
// Original bone vertices remain unchanged for drawing.
bool makeBody(const std::vector<Vec3>& points, GpuSkeletonRig& result, Vec3& center, double minimumMass) {
    if (points.empty()) return false;
    Vec3 low=points.front(), high=low;
    for (const Vec3 p:points) {
        if (!joint_detail::finite(p)) return false;
        low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); low.z=std::min(low.z,p.z);
        high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y); high.z=std::max(high.z,p.z);
    }
    center=(low+high)*0.5;
    const Vec3 extent=high-low;
    const double mass=std::clamp(extent.x*extent.y*extent.z*500.0,minimumMass,6.0);
    Body body; body.positionMass=packed(center,1.0/mass);
    const auto inv=[&](double a,double b) { return float(12.0/(mass*std::max(a*a+b*b,1e-8))); };
    body.inverseInertia={inv(extent.y,extent.z),inv(extent.x,extent.z),inv(extent.x,extent.y),0};
    body.samples[0]=static_cast<std::uint32_t>(result.spheres.size());
    // Subdivide long thin bones along their length so their proxies do not
    // become a handful of oversized spheres. Bound the per-body budget.
    const double longest=std::max({extent.x,extent.y,extent.z,1e-9});
    int divisions[3]={std::clamp(int(std::ceil(extent.x*12/longest)),1,12),
                      std::clamp(int(std::ceil(extent.y*12/longest)),1,12),
                      std::clamp(int(std::ceil(extent.z*12/longest)),1,12)};
    while (divisions[0]*divisions[1]*divisions[2]>64) {
        int axis=divisions[1]>divisions[0] ? 1 : 0;
        if (divisions[2]>divisions[axis]) axis=2;
        --divisions[axis];
    }
    struct Cell { Vec3 low{},high{}; bool used=false; };
    std::vector<Cell> cells(std::size_t(divisions[0]*divisions[1]*divisions[2]));
    const auto cellIndex=[](double value,double start,double size,int count) {
        return size<1e-12 ? 0 : std::clamp(int((value-start)*count/size),0,count-1);
    };
    for (Vec3 p:points) {
        const int x=cellIndex(p.x,low.x,extent.x,divisions[0]), y=cellIndex(p.y,low.y,extent.y,divisions[1]);
        const int z=cellIndex(p.z,low.z,extent.z,divisions[2]);
        auto& cell=cells[std::size_t(x+divisions[0]*(y+divisions[1]*z))];
        if (!cell.used) { cell.low=cell.high=p; cell.used=true; continue; }
        cell.low.x=std::min(cell.low.x,p.x); cell.low.y=std::min(cell.low.y,p.y); cell.low.z=std::min(cell.low.z,p.z);
        cell.high.x=std::max(cell.high.x,p.x); cell.high.y=std::max(cell.high.y,p.y); cell.high.z=std::max(cell.high.z,p.z);
    }
    for (const auto& cell:cells) if (cell.used) {
        const Vec3 local=(cell.low+cell.high)*0.5-center;
        const double radius=std::max(0.001,length(cell.high-cell.low)*0.5+0.001);
        result.spheres.push_back(packed(local,radius));
        body.inverseInertia[3]=std::max(body.inverseInertia[3],float(length(local)+radius+0.0001));
        body.bounds[0]=std::max(body.bounds[0],float(std::abs(local.x)+radius));
        body.bounds[1]=std::max(body.bounds[1],float(std::abs(local.y)+radius));
        body.bounds[2]=std::max(body.bounds[2],float(std::abs(local.z)+radius));
    }
    body.samples[1]=static_cast<std::uint32_t>(result.spheres.size()-body.samples[0]);
    result.bodies.push_back(body);
    return true;
}

struct FigureInput {
    const SkeletonModel* model=nullptr;
    const SkeletonComparison* comparison=nullptr;
    Vec3 origin{};
    std::uint32_t idBase=0;
};

bool appendFigure(const FigureInput& input, GpuSkeletonRig& result, std::string& error) {
    const auto& model=*input.model;
    const auto* comparison=input.comparison;
    const auto* rig=model.sourceRig();
    const auto& parts=model.sourceStructures();
    const auto& mapping=model.sourceBodyMap();
    if (!rig || mapping.size()!=parts.size() || (comparison && comparison->parts().size()!=parts.size())) {
        error="Missing skeleton rig or comparison parts"; return false;
    }
    const auto matrices=model.sourceMatrices(comparison ? Vec3{} : input.origin,0);
    if (matrices.size()!=parts.size()) { error="Cannot evaluate skeleton pose"; return false; }
    const auto bluePose=comparison ? comparison->toePose(matrices,input.origin) : std::vector<std::array<double,16>>{};
    std::vector<int> structureForNode(rig->size(),-1), groupForNode(rig->size(),-1);
    for (std::size_t i=0;i<mapping.size();++i) {
        if (mapping[i]>=rig->size() || structureForNode[mapping[i]]>=0 || !parts[i].pointCount ||
            (comparison && (comparison->parts()[i].name!=parts[i].name || comparison->parts()[i].points.empty()))) {
            error="Invalid skeleton structure mapping"; return false;
        }
        structureForNode[mapping[i]]=static_cast<int>(i);
    }
    std::vector<std::vector<std::size_t>> groups;
    for (std::size_t node=1;node<rig->size();++node) {
        const auto parent=*rig->parent(node);
        const int index=structureForNode[node];
        if (index<0 || rig->spatialJoint(node)) { error="Unsupported skeleton joint"; return false; }
        if (parent==0 || rig->joint(node)) { groupForNode[node]=int(groups.size()); groups.emplace_back(); }
        else groupForNode[node]=groupForNode[parent];
        groups[std::size_t(groupForNode[node])].push_back(std::size_t(index));
    }
    // Radius/ulna and wrist bones must stay together just like the visual rig.
    for (const auto& link:model.sourceLinks()) if (link.mobility=="radioulnar_pivot" || link.mobility=="carpal_glide") {
        const int first=groupForNode[mapping[link.first]], second=groupForNode[mapping[link.second]];
        if (first==second) continue;
        groups[std::size_t(first)].insert(groups[std::size_t(first)].end(),groups[std::size_t(second)].begin(),groups[std::size_t(second)].end());
        groups[std::size_t(second)].clear();
        for (int& group:groupForNode) if (group==second) group=first;
    }
    std::vector<int> bodyForGroup(groups.size(),-1);
    std::vector<Vec3> mappedCenters(parts.size()), rawCenters(parts.size());
    for (std::size_t group=0;group<groups.size();++group) {
        if (groups[group].empty()) continue;
        std::vector<Vec3> vertices;
        for (auto i:groups[group]) {
            const auto& part=parts[i]; const auto& raw=model.bones()[part.group].points;
            for (std::size_t p=0;p<part.pointCount;++p) rawCenters[i]+=raw[part.firstPoint+p];
            rawCenters[i]=apply(matrices[i],rawCenters[i]/double(part.pointCount));
            if (comparison) {
                const auto& blue=comparison->parts()[i];
                for (Vec3 p:blue.points) {
                    p=apply(bluePose[i],comparison->mapPart(p,blue)+input.origin);
                    mappedCenters[i]+=p; vertices.push_back(p);
                }
                mappedCenters[i]=mappedCenters[i]/double(blue.points.size());
                if (part.name.rfind("FootDistP",0)==0) {
                    auto toes=comparison->toeIcosahedron(i,input.origin);
                    for (Vec3& p:toes) p=apply(bluePose[i],p);
                    vertices.insert(vertices.end(),toes.begin(),toes.end());
                }
                if (part.name.rfind("FootMidP",0)==0 || part.name.rfind("FootProxP",0)==0) {
                    auto segment=comparison->toePrism(i,input.origin);
                    for (Vec3& p:segment) p=apply(bluePose[i],p);
                    vertices.insert(vertices.end(),segment.begin(),segment.end());
                }
            } else {
                for (std::size_t p=0;p<part.pointCount;++p) vertices.push_back(apply(matrices[i],raw[part.firstPoint+p]));
            }
        }
        Vec3 center{};
        bodyForGroup[group]=int(result.bodies.size());
        const auto& name=parts[groups[group].front()].name;
        const bool vertebra=name.size()>=2 && name.size()<=3 && (name[0]=='C' || name[0]=='T' || name[0]=='L') && name[1]>='0' && name[1]<='9';
        if (!makeBody(vertices,result,center,vertebra ? 0.3 : 0.015)) { error="Unable to build collision proxy"; return false; }
        const auto body=static_cast<std::uint32_t>(bodyForGroup[group]);
        for (auto i:groups[group]) {
            GpuSkeletonBinding binding; binding.objectId=input.idBase+static_cast<std::uint32_t>(i); binding.body=body;
            if (comparison) {
                for (int k=0;k<16;++k) binding.localFromMesh[k]=float(bluePose[i][k]);
                binding.localFromMesh[12]-=float(center.x); binding.localFromMesh[13]-=float(center.y); binding.localFromMesh[14]-=float(center.z);
            } else {
                for (int k=0;k<16;++k) binding.localFromMesh[k]=float(matrices[i][k]);
                binding.localFromMesh[12]-=float(center.x); binding.localFromMesh[13]-=float(center.y); binding.localFromMesh[14]-=float(center.z);
            }
            // Toe skin vertices use these same bone slots; the joined surface
            // has no separate rigid-piece batches to bind.
            result.bindings.push_back(binding);
        }
    }
    const auto& absolute=rig->absoluteTransforms();
    auto anchor=[&](std::size_t index, Vec3 p) {
        if (!comparison) return p;
        const auto& part=comparison->parts()[index];
        const auto mapped=[&](Vec3 q) { return comparison->map(q,part.armSide,part.cranial,part.girdle,part.humerus); };
        return mapped(p)+mappedCenters[index]-mapped(rawCenters[index]);
    };
    std::set<std::pair<int,int>> connected;
    for (std::size_t node=1;node<rig->size();++node) {
        const auto parent=*rig->parent(node); const auto* source=rig->joint(node);
        if (!parent || !source) continue;
        const int a=bodyForGroup[std::size_t(groupForNode[parent])], b=bodyForGroup[std::size_t(groupForNode[node])];
        if (a==b || !connected.insert(std::minmax(a,b)).second) continue;
        const auto transform=joint_detail::compose(absolute[parent],source->parentFrame());
        const auto pi=std::size_t(structureForNode[parent]), si=std::size_t(structureForNode[node]);
        Vec3 pivot=transform.translation, axis=direction(transform,source->axes()[0].direction);
        if (comparison) {
            const Vec3 old=pivot;
            pivot=(anchor(pi,old)+anchor(si,old))*0.5;
            axis=anchor(pi,old+axis*0.01)-anchor(pi,old);
        }
        if (length(axis)<1e-9) { error="Invalid joint axis"; return false; }
        axis=normalized(axis);
        std::string mobility;
        for (const auto& link:model.sourceLinks()) if ((link.first==si && link.second==pi) || (link.first==pi && link.second==si)) { mobility=link.mobility; break; }
        const bool ball=mobility=="hip_ball_socket" || mobility=="shoulder_ball_socket" || mobility=="sternoclavicular" || mobility=="acromioclavicular";
        const bool fitted=ball && mobility!="shoulder_ball_socket" && source->axes().size()>2;
        Vec3 tangent=cross(axis,{0,1,0});
        if (fitted) {
            const auto rotation=joint_detail::rotate(source->axes()[0].direction,source->angles()[0]);
            tangent=direction(transform,direction(rotation,source->axes()[1].direction));
            if (comparison) tangent=anchor(pi,transform.translation+tangent*0.01)-anchor(pi,transform.translation);
            tangent-=axis*dot(axis,tangent);
        }
        if (length(tangent)<1e-6) tangent=cross(axis,{1,0,0});
        if (length(tangent)<1e-6) tangent=cross(axis,{0,0,1});
        tangent=normalized(tangent);
        Joint joint; joint.bodies={std::uint32_t(a),std::uint32_t(b),ball ? 0u : 1u,0};
        joint.parentAnchor=packed(pivot-vectorOf(result.bodies[std::size_t(a)].positionMass));
        joint.childAnchor=packed(pivot-vectorOf(result.bodies[std::size_t(b)].positionMass));
        const double piValue=3.14159265358979323846;
        const auto lower=[&](std::size_t k) { return float(std::clamp(source->axes()[k].minimum-source->angles()[k],-piValue,piValue)); };
        const auto upper=[&](std::size_t k) { return float(std::clamp(source->axes()[k].maximum-source->angles()[k],-piValue,piValue)); };
        joint.parentAxis=packed(axis,lower(0)); joint.childAxis=packed(axis,upper(0));
        joint.parentTangent=joint.childTangent=packed(tangent);
        joint.lowerAngles={lower(0),float(-piValue/2),float(-piValue/2),0};
        joint.upperAngles={upper(0),float(piValue/2),float(piValue/2),0};
        if (fitted) {
            joint.lowerAngles[1]=lower(1); joint.upperAngles[1]=upper(1);
            joint.lowerAngles[2]=lower(2); joint.upperAngles[2]=upper(2);
            const auto first=joint_detail::rotate(source->axes()[0].direction,source->angles()[0]);
            const auto second=joint_detail::rotate(source->axes()[1].direction,source->angles()[1]);
            Vec3 third=direction(transform,direction(joint_detail::compose(first,second),source->axes()[2].direction));
            if (comparison) third=anchor(pi,transform.translation+third*0.01)-anchor(pi,transform.translation);
            if (dot(cross(axis,tangent),third)<0) {
                const float old=joint.lowerAngles[2]; joint.lowerAngles[2]=-joint.upperAngles[2]; joint.upperAngles[2]=-old;
            }
        }
        result.joints.push_back(joint);
    }
    // Close the rib/sternum loops at the nearest proxy attachment, rather than
    // leaving the rib cage connected only through the vertebral chain.
    for (const auto& link:model.sourceLinks()) if (link.kind=="cartilage") {
        const int a=bodyForGroup[std::size_t(groupForNode[mapping[link.first]])];
        const int b=bodyForGroup[std::size_t(groupForNode[mapping[link.second]])];
        if (a==b || !connected.insert(std::minmax(a,b)).second) continue;
        const Body& ba=result.bodies[std::size_t(a)]; const Body& bb=result.bodies[std::size_t(b)];
        Vec3 ca=vectorOf(ba.positionMass), cb=vectorOf(bb.positionMass), pivot{};
        double best=std::numeric_limits<double>::infinity();
        for (std::uint32_t i=0;i<ba.samples[1];++i) for (std::uint32_t j=0;j<bb.samples[1];++j) {
            const auto& sa=result.spheres[ba.samples[0]+i]; const auto& sb=result.spheres[bb.samples[0]+j];
            const Vec3 pa=ca+vectorOf(sa), pb=cb+vectorOf(sb), d=pb-pa;
            const double len=length(d), gap=len-sa[3]-sb[3];
            if (gap<best) {
                const Vec3 n=len>1e-9 ? d/len : Vec3{0,1,0};
                pivot=(pa+n*sa[3]+pb-n*sb[3])*0.5; best=gap;
            }
        }
        if (!std::isfinite(best)) { error="Missing rib contact proxy"; return false; }
        Joint joint; joint.bodies={std::uint32_t(a),std::uint32_t(b),2,0};
        joint.parentAnchor=packed(pivot-ca); joint.childAnchor=packed(pivot-cb);
        result.joints.push_back(joint);
    }
    // Break the exactly balanced stance in the same direction as the old drop.
    if (!groups.empty() && bodyForGroup.front()>=0) {
        auto& root=result.bodies[std::size_t(bodyForGroup.front())];
        root.linearVelocity[2]=0.8f*root.positionMass[3];
    }
    return true;
}

void appendChunk(GpuSkeletonRig& output, GpuSkeletonRig&& chunk) {
    const auto base=static_cast<std::uint32_t>(output.bodies.size());
    const auto sampleBase=static_cast<std::uint32_t>(output.spheres.size());
    for (Body& body:chunk.bodies) body.samples[0]+=sampleBase;
    for (Joint& joint:chunk.joints) { joint.bodies[0]+=base; joint.bodies[1]+=base; }
    for (GpuSkeletonBinding& binding:chunk.bindings) binding.body+=base;
    output.bodies.insert(output.bodies.end(),chunk.bodies.begin(),chunk.bodies.end());
    output.spheres.insert(output.spheres.end(),chunk.spheres.begin(),chunk.spheres.end());
    output.joints.insert(output.joints.end(),chunk.joints.begin(),chunk.joints.end());
    output.bindings.insert(output.bindings.end(),chunk.bindings.begin(),chunk.bindings.end());
}

void fillExclusions(GpuSkeletonRig& rig) {
    const std::size_t count=rig.bodies.size(), stride=(count+31)/32;
    rig.exclusions.assign(count*stride,0);
    auto exclude=[&](std::size_t a,std::size_t b) {
        rig.exclusions[a*stride+b/32]|=1u<<(b%32);
        rig.exclusions[b*stride+a/32]|=1u<<(a%32);
    };
    std::vector<std::vector<std::uint32_t>> adjacent(count);
    for (const Joint& joint:rig.joints) {
        const auto a=joint.bodies[0],b=joint.bodies[1];
        exclude(a,b); adjacent[a].push_back(b); adjacent[b].push_back(a);
    }
    // Nearby connected bones have overlapping proxies, especially fingers.
    for (std::size_t a=0;a<count;++a) for (auto b:adjacent[a]) for (auto c:adjacent[b]) if (a!=c) exclude(a,c);
    // Preserve source meshes which already intersect: don't launch them apart.
    for (std::size_t i=0;i<count;++i) for (std::size_t j=i+1;j<count;++j) {
        if ((rig.exclusions[i*stride+j/32] & (1u<<(j%32)))!=0) continue;
        const Body& a=rig.bodies[i]; const Body& b=rig.bodies[j];
        const Vec3 ca=vectorOf(a.positionMass), cb=vectorOf(b.positionMass), delta=ca-cb;
        const double limit=a.inverseInertia[3]+b.inverseInertia[3];
        if (dot(delta,delta)>limit*limit || std::abs(delta.x)>a.bounds[0]+b.bounds[0] ||
            std::abs(delta.y)>a.bounds[1]+b.bounds[1] || std::abs(delta.z)>a.bounds[2]+b.bounds[2]) continue;
        bool overlap=false;
        for (std::uint32_t x=0;x<a.samples[1] && !overlap;++x) for (std::uint32_t y=0;y<b.samples[1];++y) {
            const auto& sa=rig.spheres[a.samples[0]+x]; const auto& sb=rig.spheres[b.samples[0]+y];
            const Vec3 d=ca+vectorOf(sa)-cb-vectorOf(sb);
            const double radii=sa[3]+sb[3];
            if (dot(d,d)<radii*radii) { overlap=true; break; }
        }
        if (overlap) exclude(i,j);
    }
}
} // namespace

bool buildGpuSkeletonRig(const SkeletonModel& male, Vec3 maleOrigin, const SkeletonComparison& blueMale,
                         const SkeletonModel& female, Vec3 femaleOrigin, const SkeletonComparison& blueFemale,
                         GpuSkeletonRig& out, std::string& error) {
    struct Chunk { GpuSkeletonRig rig; std::string error; bool ok=false; };
    auto build=[](const SkeletonModel& model, Vec3 origin, const SkeletonComparison& blue, bool maleFigure) {
        Chunk chunk;
        const FigureInput white{&model,nullptr,origin,maleFigure ? 9001u : 8001u};
        const FigureInput comparison{&model,&blue,{maleFigure ? -2.85 : 2.85,0.10,0},maleFigure ? 12000u : 14000u};
        chunk.ok=appendFigure(white,chunk.rig,chunk.error) && appendFigure(comparison,chunk.rig,chunk.error);
        return chunk;
    };
    try {
        auto maleWork=std::async(std::launch::async,build,std::cref(male),maleOrigin,std::cref(blueMale),true);
        Chunk femaleChunk=build(female,femaleOrigin,blueFemale,false);
        Chunk maleChunk=maleWork.get();
        if (!maleChunk.ok || !femaleChunk.ok) { error=maleChunk.ok ? femaleChunk.error : maleChunk.error; return false; }
        GpuSkeletonRig next;
        appendChunk(next,std::move(maleChunk.rig)); appendChunk(next,std::move(femaleChunk.rig));
        if (next.bodies.empty() || next.bodies.size()>8192) { error="Too many GPU skeleton bodies"; return false; }
        fillExclusions(next);
        std::vector<Joint> ordered;
        if (!gpu_physics::colourJoints(next.bodies.size(),next.joints,ordered,next.batches)) {
            error="Unable to colour skeleton joints"; return false;
        }
        next.joints=std::move(ordered);
        out=std::move(next); error.clear(); return true;
    } catch (const std::exception& e) { error=e.what(); return false; }
}
} // namespace gtc::native
