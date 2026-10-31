#include "client/skeleton_room.h"
#include "client/room_freecam.h"
#include "client/skeleton_rest_toes.h"
#include "client/skeleton_toe_yaw.h"
#include "client/threadpool.h"
#include "client/ui_layer.h"

#include <thread>

#include <algorithm>
#include <cmath>

namespace gtc::native {
namespace {

// A room measured in metres, like the skeletons in it: eight metres by eight,
// three metres of headroom. The old room was built in engine units and the
// conversion is exactly the sort of thing that turns a rig test into an
// argument about scale.
constexpr double kRoomHalf = 6.0;
constexpr double kRoomHeight = 3.0;
constexpr double kTile = 1.0;

Vec3 structureCenter(const SkeletonModel& model, int index) {
    if (index<0) return {};
    const auto& part=model.sourceStructures()[static_cast<std::size_t>(index)];
    const auto& points=model.bones()[part.group].points; Vec3 result;
    for (std::size_t i=part.firstPoint;i<part.firstPoint+part.pointCount;++i) result+=points[i];
    return part.pointCount ? result/static_cast<double>(part.pointCount) : result;
}
Vec3 transformed(const std::array<double,16>& m, Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
double minimumY(const SkeletonModel& model) {
    const auto matrices=model.sourceMatrices({},0); double result=1e9;
    for (std::size_t i=0;i<matrices.size();++i) {
        const auto& part=model.sourceStructures()[i]; const auto& points=model.bones()[part.group].points;
        for (std::size_t j=part.firstPoint;j<part.firstPoint+part.pointCount;++j)
            result=std::min(result,transformed(matrices[i],points[j]).y);
    }
    return result;
}

void drawFloor(Rasterizer& rasterizer, const Camera& camera) {
    Material dark;
    dark.color = colorHex("#26332c");
    dark.constantLighting = true;
    Material pale;
    pale.color = colorHex("#54614f");
    pale.constantLighting = true;
    // A slab first. Without it the grout gap between tiles shows the empty void
    // below the room, which reads as a crack running through the floor.
    Material slab;
    slab.color = colorHex("#1d2823");
    slab.constantLighting = true;
    drawBox(rasterizer, camera, {0, -0.06, 0}, {kRoomHalf * 2, 0.08, kRoomHalf * 2}, slab);
    // Tiles are drawn individually rather than as one textured plane: the room
    // is a test bench, so a tile is a unit of measurement you can count.
    const int tiles = static_cast<int>(kRoomHalf * 2 / kTile);
    for (int x = 0; x < tiles; ++x) {
        for (int z = 0; z < tiles; ++z) {
            const double x0 = -kRoomHalf + x * kTile, z0 = -kRoomHalf + z * kTile;
            drawBox(rasterizer, camera, {x0 + kTile * 0.5, -0.02, z0 + kTile * 0.5}, {kTile * 0.98, 0.04, kTile * 0.98},
                    ((x + z) & 1) ? pale : dark);
        }
    }
}

void drawWalls(Rasterizer& rasterizer, const Camera& camera) {
    Material wall;
    wall.color = colorHex("#2f3a33");
    wall.constantLighting = true;
    Material skirting;
    skirting.color = colorHex("#1b241f");
    skirting.constantLighting = true;
    // Thickness 0.1 sits the inner face exactly on the room bound; the boxes
    // that meet in a corner therefore share a plane, and two coplanar faces at
    // identical depth z-fight into a seam. Each wall is pushed back by a
    // millimetre so the meeting faces are not coincident.
    const double inset = 0.001;
    drawBox(rasterizer, camera, {0, kRoomHeight * 0.5, -kRoomHalf - inset}, {kRoomHalf * 2, kRoomHeight, 0.1}, wall);
    drawBox(rasterizer, camera, {0, kRoomHeight * 0.5, kRoomHalf + inset}, {kRoomHalf * 2, kRoomHeight, 0.1}, wall);
    drawBox(rasterizer, camera, {-kRoomHalf - inset, kRoomHeight * 0.5, 0}, {0.1, kRoomHeight, kRoomHalf * 2}, wall);
    drawBox(rasterizer, camera, {kRoomHalf + inset, kRoomHeight * 0.5, 0}, {0.1, kRoomHeight, kRoomHalf * 2}, wall);
    drawBox(rasterizer, camera, {0, kRoomHeight, 0}, {kRoomHalf * 2 + 0.2, 0.1, kRoomHalf * 2 + 0.2}, skirting);
    // One-metre ticks on the back wall, so the skeletons' heights can be read
    // off the picture instead of trusted.
    Material tick;
    tick.color = colorHex("#7d8f6a");
    tick.constantLighting = true;
    for (int metre = 1; metre <= 3; ++metre) {
        drawBox(rasterizer, camera, {-kRoomHalf + 0.4, metre * 1.0, -kRoomHalf + 0.06},
                {0.5, 0.02, 0.02}, tick);
    }
}

} // namespace

bool SkeletonRoom::load(const std::string& root, std::string& error) {
    ready_ = false;
    lineSceneReady_ = false;
    restReady_ = false;
    sceneReady_ = false;
    const std::string directory = root + "/tools/skeleton-source/";
    if (!male_.load(directory + "skeleton-male.obj", error)) return false;
    if (!female_.load(directory + "skeleton-female.obj", error)) return false;
    if (!male_.loadOrbitCenters(directory + "skeleton-male.orbits", error)) return false;
    if (!female_.loadOrbitCenters(directory + "skeleton-female.orbits", error)) return false;
    if (!male_.loadHipCenters(directory + "skeleton-male.hips", error)) return false;
    if (!female_.loadHipCenters(directory + "skeleton-female.hips", error)) return false;
    if (!male_.loadShoulderCenters(directory + "skeleton-male.shoulders", error)) return false;
    if (!female_.loadShoulderCenters(directory + "skeleton-female.shoulders", error)) return false;
    if (!male_.loadKneeAxes(directory + "skeleton-male.knees", error)) return false;
    if (!female_.loadKneeAxes(directory + "skeleton-female.knees", error)) return false;
    if (!male_.loadElbowAxes(directory + "skeleton-male.elbows", error)) return false;
    if (!female_.loadElbowAxes(directory + "skeleton-female.elbows", error)) return false;
    if (!male_.loadSourceLinks(directory + "skeleton-joint-links.txt", error)) return false;
    if (!female_.loadSourceLinks(directory + "skeleton-joint-links.txt", error)) return false;
    if (!male_.installReferenceRig(error)) return false;
    if (!female_.installReferenceRig(error)) return false;
    if (!maleLines_.load(directory + "skeleton-male.lines", error)) return false;
    if (!femaleLines_.load(directory + "skeleton-female.lines", error)) return false;
    if (!maleLines_.bind(male_, error) || !femaleLines_.bind(female_, error)) return false;
    ready_ = true;
    if (!restoreReferencePose()) { ready_ = false; error = "Unable to pose skeletons"; return false; }
    const std::string curves=directory+"anthropometry/";
    if (!maleComparison_.loadCurves(curves,error) || !femaleComparison_.loadCurves(curves,error) ||
        !maleComparison_.load(male_,true,directory,error) || !femaleComparison_.load(female_,false,directory,error)) {
        ready_=false; return false;
    }
    error_.clear();
    return true;
}

bool SkeletonRoom::adjustReferenceGroup(int key, double degrees, int fingerJoint) {
    if (!ready_ || ragdoll_ || !std::isfinite(degrees)) return false;
    // Keys select concrete anatomical groups; there is no hidden joint-selection
    // mode. Every finger/toe can be moved independently of its neighbours.
    int digit = key >= '1' && key <= '5' ? key-'0' :
                key >= '6' && key <= '9' ? key-'5' : key == '0' ? 5 : 0;
    const bool hand = key >= '1' && key <= '5';
    bool changed = false;
    for (SkeletonModel* model : {&male_, &female_}) {
        for (std::size_t i=0; i<model->sourceLinks().size(); ++i) {
            const auto& link=model->sourceLinks()[i];
            const auto& a=model->sourceStructures()[link.first].name;
            const auto& b=model->sourceStructures()[link.second].name;
            const std::string& kind=link.mobility;
            bool match=false;
            if (digit) {
                const std::string token=std::string("P")+static_cast<char>('0'+digit)+"_";
                const auto& prefix=hand ? "Hand" : "Foot";
                match = (a.rfind(prefix,0)==0 || b.rfind(prefix,0)==0) &&
                        (a.find(token)!=std::string::npos || b.find(token)!=std::string::npos) &&
                        (kind=="thumb_mcp" || kind=="thumb_ip_hinge" ||
                         kind=="finger_mcp_condyloid" || kind=="finger_pip_hinge" || kind=="finger_dip_hinge" ||
                         kind=="great_toe_mtp" || kind=="lesser_toe_mtp" || kind=="toe_ip_hinge");
                if (fingerJoint) {
                    const bool base=hand ? (kind=="thumb_mcp" || kind=="finger_mcp_condyloid")
                                         : (kind=="great_toe_mtp" || kind=="lesser_toe_mtp");
                    const bool middle=hand ? (kind=="thumb_ip_hinge" || kind=="finger_pip_hinge")
                                           : (kind=="toe_ip_hinge" && (b.rfind("FootMidP",0)==0 || digit==1));
                    const bool tip=hand ? kind=="finger_dip_hinge"
                                        : (kind=="toe_ip_hinge" && digit!=1 && b.rfind("FootDistP",0)==0);
                    match=match && ((fingerJoint==1 && base) || (fingerJoint==2 && middle) || (fingerJoint==3 && tip));
                }
            } else if (key=='q') match=kind=="shoulder_ball_socket";
            else if (key=='e') match=kind=="elbow_hinge" && b.rfind("Ulna_",0)!=0;
            else if (key=='f') match=kind=="wrist_condyloid" && b.rfind("HandLuna_",0)!=0;
            else if (key=='g') match=kind=="hip_ball_socket";
            else if (key=='h') match=kind=="knee_modified_hinge";
            else if (key=='j') match=kind=="ankle_hinge";
            else if (key=='k') match=kind=="intervertebral";
            else if (key=='l') match=kind=="temporomandibular" || kind=="atlanto_occipital" || kind=="atlanto_axial";
            else if (key=='z') match=kind=="sternoclavicular" || kind=="acromioclavicular";
            else if (key=='x') match=kind=="thumb_cmc_saddle";
            if (key=='m' && kind=="elbow_hinge" && b.rfind("Ulna_",0)!=0) {
                const auto rotation=model->referenceForearmRotation(i);
                if (rotation && model->setReferenceForearmRotation(i,*rotation+degrees)) changed=true;
                continue;
            }
            if (key=='u' && kind=="hip_ball_socket") {
                const auto abduction=model->referenceHipAbduction(i);
                if (abduction && model->setReferenceHipAbduction(i,*abduction+degrees)) changed=true;
                continue;
            }
            if (key=='n' && kind=="hip_ball_socket") {
                const auto rotation=model->referenceHipRotation(i);
                if (rotation && model->setReferenceHipRotation(i,*rotation+degrees)) changed=true;
                continue;
            }
            if ((key=='i' || key=='o') && (kind=="sternoclavicular" || kind=="acromioclavicular")) {
                const std::size_t coordinate=key=='i' ? 1 : 2;
                const auto angle=model->referenceGirdleCoordinate(i,coordinate);
                if (angle && model->setReferenceGirdleCoordinate(i,coordinate,*angle+degrees)) changed=true;
                continue;
            }
            if (!match) continue;
            const auto angle=model->referenceBend(i);
            if (angle && model->setReferenceBend(i,*angle+degrees)) changed=true;
        }
    }
    return changed;
}

bool SkeletonRoom::restoreReferencePose() {
    if (!ready_) return false;
    ragdoll_.reset(); male_.clearDynamicSourcePose(); female_.clearDynamicSourcePose();
    for (SkeletonModel* model : {&male_,&female_}) {
        if (!model->restoreSourcePose() || !model->levelReferencePelvis()) return false;
        for (std::size_t i=0;i<model->sourceLinks().size();++i) {
            const auto& kind=model->sourceLinks()[i].mobility;
            if (!model->referenceBend(i)) continue;
            double target=0;
            if (kind=="shoulder_ball_socket") target=90;
            else if (kind!="sternoclavicular" && kind!="acromioclavicular" && kind!="intervertebral" && kind!="atlanto_axial" && kind!="atlanto_occipital" && kind!="hip_ball_socket" && kind!="elbow_hinge" && kind!="knee_modified_hinge" && kind!="ankle_hinge" &&
                     kind!="wrist_condyloid" && kind!="thumb_cmc_saddle" && kind!="thumb_mcp" && kind!="thumb_ip_hinge" &&
                     kind!="finger_mcp_condyloid" && kind!="finger_pip_hinge" && kind!="finger_dip_hinge" &&
                     kind!="great_toe_mtp" && kind!="lesser_toe_mtp" && kind!="toe_ip_hinge") continue;
            if (!model->setReferenceBend(i,target)) return false;
        }
        if (!restToesOnSole(*model)) return false;
        for (int pass=0;pass<2;++pass)
            if (!alignReferenceToeDirections(*model,model==&male_ ? maleLines_ : femaleLines_) || !settleReferenceToes(*model)) return false;
    }
    // Reposition each whole body onto the floor; no geometry is scaled.
    maleFloor_=-minimumY(male_); femaleFloor_=-minimumY(female_);
    return true;
}

bool SkeletonRoom::restoreOriginalPose() {
    ragdoll_.reset(); male_.clearDynamicSourcePose(); female_.clearDynamicSourcePose();
    if (!ready_ || !male_.restoreSourcePose() || !female_.restoreSourcePose()) return false;
    maleFloor_=femaleFloor_=0;
    return true;
}

bool SkeletonRoom::toggleRagdoll() {
    if (!ready_) return false;
    if (ragdoll_) return restoreReferencePose();
    auto simulation=std::make_unique<SkeletonRagdoll>();
    if (!simulation->create(male_,{-0.95,maleFloor_,0},female_,{0.95,femaleFloor_,0})) return false;
    ragdoll_=std::move(simulation);
    return true;
}

void SkeletonRoom::advanceRagdoll(double seconds) { if (ragdoll_) ragdoll_->advance(seconds); }

void SkeletonRoom::render(NativeFrame& frame, const ViewState& view, double seconds, const RoomFreecam* freecam) {
    Camera camera = freecam ? freecam->camera(view.width, view.height, view.gpu) : Camera{};
    if (!freecam) {
        // Existing benchmark/preview orbit, independent of the interactive freecam.
        camera.width = std::max(1, view.width);
        camera.height = std::max(1, view.height);
        camera.fovDegrees = 50;
        camera.target = {0, 0.95, 0};
        camera.position = {std::sin(view.yaw) * 3.5, 1.15, std::cos(view.yaw) * 3.5};
        camera.worldSpaceGpu = view.gpu;
    }
    const auto submit = [&](GpuScene& scene) {
        const std::size_t worldCount=scene.batches.size();
        frame.interfaceInFrame=false;
        if (frame.uiLayer && !frame.uiLayer->empty()) {
            appendUiBatches(*frame.uiLayer,camera,scene);
            frame.interfaceInFrame=true;
        }
        const bool ok=renderGpuScene(scene,frame);
        scene.batches.resize(worldCount);
        if (!ok) frame.rgb.clear();
    };
    if (lineView_) {
        // Build only when a pose changes; a static view remains cached on GPU.
        if (view.gpu) {
            if (!lineSceneReady_ || lineMalePoseRevision_ != male_.sourcePoseRevision() ||
                lineFemalePoseRevision_ != female_.sourcePoseRevision()) {
                Rasterizer raster(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), true);
                raster.setCamera(camera);
                drawFloor(raster, camera); drawWalls(raster, camera);
                maleLines_.draw(raster, {-0.95, maleFloor_, 0}, 0.0, 9001, male_.sourceMatrices({},0));
                femaleLines_.draw(raster, {0.95, femaleFloor_, 0}, 0.0, 8001, female_.sourceMatrices({},0));
                maleComparison_.draw(raster,-2.85,12000); femaleComparison_.draw(raster,2.85,14000);
                lineScene_ = raster.takeGpuScene();
                lineScene_.revision = ++revision_;
                lineMalePoseRevision_=male_.sourcePoseRevision();
                lineFemalePoseRevision_=female_.sourcePoseRevision();
                lineSceneReady_ = true;
            }
            lineScene_.width = camera.width; lineScene_.height = camera.height;
            lineScene_.focal = camera.focal(); lineScene_.viewMatrix = camera.viewMatrix();
            lineScene_.covered = freecam == nullptr;
            frame.width = camera.width; frame.height = camera.height;
            frame.wantsBgra = true; frame.rgb.clear();
            submit(lineScene_);
            frame.logicalUi();
            return;
        }
        Rasterizer raster(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), false);
        raster.setCamera(camera);
        drawFloor(raster, camera); drawWalls(raster, camera);
        maleLines_.draw(raster, {-0.95, maleFloor_, 0}, 0.0, 9001, male_.sourceMatrices({},0));
        femaleLines_.draw(raster, {0.95, femaleFloor_, 0}, 0.0, 8001, female_.sourceMatrices({},0));
        maleComparison_.draw(raster,-2.85); femaleComparison_.draw(raster,2.85);
        const UiLayer* ui=frame.uiLayer;
        frame = raster.finish();
        if (ui) compositeUi(frame,*ui);
        frame.logicalUi();
        return;
    }
    // Room geometry is in world space: resizing or orbiting changes only the
    // projection and view matrix, not the floor, walls or skeleton meshes.
    const auto updateCamera = [&] {
        scene_.width = camera.width;
        scene_.height = camera.height;
        scene_.focal = camera.focal();
        scene_.viewMatrix = camera.viewMatrix();
        // A free camera can leave the room, so its background must be cleared.
        scene_.covered = freecam == nullptr;
    };
    (void)seconds;
    // A slider updates only the corresponding blue meshes. Keep the room and
    // original bones in their existing GPU slots, with their pose matrices.
    if (view.gpu && sceneReady_ && (blueMaleRevision_!=maleComparison_.revision() ||
                                    blueFemaleRevision_!=femaleComparison_.revision())) {
        Rasterizer changed(camera.width,camera.height,colorHex("#0d1210"),camera.focal(),true);
        changed.setCamera(camera);
        if (blueMaleRevision_!=maleComparison_.revision()) maleComparison_.draw(changed,-2.85,12000);
        if (blueFemaleRevision_!=femaleComparison_.revision()) femaleComparison_.draw(changed,2.85,14000);
        GpuScene updates=changed.takeGpuScene();
        for (GpuBatch& update:updates.batches) {
            const auto it=std::find_if(scene_.batches.begin(),scene_.batches.end(),
                                       [&](const GpuBatch& batch) { return batch.objectId==update.objectId; });
            if (it!=scene_.batches.end()) { update.revision=++blueBatchRevision_; *it=std::move(update); }
        }
        blueMaleRevision_=maleComparison_.revision(); blueFemaleRevision_=femaleComparison_.revision();
    }
    const double pose = 0.0;
    const bool revisionsMatch = cachedMalePoseRevision_ == male_.sourcePoseRevision() &&
                                cachedFemalePoseRevision_ == female_.sourcePoseRevision();
    const bool reuse = view.gpu && sceneReady_ && pose == cachedPose_ && revisionsMatch;
    if (reuse) {
        updateCamera();
        frame.width = camera.width;
        frame.height = camera.height;
        frame.wantsBgra = true;
        frame.rgb.clear();
        submit(scene_);
        frame.logicalUi();
        return;
    }
    // Pose changes update the rigid source-batch matrices.
    const bool poseChanged = pose != cachedPose_ || !revisionsMatch;

