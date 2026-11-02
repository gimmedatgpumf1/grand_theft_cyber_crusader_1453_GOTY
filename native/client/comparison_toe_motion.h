#pragma once

// Map the source rig's real MTP/IP movements into a fitted blue comparison
// toe. Upstream metatarsal motion is cancelled because comparison bodies are
// static; only the existing individual toe joints drive these matrices.
#include "client/skeleton_body.h"
#include <array>
#include <string>
#include <vector>

namespace gtc::native {
class ComparisonToeMotion {
public:
    using Matrix=std::array<double,16>;
    bool load(const SkeletonModel& model,std::string& error) {
        bind_=model.sourceMatrices({},0); centers_.resize(bind_.size());
        for (int s=0;s<2;++s) for (int d=0;d<5;++d) {
            const std::string name=std::to_string(d+1)+(s?"_R":"_L");
            const int meta=model.sourceStructure("FootMeta"+name); if (meta<0) { error="Missing toe metatarsal"; return false; }
            meta_[s][d]=meta;
            const std::array<std::string,3> names={"FootProxP"+name,d==0?"":"FootMidP"+name,"FootDistP"+name};
            for (int j=0;j<3;++j) {
                part_[s][d][j]=-1;
                if (names[j].empty()) continue;
                const int id=model.sourceStructure(names[j]); if (id<0) { error="Missing toe phalanx"; return false; }
                part_[s][d][j]=id;
                const auto& structure=model.sourceStructures()[id]; Vec3 c;
                for (std::size_t i=0;i<structure.pointCount;++i) c+=model.bones()[structure.group].points[structure.firstPoint+i];
                if (!structure.pointCount) { error="Empty toe phalanx"; return false; }
                centers_[id]=transform(bind_[id],c/static_cast<double>(structure.pointCount));
            }
        }
        error.clear(); return true;
    }
    const auto& parts() const { return part_; }
    const auto& metatarsals() const { return meta_; }
    std::vector<Matrix> pose(const SkeletonModel& model,const std::vector<Vec3>& blueCenters,double scale) const {
        const auto current=model.sourceMatrices({},0);
        std::vector<Matrix> result(bind_.size(),identity());
        if (current.size()!=bind_.size() || blueCenters.size()!=bind_.size()) return {};
        for (int s=0;s<2;++s) for (int d=0;d<5;++d) for (int j=0;j<3;++j) {
            const int id=part_[s][d][j]; if (id<0) continue;
            const int meta=meta_[s][d];
            // Both current and bind matrices map source coordinates to posed
            // world coordinates. This quotient keeps metatarsal motion out.
            Matrix delta=multiply(multiply(multiply(bind_[meta],inverse(current[meta])),current[id]),inverse(bind_[id]));
            const Vec3 displacement=(transform(delta,centers_[id])-centers_[id])*scale;
            const Vec3 c=blueCenters[id];
            const Vec3 rotated{delta[0]*c.x+delta[4]*c.y+delta[8]*c.z,
                               delta[1]*c.x+delta[5]*c.y+delta[9]*c.z,
                               delta[2]*c.x+delta[6]*c.y+delta[10]*c.z};
            const Vec3 t=c+displacement-rotated; delta[12]=t.x; delta[13]=t.y; delta[14]=t.z;
            result[id]=delta;
        }
        return result;
    }
    static Matrix identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
    static Vec3 transform(const Matrix& m,Vec3 p) {
        return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
                m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
    }
private:
    std::vector<Matrix> bind_; std::vector<Vec3> centers_;
    std::array<std::array<std::array<int,3>,5>,2> part_{};
    std::array<std::array<int,5>,2> meta_{};
    static Matrix multiply(const Matrix& a,const Matrix& b) {
        Matrix c{}; for (int col=0;col<4;++col) for (int row=0;row<4;++row)
            for (int k=0;k<4;++k) c[col*4+row]+=a[k*4+row]*b[col*4+k];
        return c;
    }
    static Matrix inverse(const Matrix& m) {
        Matrix r=identity();
        for (int row=0;row<3;++row) for (int col=0;col<3;++col) r[col*4+row]=m[row*4+col];
        const Vec3 t{m[12],m[13],m[14]};
        const Vec3 v{-dot(Vec3{m[0],m[1],m[2]},t),-dot(Vec3{m[4],m[5],m[6]},t),-dot(Vec3{m[8],m[9],m[10]},t)};
        r[12]=v.x; r[13]=v.y; r[14]=v.z; return r;
    }
};
} // namespace gtc::native
