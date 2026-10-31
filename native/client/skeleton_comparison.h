#pragma once

// A visual comparison, not an anatomical reconstruction. The reference curves
// describe skin landmarks; the available landmarks below are skeletal proxies.
// See COMPARISON_SLIDERS.md for the definitions and limitations.
#include "client/skeleton_body.h"
#include "client/body_proportion_curves.h"
#include "client/height_body_reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace gtc::native {

class SkeletonComparison {
public:
    enum Dimension { Height, Head, Neck, Torso, Arm, Leg, Hip, Shoulder, Count };
    struct Part {
        std::string name; std::vector<Vec3> points; std::vector<std::array<int,3>> faces;
        int armSide = 0; bool cranial = false;
    };
    struct Profile {
        double height = 0;
        std::array<double, Count> multiplier{1,1,1,1,1,1,1,1};
    };

    bool load(const SkeletonModel& model, bool male, const std::string& directory, std::string& error) {
        male_ = male;
        const auto matrices = model.sourceMatrices({}, 0);
        parts_.clear();
        for (std::size_t i=0; i<matrices.size(); ++i) {
            const auto& source = model.sourceStructures()[i];
            const auto& group = model.bones()[source.group];
            Part part; part.name=source.name;
            part.cranial=part.name=="Head" || part.name=="Jaw" || part.name=="C1";
            if (part.name.rfind("Humerus_",0)==0 || part.name.rfind("Radius_",0)==0 ||
                part.name.rfind("Ulna_",0)==0 || part.name.rfind("Hand",0)==0) {
                if (part.name.size()>2 && part.name.substr(part.name.size()-2)=="_L") part.armSide=1;
                if (part.name.size()>2 && part.name.substr(part.name.size()-2)=="_R") part.armSide=-1;
            }
            part.points.reserve(source.pointCount);
            for (std::size_t p=source.firstPoint; p<source.firstPoint+source.pointCount; ++p)
                part.points.push_back(transform(matrices[i],group.points[p]));
            part.faces.reserve(source.triangleCount);
            for (std::size_t f=source.firstTriangle; f<source.firstTriangle+source.triangleCount; ++f) {
                auto face=group.triangles[f];
                for (int& vertex:face) vertex-=static_cast<int>(source.firstPoint);
                part.faces.push_back(face);
            }
            parts_.push_back(std::move(part));
        }
        auto named=[&](const std::string& name)->const Part* {
            for (const auto& part:parts_) if (part.name==name) return &part;
            return nullptr;
        };
        const Part* head=named("Head"), *jaw=named("Jaw"), *atlas=named("C1");
        const Part *pelvisL=named("Pelvis_L"), *pelvisR=named("Pelvis_R"), *sacrum=named("Sacrum");
        const Part *scapL=named("Scapula_L"), *scapR=named("Scapula_R");
        const Part *humerusL=named("Humerus_L"), *humerusR=named("Humerus_R");
        const Part *fingerL=named("HandDistP3_L"), *fingerR=named("HandDistP3_R");
        if (!head || !jaw || !atlas || !pelvisL || !pelvisR || !sacrum || !scapL || !scapR ||
            !humerusL || !humerusR || !fingerL || !fingerR) {
            error="Comparison skeleton is missing a landmark structure"; return false;
        }
        auto lo=[](const Part* p,int axis) { double v=1e30; for (Vec3 q:p->points) v=std::min(v,axis==0?q.x:q.y); return v; };
        auto hi=[](const Part* p,int axis) { double v=-1e30; for (Vec3 q:p->points) v=std::max(v,axis==0?q.x:q.y); return v; };
        floor_=1e30; for (const auto& part:parts_) for (Vec3 p:part.points) floor_=std::min(floor_,p.y);
        top_=hi(head,1); jaw_=lo(jaw,1);
        // The atlas joins the common torso field at its lowest vertex. The
        // jaw uses the cranial transform even if its front is below the C7.
        cranialJoin_=lo(atlas,1);
        pelvis_=std::min(lo(pelvisL,1),lo(pelvisR,1));
        pelvisTop_=std::max(hi(pelvisL,1),hi(pelvisR,1));
        scapBottom_=std::min(lo(scapL,1),lo(scapR,1));
        scapTop_=std::max(hi(scapL,1),hi(scapR,1));
        hipWidth_=std::max(hi(pelvisL,0),hi(pelvisR,0))-std::min(lo(pelvisL,0),lo(pelvisR,0));
        shoulderWidth_=std::max(hi(scapL,0),hi(scapR,0))-std::min(lo(scapL,0),lo(scapR,0));
        const std::string sex=male?"male":"female";
        // The same geometric hip/head estimates used by the reference rig.
        auto landmarks=[&](const std::string& suffix, const std::string& bone, std::array<Vec3,2>& out) {
            std::ifstream in(directory+"skeleton-"+sex+suffix); std::string line; bool seen[2]={false,false};
            while (std::getline(in,line)) {
                if (line.empty() || line[0]=='#') continue;
                std::istringstream row(line); char side; Vec3 p; double radius;
                if (!(row>>side>>p.x>>p.y>>p.z>>radius)) return false;
                const int index=model.sourceStructure(bone+"_"+side);
                if (index<0 || (side!='L' && side!='R')) return false;
                const int slot=side=='L'?0:1; if (seen[slot]) return false; seen[slot]=true;
                out[slot]=transform(matrices[static_cast<std::size_t>(index)],p);
            }
            return bool(in.eof()) && seen[0] && seen[1];
        };
        std::array<Vec3,2> hips, shoulders;
        if (!landmarks(".hips","Femur",hips) || !landmarks(".shoulders","Humerus",shoulders)) {
            error="Comparison joint estimates could not be loaded"; return false;
        }
        hipY_={hips[0].y,hips[1].y};
        hip_=(hipY_[0]+hipY_[1])*0.5;
        centerX_=(shoulders[0].x+shoulders[1].x)*0.5;
        armAnchor_=shoulders;
        auto extreme=[](const Part* part,int side)->Vec3 {
            Vec3 result=part->points.front();
            for (Vec3 p:part->points) if (side*p.x>side*result.x) result=p;
            return result;
        };
        armStart_[0]=extreme(humerusL,-1); armStart_[1]=extreme(humerusR,1);
        armEnd_[0]=extreme(fingerL,1); armEnd_[1]=extreme(fingerR,-1);
        // Published audit landmarks are in the registered source frame; apply
        // the exact T-pose matrices before using them as visual proxies.
        std::ifstream audit(directory+"anthropometry/SKELETON_NECK_SKULL_CONTACT.tsv");
        std::string row; std::getline(audit,row); int matches=0; neck_=0; neckZ_=0;
        while (std::getline(audit,row)) {
            std::istringstream fields(row); std::vector<std::string> cols; std::string field;
            while (std::getline(fields,field,'\t')) cols.push_back(field);
            if (cols.size()<15 || cols[1]!=sex) continue;
            if (matches>=2) { error="Duplicate comparison neck landmarks"; return false; }
            try {
                Vec3 c{std::stod(cols[3]),std::stod(cols[4]),std::stod(cols[5])};
                Vec3 n{std::stod(cols[12]),std::stod(cols[13]),std::stod(cols[14])};
                int ci=model.sourceStructure("C7"), ni=model.sourceStructure("C1");
                if (ci<0 || ni<0) break;
                c7_=transform(matrices[ci],c).y;
                const Vec3 posedContact=transform(matrices[ni],n);
                neck_+=posedContact.y; neckZ_+=posedContact.z; ++matches;
            } catch (...) { break; }
        }
        if (matches!=2) { error="Comparison neck landmarks unavailable"; return false; }
        neck_/=matches; neckZ_/=matches;
        if (!(floor_<pelvis_ && pelvis_<hip_ && hip_<c7_ && c7_<jaw_ && jaw_<neck_ && neck_<top_) ||
            !(pelvisTop_<scapBottom_ && scapTop_<cranialJoin_ && c7_<cranialJoin_ && cranialJoin_<neck_) ||
            !(lo(sacrum,1)>=pelvis_ && hi(sacrum,1)<=pelvisTop_ &&
              hipY_[0]>=pelvis_ && hipY_[0]<=pelvisTop_ && hipY_[1]>=pelvis_ && hipY_[1]<=pelvisTop_) ||
            hipWidth_<=0 || shoulderWidth_<=0) {
            error="Comparison landmarks have an unexpected order"; return false;
        }
        profile_=Profile{}; profile_.height=(top_-floor_)*100;
        return update(curves_,error);
    }

