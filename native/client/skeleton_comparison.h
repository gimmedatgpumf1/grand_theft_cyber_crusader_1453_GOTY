#pragma once
#include "client/skeleton_original_toe_reference.h"
// A visual comparison, not an anatomical reconstruction. The reference curves
// describe skin landmarks; the available landmarks below are skeletal proxies.
// See COMPARISON_SLIDERS.md for the definitions and limitations.
#include "client/skeleton_body.h"
#include "client/skeleton_toe_skin.h"
#include "client/skeleton_makehuman_draw.h"
#include "client/skeleton_metatarsal_plane.h"
#include "client/skeleton_medial_plane.h"
#include "client/skeleton_heel_planes.h"
#include "client/skeleton_sole.h"
#include "client/skeleton_foot_stitch.h"
#include "client/skeleton_bilateral.h"
#include "client/skeleton_standing_foot.h"
#include "client/skeleton_toe_mirror.h"
#include "client/skeleton_toe_attachment.h"
#include "client/skeleton_surface_average.h"
#include "client/skull_midplane.h"
#include "client/vertebra_midplane.h"
#include "client/body_proportion_curves.h"
#include "client/height_body_reference.h"
#include "client/male_comparison_formulas.h"
#include "client/female_comparison_formulas.h"
#include "client/comparison_standard_deviations.h"
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
        int armSide = 0; bool cranial = false; bool girdle = false; bool humerus = false;
        bool vertebra = false; Vec3 vertebraAnchor{},vertebraNormal{},vertebraUp{},vertebraForward{};
        Vec3 mappedAnchor{},mappedNormal{},mappedUp{},mappedForward{},jointClearance{},mappedClearance{};
        double mappedNormalScale=1;
    };
    struct Profile { double height = 0; std::array<double, Count> multiplier{1,1,1,1,1,1,1,1}; };
    bool load(const SkeletonModel& model, bool male, const std::string& directory, std::string& error);
    bool loadCurves(const std::string& directory, std::string& error) {
        return curves_.load(directory+"BODY_PART_CURVE_KNOTS.tsv",error) &&
               curves_.loadSelectedModels(directory+"BODY_PART_SELECTED_MODELS.tsv",error);
    }
    const Profile& profile() const { return profile_; }
    const std::vector<Part>& parts() const { return parts_; }
    const std::array<Vec3,2>& hipCenters() const { return hipCenters_; }
    double initialHeight() const { return initialHeight_; }
    double defaultHeight() const { return male_ ? 172.2 : 159.4; }
    const std::array<double,Count>& targets() const { return targets_; }
    const std::array<double,Count>& requestedTargets() const { return requestedTargets_; }
    struct DimensionLimits { double center, sd, minimum, maximum; };
    std::optional<DimensionLimits> dimensionLimits(int dimension, double height) const {
        if (dimension<=Height || dimension>=Count || !std::isfinite(height) || height<50.) return std::nullopt;
        const auto values=male_ ? maleComparisonFormulas(height) : femaleComparisonFormulas(height);
        if (!values) return std::nullopt;
        const double center=(*values)[dimension-1];
        const double sd=(male_ ? maleComparisonSdCm : femaleComparisonSdCm)[dimension-1];
        // The formula minus 1.5 SD can be negative, especially at 50 cm. A mesh
        // cannot have zero or negative dimensions; this is a geometric floor.
        return DimensionLimits{center,sd,std::max(0.001,center-1.5*sd),center+1.5*sd};
    }
    bool feasible() const { return feasible_; }
    bool exact() const { return exact_; }
    std::uint64_t revision() const { return revision_; }
    bool setProfile(const Profile& profile, std::string& error) {
        if (!std::isfinite(profile.height) || profile.height<50.0) {
            error="Height must be at least 50 cm"; return false;
        }
        for (double m:profile.multiplier) if (!std::isfinite(m) || m<=0) {
            error="Dimensions must be positive and finite"; return false;
        }
        Profile limited=profile;
        for (int d=Head;d<Count;++d) {
            const auto bounds=dimensionLimits(d,profile.height);
            if (!bounds || !std::isfinite(bounds->maximum)) { error="Size limits unavailable"; return false; }
            limited.multiplier[d]=std::clamp(bounds->center*profile.multiplier[d],bounds->minimum,bounds->maximum)/bounds->center;
        }
        if (limited.height==profile_.height && limited.multiplier==profile_.multiplier) { error.clear(); return true; }
        const auto previous=profile_; const auto previousRevision=revision_; profile_=limited;
        if (!update(error)) {
            const std::string reason=error; profile_=previous; std::string ignored;
            update(ignored); revision_=previousRevision; error=reason; return false;
        }
        return true;
    }
    void reset(std::string& error) { profile_=Profile{}; profile_.height=defaultHeight(); update(error); }
    // The skull, jaw and C1 share a single 3D similarity transform. The torso
    // field meets it at the bottom of C1; the jaw can lie lower than C7 because
    // those are distinct anterior/posterior structures, not one vertical chain.
    Vec3 map(Vec3 p, int armSide=0, bool cranial=false, bool girdle=false, bool humerus=false) const {
        if (cranial) return {(p.x-centerX_)*headScale_, profile_.height/100-(top_-p.y)*headScale_,
                             neckZ_*heightScale_+(p.z-neckZ_)*headScale_};
        if (armSide || girdle) {
            // Scapula and clavicle use the same affine shoulder frame, so the
            // vertical field knot cannot slice through their joint. The arm
            // shares the frame except for its own horizontal length scale.
            const double shoulderY=(armAnchor_[0].y+armAnchor_[1].y)*0.5;
            double x=(p.x-centerX_)*shoulderScale_;
            if (armSide) {
                const int side=armSide>0?0:1;
                const Vec3 shoulder=armAnchor_[side], elbow=elbowAnchor_[side];
                const double boundary=shoulder.x+armSide*std::abs(armStart_[side].x-shoulder.x);
                const auto upperX=[&](double source) {
                    return (boundary-centerX_)*shoulderScale_+(source-boundary)*upperArmScale_;
                };
                if (humerus && armSide*(p.x-boundary)>0) x=upperX(p.x);
                else if (!humerus) x=upperX(elbow.x)+(p.x-elbow.x)*armScale_;
            }
            return {x,shoulderYTarget_+(p.y-shoulderY)*shoulderVertical_,
                    neckZ_*heightScale_+(p.z-neckZ_)*heightScale_};
        }
        const double sourceY=p.y;
        double scale=heightScale_;
        if (sourceY>=pelvis_ && sourceY<=pelvisTop_) scale=hipScale_;
        else if (sourceY>pelvisTop_ && sourceY<scapBottom_)
            scale=lerp(hipScale_,shoulderScale_,(sourceY-pelvisTop_)/(scapBottom_-pelvisTop_));
        else if (sourceY>=scapBottom_ && sourceY<=scapTop_) scale=shoulderScale_;
        else if (sourceY>scapTop_ && sourceY<cranialJoin_)
            scale=lerp(shoulderScale_,headScale_,(sourceY-scapTop_)/(cranialJoin_-scapTop_));
        else if (sourceY>=cranialJoin_) scale=headScale_;
        else if (sourceY<pelvis_) scale=hipScale_;
        double x=centerX_+(p.x-centerX_)*scale;
        // The complete pelvic ring uses one uniform 3D scale. Join the
        // surrounding body field continuously at its upper and lower bounds.
        double depthScale=heightScale_;
        if (sourceY>=pelvis_ && sourceY<=pelvisTop_) depthScale=hipScale_;
        else if (sourceY<pelvis_) depthScale=hipScale_;
        else if (sourceY>pelvisTop_ && sourceY<scapBottom_)
            depthScale=lerp(hipScale_,heightScale_,(sourceY-pelvisTop_)/(scapBottom_-pelvisTop_));
        else if (sourceY>=cranialJoin_) depthScale=headScale_;
        else if (sourceY>scapTop_)
            depthScale=lerp(heightScale_,headScale_,(sourceY-scapTop_)/(cranialJoin_-scapTop_));
        return {x-centerX_,mapY(sourceY),neckZ_*heightScale_+(p.z-neckZ_)*depthScale};
    }
    Vec3 mapPart(Vec3 p,const Part& part) const {
        if (!part.vertebra || part.cranial) {
            Vec3 mapped=map(p,part.armSide,part.cranial,part.girdle,part.humerus);
            mapped.y=comparisonFootHeight(part.name,p,mapped.y,floor_,ankleY_,kneeY_,hipScale_,[&](double y) {return mapY(y);});
            return mapped;
        }
        // Map the retained half through the original body field, then reflect
        // it in the cached mapped 3D plane. This retains the field's actual
        // nonlinear shape at knots without shearing the two sides differently.
        const double side=dot(p-part.vertebraAnchor,part.vertebraNormal);
        const Vec3 retained=side<0 ? p-part.vertebraNormal*(2*side) : p;
        const Vec3 result=map(retained)+part.mappedClearance;
        // Keep the mapped tangential position, and use the fitted normal scale
        // for signed distance so no point crosses at a nonlinear field knot.
        const double distance=dot(result-part.mappedAnchor,part.mappedNormal);
        return result+part.mappedNormal*(side*part.mappedNormalScale-distance);
    }
    // The same vertices are used for rendering and for distal-toe collision.
    std::vector<Vec3> toeIcosahedron(std::size_t i, Vec3 origin) const {
        const Part& part=parts_[i];
        if (part.name.rfind("FootDistP",0)!=0 || part.points.empty()) return {};
        std::vector<Vec3> points; points.reserve(part.points.size());
        for (Vec3 p:part.points) points.push_back(mapPart(p,part)+origin);
        const std::string suffix=part.name.substr(9);
        const std::string parentName=std::string(suffix[0]=='1' ? "FootProxP" : "FootMidP")+suffix;
        const auto parent=std::find_if(parts_.begin(),parts_.end(),
            [&](const Part& candidate) { return candidate.name==parentName; });
        if (parent==parts_.end() || parent->points.empty()) return {};
        Vec3 distalCenter{}, parentCenter{};
        for (Vec3 p:points) distalCenter+=p;
        distalCenter=distalCenter/static_cast<double>(points.size());
        for (Vec3 p:parent->points) parentCenter+=mapPart(p,*parent)+origin;
        parentCenter=parentCenter/static_cast<double>(parent->points.size());
        const Vec3 forward=normalized(distalCenter-parentCenter);
        const Vec3 lateral=normalized(cross(Vec3{0,1,0},forward));
        const Vec3 up=normalized(cross(forward,lateral));
        auto local=[&](Vec3 p) { return Vec3{dot(p,lateral),dot(p,up),dot(p,forward)}; };
        Vec3 low=local(points.front()), high=low;
        for (Vec3 p:points) {
            p=local(p);
            low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); low.z=std::min(low.z,p.z);
            high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y); high.z=std::max(high.z,p.z);
        }
        const Vec3 mid=(low+high)*0.5;
        Vec3 center=lateral*mid.x+up*mid.y+forward*mid.z;
        // Size the big-toe tip while retaining its forwardmost point. The
        // prism follows the tip midpoint; its rear stays fixed.
        constexpr double greatTipScale=0.95*0.90*1.05;
        if (suffix[0]=='1') center+=forward*((high.z-low.z)*0.95*0.85065080835204*(1.0-greatTipScale));
        const Vec3 size=(high-low)*(0.95*(suffix[0]=='1' ? greatTipScale : 1.0));
        constexpr double phi=1.618033988749895;
        const Vec3 corners[]={{-1,phi,0},{1,phi,0},{-1,-phi,0},{1,-phi,0},
            {0,-1,phi},{0,1,phi},{0,-1,-phi},{0,1,-phi},
            {phi,0,-1},{phi,0,1},{-phi,0,-1},{-phi,0,1}};
        std::vector<Vec3> vertices; vertices.reserve(12);
        for (Vec3 corner:corners) {
            corner=normalized(corner);
            vertices.push_back(center+lateral*(corner.x*size.x)+up*(corner.y*size.y)+forward*(corner.z*size.z));
        }
        return vertices;
    }
    // Rigid toe deltas from the source pose used to fit this comparison. The
    // mapped centre anchors each rotation in the scaled blue figure. This is a
    // geometric approximation, not a deformable tissue or clinical fit.
    void setCorrectedToeReference(const std::vector<std::array<double,16>>& matrices) { correctedToeMatrices_=matrices; toeAttachmentRevision_=0; }
    void setToeStandingReference(const std::vector<std::array<double,16>>& matrices) { standingToeMatrices_=matrices; toeAttachmentRevision_=0; }
    const ToeSkinCalibration& toeSkinCalibration() const { return toeSkinCalibration_; } void setToeSkinCalibration(ToeSkinCalibration c) { if(c.before!=toeSkinCalibration_.before || c.after!=toeSkinCalibration_.after) { toeSkinCalibration_=std::move(c); ++revision_; } }
    bool toeAttachmentsEnabled() const { return toeAttachmentsEnabled_; } void setToeAttachmentsEnabled(bool enabled) { if (toeAttachmentsEnabled_!=enabled) { toeAttachmentsEnabled_=enabled; ++revision_; toeAttachmentRevision_=0; } }
    const auto& correctedToeReference() const { if(!toeAttachmentsEnabled_ && !male_ && originalSkinToeMatrices_.size()==parts_.size())return originalSkinToeMatrices_;
        return standingToeMatrices_.size()==parts_.size()?standingToeMatrices_:correctedToeMatrices_; }
    const auto& littleToeSkinReference() const { return referenceMatrices_; } std::vector<std::array<double,16>> toePose(const std::vector<std::array<double,16>>& current, Vec3 origin,
                                               const std::vector<std::array<double,16>>* reference=nullptr) const {
        const std::array<double,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        std::vector<std::array<double,16>> result(parts_.size(),identity);
        if (current.size()!=parts_.size() || referenceMatrices_.size()!=parts_.size() || metatarsalFrames_.size()!=parts_.size()) return result;
        for (std::size_t i=0;i<parts_.size();++i) {
            const auto& p=parts_[i];
            if (p.points.empty() || (p.name.rfind("FootProxP",0)!=0 && p.name.rfind("FootMidP",0)!=0 &&
                                     p.name.rfind("FootDistP",0)!=0)) continue;
            Vec3 center{}; for (Vec3 q:p.points) center+=q;
            center=center/static_cast<double>(p.points.size());
            result[i]=registeredToeDelta(current[i],referenceMatrices_[i],metatarsalFrames_[i],center,
                                        [&](Vec3 q) {return mapPart(q,p)+origin;});
        }
        mirrorReferenceToePose(parts_,result,referenceMatrices_,reference?*reference:(!toeAttachmentsEnabled_ && !male_ && originalSkinToeMatrices_.size()==parts_.size()?originalSkinToeMatrices_:
                                   !toeAttachmentsEnabled_ && standingToeMatrices_.size()==parts_.size()?standingToeMatrices_:correctedToeMatrices_),metatarsalFrames_,origin,
                               [&](Vec3 q,const Part& p){return mapPart(q,p);});
        if (reference && toeAttachmentsEnabled_ && standingToeMatrices_.size()==parts_.size() && *reference==standingToeMatrices_) fitComparisonToePlacement(parts_,result,[&](Vec3 q,const Part& p){return mapPart(q,p);},male_,origin);
        // Explicit references are static snapshots used to retarget the skin.
        // They must remain unchanged; only subsequent animation needs hinges.
        if (!reference && toeAttachmentsEnabled_) {
            const auto& rest=correctedToeMatrices_.size()==parts_.size() ? correctedToeMatrices_ : referenceMatrices_;
            const auto map=[&](Vec3 q,const Part& p){return mapPart(q,p);};
            auto neutral=comparisonToeNeutral(parts_,referenceMatrices_,rest,correctedToeMatrices_,metatarsalFrames_,origin,map);
            if (standingToeMatrices_.size()==parts_.size()) {
                auto standing=comparisonToeNeutral(parts_,referenceMatrices_,standingToeMatrices_,standingToeMatrices_,metatarsalFrames_,origin,map);
                fitComparisonToePlacement(parts_,standing,map,male_,origin);
                result=rebaseComparisonToePose(result,neutral,standing);neutral=standing;
            }
            if (toeAttachmentRevision_!=revision_) {
                toeAttachments_=comparisonToeAttachments(parts_,localToeMatrices(neutral,origin),map);
                toeAttachmentRevision_=revision_;
            }
            attachComparisonToes(toeAttachments_,neutral,result,origin);
        }
        return result;
    }
    std::vector<ComparisonToeAttachment> toePivots(const std::vector<std::array<double,16>>& posed,Vec3 origin) const {
        if(!toeAttachmentsEnabled_ || posed.size()!=parts_.size() || toeAttachmentRevision_!=revision_)return {};
        const auto& rest=standingToeMatrices_.size()==parts_.size()?standingToeMatrices_:
                         correctedToeMatrices_.size()==parts_.size()?correctedToeMatrices_:referenceMatrices_;
        auto neutral=comparisonToeNeutral(parts_,referenceMatrices_,rest,rest,metatarsalFrames_,origin,[&](Vec3 q,const Part& p){return mapPart(q,p);});
        if (standingToeMatrices_.size()==parts_.size()) fitComparisonToePlacement(parts_,neutral,[&](Vec3 q,const Part& p){return mapPart(q,p);},male_,origin);
        return posedToeAttachments(toeAttachments_,neutral,posed,origin);
    }
    const JointRigidTransform& metatarsalFrame(std::size_t i) const { return metatarsalFrames_[i]; }
    // Toe prisms belong to their own phalanges, so the pieces articulate
    // independently. The great toe has only two phalanges.
    std::vector<Vec3> toePrism(std::size_t i, Vec3 origin) const {
        const Part& part=parts_[i];
        const bool middle=part.name.rfind("FootMidP",0)==0;
        const bool great=part.name.rfind("FootProxP1_",0)==0;
        const bool lesserProx=part.name.rfind("FootProxP",0)==0 && !great;
        if (lesserProx) return proximalToePrism(i,origin);
        if ((!middle && !great) || part.points.empty()) return {};
        const std::string suffix=part.name.substr(middle ? 8 : 9);
        const std::string distalName="FootDistP"+suffix;
        const auto distal=std::find_if(parts_.begin(),parts_.end(),
            [&](const Part& candidate) { return candidate.name==distalName; });
        if (distal==parts_.end() || distal->points.empty()) return {};
        Vec3 center{},tipCenter{};
        for (Vec3 p:part.points) center+=mapPart(p,part)+origin;
        for (Vec3 p:distal->points) tipCenter+=mapPart(p,*distal)+origin;
        center=center/static_cast<double>(part.points.size());
        tipCenter=tipCenter/static_cast<double>(distal->points.size());
        Vec3 forward=normalized(tipCenter-center);
        Vec3 lateral=normalized(cross(Vec3{0,1,0},forward));
        Vec3 up=normalized(cross(forward,lateral));
        if (length(forward)<1e-8 || length(lateral)<1e-8 || length(up)<1e-8) return {};
        const auto local=[&](Vec3 p) { return Vec3{dot(p,lateral),dot(p,up),dot(p,forward)}; };
        Vec3 low=local(mapPart(part.points.front(),part)+origin),high=low;
        for (Vec3 p:part.points) {
            p=local(mapPart(p,part)+origin);
            low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); low.z=std::min(low.z,p.z);
            high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y); high.z=std::max(high.z,p.z);
        }
        const auto tip=toeIcosahedron(static_cast<std::size_t>(distal-parts_.begin()),origin);
        if (tip.empty()) return {};
        Vec3 tipLow=local(tip.front()),tipHigh=tipLow;
        for (Vec3 p:tip) {
            p=local(p);
            tipLow.x=std::min(tipLow.x,p.x); tipLow.y=std::min(tipLow.y,p.y); tipLow.z=std::min(tipLow.z,p.z);
            tipHigh.x=std::max(tipHigh.x,p.x); tipHigh.y=std::max(tipHigh.y,p.y); tipHigh.z=std::max(tipHigh.z,p.z);
        }
        // Reach the tip's midpoint at rest; both pieces remain independently
        // attached to their own bones.
        const double back=low.z-0.08*(high.z-low.z);
        const double front=(tipLow.z+tipHigh.z)*0.5;
        const double x=(low.x+high.x)*0.5, y=(low.y+high.y)*0.5;
        // The octagon's extrema are cos(pi/8) times its radii. Fit the back
        // to this bone with a 30% flesh allowance; keep the front at 80% of
        // the tip's width and height.
        constexpr double octagonExtent=0.9238795325112867;
        constexpr double pi=3.14159265358979323846;
        std::vector<Vec3> vertices; vertices.reserve(16);
        for (int end=0;end<2;++end) for (int k=0;k<8;++k) {
            const double z=end ? front : back;
            const double rx=(end ? (tipHigh.x-tipLow.x)*0.40 : (high.x-low.x)*0.65)/octagonExtent;
            const double ry=(end ? (tipHigh.y-tipLow.y)*0.40 : (high.y-low.y)*0.65)/octagonExtent;
            const double angle=(double(k)+0.5)*pi/4.0;
            vertices.push_back(lateral*(x+rx*std::cos(angle))+up*(y+ry*std::sin(angle))+forward*z);
        }
        return vertices;
    }
    // The four lesser toes have a third phalanx. Fit its prism to that
    // proximal bone and overlap the next prism slightly at the joint.
    std::vector<Vec3> proximalToePrism(std::size_t i, Vec3 origin) const {
        const Part& part=parts_[i];
        if (part.points.empty()) return {};
        const std::string nextName="FootMidP"+part.name.substr(9);
        const auto next=std::find_if(parts_.begin(),parts_.end(),
            [&](const Part& candidate) { return candidate.name==nextName; });
        if (next==parts_.end() || next->points.empty()) return {};
        const auto nextPrism=toePrism(static_cast<std::size_t>(next-parts_.begin()),origin);
        if (nextPrism.size()!=16) return {};
        Vec3 center{}, nextCenter{};
        for (Vec3 p:part.points) center+=mapPart(p,part)+origin;
        for (Vec3 p:next->points) nextCenter+=mapPart(p,*next)+origin;
        center=center/static_cast<double>(part.points.size());
        nextCenter=nextCenter/static_cast<double>(next->points.size());
        const Vec3 forward=normalized(nextCenter-center);
        const Vec3 lateral=normalized(cross(Vec3{0,1,0},forward));
        const Vec3 up=normalized(cross(forward,lateral));
        if (length(forward)<1e-8 || length(lateral)<1e-8 || length(up)<1e-8) return {};
        const auto local=[&](Vec3 p) { return Vec3{dot(p,lateral),dot(p,up),dot(p,forward)}; };
        Vec3 low=local(mapPart(part.points.front(),part)+origin), high=low;
        for (Vec3 p:part.points) {
            p=local(mapPart(p,part)+origin);
            low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); low.z=std::min(low.z,p.z);
            high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y); high.z=std::max(high.z,p.z);
        }
        Vec3 nextLow=local(nextPrism[0]), nextHigh=nextLow;
        for (int k=1;k<8;++k) {
            const Vec3 p=local(nextPrism[k]);
            nextLow.x=std::min(nextLow.x,p.x); nextLow.y=std::min(nextLow.y,p.y); nextLow.z=std::min(nextLow.z,p.z);
            nextHigh.x=std::max(nextHigh.x,p.x); nextHigh.y=std::max(nextHigh.y,p.y); nextHigh.z=std::max(nextHigh.z,p.z);
        }
        const double back=low.z-0.08*(high.z-low.z);
        const double front=std::max(high.z,(nextLow.z+nextHigh.z)*0.5+0.12*(high.z-low.z));
        constexpr double extent=0.9238795325112867, pi=3.14159265358979323846;
        std::vector<Vec3> vertices; vertices.reserve(16);
        for (int end=0;end<2;++end) for (int k=0;k<8;++k) {
            const double x=end ? (nextLow.x+nextHigh.x)*0.5 : (low.x+high.x)*0.5;
            const double y=end ? (nextLow.y+nextHigh.y)*0.5 : (low.y+high.y)*0.5;
            const double rx=(end ? (nextHigh.x-nextLow.x)*0.5 : (high.x-low.x)*0.65)/extent;
            const double ry=(end ? (nextHigh.y-nextLow.y)*0.5 : (high.y-low.y)*0.65)/extent;
            const double angle=(double(k)+0.5)*pi/4.0;
            vertices.push_back(lateral*(x+rx*std::cos(angle))+up*(y+ry*std::sin(angle))+forward*(end ? front : back));
        }
        return vertices;
    }
    // Two successive 5% reductions of the big-toe exterior. Bone geometry
    // and bone transforms remain unchanged.
    static constexpr double greatToeScale=0.95*0.95;
    bool greatToeAnchor(std::size_t i, Vec3 origin, Vec3& anchor) const {
        const auto& name=parts_[i].name;
        const bool distal=name.rfind("FootDistP1_",0)==0;
        const bool proximal=name.rfind("FootProxP1_",0)==0;
        if (!distal && !proximal) return false;
        const std::string proximalName=distal ? "FootProxP"+name.substr(9) : name;
        const auto part=std::find_if(parts_.begin(),parts_.end(),
            [&](const Part& p) { return p.name==proximalName; });
        if (part==parts_.end()) return false;
        const auto prism=toePrism(static_cast<std::size_t>(part-parts_.begin()),origin);
        if (prism.size()!=16) return false;
        anchor={}; for (int k=0;k<8;++k) anchor+=prism[k];
        anchor=anchor/8.0;
        // Anchor the reduction at the upper surface so it does not uncover
        // the unchanged bone at the top of the toe joint.
        for (int k=0;k<8;++k) anchor.y=std::max(anchor.y,prism[k].y);
        return true;
    }
    void resizeGreatToe(std::size_t i, Vec3 origin, std::vector<Vec3>& points) const {
        Vec3 anchor{};
        if (!greatToeAnchor(i,origin,anchor)) return;
        for (Vec3& p:points) p=anchor+(p-anchor)*greatToeScale;
    }
    ToeSkinMesh toeSurface(std::size_t i, Vec3 origin) const {
        const auto& name=parts_[i].name;
        if (name.rfind("FootDistP",0)!=0) return {};
        const std::string suffix=name.substr(9);
        const auto findPart=[&](const std::string& partName) {
            return std::find_if(parts_.begin(),parts_.end(),[&](const Part& p) { return p.name==partName; });
        };
        const auto proximal=findPart("FootProxP"+suffix);
        const auto middle=findPart("FootMidP"+suffix);
        if (proximal==parts_.end() || (suffix[0]!='1' && middle==parts_.end())) return {};
        std::vector<std::vector<Vec3>> prisms;
        std::vector<int> owners;
        const auto add=[&](const auto& part) {
            const auto index=static_cast<std::size_t>(part-parts_.begin());
            prisms.push_back(toePrism(index,origin)); owners.push_back(static_cast<int>(index));
        };
        add(proximal); if (suffix[0]!='1') add(middle);
        std::vector<Vec3> tipBone; tipBone.reserve(parts_[i].points.size());
        for (Vec3 p:parts_[i].points) tipBone.push_back(mapPart(p,parts_[i])+origin);
        // Determine nail clearance against the unchanged bone in the
        // pre-scaled frame, then scale the finished toe exterior.
        Vec3 anchor{};
        if (suffix[0]=='1' && greatToeAnchor(i,origin,anchor))
            for (Vec3& p:tipBone) p=anchor+(p-anchor)/greatToeScale;
        auto skin=makeToeSkin(prisms,owners,toeIcosahedron(i,origin),tipBone,static_cast<int>(i));
        if (suffix[0]=='1') {
            resizeGreatToe(i,origin,skin.points);
            resizeGreatToe(i,origin,skin.nailPoints);
        }
        return skin;
    }
    void draw(Rasterizer& rasterizer, double x, std::uint32_t idBase=12000,
              const std::vector<std::array<double,16>>* pose=nullptr, bool jointViewOwners=false) const {
        Material blue; blue.color=colorHex("#358fff"); blue.specular=0.12;
        Material toe; toe.color=colorHex("#e9bd9f"); toe.specular=0.12; toe.opacity=0.5;
        const Vec3 origin{x,0.10,0};
        for (std::size_t i=0; i<parts_.size(); ++i) {
            const Part& part=parts_[i]; std::vector<Vec3> points; points.reserve(part.points.size());
            for (Vec3 p:part.points) points.push_back(mapPart(p,part)+origin);
            if (!rasterizer.gpu() && pose && i<pose->size()) for (Vec3& p:points) p=transform((*pose)[i],p);
            rasterizer.setObject(idBase+static_cast<std::uint32_t>(i),{},0);
            rasterizer.mesh(points,part.faces,blue);
            if (pose && i<pose->size()) rasterizer.setBatchPose((*pose)[i]);
            if (jointViewOwners || part.name.rfind("FootProxP",0)==0 || part.name.rfind("FootMidP",0)==0 || part.name.rfind("FootDistP",0)==0 ||
                part.name.rfind("FootMeta",0)==0 || part.name.rfind("Cal_",0)==0 || part.name.rfind("Tal_",0)==0 ||
                part.name.rfind("Nav_",0)==0 || part.name.rfind("Cub_",0)==0 || part.name.find("Cun_")!=std::string::npos ||
                part.name.rfind("Tibia_",0)==0 || part.name.rfind("Fibula_",0)==0) rasterizer.markSkinOwner();
        }
        drawMakeHumanFeet(*this,male_,rasterizer,origin,idBase,pose,toe);
    }
