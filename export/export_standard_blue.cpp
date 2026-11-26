// Formula-driven blue skeleton export. Build from the repository root with rescale.sh.
#include "client/skeleton_room.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

using namespace gtc::native;

static Vec3 transform(const std::array<double,16>& m, Vec3 p) {
    return {m[0]*p.x+m[4]*p.y+m[8]*p.z+m[12],
            m[1]*p.x+m[5]*p.y+m[9]*p.z+m[13],
            m[2]*p.x+m[6]*p.y+m[10]*p.z+m[14]};
}
static bool secondaryMergedBone(const std::string& name) {
    return name=="Ulna_L" || name=="Ulna_R" || name=="Fibula_L" || name=="Fibula_R";
}
static std::string category(const std::string& n) {
    if(n=="Head"||n=="Jaw"||n=="C1")return "head";
    if(n.size()==2&&n[0]=='C'&&n[1]>='2'&&n[1]<='7')return "neck";
    for(int i=1;i<=12;++i)if(n=="T"+std::to_string(i))return "torso";
    for(int i=1;i<=5;++i)if(n=="L"+std::to_string(i))return "torso";
    if(n=="Sternum"||n.rfind("Rib",0)==0)return "torso";
    if(n.rfind("Pelvis_",0)==0||n=="Sacrum")return "hip";
    if(n.rfind("Scapula_",0)==0||n.rfind("Clavicula_",0)==0)return "shoulder";
    if(n.rfind("Humerus_",0)==0||n.rfind("Radius_",0)==0||n.rfind("Hand",0)==0)return "arm";
    return "leg";
}
static void vecJson(std::ostream& o,Vec3 p) {o<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
static void arrayJson(std::ostream& o,const std::array<double,3>& a) {
    o<<'['<<a[0]<<','<<a[1]<<','<<a[2]<<']';
}
static bool exportOne(SkeletonRoom& room,bool male,double heightM,const std::string& base) {
    const auto& initial=room.comparison(male);
    auto profile=initial.profile();
    profile.height=heightM*100.0;
    std::string error;
    if(!room.setComparison(male,profile,error)) {
        std::fprintf(stderr,"Cannot fit requested profile: %s\n",error.c_str());
        return false;
    }
    const auto& comparison=room.comparison(male);
    const auto& parts=comparison.parts();
    const auto pose=room.comparisonToePose(male,{});
    if(parts.empty()||pose.size()!=parts.size())return false;

    std::ofstream out(base+".obj");if(!out)return false;
    out<<"# Formula-fitted blue comparison skeleton.\n"
       <<"# Units: m. Axes: X right, Y up, Z forward.\n"
       <<"mtllib standard_blue.mtl\nusemtl standard_blue\n"<<std::setprecision(12);
    std::size_t vertexBase=1,vertexCount=0,faceCount=0,objectCount=0;
    Vec3 low{1e30,1e30,1e30},high{-1e30,-1e30,-1e30};
    for(std::size_t i=0;i<parts.size();++i){
        const auto& part=parts[i];
        if(secondaryMergedBone(part.name)||part.points.empty()||part.faces.empty())continue;
        out<<"\no "<<part.name<<"\ng "<<part.name<<"\n";
        for(Vec3 p:part.points){
            p=transform(pose[i],comparison.mapPart(p,part));
            low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);
            high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);
            out<<"v "<<p.x<<' '<<p.y<<' '<<p.z<<'\n';
        }
        for(const auto& f:part.faces)
            out<<"f "<<vertexBase+std::size_t(f[0])<<' '
               <<vertexBase+std::size_t(f[1])<<' '
               <<vertexBase+std::size_t(f[2])<<'\n';
        vertexBase+=part.points.size();vertexCount+=part.points.size();
        faceCount+=part.faces.size();++objectCount;
    }
    out.close();if(!out||!objectCount||!vertexCount||!faceCount)return false;

    const std::array<const char*,7> names={"head","neck","torso","arm","leg","hip","shoulder"};
    std::array<double,3> lowA={low.x,low.y,low.z},highA={high.x,high.y,high.z};
    std::ofstream meta(base+".json");if(!meta)return false;
    const std::string outputName=std::filesystem::path(base).filename().string();
    meta<<std::setprecision(12)<<"{\n  \"name\": \""<<outputName<<"\",\n"
        <<"  \"license\": \"CC BY 4.0\",\n  \"default_height_m\": "<<heightM<<",\n"
        <<"  \"units\": \"m\",\n  \"up_axis\": \"Y\",\n  \"forward_axis\": \"Z\",\n"
        <<"  \"format\": \"OBJ + MTL\",\n  \"objects\": "<<objectCount<<",\n"
        <<"  \"vertices\": "<<vertexCount<<",\n  \"triangles\": "<<faceCount<<",\n"
        <<"  \"mesh_bounds_m\": {\"min\":";arrayJson(meta,lowA);
    meta<<",\"max\":";arrayJson(meta,highA);meta<<"},\n"
        <<"  \"target_measurements_m\": {\n    \"names\":[";
    for(std::size_t i=0;i<names.size();++i){if(i)meta<<',';meta<<'\"'<<names[i]<<'\"';}
    meta<<"],\n    \"requested\":[";
    for(std::size_t i=1;i<8;++i){if(i>1)meta<<',';meta<<comparison.requestedTargets()[i]/100.0;}
    meta<<"],\n    \"fitted\":[";
    for(std::size_t i=1;i<8;++i){if(i>1)meta<<',';meta<<comparison.targets()[i]/100.0;}
    meta<<"]\n  }\n}\n";
    return bool(meta);
}

int main(int argc,char** argv) {
    if(argc!=4){
        std::fprintf(stderr,"Usage: blue-export <male|female> <height_m> <output-base>\n");
        return 2;
    }
    const std::string sex=argv[1];
    if(sex!="male"&&sex!="female"){std::fprintf(stderr,"Sex must be male or female.\n");return 2;}
    char* end=nullptr;const double heightM=std::strtod(argv[2],&end);
    if(!end||*end||!std::isfinite(heightM)||heightM<0.5){
        std::fprintf(stderr,"Height must be at least 0.5 m and finite.\n");return 2;
    }
    const std::string base=argv[3];
    const auto parent=std::filesystem::path(base).parent_path();
    if(!parent.empty())std::filesystem::create_directories(parent);
    SkeletonRoom room;std::string error;
    if(!room.load(".",error)){std::fprintf(stderr,"Cannot load skeleton source: %s\n",error.c_str());return 1;}
    if(!exportOne(room,sex=="male",heightM,base)){
        std::fprintf(stderr,"Export failed.\n");return 1;
    }
    return 0;
}
