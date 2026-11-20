#pragma once

#include "client/skeleton_comparison.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace gtc::native {
// A click is a rigid world-axis adjustment, not an anatomical estimate.
// Keeping the clicks in order makes save/load and undo deterministic.
struct ToeEdit {
    bool male=true, blue=false;
    char side='L';
    int digit=1, segment=0, axis=0; // metatarsal/proximal/middle/distal; rotate XYZ, move XYZ
    double amount=0;                // degrees or metres
    bool original=false;           // edits on B's original pose are separate
};
inline bool validToeEdit(const ToeEdit& e) {
    return (e.side=='L' || e.side=='R') && e.digit>=1 && e.digit<=5 &&
        e.segment>=-1 && e.segment<=2 && !(e.digit==1 && e.segment==1) &&
        e.axis>=0 && e.axis<6 && std::isfinite(e.amount) &&
        std::abs(e.amount)<=(e.axis<3?180.0:0.1);
}
inline std::string toeEditName(const ToeEdit& e,int segment) {
    const char* prefix=segment<0?"FootMeta":segment==0?"FootProxP":segment==1?"FootMidP":"FootDistP";
    return std::string(prefix)+std::to_string(e.digit)+"_"+e.side;
}
inline std::size_t toeEditLink(const SkeletonModel& model,const std::string& a,const std::string& b) {
    const int first=model.sourceStructure(a),second=model.sourceStructure(b);
    if(first<0 || second<0)return model.sourceLinks().size();
    for(std::size_t i=0;i<model.sourceLinks().size();++i) {
        const auto& l=model.sourceLinks()[i];
        if((l.first==std::size_t(first) && l.second==std::size_t(second)) ||
           (l.second==std::size_t(first) && l.first==std::size_t(second)))return i;
    }
    return model.sourceLinks().size();
}
inline JointRigidTransform toeEditTransform(const ToeEdit& e,Vec3 center) {
    const Vec3 axis=e.axis%3==0?Vec3{1,0,0}:e.axis%3==1?Vec3{0,1,0}:Vec3{0,0,1};
    if(e.axis>=3) { JointRigidTransform move; move.translation=axis*e.amount; return move; }
    auto turn=joint_detail::rotate(axis,e.amount*3.14159265358979323846/180.0);
    turn.translation=center-frameVector(turn,center);return turn;
}
inline bool applyWhiteToeTransform(SkeletonModel& model,const ToeEdit& e,const JointRigidTransform& turn) {
    if(!validToeEdit(e) || e.blue)return false;
    const std::string name=toeEditName(e,e.segment);
    const std::string parent=e.segment==-1?std::string(e.digit==1?"MedCun":e.digit==2?"InterCun":e.digit==3?"LatCun":"Cub")+"_"+e.side:
        e.segment==0?"FootMeta"+std::to_string(e.digit)+"_"+e.side:toeEditName(e,e.segment==2 && e.digit==1?0:e.segment-1);
    const auto edge=toeEditLink(model,parent,name);
    const int index=model.sourceStructure(name);
    if(index<0 || edge==model.sourceLinks().size())return false;
    // Counter-transform the next bone so each button moves only the selected
    // phalanx, even though the underlying rig is a parent/child chain.
    const int next=e.segment==-1?0:e.segment==0?(e.digit==1?2:1):e.segment==1?2:-1;
    const auto child=next<0?model.sourceLinks().size():toeEditLink(model,name,toeEditName(e,next));
    if(next>=0 && child==model.sourceLinks().size())return false;
    if(!model.adjustReferenceToe(edge,turn))return false;
    const auto reverse=joint_detail::inverse(turn);
    if(next>=0 && !model.adjustReferenceToe(child,reverse))return false;
    if(e.segment==-1 && e.digit==1)for(const char* sesamoid:{"FootSesaMed_","FootSesaLat_"}) {
        const auto link=toeEditLink(model,name,std::string(sesamoid)+e.side);
        if(link<model.sourceLinks().size() && !model.adjustReferenceToe(link,reverse))return false;
    }
    return true;
}
inline bool applyWhiteToeEdit(SkeletonModel& model,const ToeEdit& e) {
    if(!validToeEdit(e) || e.blue)return false;
    const int index=model.sourceStructure(toeEditName(e,e.segment));
    if(index<0)return false;
    const auto& part=model.sourceStructures()[std::size_t(index)];if(!part.pointCount)return false;
    Vec3 center{};for(std::size_t i=0;i<part.pointCount;++i)center+=model.bones()[part.group].points[part.firstPoint+i];
    center=toePoint(model.sourceMatrices({},0)[std::size_t(index)],center/double(part.pointCount));
    return applyWhiteToeTransform(model,e,toeEditTransform(e,center));
}
inline void applyBlueToeEdits(const SkeletonComparison& c,std::vector<std::array<double,16>>& pose,
                              Vec3 origin,const std::vector<ToeEdit>& edits,bool male,bool original) {
    for(const auto& e:edits) {
        if(!e.blue || e.male!=male || e.original!=original || !validToeEdit(e))continue;
        const auto name=toeEditName(e,e.segment);
        for(std::size_t i=0;i<c.parts().size() && i<pose.size();++i) {
            const auto& part=c.parts()[i];if(part.name!=name || part.points.empty())continue;
            Vec3 center{};for(Vec3 p:part.points)center+=c.mapPart(p,part)+origin;
            center=toePoint(pose[i],center/double(part.points.size()));
            const auto t=joint_detail::compose(toeEditTransform(e,center),toeRigid(pose[i]));
            const auto& r=t.rotation;
            pose[i]={r[0],r[3],r[6],0,r[1],r[4],r[7],0,r[2],r[5],r[8],0,t.translation.x,t.translation.y,t.translation.z,1};
            break;
        }
    }
}
inline bool saveToeEdits(const std::string& path,const std::vector<ToeEdit>& edits) {
    std::ofstream out(path,std::ios::trunc);if(!out)return false;
    out<<"toe-edits-v2\n"<<std::setprecision(17);
    for(const auto& e:edits)out<<e.male<<' '<<e.blue<<' '<<e.side<<' '<<e.digit<<' '<<e.segment<<' '<<e.axis<<' '<<e.amount<<' '<<e.original<<'\n';
    return bool(out);
}
inline bool loadToeEdits(const std::string& path,std::vector<ToeEdit>& edits) {
    std::ifstream in(path);std::string header;if(!(in>>header) || header!="toe-edits-v2")return false;
    std::vector<ToeEdit> loaded;ToeEdit e;int male,blue,original;
    while(in>>male) {
        if(!(in>>blue>>e.side>>e.digit>>e.segment>>e.axis>>e.amount>>original) ||
           (male!=0 && male!=1) || (blue!=0 && blue!=1) || (original!=0 && original!=1))return false;
        e.male=male!=0;e.blue=blue!=0;e.original=original!=0;
        if(!validToeEdit(e) || loaded.size()>=10000)return false;
        loaded.push_back(e);
    }
    if(!in.eof())return false;
    edits=std::move(loaded);return true;
}
} // namespace gtc::native
