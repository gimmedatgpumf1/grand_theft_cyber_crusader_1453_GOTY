// Exercise each actual thumb and finger joint independently in both room views.
#include "client/skeleton_room.h"
#include "client/skeleton_rom.h"
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
static Vec3 center(const SkeletonModel& m, const std::string& name) {
    int i=m.sourceStructure(name); assert(i>=0);
    const auto& s=m.sourceStructures()[i]; Vec3 c;
    for (std::size_t j=0;j<s.pointCount;++j) c+=m.bones()[s.group].points[s.firstPoint+j];
    return c/static_cast<double>(s.pointCount);
}
static Vec3 posed(const SkeletonModel& model, const std::vector<std::array<double,16>>& matrices, const std::string& name) {
    return transform(matrices[model.sourceStructure(name)],center(model,name));
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
static Vec3 palmNormal(const SkeletonModel& model, const std::vector<std::array<double,16>>& m, char side) {
    const auto s=std::string("_")+side;
    return normalized(cross(posed(model,m,"HandMeta2"+s)-posed(model,m,"HandMeta5"+s),
                            posed(model,m,"HandMeta3"+s)-posed(model,m,"HandLuna"+s)))*(side=='L'?-1.0:1.0);
}
static void test(SkeletonRoom& room, SkeletonModel& model, const char* sex) {
    const std::string path=std::string("tools/skeleton-source/skeleton-")+sex+".lines";
    SkeletonLines lines; std::string error; assert(lines.load(path,error) && lines.bind(model,error));
    const auto segments=lineSegments(path);
    int total=0;
    for (char side : {'L','R'}) for (int digit=1;digit<=5;++digit) {
        assert(room.restoreReferencePose());
        const auto suffix=std::string("_")+side, number=std::to_string(digit);
        const std::string meta="HandMeta"+number+suffix, prox="HandProxP"+number+suffix;
        const std::string mid="HandMidP"+number+suffix, tip="HandDistP"+number+suffix;
        auto bind=model.sourceMatrices({},0);
        const Vec3 a=posed(model,bind,meta), b=posed(model,bind,prox);
        const Vec3 c=posed(model,bind,digit==1 ? tip : mid);
        // In the reference pose the actual phalanges are extended, not merely
        // assigned a numerical zero on unrelated axes.
        assert(dot(normalized(b-a),normalized(c-b))>std::cos(12*3.141592653589793/180));
        if (digit!=1) {
            const Vec3 d=posed(model,bind,tip);
            assert(dot(normalized(c-b),normalized(d-c))>std::cos(12*3.141592653589793/180));
        }
        const std::string neighbour="HandDistP"+std::to_string(digit==5 ? 2 : digit+1)+suffix;
        const std::string parents[]={meta,prox,mid}, children[]={prox,digit==1 ? tip : mid,tip};
        for (int joint=0;joint<(digit==1 ? 2 : 3);++joint) {
            assert(room.restoreReferencePose()); bind=model.sourceMatrices({},0);
            const auto beforeLines=lineVertices(lines,model);
            const auto i=namedLink(model,parents[joint],children[joint]);
            assert(model.referenceBend(i) && std::abs(*model.referenceBend(i))<1e-7);
            const auto& e=model.sourceLinks()[i];
            const auto range=referenceBendRange(e.mobility,parents[joint],children[joint]); assert(range);
            const Vec3 normal=palmNormal(model,bind,side), start=posed(model,bind,tip);
            assert(model.setReferenceBend(i,10));
            auto moved=model.sourceMatrices({},0);
            const double curl=dot(posed(model,moved,tip)-start,normal);
            assert(curl>0.00005); // each MCP, PIP/IP and DIP curls toward the palm
            for (int k=0;k<16;++k) assert(std::abs(moved[model.sourceStructure(neighbour)][k]-bind[model.sourceStructure(neighbour)][k])<1e-7);
            const auto afterLines=lineVertices(lines,model);
            assert(segments.count(tip) && segments.count(neighbour));
            assert(difference(beforeLines,afterLines,segments.at(tip))>0.0001);
            assert(difference(beforeLines,afterLines,segments.at(neighbour))<1e-6);
            assert(model.setReferenceBend(i,1000)); assert(std::abs(*model.referenceBend(i)-range->flexion)<1e-7);
            assert(model.setReferenceBend(i,-1000)); assert(std::abs(*model.referenceBend(i)-range->extension)<1e-7);
            ++total;
        }
    }
    assert(total==28);
    std::cout << sex << ": " << total << " independent finger joints, mesh and V lines PASS\n";
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    test(room,room.male(),"male"); test(room,room.female(),"female");
    assert(room.restoreOriginalPose());
    for (const auto* model : {&room.male(),&room.female()}) for (const auto& m:model->sourceMatrices({},0)) {
        const std::array<double,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        for (int i=0;i<16;++i) assert(std::abs(m[i]-identity[i])<1e-7);
    }
    std::puts("Original source hands restore without scaling: PASS");
}
