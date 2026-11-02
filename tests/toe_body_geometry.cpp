// Experimental bone-fitted toe surface, solid nail topology and motion audit.
// This does not render a finished body or certify appearance or anatomy.
#include "client/skeleton_room.h"
#include "client/skeleton_toe_body.h"
#include "client/comparison_toe_motion.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
using namespace gtc::native;
static std::size_t findLink(const SkeletonModel& model,const std::string& a,const std::string& b) {
    for (std::size_t i=0;i<model.sourceLinks().size();++i) {
        const auto& e=model.sourceLinks()[i];
        if ((model.sourceStructures()[e.first].name==a && model.sourceStructures()[e.second].name==b) ||
            (model.sourceStructures()[e.first].name==b && model.sourceStructures()[e.second].name==a)) return i;
    }
    assert(false); return 0;
}
static void topology(const std::vector<SkinVertex>& v,const std::vector<std::array<int,3>>& faces) {
    std::map<std::pair<int,int>,int> edges,orientation;
    for (const auto& p:v) {
        assert(std::isfinite(p.position.x) && std::isfinite(p.position.y) && std::isfinite(p.position.z));
        assert(p.position.y>=-1e-9 && p.count>0 && p.count<=4);
        double sum=0; for (std::size_t i=0;i<p.count;++i) { assert(p.influences[i].weight>0); sum+=p.influences[i].weight; }
        assert(std::abs(sum-1)<1e-8);
    }
    for (auto f:faces) {
        for (int i:f) assert(i>=0 && i<static_cast<int>(v.size()));
        assert(length(cross(v[f[1]].position-v[f[0]].position,v[f[2]].position-v[f[0]].position))>1e-16);
        for (int j=0;j<3;++j) {
            const int a=f[j],b=f[(j+1)%3]; auto e=std::minmax(a,b);
            ++edges[e]; orientation[e]+=a<b?1:-1;
        }
    }
    for (const auto& [e,count]:edges) assert(count==2 && orientation[e]==0);
}
static void test(SkeletonRoom& room,bool male,double height) {
    std::string error; assert(room.restoreReferencePose());
    auto& model=male?room.male():room.female(); const auto& copy=room.comparison(male);
    auto profile=copy.profile(); profile.height=height; profile.multiplier.fill(1); assert(room.setComparison(male,profile,error));
    ComparisonToeMotion motion; assert(motion.load(model,error));
    std::array<std::array<ToeBoneSample,3>,5> bones; std::array<std::size_t,5> meta{};
    std::vector<Vec3> centers(copy.parts().size());
    for (int side=0;side<2;++side) for (int d=0;d<5;++d) {
        if (side==0) meta[d]=motion.metatarsals()[side][d];
        for (int j=0;j<3;++j) {
            const int id=motion.parts()[side][d][j]; if (id<0) continue;
            const auto& part=copy.parts()[id]; Vec3 sum;
            for (Vec3 p:part.points) { p=copy.mapPart(p,part); sum+=p; if (side==0) bones[d][j].points.push_back(p); }
            assert(!part.points.empty()); centers[id]=sum/static_cast<double>(part.points.size());
            if (side==0) bones[d][j].structure=id;
        }
    }
    const auto mesh=buildToeBody(bones,meta,height/copy.defaultHeight()); assert(mesh.valid);
    assert(!mesh.skin.empty() && !mesh.nails.empty()); topology(mesh.skin,mesh.skinFaces); topology(mesh.nails,mesh.nailFaces);
    std::vector<ComparisonToeMotion::Matrix> identity(copy.parts().size(),ComparisonToeMotion::identity());
    SkeletonSkinBinding skin,nail; assert(skin.bind(mesh.skin,identity,error)); assert(nail.bind(mesh.nails,identity,error));
    std::vector<Vec3> beforeSkin,beforeNails; assert(skin.deform(identity,beforeSkin,error)); assert(nail.deform(identity,beforeNails,error));
    const auto neutral=motion.pose(model,centers,height/copy.defaultHeight()); assert(neutral.size()==identity.size());
    for (int side=0;side<2;++side) for (int d=0;d<5;++d) for (int j=0;j<3;++j) {
        const int id=motion.parts()[side][d][j]; if (id<0) continue;
        for (int i=0;i<16;++i) assert(std::abs(neutral[id][i]-identity[id][i])<1e-8);
    }
    for (int side=0;side<2;++side) for (int d=0;d<5;++d) for (int j=0;j<(d==0?2:3);++j) {
        assert(room.restoreReferencePose()); const auto tag=std::to_string(d+1)+(side?"_R":"_L");
        const std::string prox="FootProxP"+tag,mid="FootMidP"+tag,dist="FootDistP"+tag;
        const std::string a=j==0?"FootMeta"+tag:(j==1?prox:mid),b=j==0?prox:(j==1 && d!=0?mid:dist);
        const auto link=findLink(model,a,b); const auto angle=model.referenceBend(link); assert(angle);
        const auto& e=model.sourceLinks()[link]; const auto range=referenceBendRange(e.mobility,a,b); assert(range);
        assert(model.setReferenceBend(link,*angle+(*angle+5<=range->flexion?5:-5)));
        const auto pose=motion.pose(model,centers,height/copy.defaultHeight()); assert(pose.size()==identity.size());
        // Every distal bone is independent, including the other foot.
        for (int s=0;s<2;++s) for (int k=0;k<5;++k) {
            const int id=motion.parts()[s][k][2]; double difference=0;
            for (int i=0;i<16;++i) difference=std::max(difference,std::abs(pose[id][i]-identity[id][i]));
            if (s==side && k==d) assert(difference>1e-5); else assert(difference<1e-8);
        }
        if (side==0) {
            std::vector<Vec3> afterSkin,afterNails; assert(skin.deform(pose,afterSkin,error)); assert(nail.deform(pose,afterNails,error));
            bool nailMoved=false,skinMoved=false;
            const int distal=motion.parts()[0][d][2];
            for (std::size_t i=0;i<mesh.nails.size();++i) {
                const double movement=length(afterNails[i]-beforeNails[i]);
                if (mesh.nails[i].influences[0].bone==static_cast<std::size_t>(distal)) nailMoved=nailMoved || movement>1e-6;
                else assert(movement<1e-8);
            }
            for (std::size_t i=0;i<mesh.skin.size();++i) skinMoved=skinMoved || length(afterSkin[i]-beforeSkin[i])>1e-6;
            assert(nailMoved && skinMoved);
        }
    }
    assert(room.restoreReferencePose());
    std::printf("%s %.0f cm: bone-fitted experimental surface, solid nails and individual joint binding PASS\n",male?"male":"female",height);
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    for (bool male:{true,false}) for (double height:{50.,170.,250.}) test(room,male,height);
    // Incomplete input must not yield a surface silently.
    assert(!buildToeBody({}, {},1).valid);
}
