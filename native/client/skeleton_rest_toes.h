#pragma once

#include "client/skeleton_leg_frame.h"

namespace gtc::native {
// Place each toe against its own foot's sole by a bounded MTP bend. The source
// bone vertices and anatomical joint pivot determine the small resting angle;
// this is done only when entering/restoring the reference pose.
inline bool restToesOnSole(SkeletonModel& model) {
    constexpr double pi=3.14159265358979323846;
    const auto matrices=model.sourceMatrices({},0);
    const auto point=[](const std::array<double,16>& m,Vec3 p) {
        return Vec3{m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                    m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    };
    const auto points=[&](int i) {
        const auto& s=model.sourceStructures()[i];
        return std::pair<const Vec3*,std::size_t>{model.bones()[s.group].points.data()+s.firstPoint,s.pointCount};
    };
    const auto center=[&](int i) {
        const auto [p,n]=points(i); Vec3 sum; for (std::size_t k=0;k<n;++k) sum+=p[k];
        return n ? sum/static_cast<double>(n) : Vec3{};
    };
    const auto contact=[&](int a,int b) {
        const auto [p,n]=points(a); const auto [q,k]=points(b);
        const auto ps=std::max<std::size_t>(1,n/192), qs=std::max<std::size_t>(1,k/192);
        Vec3 result=(center(a)+center(b))*0.5; double best=1e9;
        for (std::size_t x=0;x<n;x+=ps) for (std::size_t y=0;y<k;y+=qs) {
            const double d=length(p[x]-q[y]); if (d<best) { best=d; result=(p[x]+q[y])*0.5; }
        }
        return result;
    };
    for (int side=0;side<2;++side) {
        const std::string tag=side==0 ? "_L" : "_R";
        const auto frame=sourceLegFrame(model,side); if (!frame) return false;
        const int heel=model.sourceStructure("Cal"+tag), middle=model.sourceStructure("FootMeta3"+tag);
        if (heel<0 || middle<0) return false;
        const auto [hp,hn]=points(heel); double ground=1e9;
        for (std::size_t k=0;k<hn;++k) ground=std::min(ground,point(matrices[heel],hp[k]).y);
        const Vec3 direction=center(middle)-center(heel);
        const Vec3 forward=normalized(direction-frame->soleNormal*dot(direction,frame->soleNormal));
        const Vec3 axisSource=normalized(cross(forward,frame->soleNormal*-1.0));
        for (int digit=1;digit<=5;++digit) {
            const auto number=std::to_string(digit);
            const int meta=model.sourceStructure("FootMeta"+number+tag), prox=model.sourceStructure("FootProxP"+number+tag);
            const int mid=digit==1 ? -1 : model.sourceStructure("FootMidP"+number+tag);
            const int tip=model.sourceStructure("FootDistP"+number+tag);
            if (meta<0 || prox<0 || tip<0 || (digit!=1 && mid<0)) return false;
            std::size_t link=0;
            for (;link<model.sourceLinks().size();++link) {
                const auto& e=model.sourceLinks()[link];
                if ((e.first==static_cast<std::size_t>(meta) && e.second==static_cast<std::size_t>(prox)) ||
                    (e.second==static_cast<std::size_t>(meta) && e.first==static_cast<std::size_t>(prox))) break;
            }
            if (link==model.sourceLinks().size()) return false;
            const auto range=referenceBendRange(model.sourceLinks()[link].mobility); if (!range) return false;
            const Vec3 pivot=point(matrices[meta],contact(meta,prox));
            const auto& matrix=matrices[meta];
            const Vec3 axis=normalized(Vec3{matrix[0]*axisSource.x+matrix[4]*axisSource.y+matrix[8]*axisSource.z,
                                            matrix[1]*axisSource.x+matrix[5]*axisSource.y+matrix[9]*axisSource.z,
                                            matrix[2]*axisSource.x+matrix[6]*axisSource.y+matrix[10]*axisSource.z});
            std::vector<Vec3> vertices;
            for (int bone : {prox,mid,tip}) if (bone>=0) {
                const auto [p,n]=points(bone);
                for (std::size_t k=0;k<n;++k) vertices.push_back(point(matrices[bone],p[k]));
            }
            if (vertices.empty()) return false;
            double lowest=1e9; for (Vec3 v:vertices) lowest=std::min(lowest,v.y);
            const bool lift=lowest<ground-1e-6;
            double angle=lift ? 0 : range->flexion*pi/180.0; bool found=false;
            for (Vec3 vertex:vertices) {
                if (lift && vertex.y>=ground) continue;
                const Vec3 v=vertex-pivot; const double along=dot(axis,v);
                const double a=v.y-axis.y*along, b=cross(axis,v).y, c=pivot.y+axis.y*along;
                const double radius=std::hypot(a,b); if (radius<1e-10) continue;
                const double value=(ground-c)/radius; if (value<-1 || value>1) continue;
                const double phase=std::atan2(b,a), arc=std::acos(std::clamp(value,-1.0,1.0));
                double closest=lift ? -1e9 : 1e9;
                for (int turn=-1;turn<=1;++turn) for (double sign:{-1.0,1.0}) {
                    const double q=phase+sign*arc+2*pi*turn;
                    if (lift && q<=0 && q>=range->extension*pi/180.0) closest=std::max(closest,q);
                    if (!lift && q>=0 && q<=range->flexion*pi/180.0) closest=std::min(closest,q);
                }
                if (lift && closest>-1e8) { angle=std::min(angle,closest); found=true; }
                if (!lift && closest<1e8) { angle=std::min(angle,closest); found=true; }
            }
            if (!found) angle=0;
            if (!model.setReferenceBend(link,angle*180.0/pi)) return false;
            // If the proximal bone contacted first, use the remaining toe
            // hinges to lower the tip rather than leaving it raised.
            const auto tipHeight=[&]() {
                const auto pose=model.sourceMatrices({},0); const auto [p,n]=points(tip); double y=1e9;
                for (std::size_t k=0;k<n;++k) y=std::min(y,point(pose[tip],p[k]).y);
                return y;
            };
            for (int child : {mid,tip}) {
                if (child<0 || (digit==1 && child==tip)) continue;
                const int parent=child==mid ? prox : mid;
                std::size_t ip=0;
                for (;ip<model.sourceLinks().size();++ip) {
                    const auto& e=model.sourceLinks()[ip];
                    if ((e.first==static_cast<std::size_t>(parent) && e.second==static_cast<std::size_t>(child)) ||
                        (e.second==static_cast<std::size_t>(parent) && e.first==static_cast<std::size_t>(child))) break;
                }
                if (ip==model.sourceLinks().size()) return false;
                const auto& edge=model.sourceLinks()[ip];
                const auto bound=referenceBendRange(edge.mobility,model.sourceStructures()[edge.first].name,model.sourceStructures()[edge.second].name);
                if (!bound || tipHeight()<=ground+0.0005) continue;
                const double before=tipHeight();
                if (!model.setReferenceBend(ip,5)) return false;
                const double drop=before-tipHeight();
                // Resting flexion stays gentle; the controls retain the full ROM.
                const double target=drop>1e-8 ? std::clamp(5*(before-ground)/drop,0.0,std::min(20.0,bound->flexion)) : 0.0;
                if (!model.setReferenceBend(ip,target)) return false;
            }
            // A registered small-toe proximal bone may already touch first.
            // After using both hinges, bring the tip closer with the MTP joint.
            if (lift && tipHeight()>ground+0.001) {
                const double initial=*model.referenceBend(link), before=tipHeight();
                const double trial=std::min(0.0,initial+5.0);
                if (!model.setReferenceBend(link,trial)) return false;
                const double drop=before-tipHeight();
                const double target=drop>1e-8 ? std::min(0.0,initial+(trial-initial)*(before-ground)/drop) : initial;
                if (!model.setReferenceBend(link,target)) return false;
            }
        }
    }
    return true;
}
} // namespace gtc::native
