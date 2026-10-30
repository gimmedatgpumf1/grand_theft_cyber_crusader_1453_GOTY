// Exercise the real toe chains in both subjects, including GPU V-view owners.
#include "client/skeleton_room.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
using namespace gtc::native;

static Vec3 transform(const std::array<double,16>& m, Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static Vec3 center(const SkeletonModel& model, const std::string& name) {
    const int i=model.sourceStructure(name); assert(i>=0);
    const auto& s=model.sourceStructures()[i]; Vec3 c;
    for (std::size_t j=0;j<s.pointCount;++j) c+=model.bones()[s.group].points[s.firstPoint+j];
    return c/static_cast<double>(s.pointCount);
}
static Vec3 posed(const SkeletonModel& model, const std::vector<std::array<double,16>>& m, const std::string& name) {
    return transform(m[model.sourceStructure(name)],center(model,name));
}
static double bottom(const SkeletonModel& model, const std::vector<std::array<double,16>>& matrices, const std::string& name) {
    const int i=model.sourceStructure(name); assert(i>=0); const auto& part=model.sourceStructures()[i];
    double y=1e9; for (std::size_t j=0;j<part.pointCount;++j)
        y=std::min(y,transform(matrices[i],model.bones()[part.group].points[part.firstPoint+j]).y);
    return y;
}
static std::size_t namedLink(const SkeletonModel& model, const std::string& first, const std::string& second) {
    for (std::size_t i=0;i<model.sourceLinks().size();++i) {
        const auto& e=model.sourceLinks()[i];
        if (model.sourceStructures()[e.first].name==first && model.sourceStructures()[e.second].name==second) return i;
    }
    assert(false); return 0;
}
static std::map<std::string,std::vector<std::size_t>> lineSegments(const std::string& path) {
    std::ifstream file(path); assert(file); std::map<std::string,std::vector<std::size_t>> result;
    std::string row; std::size_t index=0;
    while (std::getline(file,row)) if (row.rfind("B ",0)==0) {
        std::istringstream in(row); char type; std::string name; in >> type >> name;
        result[name].push_back(index++);
    }
    return result;
}
static std::vector<Vec3> lineVertices(const SkeletonLines& lines, const SkeletonModel& model) {
    Rasterizer out(640,480,colorHex("#000000"),400,true);
    lines.draw(out,{},0,9001,model.sourceMatrices({},0));
    const auto scene=out.takeGpuScene(); assert(!scene.batches.empty());
    std::vector<Vec3> result;
    for (const auto& v:scene.batches[0].vertices) result.push_back({v.x,v.y,v.z});
    return result;
}
static double difference(const std::vector<Vec3>& a, const std::vector<Vec3>& b,
                         const std::vector<std::size_t>& segments) {
    double distance=0;
    for (std::size_t segment : segments) for (std::size_t i=segment*12;i<segment*12+12;++i) {
        assert(i<a.size() && i<b.size()); distance=std::max(distance,length(a[i]-b[i]));
    }
    return distance;
}
static void test(SkeletonRoom& room, SkeletonModel& model, const char* sex) {
    const std::string path=std::string("tools/skeleton-source/skeleton-")+sex+".lines";
    SkeletonLines lines; std::string error; assert(lines.load(path,error) && lines.bind(model,error));
    const auto segments=lineSegments(path);
    int total=0; double sourceMaximum=0, referenceMaximum=0;
    assert(room.restoreOriginalPose());
    const auto source=model.sourceMatrices({},0);
    for (char side : {'L','R'}) for (int digit=1;digit<=5;++digit) {
        assert(room.restoreReferencePose());
        const auto suffix=std::string("_")+side, number=std::to_string(digit);
        const std::string meta="FootMeta"+number+suffix, prox="FootProxP"+number+suffix;
        const std::string mid="FootMidP"+number+suffix, tip="FootDistP"+number+suffix;
        const std::string parents[]={meta,prox,mid}, children[]={prox,digit==1 ? tip : mid,tip};
        const std::string neighbour="FootDistP"+std::to_string(digit==5 ? 2 : digit+1)+suffix;
        const Vec3 sourceForward=normalized(posed(model,source,"FootMeta3"+suffix)-posed(model,source,"Cal"+suffix));
        const Vec3 sourceToe=normalized(posed(model,source,tip)-posed(model,source,prox));
        const double sourceAngle=std::acos(std::clamp(dot(sourceForward,sourceToe),-1.0,1.0))*180/3.141592653589793;
        sourceMaximum=std::max(sourceMaximum,sourceAngle);
        for (int joint=0;joint<(digit==1 ? 2 : 3);++joint) {
            assert(room.restoreReferencePose()); const auto before=model.sourceMatrices({},0);
            const auto index=namedLink(model,parents[joint],children[joint]);
            assert(model.referenceBend(index)); const double rest=*model.referenceBend(index);
            const auto& edge=model.sourceLinks()[index];
            const auto range=referenceBendRange(edge.mobility,parents[joint],children[joint]); assert(range);
            const double increment=rest+5<=range->flexion ? 5 : -5;
            const Vec3 forward=normalized(posed(model,before,"FootMeta3"+suffix)-posed(model,before,"Cal"+suffix));
            Vec3 dorsal=normalized(cross(posed(model,before,"FootMeta1"+suffix)-posed(model,before,"FootMeta5"+suffix),forward));
            if (dot(dorsal,posed(model,before,"Tibia"+suffix)-posed(model,before,"Tal"+suffix))<0) dorsal=dorsal*-1.0;
            const Vec3 start=posed(model,before,tip), startProx=posed(model,before,prox);
            assert(std::abs(bottom(model,before,tip)-bottom(model,before,"Cal"+suffix))<0.0002);
            for (const auto& part : {prox, digit==1 ? tip : mid, tip})
                assert(bottom(model,before,part)>=bottom(model,before,"Cal"+suffix)-0.00001);
            referenceMaximum=std::max(referenceMaximum,std::acos(std::clamp(dot(normalized(start-startProx),forward),-1.0,1.0))*180/3.141592653589793);
            const auto beforeLines=lineVertices(lines,model);
            assert(model.setReferenceBend(index,rest+increment)); const auto after=model.sourceMatrices({},0);
            const Vec3 delta=posed(model,after,tip)-start;
            assert(dot(delta,dorsal)*increment<-0.00001); // positive bends each joint toward sole
            for (int k=0;k<16;++k) assert(std::abs(after[model.sourceStructure(neighbour)][k]-before[model.sourceStructure(neighbour)][k])<1e-7);
            const auto afterLines=lineVertices(lines,model);
            assert(segments.count(tip) && segments.count(neighbour));
            assert(difference(beforeLines,afterLines,segments.at(tip))>0.00001);
            assert(difference(beforeLines,afterLines,segments.at(neighbour))<1e-6);
            assert(model.setReferenceBend(index,1000)); assert(std::abs(*model.referenceBend(index)-range->flexion)<1e-7);
            assert(model.setReferenceBend(index,-1000)); assert(std::abs(*model.referenceBend(index)-range->extension)<1e-7);
            ++total;
        }
    }
    assert(total==28);
    std::cout << sex << ": maximum toe direction from foot forward, source " << sourceMaximum
              << " degrees; reference " << referenceMaximum << " degrees\n";
    std::cout << sex << ": 28 independent toe joints, directions, limits, mesh and V-view PASS\n";
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    test(room,room.male(),"male"); test(room,room.female(),"female");
    assert(room.restoreOriginalPose());
    for (const auto* model : {&room.male(),&room.female()}) for (const auto& m:model->sourceMatrices({},0)) {
        const std::array<double,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for (int i=0;i<16;++i) assert(std::abs(m[i]-identity[i])<1e-7);
    }
    std::puts("Original source toes restore without scaling: PASS");
}
