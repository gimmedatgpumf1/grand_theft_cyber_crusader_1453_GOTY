#include "client/skeleton_ragdoll.h"
#include "client/skeleton_body.h"
#include <btBulletDynamicsCommon.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

namespace gtc::native {
namespace {
btVector3 v(Vec3 p) { return {btScalar(p.x),btScalar(p.y),btScalar(p.z)}; }
btTransform rigid(const JointRigidTransform& t) {
    const auto& r=t.rotation;
    return btTransform(btMatrix3x3(btScalar(r[0]),btScalar(r[1]),btScalar(r[2]),
                        btScalar(r[3]),btScalar(r[4]),btScalar(r[5]),
                        btScalar(r[6]),btScalar(r[7]),btScalar(r[8])),v(t.translation));
}
btTransform rigid(const std::array<double,16>& m) {
    return btTransform(btMatrix3x3(btScalar(m[0]),btScalar(m[4]),btScalar(m[8]),
                        btScalar(m[1]),btScalar(m[5]),btScalar(m[9]),
                        btScalar(m[2]),btScalar(m[6]),btScalar(m[10])),
            btVector3(btScalar(m[12]),btScalar(m[13]),btScalar(m[14])));
}
std::array<double,16> matrix(const btTransform& t, Vec3 origin) {
    const auto& b=t.getBasis(); const auto& p=t.getOrigin();
    return {b[0][0],b[1][0],b[2][0],0, b[0][1],b[1][1],b[2][1],0,
            b[0][2],b[1][2],b[2][2],0, p.x()-origin.x,p.y()-origin.y,p.z()-origin.z,1};
}
}

struct SkeletonRagdoll::Impl {
    struct Figure {
        SkeletonModel* model=nullptr;
        Vec3 origin{};
        std::vector<int> bodyForStructure;
        std::vector<btTransform> localSkin;
    };
    std::unique_ptr<btDefaultCollisionConfiguration> config=std::make_unique<btDefaultCollisionConfiguration>();
    std::unique_ptr<btCollisionDispatcher> dispatcher=std::make_unique<btCollisionDispatcher>(config.get());
    std::unique_ptr<btBroadphaseInterface> broadphase=std::make_unique<btDbvtBroadphase>();
    std::unique_ptr<btSequentialImpulseConstraintSolver> solver=std::make_unique<btSequentialImpulseConstraintSolver>();
    std::unique_ptr<btDiscreteDynamicsWorld> world=std::make_unique<btDiscreteDynamicsWorld>(dispatcher.get(),broadphase.get(),solver.get(),config.get());
    std::vector<std::unique_ptr<btCollisionShape>> shapes;
    std::vector<std::unique_ptr<btRigidBody>> bodies;
    std::vector<std::unique_ptr<btTypedConstraint>> constraints;
    std::vector<btHingeConstraint*> fingerJoints;
    std::vector<btVector3> centers;
    std::vector<Figure> figures;
    // Sequential impulses can leave a visible gap at a tiny finger joint
    // during a hard contact. Finish each physics substep with a positional
    // projection of the existing joint anchors, in parent-to-child order.
    // The bodies, collision proxies and rendered matrices all use the corrected
    // transforms; angular freedom and joint limits remain those of the hinge.
    static void projectFingerJoints(btDynamicsWorld* dynamics, btScalar) {
        auto* self=static_cast<Impl*>(dynamics->getWorldUserInfo());
        for (auto* hinge:self->fingerJoints) {
            auto& parent=hinge->getRigidBodyA(); auto& child=hinge->getRigidBodyB();
            const auto parentAnchor=(parent.getWorldTransform()*hinge->getAFrame()).getOrigin();
            const auto childAnchor=(child.getWorldTransform()*hinge->getBFrame()).getOrigin();
            auto correction=parentAnchor-childAnchor;
            if (correction.length2()<1e-14f) continue;
            auto transform=child.getWorldTransform();
            transform.setOrigin(transform.getOrigin()+correction);
            child.setCenterOfMassTransform(transform);
            self->world->updateSingleAabb(&child);
        }
    }
    Impl() {
        world->setInternalTickCallback(&projectFingerJoints,this,false);
        world->setGravity(btVector3(0,-9.81f,0));
        world->getSolverInfo().m_numIterations=100;
        world->getSolverInfo().m_splitImpulse=true;
        auto floor=std::make_unique<btBoxShape>(btVector3(20,0.5f,20));
        btTransform t; t.setIdentity(); t.setOrigin(btVector3(0,-0.5f,0));
        btRigidBody::btRigidBodyConstructionInfo ci(0,nullptr,floor.get());
        auto body=std::make_unique<btRigidBody>(ci); body->setWorldTransform(t); body->setFriction(0.8f);
        body->setUserIndex(-1); world->addRigidBody(body.get()); shapes.push_back(std::move(floor)); bodies.push_back(std::move(body)); centers.emplace_back(0,0,0);
    }
    ~Impl() {
        for (auto& c:constraints) world->removeConstraint(c.get());
        for (auto& b:bodies) world->removeRigidBody(b.get());
    }
    static btVector3 sourceCenter(const SkeletonModel& model, int index) {
        const auto& part=model.sourceStructures()[std::size_t(index)];
        const auto& points=model.bones()[part.group].points; btVector3 sum(0,0,0);
        for (std::size_t i=0;i<part.pointCount;++i) sum+=v(points[part.firstPoint+i]);
        return part.pointCount ? sum/btScalar(part.pointCount) : sum;
    }
    void addBoneCollision(const SkeletonModel& model, std::size_t index, const btTransform& relative,
                          const btVector3& center, btCompoundShape& compound, btVector3& lo, btVector3& hi) {
        const auto& part=model.sourceStructures()[index]; const auto& points=model.bones()[part.group].points;
        std::vector<btVector3> local; local.reserve(part.pointCount);
        for (std::size_t i=0;i<part.pointCount;++i) {
            const auto p=relative*v(points[part.firstPoint+i])-center;
            local.push_back(p); lo.setMin(p); hi.setMax(p);
        }
        auto addHull=[&](const std::vector<btVector3>& vertices) {
            if (vertices.empty()) return;
            auto hull=std::make_unique<btConvexHullShape>();
            for (const auto& p:vertices) hull->addPoint(p,false);
            if (vertices.size()>=4) hull->optimizeConvexHull();
            // Small contact cushion for projected finger constraints at impact.
            hull->setMargin(part.name.rfind("Hand",0)==0 ? 0.004f : 0.001f); hull->recalcLocalAabb();
            btTransform child; child.setIdentity(); compound.addChildShape(child,hull.get());
            shapes.push_back(std::move(hull));
        };
        // A whole-rib convex hull fills the empty centre of the curved rib.
        // Divide the actual CT vertices along their source-derived arc so the
        // compound collider follows the bone instead of a huge solid wedge.
        const auto underscore=part.name.find('_');
        if (part.name.rfind("Rib",0)!=0 || underscore==std::string::npos || part.pointCount<32) {
            addHull(local); return;
        }
        const auto vertebra=model.sourceStructure("T"+part.name.substr(3,underscore-3));
        const auto sternum=model.sourceStructure("Sternum");
        if (vertebra<0 || sternum<0) { addHull(local); return; }
        const auto axis=(sourceCenter(model,vertebra)+sourceCenter(model,sternum))*0.5f;
        std::vector<double> angles; angles.reserve(part.pointCount); double sn=0,cs=0;
        for (std::size_t i=0;i<part.pointCount;++i) {
            const auto p=v(points[part.firstPoint+i])-axis; const double a=std::atan2(p.z(),p.x());
            angles.push_back(a); sn+=std::sin(a); cs+=std::cos(a);
        }
        const double mean=std::atan2(sn,cs);
        std::vector<std::pair<double,std::size_t>> ordered; ordered.reserve(part.pointCount);
        for (std::size_t i=0;i<angles.size();++i)
            ordered.emplace_back(std::atan2(std::sin(angles[i]-mean),std::cos(angles[i]-mean)),i);
        std::sort(ordered.begin(),ordered.end());
        const std::size_t overlap=std::max<std::size_t>(1,ordered.size()/80);
        for (std::size_t slice=0;slice<8;++slice) {
            const auto begin=slice*ordered.size()/8, end=(slice+1)*ordered.size()/8;
            const auto low=begin>overlap ? begin-overlap : 0, high=std::min(ordered.size(),end+overlap);
            std::vector<btVector3> vertices; vertices.reserve(high-low);
            for (auto i=low;i<high;++i) vertices.push_back(local[ordered[i].second]);
            addHull(vertices);
        }
    }
    bool addFigure(SkeletonModel& model, Vec3 origin) {
        const auto* rig=model.sourceRig();
        const auto& structures=model.sourceStructures();
        const auto& mapping=model.sourceBodyMap();
        if (!rig || mapping.size()!=structures.size()) return false;
        const auto initial=model.sourceMatrices(origin,0);
        if (initial.size()!=structures.size()) return false;
        std::vector<int> structureForNode(rig->size(),-1), groupForNode(rig->size(),-1);
        for (std::size_t i=0;i<mapping.size();++i) structureForNode[mapping[i]]=int(i);
        std::vector<std::vector<std::size_t>> groups;
        for (std::size_t node=1;node<rig->size();++node) {
            const auto p=*rig->parent(node); const int index=structureForNode[node];
            if (index<0 || rig->spatialJoint(node)) return false;
            if (p==0 || rig->joint(node)) { groupForNode[node]=int(groups.size()); groups.emplace_back(); }
            else groupForNode[node]=groupForNode[p];
            groups[std::size_t(groupForNode[node])].push_back(std::size_t(index));
        }
        // The tree has two elbow roots per forearm and two wrist roots per
        // hand. Independent sibling hinges split structures that are coupled
        // by the existing model. Merge only the explicitly linked forearm and
        // carpal groups; finger and thumb joints remain separate bodies.
        for (const auto& link:model.sourceLinks())
            if (link.mobility=="radioulnar_pivot" || link.mobility=="carpal_glide") {
            int first=groupForNode[mapping[link.first]], second=groupForNode[mapping[link.second]];
            if (first==second) continue;
            groups[std::size_t(first)].insert(groups[std::size_t(first)].end(),
                groups[std::size_t(second)].begin(),groups[std::size_t(second)].end());
            groups[std::size_t(second)].clear();
            for (auto& group:groupForNode) if (group==second) group=first;
        }
        Figure f; f.model=&model; f.origin=origin; f.bodyForStructure.resize(structures.size());
        f.localSkin.resize(structures.size());
        std::vector<int> bodyForGroup(groups.size(),-1);
        for (std::size_t group=0;group<groups.size();++group) {
            if (groups[group].empty()) continue;
            const auto first=groups[group][0]; const auto& part=structures[first];
            const auto& points=model.bones()[part.group].points;
            btVector3 center(0,0,0);
            for (std::size_t i=0;i<part.pointCount;++i) center+=v(points[part.firstPoint+i]);
            if (!part.pointCount) return false;
            center/=btScalar(part.pointCount);
            auto compound=std::make_unique<btCompoundShape>();
            btVector3 lo(BT_LARGE_FLOAT,BT_LARGE_FLOAT,BT_LARGE_FLOAT),hi(-BT_LARGE_FLOAT,-BT_LARGE_FLOAT,-BT_LARGE_FLOAT);
            for (const auto index:groups[group]) {
                const auto& s=structures[index];
                if (!s.pointCount) return false;
                const btTransform relative=rigid(initial[first]).inverse()*rigid(initial[index]);
                f.localSkin[index]=relative;
                addBoneCollision(model,index,relative,center,*compound,lo,hi);
            }
            compound->setMargin(0.001f); compound->recalculateLocalAabb();
            const auto extent=hi-lo;
            btScalar mass=std::clamp(extent.x()*extent.y()*extent.z()*500.0f,0.015f,6.0f);
            // Include a crude share of surrounding tissue in vertebral inertias:
            // tiny isolated bone masses against a heavy skull destabilize the chain.
            // This floor is a simulation approximation, not subject-specific mass.
            if (part.name.size()>=2 && part.name.size()<=3 && (part.name[0]=='C' || part.name[0]=='T' || part.name[0]=='L') && part.name[1]>='0' && part.name[1]<='9')
                mass=std::max(mass,btScalar(0.3));
            // Only fingers need direct convex shapes for CCD. Keep the other
            // bodies compound as before: converting every bone to a direct
            // hull makes Bullet's contacts substantially more expensive.
            const bool hand=part.name.rfind("Hand",0)==0;
            btCollisionShape* collision=hand && compound->getNumChildShapes()==1 ? compound->getChildShape(0) : compound.get();
            btVector3 inertia(0,0,0); collision->calculateLocalInertia(mass,inertia);
            btTransform pose=rigid(initial[first]); pose.setOrigin(pose*center);
            btRigidBody::btRigidBodyConstructionInfo ci(mass,nullptr,collision,inertia);
            auto body=std::make_unique<btRigidBody>(ci);
            body->setWorldTransform(pose); body->setFriction(0.7f); body->setRestitution(0.02f);
            body->setDamping(0.05f,0.15f);
            body->setCcdMotionThreshold(hand ? 0.01f : 0.0f);
            body->setCcdSweptSphereRadius(0.003f);
            const int id=int(bodies.size()); body->setUserIndex(int(figures.size())); world->addRigidBody(body.get());
            bodyForGroup[group]=id; centers.push_back(center); bodies.push_back(std::move(body)); shapes.push_back(std::move(compound));
            for (auto index:groups[group]) f.bodyForStructure[index]=id;
        }
        const auto& abs=rig->absoluteTransforms();
        std::set<std::pair<int,int>> connected;
        for (std::size_t node=1;node<rig->size();++node) {
            const auto parent=*rig->parent(node); const auto* joint=rig->joint(node);
            if (!parent || !joint) continue;
            const int a=bodyForGroup[std::size_t(groupForNode[parent])], b=bodyForGroup[std::size_t(groupForNode[node])];
            if (a==b || !connected.insert(std::minmax(a,b)).second) continue;
            const btTransform jointWorld=rigid(abs[parent])*rigid(joint->parentFrame());
            const auto axis=jointWorld.getBasis()*v(joint->axes()[0].direction);
            const auto pivot=jointWorld.getOrigin();
            const auto aInv=bodies[a]->getWorldTransform().inverse(), bInv=bodies[b]->getWorldTransform().inverse();
            std::string kind;
            const auto si=std::size_t(structureForNode[node]), pi=std::size_t(structureForNode[parent]);
            for (const auto& link:model.sourceLinks())
                if ((link.first==si && link.second==pi) || (link.first==pi && link.second==si)) { kind=link.mobility; break; }
            const auto& range=joint->axes()[0]; const auto q=joint->angles()[0];
            const bool girdle=kind=="sternoclavicular" || kind=="acromioclavicular";
            if (kind=="hip_ball_socket" || kind=="shoulder_ball_socket" || girdle) {
                // Align the first coordinate to the existing fitted axis. The other
                // two are bounded approximations, not a patient-specific ROM model.
                btVector3 x=axis.normalized();
                btVector3 y=bodies[b]->getWorldTransform().getOrigin()-pivot;
                if ((kind=="hip_ball_socket" || girdle) && joint->axes().size()>2) {
                    const auto rot=joint_detail::rotate(joint->axes()[0].direction,q);
                    const Vec3 d=joint->axes()[1].direction;
                    y=jointWorld.getBasis()*v({rot.rotation[0]*d.x+rot.rotation[1]*d.y+rot.rotation[2]*d.z,
                        rot.rotation[3]*d.x+rot.rotation[4]*d.y+rot.rotation[5]*d.z,
                        rot.rotation[6]*d.x+rot.rotation[7]*d.y+rot.rotation[8]*d.z});
                }
                y-=x*x.dot(y);
                if (y.length2()<1e-8f) y=x.cross(btVector3(0,0,1));
                if (y.length2()<1e-8f) y=x.cross(btVector3(0,1,0));
                y.normalize(); const btVector3 z=x.cross(y).normalized();
                btMatrix3x3 basis(x.x(),y.x(),z.x(),x.y(),y.y(),z.y(),x.z(),y.z(),z.z());
                const btTransform frame(basis,pivot);
                auto ball=std::make_unique<btGeneric6DofSpring2Constraint>(*bodies[a],*bodies[b],aInv*frame,bInv*frame);
                ball->setLinearLowerLimit(btVector3(0,0,0)); ball->setLinearUpperLimit(btVector3(0,0,0));
                // Local convergence budget; the global solver stays at 100.
                if (girdle) ball->setOverrideNumSolverIterations(180);
                btScalar yLow=-1.570796f,yHigh=1.570796f,zLow=yLow,zHigh=yHigh;
                if ((kind=="hip_ball_socket" || girdle) && joint->axes().size()>2) {
                    const auto& axes=joint->axes(); const auto& angles=joint->angles();
                    yLow=btScalar(axes[1].minimum-angles[1]); yHigh=btScalar(axes[1].maximum-angles[1]);
                    const auto first=joint_detail::rotate(axes[0].direction,angles[0]);
                    const auto second=joint_detail::rotate(axes[1].direction,angles[1]);
                    const auto rot=joint_detail::compose(first,second); const Vec3 d=axes[2].direction;
                    const btVector3 desired=jointWorld.getBasis()*v({rot.rotation[0]*d.x+rot.rotation[1]*d.y+rot.rotation[2]*d.z,
                        rot.rotation[3]*d.x+rot.rotation[4]*d.y+rot.rotation[5]*d.z,
                        rot.rotation[6]*d.x+rot.rotation[7]*d.y+rot.rotation[8]*d.z});
                    zLow=btScalar(axes[2].minimum-angles[2]); zHigh=btScalar(axes[2].maximum-angles[2]);
                    if (z.dot(desired)<0) { const auto old=zLow; zLow=-zHigh; zHigh=-old; }
                }
                ball->setAngularLowerLimit(btVector3(btScalar(range.minimum-q),yLow,zLow));
                ball->setAngularUpperLimit(btVector3(btScalar(range.maximum-q),yHigh,zHigh));
                world->addConstraint(ball.get(),true); constraints.push_back(std::move(ball));
            } else {
                const btVector3 z=axis.normalized();
                btVector3 x=z.cross(btVector3(0,1,0));
                if (x.length2()<1e-8f) x=z.cross(btVector3(1,0,0));
                x.normalize(); const btVector3 y=z.cross(x).normalized();
                const btMatrix3x3 basis(x.x(),y.x(),z.x(),x.y(),y.y(),z.y(),x.z(),y.z(),z.z());
                const btTransform frame(basis,pivot);
                auto hinge=std::make_unique<btHingeConstraint>(*bodies[a],*bodies[b],aInv*frame,bInv*frame,true);
                if (structures[si].name.rfind("Hand",0)==0 && kind!="wrist_condyloid") {
                    hinge->setParam(BT_CONSTRAINT_ERP,0.6f);
                    hinge->setOverrideNumSolverIterations(180);
                    fingerJoints.push_back(hinge.get());
                }
                if (kind=="great_toe_mtp" || kind=="lesser_toe_mtp" || kind=="toe_ip_hinge") hinge->setOverrideNumSolverIterations(180);
                if (kind=="patellofemoral_glide") { hinge->setOverrideNumSolverIterations(180); hinge->setParam(BT_CONSTRAINT_ERP,0.4f); }
                hinge->setLimit(btScalar(range.minimum-q),btScalar(range.maximum-q),0.9f,0.3f,1.0f);
                world->addConstraint(hinge.get(),true); constraints.push_back(std::move(hinge));
            }
        }
        // The source tree cannot represent the rib/sternum loops: without
        // these links each vertebral hinge can pull its ribs away from the
        // front of the cage. Approximate the missing cartilage by a limited
        // positional constraint at the closest source-derived attachment area.
        for (const auto& link:model.sourceLinks()) if (link.kind=="cartilage") {
            const int a=f.bodyForStructure[link.first], b=f.bodyForStructure[link.second];
            if (a==b) continue;
            const auto& sa=structures[link.first]; const auto& sb=structures[link.second];
            const auto& va=model.bones()[sa.group].points; const auto& vb=model.bones()[sb.group].points;
            const auto ta=rigid(initial[link.first]), tb=rigid(initial[link.second]);
            btScalar best=BT_LARGE_FLOAT; btVector3 pivot(0,0,0);
            const auto as=std::max<std::size_t>(1,sa.pointCount/250), bs=std::max<std::size_t>(1,sb.pointCount/250);
            for (std::size_t i=0;i<sa.pointCount;i+=as) {
                const btVector3 p=ta*v(va[sa.firstPoint+i]);
                for (std::size_t j=0;j<sb.pointCount;j+=bs) {
                    const btVector3 q=tb*v(vb[sb.firstPoint+j]); const auto distance=(p-q).length2();
                    if (distance<best) { best=distance; pivot=(p+q)*0.5f; }
                }
            }
            if (best==BT_LARGE_FLOAT) continue;
            btTransform frame; frame.setIdentity(); frame.setOrigin(pivot);
            const auto aFrame=bodies[a]->getWorldTransform().inverse()*frame;
            const auto bFrame=bodies[b]->getWorldTransform().inverse()*frame;
            auto cartilage=std::make_unique<btGeneric6DofSpring2Constraint>(*bodies[a],*bodies[b],aFrame,bFrame);
            cartilage->setLinearLowerLimit(btVector3(-0.004f,-0.004f,-0.004f));
            cartilage->setLinearUpperLimit(btVector3(0.004f,0.004f,0.004f));
            cartilage->setAngularLowerLimit(btVector3(1,1,1));
            cartilage->setAngularUpperLimit(btVector3(-1,-1,-1)); // rotations are free
            world->addConstraint(cartilage.get(),true); constraints.push_back(std::move(cartilage));
        }
        // Adjacent anatomy and one-hop neighbours often overlap at rest (for
        // example a patella against femur, or the two bones of a forearm).
        // Avoid proxy contact fighting the anatomical joint constraints.
        std::vector<std::vector<bool>> adjacency(groups.size(),std::vector<bool>(groups.size(),false));
        for (const auto& link:model.sourceLinks()) {
            const auto a=std::size_t(groupForNode[mapping[link.first]]), b=std::size_t(groupForNode[mapping[link.second]]);
            adjacency[a][b]=adjacency[b][a]=true;
        }
        for (std::size_t a=0;a<groups.size();++a) for (std::size_t b=a+1;b<groups.size();++b) {
            if (bodyForGroup[a]<0 || bodyForGroup[b]<0) continue;
            bool adjacent=adjacency[a][b];
            for (std::size_t c=0;!adjacent && c<groups.size();++c) adjacent=adjacency[a][c] && adjacency[c][b];
            if (adjacent) {
                auto* first=bodies[bodyForGroup[a]].get(); auto* second=bodies[bodyForGroup[b]].get();
                first->setIgnoreCollisionCheck(second,true); second->setIgnoreCollisionCheck(first,true);
            }
        }
        // A slight nudge breaks the perfectly balanced reference stance.
        const int root=bodyForGroup.front(); bodies[root]->applyCentralImpulse(btVector3(0,0,0.8f));
        figures.push_back(std::move(f));
        return true;
    }
    void publish() {
        for (auto& f:figures) {
            std::vector<std::array<double,16>> pose; pose.reserve(f.bodyForStructure.size());
            for (std::size_t i=0;i<f.bodyForStructure.size();++i) {
                const int id=f.bodyForStructure[i];
                btTransform skin=bodies[id]->getWorldTransform();
                skin.setOrigin(skin.getOrigin()-skin.getBasis()*centers[id]);
                pose.push_back(matrix(skin*f.localSkin[i],f.origin));
            }
            f.model->setDynamicSourcePose(std::move(pose));
        }
    }
};

SkeletonRagdoll::SkeletonRagdoll():impl_(std::make_unique<Impl>()) {}
SkeletonRagdoll::~SkeletonRagdoll()=default;
bool SkeletonRagdoll::create(SkeletonModel& male, Vec3 maleOrigin, SkeletonModel& female, Vec3 femaleOrigin) {
    if (!impl_->addFigure(male,maleOrigin) || !impl_->addFigure(female,femaleOrigin)) return false;
    // Existing CT geometry/proxy overlaps should not create explosive separating impulses.
    // Connected bones are already filtered by their constraints. Other pairs can collide.
    impl_->world->performDiscreteCollisionDetection();
    for (int i=0;i<impl_->dispatcher->getNumManifolds();++i) {
        auto* m=impl_->dispatcher->getManifoldByIndexInternal(i);
        auto* a=const_cast<btCollisionObject*>(static_cast<const btCollisionObject*>(m->getBody0()));
        auto* b=const_cast<btCollisionObject*>(static_cast<const btCollisionObject*>(m->getBody1()));
        if (a->isStaticObject() || b->isStaticObject()) continue;
        for (int j=0;j<m->getNumContacts();++j) if (m->getContactPoint(j).getDistance() < 0.001f) {
            a->setIgnoreCollisionCheck(b,true); b->setIgnoreCollisionCheck(a,true); break;
        }
    }
    impl_->publish(); return true;
}
SkeletonRagdoll::Contacts SkeletonRagdoll::contacts() const {
    Contacts result;
    for (int i=0;i<impl_->dispatcher->getNumManifolds();++i) {
        const auto* m=impl_->dispatcher->getManifoldByIndexInternal(i);
        const auto* a=static_cast<const btCollisionObject*>(m->getBody0());
        const auto* b=static_cast<const btCollisionObject*>(m->getBody1());
        if (!a->checkCollideWith(b) || !b->checkCollideWith(a)) continue;
        bool touching=false;
        for (int j=0;j<m->getNumContacts();++j) touching|=m->getContactPoint(j).getDistance()<=0;
        if (!touching) continue;
        if (a->isStaticObject() || b->isStaticObject()) ++result.floor;
        else if (a->getUserIndex()==b->getUserIndex()) ++result.sameFigure;
        else ++result.otherFigure;
    }
    return result;
}
void SkeletonRagdoll::advance(double seconds) {
    if (!(seconds>0) || !std::isfinite(seconds)) return;
    // Bullet accumulates time between fixed steps. Publishing when no step
    // ran only invalidates both pose caches and rebuilds the same V-view.
    if (impl_->world->stepSimulation(btScalar(std::min(seconds,0.05)),10,1.0f/120.0f)>0)
        impl_->publish();
}
} // namespace gtc::native