private:
    static Vec3 transform(const std::array<double,16>& m, Vec3 p) {
        return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    }
    static double lerp(double a,double b,double t) { return a+(b-a)*t; } double mapY(double y) const {
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
    bool update(std::string& error) {
        if (male_) {
            const auto values=maleComparisonFormulas(profile_.height);
            if (!values) { error="Male formulas returned a non-positive or non-finite value"; return false; }
            requestedTargets_={profile_.height,(*values)[0],(*values)[1],(*values)[2],
                               (*values)[3],(*values)[4],(*values)[5],(*values)[6]};
        } else {
            const auto values=femaleComparisonFormulas(profile_.height);
            if (!values) { error="Female formulas returned a non-positive or non-finite value"; return false; }
            requestedTargets_={profile_.height,(*values)[0],(*values)[1],(*values)[2],
                               (*values)[3],(*values)[4],(*values)[5],(*values)[6]};
        }
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
        const double shoulderY=(armAnchor_[0].y+armAnchor_[1].y)*0.5;
        const double epsilon=std::max(1e-9,(top_-floor_)*1e-7);
        shoulderYTarget_=mapY(shoulderY);
        shoulderVertical_=(mapY(shoulderY+epsilon)-mapY(shoulderY-epsilon))/(2*epsilon);
        // Solve the average of the two endpoint chords after the shared field.
        double desired=targets_[Arm]/100;
        auto armLength=[&](double value) {
            armScale_=value; double sum=0;
            for (int side=0;side<2;++side) sum+=length(map(armEnd_[side],side==0?1:-1)-map(armStart_[side],side==0?1:-1,false,false,true));
            return sum*0.5;
        };
        double a=1e-12,b=2;
        upperArmScale_=shoulderScale_*profile_.multiplier[Arm];
        const double normalUpperScale=upperArmScale_;
        const double fixedUpperMinimum=armLength(a);
        if (fixedUpperMinimum>desired) {
            // At very short arm requests the fixed upper arm alone can exceed
            // the target. Shorten its shaft around the protected shoulder head
            // before solving the distal scale; keep a continuous elbow frame.
            upperArmScale_=a;
            const double shortest=armLength(a);
            // A positive requested length may still be shorter than the fixed
            // vertical/depth offset between the endpoints. Keep the request and
            // fit the closest possible chord instead of blocking the slider.
            if (shortest>=desired) {
                desired=shortest+1e-10;
                targets_[Arm]=desired*100;
                exact_=false;
            }
            // The reserve tends to zero at the entry to this fallback, so
            // neither the upper arm nor the forearm jumps at the boundary.
            const double reserve=std::min(0.05*(fixedUpperMinimum-desired),0.5*(desired-shortest));
            const double upperTarget=desired-reserve;
            double low=a,high=normalUpperScale;
            for (int n=0;n<50;++n) {
                upperArmScale_=(low+high)*0.5;
                if (armLength(a)<upperTarget) low=upperArmScale_; else high=upperArmScale_;
            }
            upperArmScale_=(low+high)*0.5;
        }
        while (armLength(b)<desired && b<1e150) b*=2;
        if (armLength(a)>desired || armLength(b)<desired) { error="Arm target is incompatible with the pose"; feasible_=false; return false; }
        for (int n=0;n<50;++n) { double mid=(a+b)*0.5; if (armLength(mid)<desired) a=mid; else b=mid; }
        armScale_=(a+b)*0.5;
        // Approximate the existing body field over each entire vertebra,
        // including bones crossing a field knot, with a local least-squares
        // affine map. Remove the component of its normal axis parallel to the
        // mapped plane: this preserves reflection without forcing a uniform
        // scale or slicing a vertebra at a field boundary.
        for (auto& part:parts_) if (part.vertebra && !part.cranial) {
            double qq[3][3]{}; Vec3 tq[3]{},qsum{},tsum{};
            for (Vec3 p:part.points) {
                const Vec3 d=p-part.vertebraAnchor;
                const Vec3 q{dot(d,part.vertebraNormal),dot(d,part.vertebraUp),dot(d,part.vertebraForward)};
                const Vec3 t=map(p); const double v[3]={q.x,q.y,q.z};
                qsum+=q; tsum+=t;
                for (int i=0;i<3;++i) { tq[i]+=t*v[i]; for (int j=0;j<3;++j) qq[i][j]+=v[i]*v[j]; }
            }
            const double count=static_cast<double>(part.points.size());
            const Vec3 qm=qsum/count,tm=tsum/count; const double mv[3]={qm.x,qm.y,qm.z};
            for (int i=0;i<3;++i) {
                tq[i]=tq[i]/count-tm*mv[i];
                for (int j=0;j<3;++j) qq[i][j]=qq[i][j]/count-mv[i]*mv[j];
            }
            const Vec3 c0{qq[0][0],qq[1][0],qq[2][0]},c1{qq[0][1],qq[1][1],qq[2][1]},c2{qq[0][2],qq[1][2],qq[2][2]};
            const double det=dot(c0,cross(c1,c2));
            if (std::abs(det)<1e-30) { error="Invalid vertebra scaling frame"; return false; }
            const Vec3 rows[3]={cross(c1,c2)/det,cross(c2,c0)/det,cross(c0,c1)/det};
            const auto column=[&](int j) {
                const double v[3]={j==0?rows[0].x:j==1?rows[0].y:rows[0].z,
                                   j==0?rows[1].x:j==1?rows[1].y:rows[1].z,
                                   j==0?rows[2].x:j==1?rows[2].y:rows[2].z};
                return tq[0]*v[0]+tq[1]*v[1]+tq[2]*v[2];
            };
            const Vec3 jn=column(0),ju=column(1),jv=column(2),crossPlane=cross(ju,jv);
            if (length(crossPlane)<1e-15) { error="Invalid vertebra scaling frame"; return false; }
            const Vec3 normal=normalized(crossPlane); const double normalScale=dot(jn,normal);
            if (!(normalScale>0)) { error="Invalid vertebra normal scale"; return false; }
            part.mappedNormal=normal; part.mappedNormalScale=normalScale;
            part.mappedUp=ju; part.mappedForward=jv;
            part.mappedClearance=part.jointClearance*(profile_.height/defaultHeight());
            part.mappedAnchor=tm-normal*(normalScale*qm.x)-ju*qm.y-jv*qm.z+part.mappedClearance;
        }
        ++revision_; error.clear(); return true;
    }
    bool male_=false, feasible_=false, exact_=false, toeAttachmentsEnabled_=true; ToeSkinCalibration toeSkinCalibration_;
    BodyProportionCurves curves_;
    Profile profile_;
    std::vector<Part> parts_; std::vector<JointRigidTransform> metatarsalFrames_;
    std::vector<std::array<double,16>> referenceMatrices_, correctedToeMatrices_, standingToeMatrices_, originalSkinToeMatrices_;
    mutable std::vector<ComparisonToeAttachment> toeAttachments_; mutable std::uint64_t toeAttachmentRevision_=0;
    std::array<double,Count> targets_{}, requestedTargets_{};
    double floor_=0,top_=0,jaw_=0,cranialJoin_=0,pelvis_=0,pelvisTop_=0,hip_=0,c7_=0,neck_=0,neckZ_=0;
    double scapBottom_=0,scapTop_=0,hipWidth_=0,shoulderWidth_=0,centerX_=0;
    double initialHeight_=0,shoulderYTarget_=0,shoulderVertical_=1,ankleY_=0,kneeY_=1;
    double heightScale_=1,hipScale_=1,shoulderScale_=1,headScale_=1,armScale_=1,upperArmScale_=1;
    std::array<double,7> sourceY_{}, targetY_{};
    std::array<double,2> hipY_{};
    std::array<Vec3,2> hipCenters_{},armAnchor_{},elbowAnchor_{},armStart_{},armEnd_{};
    std::uint64_t revision_=0;
};
} // namespace gtc::native