    if (view.gpu) {
        // The original source structures are emitted once into independent
        // rigid batches. Pose revisions update their matrices without replacing
        // or rescaling the source geometry.
        if (!restReady_) {
            const int maleBase = 9001;
            const int femaleBase = 8001;
            Rasterizer room(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), true,
                            std::move(scene_));
            room.setCamera(camera);
            drawFloor(room, camera);
            drawWalls(room, camera);
            room.setObject(0, {0.0, 0.0}, 0.0F);
            GpuScene parts = room.takeGpuScene();
            male_.buildSourceRestGeometry(room, maleBase);
            female_.buildSourceRestGeometry(room, femaleBase);
            maleComparison_.draw(room,-2.85,12000);
            femaleComparison_.draw(room,2.85,14000);
            std::vector<GpuScene> halves;
            halves.push_back(std::move(parts));
            halves.push_back(room.takeGpuScene());
            scene_ = mergeScenes(std::move(halves));
            scene_.revision = ++revision_;
            blueMaleRevision_=maleComparison_.revision(); blueFemaleRevision_=femaleComparison_.revision();
            // Which batch belongs to which bone. Batches keep their identity
            // until the scene is rebuilt, so this is looked up once.
            boneBatches_.clear();
            for (std::size_t index = 0; index < scene_.batches.size(); ++index) {
                const std::uint32_t id = scene_.batches[index].objectId;
                if (id == 0) continue;
                boneBatches_.emplace_back(id, index);
            }
            sceneTriangles_ = 0;
            for (const GpuBatch& batch : scene_.batches)
                sceneTriangles_ += (batch.indices.empty() ? batch.vertices.size() : batch.indices.size()) / 3;
            restReady_ = true;
            sceneReady_ = true;
        }
        // The pose is the only thing that changes: write the bone matrices into
        // the batches they belong to. No vertex data moves. The static pose is solved only once.
        const bool poseMoved = poseChanged;
        if (poseMoved) {
            // Both reference bodies face the same direction in the room.
            cachedMaleMatrices_ = male_.sourceMatrices({-0.95, maleFloor_, 0}, 0.0);
            cachedFemaleMatrices_ = female_.sourceMatrices({0.95, femaleFloor_, 0}, 0.0);
        }
        const std::vector<std::array<double, 16>>& maleMatrices = cachedMaleMatrices_;
        const std::vector<std::array<double, 16>>& femaleMatrices = cachedFemaleMatrices_;
        for (const std::pair<std::uint32_t, std::size_t>& entry : boneBatches_) {
            if (!poseMoved) break;
            const int id = static_cast<int>(entry.first);
            const std::vector<std::array<double, 16>>* matrices = nullptr;
            std::size_t boneIndex = 0;
            if (id >= 9001 && static_cast<std::size_t>(id - 9001) < maleMatrices.size()) {
                matrices = &maleMatrices;
                boneIndex = static_cast<std::size_t>(id - 9001);
            } else if (id >= 8001 && static_cast<std::size_t>(id - 8001) < femaleMatrices.size()) {
                matrices = &femaleMatrices;
                boneIndex = static_cast<std::size_t>(id - 8001);
            }
            if (matrices == nullptr) continue;
            scene_.batches[entry.second].modelMatrix = (*matrices)[boneIndex];
        }
        cachedPose_ = pose;
        cachedMalePoseRevision_ = male_.sourcePoseRevision();
        cachedFemalePoseRevision_ = female_.sourcePoseRevision();
        updateCamera();
        frame.wantsBgra = true;
        frame.width = camera.width;
        frame.height = camera.height;
        frame.rgb.clear();
        submit(scene_);
        frame.logicalUi();
        return;
    }
    // Software path: the second half was built on its own rasterizer, so it is
    // drawn straight into the same buffers. Two threads, one body each -- the
    // software rasterizer needs posed vertices, so the rest-geometry trick does
    // not apply to it.
    Rasterizer room(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), false);
    room.setCamera(camera);
    drawFloor(room, camera);
    drawWalls(room, camera);
    Rasterizer second(camera.width, camera.height, colorHex("#0d1210"), camera.focal(), false);
    second.setCamera(camera);
    std::thread worker([&] { female_.draw(second, camera, {0.95, femaleFloor_, 0}, 0.0); });
    male_.draw(room, camera, {-0.95, maleFloor_, 0}, 0.0);
    worker.join();
    second.drain(room);
    maleComparison_.draw(room,-2.85); femaleComparison_.draw(room,2.85);
    const UiLayer* ui=frame.uiLayer;
    frame = room.finish();
    if (ui) compositeUi(frame,*ui);
    frame.logicalUi();
}

} // namespace gtc::native
