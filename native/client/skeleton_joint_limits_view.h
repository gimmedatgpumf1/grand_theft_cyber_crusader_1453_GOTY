#pragma once

#include "client/skeleton_body.h"
#include "client/skeleton_comparison.h"
#include "client/skeleton_toe_attachment.h"
#include "client/skeleton_toe_limits.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace gtc::native {
// V view only: a red joint point and an arc with end ticks show the primary
// rotational coordinate's actual minimum and maximum. The pale needle follows
// the child. This is a visualization of the existing rig, never a pose edit.
namespace joint_view {
struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<int,3>> faces;
    std::vector<int> owners;
    void vertex(Vec3 p,int owner) { vertices.push_back(p);owners.push_back(owner); }
    void triangle(int a,int b,int c) { faces.push_back({a,b,c});faces.push_back({c,b,a}); }
    void dot(Vec3 p,double radius,int owner) {
        const int n=int(vertices.size());
        for(Vec3 q:std::array<Vec3,6>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}})vertex(p+q*radius,owner);
        for(auto f:std::array<std::array<int,3>,8>{{{0,2,4},{4,2,1},{1,2,5},{5,2,0},
                                                   {4,3,0},{1,3,4},{5,3,1},{0,3,5}}})triangle(n+f[0],n+f[1],n+f[2]);
    }
    void stroke(Vec3 a,Vec3 b,Vec3 planeNormal,double width,int owner) {
        const Vec3 tangent=b-a;if(length(tangent)<1e-10)return;
        Vec3 side=normalized(cross(planeNormal,tangent))*width;
        if(length(side)<1e-8)return;
        const int n=int(vertices.size());
        vertex(a-side,owner);vertex(a+side,owner);vertex(b-side,owner);vertex(b+side,owner);
        triangle(n,n+1,n+2);triangle(n+1,n+3,n+2);
    }
};
inline Vec3 point(const std::array<double,16>& m,Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
inline Vec3 turn(Vec3 axis,double angle,Vec3 p) {return frameVector(joint_detail::rotate(axis,angle),p);}
inline void draw(Rasterizer& out,const SkeletonModel& model,Vec3 origin,std::uint32_t id,
                 const SkeletonComparison* blue=nullptr,const std::vector<std::array<double,16>>* bluePose=nullptr,
                 const std::vector<ComparisonToeAttachment>* toeJoints=nullptr) {
    const auto* rig=model.sourceRig();const auto& parts=model.sourceStructures();const auto& mapping=model.sourceBodyMap();
    if(!rig || mapping.size()!=parts.size() || (blue && (!bluePose || bluePose->size()!=parts.size())))return;
    const auto pose=model.sourceMatrices({},0);if(pose.size()!=parts.size())return;
    const auto& absolute=rig->absoluteTransforms();
    std::vector<int> structure(rig->size(),-1);
    std::vector<Vec3> rawCenter(parts.size()),blueCenter(parts.size());
    for(std::size_t i=0;i<parts.size();++i) {
        if(mapping[i]>=structure.size() || !parts[i].pointCount)continue;
        structure[mapping[i]]=int(i);const auto& part=parts[i];const auto& raw=model.bones()[part.group].points;
        Vec3 center{};for(std::size_t k=0;k<part.pointCount;++k)center+=raw[part.firstPoint+k];
        rawCenter[i]=point(pose[i],center/double(part.pointCount));
        if(blue) {
            const auto& b=blue->parts()[i];if(b.points.empty())continue;
            for(Vec3 p:b.points)blueCenter[i]+=point((*bluePose)[i],blue->mapPart(p,b)+origin);
            blueCenter[i]=blueCenter[i]/double(b.points.size());
        }
    }
    const auto anchor=[&](std::size_t i,Vec3 p) {
        if(!blue)return p+origin;
        const auto& part=blue->parts()[i];const auto& frame=blue->metatarsalFrame(i);
        const auto map=[&](Vec3 q){return blue->mapPart(frameVector(frame,q)+frame.translation,part);};
        return map(p)+blueCenter[i]-map(rawCenter[i]);
    };
    auto pivots=blue ? blue->toePivots(*bluePose,origin) : std::vector<ComparisonToeAttachment>{};
    if(toeJoints)for(const auto& mark:*toeJoints) {
        std::erase_if(pivots,[&](const auto& old){return old.parent==mark.parent && old.child==mark.child;});
        pivots.push_back(mark);
    }
    Mesh arcs,needles;
    for(std::size_t node=1;node<rig->size();++node) {
        const auto parent=rig->parent(node);const auto* joint=rig->joint(node);
        if(!parent || !*parent || !joint || joint->axes().empty() || structure[node]<0 || structure[*parent]<0)continue;
        const std::size_t pi=std::size_t(structure[*parent]),ci=std::size_t(structure[node]);
        const auto& range=joint->axes()[0];if(!std::isfinite(range.minimum)||!std::isfinite(range.maximum))continue;
        const auto frame=joint_detail::compose(absolute[*parent],joint->parentFrame());
        const Vec3 sourceAxis=normalized(frameVector(frame,range.direction));
        Vec3 center=anchor(pi,frame.translation),axis=anchor(pi,frame.translation+sourceAxis*.01)-center;
        if(blue)center=(center+anchor(ci,frame.translation))*.5;
        std::optional<MarkedToeBend> toeBend;
        double markedChildLength=0;
        for(const auto& mark:pivots)if(mark.parent==pi && mark.child==ci) {
            center=mark.pivot;
            markedChildLength=length(mark.childShaft);
            toeBend=markedToeBend(mark.parentShaft,mark.childShaft,mark.dorsal);
            if(toeBend)axis=toeBend->axis;
            else if(length(mark.parentShaft)>1e-8) {
                const Vec3 shaft=normalized(mark.parentShaft),candidate=axis-shaft*dot(axis,shaft);
                if(length(candidate)>1e-8)axis=candidate;
            }
            break;
        }
        const double angle=toeBend ? toeBend->flexion : joint->angles().empty()?0:joint->angles()[0];
        double minimum=range.minimum,maximum=range.maximum;
        bool hinge=bool(toeBend) || joint->axes().size()==1;
        if(length(axis)<1e-8)continue;
        axis=normalized(axis);
        const Vec3 target=blue?blueCenter[ci]:rawCenter[ci]+origin;
        Vec3 current=toeBend ? toeBend->current : target-center;current-=axis*dot(current,axis);
        if(length(current)<1e-8)current=cross(axis,std::abs(axis.y)<.9?Vec3{0,1,0}:Vec3{1,0,0});
        if(length(current)<1e-8)continue;
        current=normalized(current);
        // The untouched B reference can have separated surface marks. Its
        // attachment gap must not inflate the arc to the size of the foot.
        const double radius=toeBend ? std::clamp(markedChildLength*.55,.006,.025)
                                    : std::clamp(length(target-center)*.55,.006,.075);
        const double width=std::clamp(radius*.07,.0007,.0018);
        const Vec3 zero=toeBend ? toeBend->neutral : turn(axis,-angle,current);
        const int owner=blue?int(id+pi):int(pi),childOwner=blue?int(id+ci):int(ci);
        const int segments=std::clamp(int(std::ceil((maximum-minimum)*12)),4,64);
        const auto radial=[&](double a){return turn(axis,a,zero);};
        arcs.dot(center,std::clamp(radius*.18,.0018,.006),owner);
        if(maximum-minimum>1e-8) {
            for(int i=0;i<segments;++i) {
                const double a=minimum+(maximum-minimum)*i/segments;
                const double b=minimum+(maximum-minimum)*(i+1)/segments;
                arcs.stroke(center+radial(a)*radius,center+radial(b)*radius,axis,width,owner);
            }
            for(double end:{minimum,maximum}) {
                const Vec3 d=radial(end);arcs.stroke(center+d*(radius*.78),center+d*(radius*1.14),axis,width*1.2,owner);
            }
            // A projected child direction is an angle needle only for a
            // single-axis joint; other coordinates can change that projection.
            if(hinge)
                needles.stroke(center+current*(radius*.18),center+current*(radius*.91),axis,width*.8,childOwner);
        }
    }
    Material red;red.color=colorHex("#ff3845");red.unlit=true;
    Material active;active.color=colorHex("#fff0be");active.unlit=true;
    const auto emit=[&](Mesh& mesh,const Material& material) {
        if(mesh.vertices.empty())return;
        out.setObject(0,{},0);out.mesh(mesh.vertices,mesh.faces,material);
        // Inspection markers must remain visible even when the calibrated
        // pivot lies inside an opaque bone or a fitted V-view joint ball.
        out.markOverlay();
    };
    emit(arcs,red);emit(needles,active);
}
} // namespace joint_view
} // namespace gtc::native
