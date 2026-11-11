// Building the comparison from a loaded model: every part, where it sits, which
// arm it belongs to, and the reference matrices the toe attachments are measured
// against. It is the one long function in the comparison, so it is out of the
// header and in a file of its own -- the sliders and the drawing read the result
// and never need to see how it is filled.

#include "client/skeleton_comparison.h"

namespace gtc::native {

bool SkeletonComparison::load(const SkeletonModel& model, bool male, const std::string& directory,
                             std::string& error) {
        male_ = male; correctedToeMatrices_.clear(); standingToeMatrices_.clear();
        const auto matrices = model.sourceMatrices({}, 0); referenceMatrices_=matrices;
        parts_.clear();
        for (std::size_t i=0; i<matrices.size(); ++i) {
            const auto& source = model.sourceStructures()[i];
            const auto& group = model.bones()[source.group];
            Part part; part.name=source.name;
            part.cranial=part.name=="Head" || part.name=="Jaw" || part.name=="C1";
            part.girdle=part.name.rfind("Scapula_",0)==0 || part.name.rfind("Clavicula_",0)==0;
            part.humerus=part.name.rfind("Humerus_",0)==0;
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
        originalSkinToeMatrices_=male ? referenceMatrices_ : originalLittleToeSkinReference(parts_,referenceMatrices_);
        double originalFloor=1e30,originalTop=-1e30;
        for (const auto& part:parts_) for (Vec3 p:part.points) {
            originalFloor=std::min(originalFloor,p.y);
            if (part.name=="Head") originalTop=std::max(originalTop,p.y);
        }
        initialHeight_=(originalTop-originalFloor)*100;
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
        std::array<Vec3,2> hips, shoulders, elbows;
        if (!landmarks(".hips","Femur",hips) || !landmarks(".shoulders","Humerus",shoulders) ||
            !landmarks(".elbows","Humerus",elbows)) {
            error="Comparison joint estimates could not be loaded"; return false;
        }
        // Average the anatomical leg length before reflecting the paired
        // surfaces. Original/white meshes and their rig are never modified.
        const double axis=(hips[0].x+hips[1].x)*0.5;
        const double hipLevel=(hips[0].y+hips[1].y)*0.5;
        // First fit every central bone in its own scan frame. Afterwards use
        // the shortest rigid rotation to align that plane to the upright body
        // midplane. A final X translation centers it; no shape is flattened.
        const auto alignVector=[](Vec3 v,Vec3 normal) {
            const Vec3 turn=cross(normal,Vec3{1,0,0});
            return v+cross(turn,v)+cross(turn,cross(turn,v))/(1+normal.x);
        };
        const auto alignPoint=[&](Vec3 p,const SkullMidplane& plane) {
            return Vec3{axis,plane.origin.y,plane.origin.z}+alignVector(p-plane.origin,plane.normal);
        };
        auto leg=[](const std::string& name) {
            return name.rfind("Femur_",0)==0 || name.rfind("Patella_",0)==0 ||
                   name.rfind("Tibia_",0)==0 || name.rfind("Fibula_",0)==0 ||
                   name.rfind("Foot",0)==0 || name.rfind("Tal_",0)==0 ||
                   name.rfind("Cal_",0)==0 || name.rfind("Nav_",0)==0 ||
                   name.rfind("Cub_",0)==0 || name.rfind("MedCun_",0)==0 ||
                   name.rfind("InterCun_",0)==0 || name.rfind("LatCun_",0)==0;
        };
        std::array<double,2> soles{1e30,1e30};
        for (const auto& part:parts_) if (leg(part.name) && part.name.size()>2) {
            const std::string suffix=part.name.substr(part.name.size()-2);
            if (suffix!="_L" && suffix!="_R") continue;
            const int side=suffix=="_L"?0:1;
            for (Vec3 p:part.points) soles[side]=std::min(soles[side],p.y);
        }
        const double meanLength=((hips[0].y-soles[0])+(hips[1].y-soles[1]))*0.5;
        if (!(meanLength>0 && hips[0].y>soles[0] && hips[1].y>soles[1])) {
            error="Comparison leg landmarks are invalid"; return false;
        }
        for (auto& part:parts_) if (leg(part.name) && part.name.size()>2) {
            const std::string suffix=part.name.substr(part.name.size()-2);
            if (suffix!="_L" && suffix!="_R") continue;
            const int side=suffix=="_L"?0:1;
            const double factor=meanLength/(hips[side].y-soles[side]);
            for (Vec3& p:part.points) p.y=hipLevel+(p.y-hips[side].y)*factor;
        }
        if (!registerComparisonMetatarsals(parts_,metatarsalFrames_)) { error="Comparison metatarsal registration failed"; return false; }
        // Preserve the mean pubic spacing of the two registered innominates.
        double leftPubis=1e30,rightPubis=-1e30;
        for (const auto& part:parts_) {
            if (part.name=="Pelvis_L") for (Vec3 p:part.points) leftPubis=std::min(leftPubis,p.x);
            if (part.name=="Pelvis_R") for (Vec3 p:part.points) rightPubis=std::max(rightPubis,p.x);
        }
        const double meanPubicHalfGap=(leftPubis-rightPubis)*0.5;
        for (auto& left:parts_) {
            if (left.name.size()<2 || left.name.substr(left.name.size()-2)!="_L") continue;
            const std::string rightName=left.name.substr(0,left.name.size()-2)+"_R";
            auto it=std::find_if(parts_.begin(),parts_.end(),[&](const Part& p) { return p.name==rightName; });
            if (it==parts_.end()) { error="Comparison side is missing its opposite"; return false; }
            std::vector<Vec3> reflected; reflected.reserve(it->points.size());
            for (Vec3 p:it->points) { p.x=2*axis-p.x; reflected.push_back(p); }
            if (!bilateral::average(left.points,reflected,left.faces,left.name.rfind("Scapula_",0)==0)) {
                error="Comparison side has an empty mesh"; return false;
            }
            it->points=left.points;
            for (Vec3& p:it->points) p.x=2*axis-p.x;
            it->faces=left.faces;
            for (auto& face:it->faces) std::swap(face[1],face[2]);
        }
        auto mutablePart=[&](const std::string& name)->Part* {
            for (auto& part:parts_) if (part.name==name) return &part;
            return nullptr;
        };
        // Each vertebra has its own source-space 3D reflection plane. Carry
        // that complete frame through its posed bone matrix, average both
        // surfaces, then mirror only the blue mesh. No source bone moves.
        std::map<std::string,VertebraPlane> vertebraPlanes;
        if (!loadVertebraPlanes(directory+"skeleton-"+sex+".vertebra-planes",vertebraPlanes)) {
            error="Comparison vertebra planes unavailable or invalid"; return false;
        }
        for (const auto& name:vertebraNames()) {
            Part* part=mutablePart(name); const int id=model.sourceStructure(name);
            if (!part || id<0) { error="Comparison vertebra is missing"; return false; }
            const SkullMidplane plane=posedVertebraPlane(vertebraPlanes.at(name),matrices[static_cast<std::size_t>(id)]);
            if (plane.normal.x<=-0.999) { error="Invalid vertebra orientation: "+name; return false; }
            part->vertebra=true;
            for (Vec3& p:part->points) p=plane.local(p);
            if (!bilateral::surfaceAverage(part->points,part->faces,nullptr,true) ||
                !bilateral::mirrorAxial(part->points,part->faces,0)) {
                error="Comparison vertebra could not be averaged: "+name; return false;
            }
            for (Vec3& p:part->points) p=alignPoint(plane.world(p),plane);
            part->vertebraAnchor={axis,plane.origin.y,plane.origin.z};
            part->vertebraNormal={1,0,0};
            part->vertebraUp=alignVector(plane.up,plane.normal);
            part->vertebraForward=alignVector(plane.forward,plane.normal);
            // Small source-specific joint clearances, measured against the
            // posed triangle surfaces after averaging. A rigid translation
            // preserves the individual vertebra's fitted mirror symmetry.
            Vec3 clearance{};
            if (male && name=="C1") clearance.y=-0.0004;
            if (male && name=="T6") clearance.z=0.0002;
            if (!male && name=="T2") clearance.y=-0.0006;
            for (Vec3& p:part->points) p+=clearance;
            part->vertebraAnchor+=clearance;
            // Additional millimetre-scale, source-specific joint registrations
            // are applied after body scaling so each bone stays symmetric.
            if (male && name=="T5") part->jointClearance.y=0.0006;
            if (male && name=="T8") part->jointClearance.y=-0.0002;
            if (!male && (name=="T1" || name=="T3" || name=="T4" || name=="T5" ||
                          name=="T6" || name=="T7" || name=="T8")) part->jointClearance.y=0.0014;
            if (!male && name=="C5") part->jointClearance.y=0.0002;
            if (!male && name=="L1") part->jointClearance.z=-0.0023;
            if (!male && name=="L2") part->jointClearance.z=-0.0018;
            if (!male && name=="L3") part->jointClearance.z=-0.0010;
            if (!male && name=="L4") part->jointClearance.z=-0.0007;
        }
        // Reflect the skull in its own fitted geometric frame, not the
        // pelvis frame. Both skull and jaw use the same skull-derived plane;
        // no independent centroid shift separates the jaw from the skull.
        const int headId=model.sourceStructure("Head");
        if (headId<0) { error="Comparison skull structure is missing"; return false; }
        const SkullMidplane skullPlane=skullMidplane(male,matrices[static_cast<std::size_t>(headId)]);
        if (skullPlane.normal.x<=-0.999) { error="Invalid skull orientation"; return false; }
        // The offline skull-only fit also identifies outward-visible Head
        // triangles. Exterior vertices must match that surface, not the
        // interior wall through thin cranial bone.
        std::ifstream outsideFile(directory+"skeleton-"+sex+".skull-exterior");
        std::vector<bool> outside; std::string maskLine;
        while (std::getline(outsideFile,maskLine)) {
            if (!maskLine.empty() && maskLine[0]=='#') continue;
            for (char bit:maskLine) {
                if (bit=='0' || bit=='1') outside.push_back(bit=='1');
                else if (bit!='\r' && bit!=' ' && bit!='\t') {
                    error="Invalid skull exterior mask"; return false;
                }
            }
        }
        if (!outsideFile.eof()) { error="Skull exterior mask unavailable"; return false; }
        for (const char* name:{"Head","Jaw"}) {
            Part* part=mutablePart(name);
            if (!part) { error="Comparison skull structure is missing"; return false; }
            for (Vec3& p:part->points) p=skullPlane.local(p);
            if (!bilateral::surfaceAverage(part->points,part->faces,part->name=="Head" ? &outside : nullptr,true) ||
                !bilateral::mirrorAxial(part->points,part->faces,0)) {
                error="Comparison skull could not be averaged"; return false;
            }
            // Keep the fitted 3D skull shape and its jaw coupling, then rotate
            // both rigidly to the common upright plane requested for the whole
            // blue skeleton. The white source and its scan pose are untouched.
            for (Vec3& p:part->points) p=alignPoint(skullPlane.world(p),skullPlane);
        }
        // The axial sternum is also centered and mirrored.
        if (Part* sternum=mutablePart("Sternum")) {
            std::vector<Vec3> opposite; opposite.reserve(sternum->points.size());
            for (Vec3 p:sternum->points) { p.x=2*axis-p.x; opposite.push_back(p); }
            if (!bilateral::average(sternum->points,opposite,sternum->faces) ||
                !bilateral::mirrorAxial(sternum->points,sternum->faces,axis)) {
                error="Comparison sternum could not be averaged"; return false;
            }
        }
        Part* meanPelvis=mutablePart("Pelvis_L");
        Part* meanSacrum=mutablePart("Sacrum");
        if (!meanPelvis || !meanSacrum) { error="Comparison pelvic ring is missing"; return false; }
        std::vector<Vec3> reflectedSacrum; reflectedSacrum.reserve(meanSacrum->points.size());
        for (Vec3 p:meanSacrum->points) { p.x=2*axis-p.x; reflectedSacrum.push_back(p); }
        if (!bilateral::average(meanSacrum->points,reflectedSacrum,meanSacrum->faces)) {
            error="Comparison sacrum could not be averaged"; return false;
        }
        if (male) {
            // The male source coccyx tip sits to one side of the body plane.
            // Center that tip before clipping and reflection; otherwise the
            // retained off-axis tip is duplicated into two tails. Fade the
            // small X correction smoothly into the lower sacrum, leaving its
            // upper joints and the original source mesh untouched.
            const auto low=std::min_element(meanSacrum->points.begin(),meanSacrum->points.end(),
                                            [](Vec3 a,Vec3 b){return a.y<b.y;});
            const auto high=std::max_element(meanSacrum->points.begin(),meanSacrum->points.end(),
                                             [](Vec3 a,Vec3 b){return a.y<b.y;});
            const double bottom=low->y, span=high->y-bottom, offset=axis-low->x;
            if (!(span>0)) { error="Invalid sacrum extent"; return false; }
            for (Vec3& p:meanSacrum->points) {
                const double t=std::clamp((p.y-bottom-0.08*span)/(0.17*span),0.0,1.0);
                p.x+=offset*(1-t*t*(3-2*t));
            }
        }
        if (!bilateral::mirrorAxial(meanSacrum->points,meanSacrum->faces,axis)) {
            error="Comparison sacrum could not be mirrored"; return false;
        }
        double medial=1e30; for (Vec3 p:meanPelvis->points) medial=std::min(medial,p.x);
        const double outward=axis+meanPubicHalfGap-medial;
        // These source-specific registrations were fitted to the two shipped
        // pelvic surfaces, retaining their L5 contact. Sacrum width and its
        // vertical/depth offset are fitted symmetrically; this is not a
        // clinical model and must be re-audited if the source meshes change.
        const double sacralWidth=male?1.01195186:1.03691149;
        const Vec3 sacralFit=male?Vec3{0,0.00061292,-0.00065923}
                                  :Vec3{0,-0.00401286,-0.00456522};
        const Vec3 hipFit=male?Vec3{-0.000484,-0.001431,0.001137}:Vec3{};
        const Vec3 kneeFit=male?Vec3{0.000065,-0.001386,0.000294}
                                :Vec3{-0.000139,-0.001910,0.000217};
        const Vec3 scapulaFit=male?Vec3{0.005501,-0.004520,-0.003794}
                                  :Vec3{-0.005611,0.000206,-0.001211};
        for (auto& part:parts_) {
            if (part.name=="Sacrum") for (Vec3& p:part.points) {
                p.x=axis+(p.x-axis)*sacralWidth; p+=sacralFit;
            }
            if (part.name.size()<2) continue;
            const std::string suffix=part.name.substr(part.name.size()-2);
            if (suffix!="_L" && suffix!="_R") continue;
            const int side=suffix=="_L"?1:-1;
            if (part.name.rfind("Scapula_",0)==0)
                for (Vec3& p:part.points) { p.x+=side*scapulaFit.x; p.y+=scapulaFit.y; p.z+=scapulaFit.z; }
            // After centering the vertebrae, move affected rib pairs equally
            // outward in X to retain selected costovertebral clearances.
            if (part.name.rfind("Rib",0)==0) {
                const int n=std::stoi(part.name.substr(3));
                constexpr double femaleRibOffset[13]={0,.0012,.0010,0,0,.0008,.0012,.0008,.0035,.0040,.0045,.0040,.0012};
                const double offset=n<1 || n>12 ? 0 : male ? (n==10 ? .0027 : 0) : femaleRibOffset[n];
                for (Vec3& p:part.points) p.x+=side*offset;
            }
            if (part.name.rfind("Pelvis_",0)==0 || leg(part.name))
                for (Vec3& p:part.points) p.x+=side*outward;
            if (leg(part.name)) for (Vec3& p:part.points) {
                p.x+=side*hipFit.x; p.y+=hipFit.y; p.z+=hipFit.z;
                if (part.name.rfind("Femur_",0)!=0 && part.name.rfind("Patella_",0)!=0) {
                    p.x+=side*kneeFit.x; p.y+=kneeFit.y; p.z+=kneeFit.z;
                }
            }
        }
        // Regularisation can change which foot vertex is lowest. Reapply the
        // exact bilateral mean hip-to-floor length to the complete legs.
        double balancedSole=1e30;
        for (const auto& part:parts_) if (leg(part.name))
            for (Vec3 p:part.points) balancedSole=std::min(balancedSole,p.y);
        const double balancedHip=hipLevel+hipFit.y;
        const double finalLegScale=meanLength/(balancedHip-balancedSole);
        if (!std::isfinite(finalLegScale) || finalLegScale<=0) {
            error="Comparison averaged leg length is invalid"; return false;
        }
        for (auto& part:parts_) if (leg(part.name))
            for (Vec3& p:part.points) p.y=balancedHip+(p.y-balancedHip)*finalLegScale;
        const double hipDepth=(hips[0].z+hips[1].z)*0.5+hipFit.z;
        hips[0].x+=outward+hipFit.x; hips[1].x-=outward+hipFit.x;
        hips[0].y+=hipFit.y; hips[1].y+=hipFit.y;
        hips[0].z=hipDepth; hips[1].z=hipDepth;
        hipCenters_=hips;
        auto named=[&](const std::string& name)->const Part* {
            for (const auto& part:parts_) if (part.name==name) return &part;
            return nullptr;
        };
        const Part* head=named("Head"), *jaw=named("Jaw"), *atlas=named("C1");
        const Part *pelvisL=named("Pelvis_L"), *pelvisR=named("Pelvis_R"), *sacrum=named("Sacrum");
        const Part *scapL=named("Scapula_L"), *scapR=named("Scapula_R");
        const Part *humerusL=named("Humerus_L"), *humerusR=named("Humerus_R");
        const Part *fingerL=named("HandDistP3_L"), *fingerR=named("HandDistP3_R");
        const Part *radiusL=named("Radius_L");
        if (!head || !jaw || !atlas || !pelvisL || !pelvisR || !sacrum || !scapL || !scapR ||
            !humerusL || !humerusR || !fingerL || !fingerR || !radiusL) {
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
        hipY_={hips[0].y,hips[1].y};
        hip_=(hipY_[0]+hipY_[1])*0.5;
        centerX_=axis;
        const Vec3 shoulderMean={(shoulders[0].x+2*axis-shoulders[1].x)*0.5,
                                 (shoulders[0].y+shoulders[1].y)*0.5,
                                 (shoulders[0].z+shoulders[1].z)*0.5};
        armAnchor_[0]=shoulderMean;
        armAnchor_[1]={2*axis-shoulderMean.x,shoulderMean.y,shoulderMean.z};
        Vec3 elbowMean={(elbows[0].x+2*axis-elbows[1].x)*0.5,
                        (elbows[0].y+elbows[1].y)*0.5,
                        (elbows[0].z+elbows[1].z)*0.5};
        // The closest radius/humerus surfaces in the reconstructed mesh are
        // the appropriate x anchor for forearm scaling. The original axis can
        // be displaced by averaging and would let a short forearm cross the
        // humerus even though both halves still look symmetric.
        bilateral::PointTree humerusTree(humerusL->points);
        double closest=1e30;
        for (Vec3 p:radiusL->points) {
            const Vec3 q=humerusTree.nearest(p); const double d=dot(q-p,q-p);
            if (d<closest) { closest=d; elbowMean.x=p.x; }
        }
        elbowAnchor_[0]=elbowMean;
        elbowAnchor_[1]={2*axis-elbowMean.x,elbowMean.y,elbowMean.z};
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
        if (!comparisonFootLandmarks(parts_,floor_,ankleY_,kneeY_)) { error="Foot landmarks invalid"; return false; }
        profile_=Profile{}; profile_.height=defaultHeight();
        if (!update(error)) return false;
        if (!fitStandingComparisonFeet(parts_,metatarsalFrames_,[&](Vec3 p,const Part& part) { return mapPart(p,part); },male_)) {
            error="Standing foot fitting failed"; return false;
        }
        floor_=1e30; for (const auto& part:parts_) for (Vec3 p:part.points) floor_=std::min(floor_,p.y);
        return update(error);
    }

}  // namespace gtc::native