    bool loadCurves(const std::string& directory, std::string& error) {
        return curves_.load(directory+"BODY_PART_CURVE_KNOTS.tsv",error) &&
               curves_.loadSelectedModels(directory+"BODY_PART_SELECTED_MODELS.tsv",error);
    }
    const Profile& profile() const { return profile_; }
    double initialHeight() const { return (top_-floor_)*100; }
    const std::array<double,Count>& targets() const { return targets_; }
    const std::array<double,Count>& requestedTargets() const { return requestedTargets_; }
    bool feasible() const { return feasible_; }
    bool exact() const { return exact_; }
    std::uint64_t revision() const { return revision_; }
    bool setProfile(const Profile& profile, std::string& error) {
        if (!std::isfinite(profile.height) || profile.height<=0) return false;
        for (double m:profile.multiplier) if (!std::isfinite(m) || m<=0) return false;
        if (profile.height==profile_.height && profile.multiplier==profile_.multiplier) { error.clear(); return true; }
        const auto previous=profile_; const auto previousRevision=revision_; profile_=profile;
        if (!update(curves_,error)) {
            const std::string reason=error; profile_=previous; std::string ignored;
            update(curves_,ignored); revision_=previousRevision; error=reason; return false;
        }
        return true;
    }
    void reset(std::string& error) { profile_=Profile{}; profile_.height=initialHeight(); update(curves_,error); }

