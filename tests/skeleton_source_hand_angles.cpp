// Measure source and posed shaft directions from the actual labelled meshes.
// These are geometric angles, not individual clinical goniometer measurements.
#include "client/skeleton_room.h"
#include "client/skeleton_finger_frame.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
using namespace gtc::native;

static Vec3 center(const SkeletonModel& model, int index) {
    const auto& part=model.sourceStructures()[index]; Vec3 result;
    const auto* points=model.bones()[part.group].points.data()+part.firstPoint;
    for (std::size_t i=0;i<part.pointCount;++i) result+=points[i];
    return result/static_cast<double>(part.pointCount);
}
static Vec3 axis(const SkeletonModel& model, const std::vector<std::array<double,16>>& matrices,
                 const std::string& name, const std::string& toward) {
    const int index=model.sourceStructure(name), next=model.sourceStructure(toward); assert(index>=0 && next>=0);
    const auto& part=model.sourceStructures()[index];
    const auto* points=model.bones()[part.group].points.data()+part.firstPoint;
    const Vec3 direction=FingerReferenceFrames::shaft(points,part.pointCount,center(model,index),center(model,next)-center(model,index));
    const auto& m=matrices[index];
    return normalized(Vec3{m[0]*direction.x+m[4]*direction.y+m[8]*direction.z,
                           m[1]*direction.x+m[5]*direction.y+m[9]*direction.z,
                           m[2]*direction.x+m[6]*direction.y+m[10]*direction.z});
}
static double deviation(const SkeletonModel& model, const std::vector<std::array<double,16>>& matrices,
                        const std::string& parent, const std::string& child) {
    const Vec3 a=axis(model,matrices,parent,child), b=axis(model,matrices,child,parent)*-1.0;
    return std::acos(std::clamp(dot(a,b),-1.0,1.0))*180/3.14159265358979323846;
}
static void report(SkeletonRoom& room, SkeletonModel& model, const char* name) {
    assert(room.restoreOriginalPose()); const auto source=model.sourceMatrices({},0);
    assert(room.restoreReferencePose()); const auto reference=model.sourceMatrices({},0);
    int count=0;
    for (char side : {'L','R'}) for (int digit=1;digit<=5;++digit) {
        const auto suffix=std::to_string(digit)+"_"+side;
        const std::string meta="HandMeta"+suffix, prox="HandProxP"+suffix;
        const std::string mid="HandMidP"+suffix, tip="HandDistP"+suffix;
        const std::string parents[]={meta,prox,mid}, children[]={prox,digit==1 ? tip : mid,tip};
        for (int joint=0;joint<(digit==1 ? 2 : 3);++joint) {
            const double initial=deviation(model,source,parents[joint],children[joint]);
            const double posed=deviation(model,reference,parents[joint],children[joint]);
            assert(std::isfinite(initial) && std::isfinite(posed));
            if (joint>0) assert(posed<1.0); // phalangeal shafts actually straighten
            std::printf("%s %c digit %d %s: source %.2f, reference %.2f degrees\n",name,side,digit,
                        joint==0 ? "MCP" : joint==1 ? (digit==1 ? "IP" : "PIP") : "DIP",initial,posed);
            ++count;
        }
    }
    assert(count==28);
}
int main() {
    SkeletonRoom room; std::string error; assert(room.load(".",error));
    report(room,room.male(),"male"); report(room,room.female(),"female");
    std::puts("Source and reference finger shaft measurements: PASS (geometric, not clinical angles)");
}
