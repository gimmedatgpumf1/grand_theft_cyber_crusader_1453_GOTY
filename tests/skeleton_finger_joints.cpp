// Exercise each actual thumb and finger joint independently in both room views.
#include "client/skeleton_room.h"
#include "client/skeleton_finger_frame.h"
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
static Vec3 shaft(const SkeletonModel& model, const std::vector<std::array<double,16>>& matrices,
                  const std::string& name, const std::string& next) {
    const auto index=model.sourceStructure(name); const auto& part=model.sourceStructures()[index];
    const Vec3* points=model.bones()[part.group].points.data()+part.firstPoint;
    const Vec3 direction=FingerReferenceFrames::shaft(points,part.pointCount,center(model,name),center(model,next)-center(model,name));
    const auto& m=matrices[index];
    return normalized(Vec3{m[0]*direction.x+m[4]*direction.y+m[8]*direction.z,
                           m[1]*direction.x+m[5]*direction.y+m[9]*direction.z,
                           m[2]*direction.x+m[6]*direction.y+m[10]*direction.z});
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
    assert(room.restoreOriginalPose()); const auto source=model.sourceMatrices({},0);
    int total=0; double direction[2][5][3]{}, maxSource[3]{};
    for (char side : {'L','R'}) for (int digit=1;digit<=5;++digit) {
        assert(room.restoreReferencePose());
        const auto suffix=std::string("_")+side, number=std::to_string(digit);
        const std::string meta="HandMeta"+number+suffix, prox="HandProxP"+number+suffix;
        const std::string mid="HandMidP"+number+suffix, tip="HandDistP"+number+suffix;
        auto bind=model.sourceMatrices({},0);
        // Check the actual long axes, not only centroid offsets or stored zeroes.
        // The four fingers and thumb point outward along the arm in the reference.
        const Vec3 outward=side=='L' ? Vec3{1,0,0} : Vec3{-1,0,0};
        const auto firstNext=digit==1 ? tip : mid;
        const Vec3 proximal=shaft(model,bind,prox,firstNext);
        assert(dot(proximal,outward)>std::cos(3*3.141592653589793/180));
        if (digit!=1) {
            const Vec3 middle=shaft(model,bind,mid,tip);
            assert(dot(middle,outward)>std::cos(3*3.141592653589793/180));
        }
        const Vec3 distal=shaft(model,bind,tip,digit==1 ? prox : mid)*-1.0;
        assert(dot(distal,outward)>std::cos(3*3.141592653589793/180));
        if (digit==1) assert(posed(model,bind,tip).y>posed(model,bind,"HandDistP2"+suffix).y);
        const std::string neighbour="HandDistP"+std::to_string(digit==5 ? 2 : digit+1)+suffix;
        const std::string parents[]={meta,prox,mid}, children[]={prox,digit==1 ? tip : mid,tip};
        for (int joint=0;joint<(digit==1 ? 2 : 3);++joint) {
            assert(room.restoreReferencePose()); bind=model.sourceMatrices({},0);
            const auto beforeLines=lineVertices(lines,model);
            const auto i=namedLink(model,parents[joint],children[joint]);
            const Vec3 originalParent=shaft(model,source,parents[joint],children[joint]);
            const Vec3 originalChild=shaft(model,source,children[joint],parents[joint])*-1.0;
            const double originalAngle=std::acos(std::clamp(dot(originalParent,originalChild),-1.0,1.0))*180/3.141592653589793;
            maxSource[joint]=std::max(maxSource[joint],originalAngle);
            assert(model.referenceBend(i) && std::abs(*model.referenceBend(i))<1e-7);
            const auto& e=model.sourceLinks()[i];
            const auto range=referenceBendRange(e.mobility,parents[joint],children[joint]); assert(range);
            const Vec3 normal=palmNormal(model,bind,side), start=posed(model,bind,tip);
            assert(model.setReferenceBend(i,10));
            auto moved=model.sourceMatrices({},0);
            const Vec3 displacement=posed(model,moved,tip)-start;
            const double curl=dot(displacement,normal);
            assert(curl>0.00005); // each MCP, PIP/IP and DIP curls toward the palm
            const double ratio=curl/length(displacement);
            assert(ratio>(digit==1 ? 0.6 : 0.8));
            direction[side=='L' ? 0 : 1][digit-1][joint]=ratio;
            for (int k=0;k<16;++k) assert(std::abs(moved[model.sourceStructure(neighbour)][k]-bind[model.sourceStructure(neighbour)][k])<1e-7);
            const auto afterLines=lineVertices(lines,model);
            assert(segments.count(tip) && segments.count(neighbour));
            assert(difference(beforeLines,afterLines,segments.at(tip))>0.0001);
            assert(difference(beforeLines,afterLines,segments.at(neighbour))<1e-6);
            assert(model.setReferenceBend(i,-10));
            const Vec3 extension=posed(model,model.sourceMatrices({},0),tip)-start;
            if (digit>1 && (joint==0 || joint==2)) {
                assert(dot(extension,normal)<-0.00005);
                assert(dot(extension,normal)/length(extension)<-0.8);
            } else assert(length(extension)<1e-9); // no extension beyond neutral
            assert(model.setReferenceBend(i,1000)); assert(std::abs(*model.referenceBend(i)-range->flexion)<1e-7);
            assert(model.setReferenceBend(i,-1000)); assert(std::abs(*model.referenceBend(i)-range->extension)<1e-7);
            ++total;
        }
    }
    assert(total==28);
    for (int digit=0;digit<5;++digit) for (int joint=0;joint<(digit==0 ? 2 : 3);++joint)
        assert(std::abs(direction[0][digit][joint]-direction[1][digit][joint]) < (digit==0 ? 0.35 : 0.15));
    assert(maxSource[0]>20);
    std::cout << sex << ": source shaft deviation maxima MCP " << maxSource[0] << ", PIP/IP " << maxSource[1]
              << ", DIP " << maxSource[2] << " degrees; " << total << " independent joints PASS\n";
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