    // The skull, jaw and C1 share a single 3D similarity transform. The torso
    // field meets it at the bottom of C1; the jaw can lie lower than C7 because
    // those are distinct anterior/posterior structures, not one vertical chain.
    Vec3 map(Vec3 p, int armSide=0, bool cranial=false) const {
        if (cranial) return {(p.x-centerX_)*headScale_, profile_.height/100-(top_-p.y)*headScale_,
                             neckZ_*heightScale_+(p.z-neckZ_)*headScale_};
        const double sourceY=p.y;
        double scale=heightScale_;
        if (sourceY>=pelvis_ && sourceY<=pelvisTop_) scale=hipScale_;
        else if (sourceY>pelvisTop_ && sourceY<scapBottom_)
            scale=lerp(hipScale_,shoulderScale_,(sourceY-pelvisTop_)/(scapBottom_-pelvisTop_));
        else if (sourceY>=scapBottom_ && sourceY<=scapTop_) scale=shoulderScale_;
        else if (sourceY>scapTop_ && sourceY<cranialJoin_)
            scale=lerp(shoulderScale_,headScale_,(sourceY-scapTop_)/(cranialJoin_-scapTop_));
        else if (sourceY>=cranialJoin_) scale=headScale_;
        else if (sourceY<pelvis_ && sourceY>floor_)
            scale=lerp(heightScale_,hipScale_,(sourceY-floor_)/(pelvis_-floor_));
        double x=centerX_+(p.x-centerX_)*scale;
        if (armSide) {
            const int side=armSide>0?0:1;
            const Vec3 anchor=armAnchor_[side];
            // The anchor is mapped by the same trunk field, while all arm
            // structures share one scale about that shoulder point.
            const double mappedAnchor=map(anchor,0).x+centerX_;
            x=mappedAnchor+(p.x-anchor.x)*armScale_;
        }
        // The complete pelvic ring uses one uniform 3D scale. Join the
        // surrounding body field continuously at its upper and lower bounds.
        double depthScale=heightScale_;
        if (sourceY>=pelvis_ && sourceY<=pelvisTop_) depthScale=hipScale_;
        else if (sourceY<pelvis_ && sourceY>floor_)
            depthScale=lerp(heightScale_,hipScale_,(sourceY-floor_)/(pelvis_-floor_));
        else if (sourceY>pelvisTop_ && sourceY<scapBottom_)
            depthScale=lerp(hipScale_,heightScale_,(sourceY-pelvisTop_)/(scapBottom_-pelvisTop_));
        else if (sourceY>=cranialJoin_) depthScale=headScale_;
        else if (sourceY>scapTop_)
            depthScale=lerp(heightScale_,headScale_,(sourceY-scapTop_)/(cranialJoin_-scapTop_));
        return {x-centerX_,mapY(sourceY),neckZ_*heightScale_+(p.z-neckZ_)*depthScale};
    }
    void draw(Rasterizer& rasterizer, double x, std::uint32_t idBase=12000) const {
        Material blue; blue.color=colorHex("#358fff"); blue.specular=0.12;
        for (std::size_t i=0; i<parts_.size(); ++i) {
            const Part& part=parts_[i]; std::vector<Vec3> points; points.reserve(part.points.size());
            for (Vec3 p:part.points) { p=map(p,part.armSide,part.cranial); p.x+=x; points.push_back(p); }
            rasterizer.setObject(idBase+static_cast<std::uint32_t>(i),{},0);
            rasterizer.mesh(points,part.faces,blue);
        }
    }

private:
    static Vec3 transform(const std::array<double,16>& m, Vec3 p) {
        return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    }
    static double lerp(double a,double b,double t) { return a+(b-a)*t; }
    double mapY(double y) const {
        std::size_t i=0; while (i+2<sourceY_.size() && y>sourceY_[i+1]) ++i;
        return lerp(targetY_[i],targetY_[i+1],(y-sourceY_[i])/(sourceY_[i+1]-sourceY_[i]));
    }
    bool fitVertical() {
        const double h=profile_.height/100, head=targets_[Head]/100, neck=targets_[Neck]/100;
        const double torso=targets_[Torso]/100, leg=targets_[Leg]/100;
        const double epsilon=1e-6*h;
        headScale_=head/(top_-jaw_);
        hipScale_=targets_[Hip]/(hipWidth_*100);
        const double contact=h-(top_-neck_)*headScale_;
        const double join=h-(top_-cranialJoin_)*headScale_;
        const double c=contact-neck;
        const double ringHeight=(pelvisTop_-pelvis_)*hipScale_;
        const double hipOffset=(hip_-pelvis_)*hipScale_;
        feasible_=headScale_>0 && hipScale_>0 && c>ringHeight+2*epsilon &&
                  c<join-epsilon && join+epsilon<contact && contact<h-epsilon;
        if (!feasible_) return false;
        // A rigid pelvic ring fixes the separation of its lower edge and hip
        // centres. Project the two requested landmarks onto that constraint;
        // both controls continue to affect the result without bending the bone.
        const double bottom=std::clamp(((c-torso)+(leg-hipOffset))*0.5,epsilon,c-ringHeight-epsilon);
        sourceY_={floor_,pelvis_,pelvisTop_,c7_,cranialJoin_,neck_,top_};
        targetY_={0,bottom,bottom+ringHeight,c,join,contact,h};
        return true;
    }
    bool update(const BodyProportionCurves& curves, std::string& error) {
        const auto body=estimateHeightBodyReference(male_?BodyReferenceSex::male:BodyReferenceSex::female,profile_.height);
        const auto hip=curves.blendedNhanesDimensionCm(male_?"male":"female","pelvis_surface_breadth",profile_.height);
        const auto shoulder=curves.blendedNhanesDimensionCm(male_?"male":"female","shoulder_breadth",profile_.height);
        if (!body || !hip || !shoulder) { error="Height reference could not be evaluated"; return false; }
        requestedTargets_={profile_.height,body->headCm,body->neckCm,body->torsoCm,body->armCm,body->legCm,*hip,*shoulder};
        for (int i=1;i<Count;++i) requestedTargets_[i]*=profile_.multiplier[i];
        targets_=requestedTargets_;
        heightScale_=profile_.height/initialHeight();
        shoulderScale_=targets_[Shoulder]/(shoulderWidth_*100);
        // If even the rigid ring cannot fit below C7, continuously blend the
        // conflicting cranial and hip-width requests toward source scaling.
        if (!fitVertical()) {
            auto base=requestedTargets_;
            base[Head]=(top_-jaw_)*heightScale_*100;
            base[Neck]=(neck_-c7_)*heightScale_*100;
            base[Hip]=hipWidth_*heightScale_*100;
            const auto blend=[&](double amount) {
                for (int dimension:{Head,Neck,Hip})
                    targets_[dimension]=lerp(base[dimension],requestedTargets_[dimension],amount);
            };
            blend(0);
            if (!fitVertical()) { error="Unable to fit ordered skeleton landmarks"; return false; }
            double low=0,high=1;
            for (int i=0;i<48;++i) { const double mid=(low+high)*0.5; blend(mid);
                if (fitVertical()) low=mid; else high=mid; }
            blend(low*(1-1e-8));
            if (!fitVertical()) { error="Unable to fit ordered skeleton landmarks"; return false; }
        }
        targets_[Torso]=(targetY_[3]-targetY_[1])*100;
        targets_[Leg]=((mapY(hipY_[0])+mapY(hipY_[1]))*0.5)*100;
        exact_=true;
        for (int dimension=1;dimension<Count;++dimension)
            if (std::abs(targets_[dimension]-requestedTargets_[dimension])>1e-8*std::max(1.0,requestedTargets_[dimension]))
                exact_=false;
        // Solve the average of the two endpoint chords after the shared field.
        const double desired=targets_[Arm]/100;
        auto armLength=[&](double value) {
            armScale_=value; double sum=0;
            for (int side=0;side<2;++side) sum+=length(map(armEnd_[side],side==0?1:-1)-map(armStart_[side],side==0?1:-1));
            return sum*0.5;
        };
        double a=1e-12,b=2;
        while (armLength(b)<desired && b<1e150) b*=2;
        if (armLength(a)>desired || armLength(b)<desired) { error="Arm target is incompatible with the pose"; feasible_=false; return false; }
        for (int n=0;n<50;++n) { double mid=(a+b)*0.5; if (armLength(mid)<desired) a=mid; else b=mid; }
        armScale_=(a+b)*0.5; ++revision_; error.clear(); return true;
    }
    bool male_=false, feasible_=false, exact_=false;
    BodyProportionCurves curves_;
    Profile profile_;
    std::vector<Part> parts_;
    std::array<double,Count> targets_{}, requestedTargets_{};
    double floor_=0,top_=0,jaw_=0,cranialJoin_=0,pelvis_=0,pelvisTop_=0,hip_=0,c7_=0,neck_=0,neckZ_=0;
    double scapBottom_=0,scapTop_=0,hipWidth_=0,shoulderWidth_=0,centerX_=0;
    double heightScale_=1,hipScale_=1,shoulderScale_=1,headScale_=1,armScale_=1;
    std::array<double,7> sourceY_{}, targetY_{};
    std::array<double,2> hipY_{};
    std::array<Vec3,2> armAnchor_{},armStart_{},armEnd_{};
    std::uint64_t revision_=0;
};
} // namespace gtc::native
