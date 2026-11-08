#pragma once

#include "client/skeleton_toe_editor_ui.h"
#include "client/ui_layer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

namespace gtc::native {
// A landmark is attached to one bone's mesh frame. It is not a joint or an
// automatic fit: the user places each end and can save those choices separately.
class SkeletonToeWorkspace {
    struct Key {
        bool male,blue; char side; int digit,segment;
        auto operator<=>(const Key&) const = default;
    };
    struct Bone {
        Key key; std::string name; std::array<Vec3,2> local,world,baseLocal;
        std::array<double,16> pose; std::vector<Vec3> points;
        std::vector<std::array<int,3>> faces;
    };
public:
    explicit SkeletonToeWorkspace(std::string root) : path_(std::move(root)+"/toe-bone-axes.txt") { load(); }
    void syncWhiteAxes(SkeletonRoom& room) const {
        room.resetDisplayToeAxes();
        for(const auto& [k,p]:axes_)if(!k.blue) {
            ToeEdit e;e.male=k.male;e.side=k.side;e.digit=k.digit;e.segment=k.segment;
            room.setDisplayToeAxis(k.male,toeEditName(e,k.segment),p);
        }
    }
    void drawUi(NativeFrame& f,const SkeletonToeEditorUi& ui) const {
        if(!ui.open())return;
        f.blendRectangle(8,8,630,94,colorHex("#10212c"),.88);
        button(f,14,13,62,preset_ && top_?"TOP *":"TOP");button(f,80,13,64,preset_ && !top_?"SIDE *":"SIDE");
        button(f,150,13,108,"SAVE AXES");button(f,263,13,108,"LOAD AXES");button(f,376,13,122,"RESET AXIS");
        button(f,503,13,130,focus_==0?"ALL BONES":focus_==1?"ONE TOE":"ONE BONE");
        drawTextPixels(f,14,43,status_.empty()?"Left drag: landmarks (not joints). Right: orbit.":status_,colorHex("#e0effb"),20);
        drawTextPixels(f,14,67,"TOP/SIDE reset. Wheel: zoom. H: controls.",colorHex("#e0effb"),20);
        for(const auto& b:bones_) {
            const bool selected=same(b.key,ui.selection());
            auto a=project(b.world[0]),c=project(b.world[1]);
            if(!a || !c)continue;
            const int steps=std::max(1,int(std::hypot(c->x-a->x,c->y-a->y)/3));
            const Color line=selected?colorHex("#ffde70"):colorHex("#43d9dc");
            for(int i=0;i<=steps;++i) {
                const double t=double(i)/steps;
                f.blendRectangle(int(a->x+(c->x-a->x)*t)-1,int(a->y+(c->y-a->y)*t)-1,3,3,line,.95);
            }
            f.blendRectangle(int(a->x)-5,int(a->y)-5,10,10,colorHex("#58f5ad"),1);
            f.blendRectangle(int(c->x)-5,int(c->y)-5,10,10,colorHex("#ff7664"),1);
        }
    }
    // Build just one selected foot; no skin, other foot, room or body geometry.
    bool prepare(const NativeFrame& frame,const SkeletonRoom& room,const SkeletonToeEditorUi& ui) {
        const auto selection=ui.selection();
        const auto& model=selection.male?room.male():room.female();
        const auto& comparison=room.comparison(selection.male);
        if(cached_ && key(selection)==cachedSelection_ && cachedPose_==model.sourcePoseRevision() &&
           cachedComparison_==comparison.revision() && cachedEdits_==room.toeEditRevision() && cachedTop_==top_ && cachedFocus_==focus_ && cachedPanel_==ui.panelVisible() &&
           scene_.width==frame.width && scene_.height==frame.height) {
            refreshAxes();return true;
        }
        bones_.clear();
        const auto whitePose=model.sourceMatrices({},0);
        const auto bluePose=selection.blue?room.comparisonToePose(selection.male,{}):std::vector<std::array<double,16>>{};
        Vec3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
        for(int digit=1;digit<=5;++digit) for(int segment=0;segment<3;++segment) {
            if((digit==1 && segment==1) || (focus_>=1 && digit!=selection.digit) ||
               (focus_>=2 && segment!=selection.segment))continue;
            ToeEdit e=selection;e.digit=digit;e.segment=segment;
            const std::string name=toeEditName(e,segment);const int idx=model.sourceStructure(name);
            const auto ends=room.toeAxis(selection.male,name);
            if(idx<0 || !ends)continue;
            Bone b;b.key={selection.male,selection.blue,selection.side,digit,segment};b.name=name;
            const auto i=std::size_t(idx);
            if(selection.blue) {
                const auto& part=comparison.parts()[i];if(part.points.empty() || i>=bluePose.size())continue;
                b.pose=bluePose[i];b.faces=part.faces;
                for(Vec3 p:part.points)b.points.push_back(toePoint(b.pose,comparison.mapPart(p,part)));
                // Blue geometry was first posed, registered and averaged. Map
                // the source axis through that same frame and centre it on the
                // averaged bone rather than treating raw CT coordinates as blue.
                const auto& source=model.sourceStructures()[i];const auto& group=model.bones()[source.group];
                Vec3 rawCenter{},blueCenter{};
                for(std::size_t j=0;j<source.pointCount;++j)rawCenter+=group.points[source.firstPoint+j];
                for(Vec3 p:part.points)blueCenter+=p;
                rawCenter=rawCenter/double(source.pointCount);blueCenter=blueCenter/double(part.points.size());
                const auto& ref=comparison.littleToeSkinReference()[i];const auto& reg=comparison.metatarsalFrame(i);
                const auto registered=[&](Vec3 p){return frameVector(reg,toePoint(ref,p))+reg.translation;};
                const Vec3 offset=blueCenter-registered(rawCenter);
                b.local={comparison.mapPart(registered((*ends)[0])+offset,part),comparison.mapPart(registered((*ends)[1])+offset,part)};
            } else {
                const auto& part=model.sourceStructures()[i];const auto& group=model.bones()[part.group];
                b.pose=whitePose[i];
                for(std::size_t j=0;j<part.pointCount;++j)b.points.push_back(toePoint(b.pose,group.points[part.firstPoint+j]));
                for(std::size_t j=0;j<part.triangleCount;++j) {
                    auto face=group.triangles[part.firstTriangle+j];
                    b.faces.push_back({face[0]-int(part.firstPoint),face[1]-int(part.firstPoint),face[2]-int(part.firstPoint)});
                }
                b.local=*ends;
            }
            b.baseLocal=b.local;
            if(auto found=axes_.find(b.key);found!=axes_.end())b.local=found->second;
            for(int k=0;k<2;++k)b.world[k]=toePoint(b.pose,b.local[k]);
            for(Vec3 p:b.points) {lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
                hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
            bones_.push_back(std::move(b));
        }
        if(bones_.empty())return false;
        const Vec3 center=(lo+hi)*.5;
        camera_=Camera{};camera_.width=frame.width;camera_.height=frame.height;camera_.fovDegrees=34;camera_.worldSpaceGpu=true;
        const int available=std::max(120,frame.width-(ui.panelVisible()?430:20));
        // Orient the camera to the selected foot, not the world's vertical:
        // the original CT foot may be rotated even though it is unmodified.
        const auto boneCenter=[&](const std::string& name)->std::optional<Vec3> {
            const int index=model.sourceStructure(name);if(index<0)return std::nullopt;
            const auto& part=model.sourceStructures()[std::size_t(index)];if(!part.pointCount)return std::nullopt;
            Vec3 c{};for(std::size_t j=0;j<part.pointCount;++j)c+=model.bones()[part.group].points[part.firstPoint+j];
            return toePoint(whitePose[std::size_t(index)],c/double(part.pointCount));
        };
        const std::string side=std::string("_")+selection.side;
        const auto heel=boneCenter("Cal"+side),first=boneCenter("FootMeta1"+side),fifth=boneCenter("FootMeta5"+side),shin=boneCenter("Tibia"+side);
        Vec3 lateral{1,0,0},forward{0,0,1},dorsal{0,1,0};
        if(heel && first && fifth && shin) {
            lateral=normalized(*first-*fifth);forward=normalized((*first+*fifth)*.5-*heel);
            forward=normalized(forward-lateral*dot(forward,lateral));dorsal=normalized(cross(lateral,forward));
            if(dot(dorsal,*shin-*heel)<0)dorsal=dorsal*-1;
            if(length(dorsal)<.5 || length(forward)<.5){lateral={1,0,0};forward={0,0,1};dorsal={0,1,0};}
        }
        const Vec3 direction=lateral*(std::cos(elevation_)*std::sin(azimuth_))+dorsal*std::sin(elevation_)+forward*(std::cos(elevation_)*std::cos(azimuth_));
        camera_.position=center+direction;camera_.target=center;camera_.up=dorsal;
        Vec3 projectedLo{1e30,1e30,1e30},projectedHi{-1e30,-1e30,-1e30};
        for(const auto& bone:bones_)for(Vec3 p:bone.points) {
            const Vec3 q=camera_.view(p);
            projectedLo.x=std::min(projectedLo.x,q.x);projectedHi.x=std::max(projectedHi.x,q.x);
            projectedLo.y=std::min(projectedLo.y,q.y);projectedHi.y=std::max(projectedHi.y,q.y);
            projectedLo.z=std::min(projectedLo.z,q.z);projectedHi.z=std::max(projectedHi.z,q.z);
        }
        const double spanX=projectedHi.x-projectedLo.x,spanY=projectedHi.y-projectedLo.y;
        const double margin=focus_==2?.003:focus_==1?.010:.035;
        const double depth=projectedHi.z-projectedLo.z;
        const double distance=std::max(.040+depth*.5,std::max(camera_.focal()*(spanX+margin)/available,
            camera_.focal()*(spanY+margin)/std::max(120,frame.height-120))*1.20*zoom_);
        camera_.position=center+direction*distance;camera_.target=center;
        const Vec3 right=camera_.world({1,0,0})-camera_.position;
        const Vec3 shift=right*((frame.width-available)*.5*distance/camera_.focal());
        camera_.position+=shift;camera_.target+=shift;
        Rasterizer raster(frame.width,frame.height,colorHex("#0d1816"),camera_.focal(),true);raster.setCamera(camera_);
        for(std::size_t i=0;i<bones_.size();++i) {
            Material m;m.color=colorHex(same(bones_[i].key,selection)?"#e4c779":selection.blue?"#69b6e9":"#e6dfc9");
            m.opacity=.48;m.specular=.12;raster.setObject(30000+std::uint32_t(i),{},0);raster.mesh(bones_[i].points,bones_[i].faces,m);
        }
        scene_=raster.takeGpuScene();scene_.revision=++revision_;
        for(auto& b:scene_.batches)b.translucent=true;
        scene_.covered=false;scene_.width=frame.width;scene_.height=frame.height;scene_.focal=camera_.focal();scene_.viewMatrix=camera_.viewMatrix();
        cached_=true;cachedSelection_=key(selection);cachedPose_=model.sourcePoseRevision();
        cachedComparison_=comparison.revision();cachedEdits_=room.toeEditRevision();cachedTop_=top_;cachedFocus_=focus_;cachedPanel_=ui.panelVisible();
        return true;
    }
    bool render(NativeFrame& frame) { return cached_ && submit(frame); }
    bool handle(const WindowEvent& e,SkeletonToeEditorUi& ui,SkeletonRoom& room) {
        if(!ui.open())return false;
        if(e.type==WindowEvent::Type::FocusLoss){dragging_=false;orbiting_=false;return false;}
        if(e.type==WindowEvent::Type::MouseDown && e.button==3) {orbiting_=true;lastX_=e.x;lastY_=e.y;return true;}
        if(e.type==WindowEvent::Type::MouseUp && e.button==3 && orbiting_){orbiting_=false;return true;}
        if(e.type==WindowEvent::Type::MouseMove && orbiting_) {
            azimuth_+=(e.x-lastX_)*.008;elevation_=std::clamp(elevation_+(e.y-lastY_)*.008,-1.56,1.56);preset_=false;
            lastX_=e.x;lastY_=e.y;cached_=false;return true;
        }
        if(e.type==WindowEvent::Type::Wheel) {zoom_=std::clamp(zoom_*std::pow(.85,e.wheel),.3,4.);cached_=false;return true;}
        if(e.type==WindowEvent::Type::MouseUp && e.button==1 && dragging_){dragging_=false;return true;}
        if(e.type==WindowEvent::Type::MouseMove && dragging_) {
            auto it=std::find_if(bones_.begin(),bones_.end(),[&](const Bone& b){return b.key==dragKey_;});
            if(it==bones_.end()){dragging_=false;return true;}
            const double depth=camera_.view(it->world[dragEnd_]).z;
            const Vec3 right=camera_.world({1,0,0})-camera_.position,up=camera_.world({0,1,0})-camera_.position;
            const Vec3 delta=(right*(e.x-lastX_)-up*(e.y-lastY_))*(depth/camera_.focal());
            auto pair=it->local;const auto inv=joint_detail::inverse(toeRigid(it->pose));
            pair[dragEnd_]+=frameVector(inv,delta);
            axes_[it->key]=pair;
            if(!it->key.blue)room.setDisplayToeAxis(it->key.male,it->name,pair);
            lastX_=e.x;lastY_=e.y;return true;
        }
        if(e.type!=WindowEvent::Type::MouseDown || e.button!=1)return false;
        if(e.y>=13 && e.y<37) {
            if(e.x>=14 && e.x<76){top_=true;preset_=true;azimuth_=0;elevation_=1.56;cached_=false;return true;}
            if(e.x>=80 && e.x<144){top_=false;preset_=true;azimuth_=1.5707963267948966;elevation_=0;cached_=false;return true;}
            if(e.x>=150 && e.x<258){status_=save()?"Axes saved to toe-bone-axes.txt":"Could not save axes.";return true;}
            if(e.x>=263 && e.x<371){if(load()){syncWhiteAxes(room);status_="Axes loaded.";}else status_="No valid axes file.";return true;}
            if(e.x>=376 && e.x<498){axes_.erase(key(ui.selection()));syncWhiteAxes(room);status_="Selected axis reset.";return true;}
            if(e.x>=503 && e.x<633){focus_=(focus_+1)%3;return true;}
        }
        double best=15*15;const Bone* hit=nullptr;int end=0;
        for(const auto& b:bones_)for(int k=0;k<2;++k)if(auto p=project(b.world[k])) {
            const double d=(p->x-e.x)*(p->x-e.x)+(p->y-e.y)*(p->y-e.y);
            if(d<best){best=d;hit=&b;end=k;}
        }
        if(hit) {ui.select(hit->key.male,hit->key.blue,hit->key.side,hit->key.digit,hit->key.segment);
            if(hit->key.blue || !room.unmodifiedToeSource()) {status_="Mark on the unmoved WHITE B source.";return true;}
            dragKey_=hit->key;dragEnd_=end;lastX_=e.x;lastY_=e.y;dragging_=true;status_.clear();}
        return true;
    }
private:
    void refreshAxes() {
        for(auto& b:bones_) {
            const auto found=axes_.find(b.key);b.local=found==axes_.end()?b.baseLocal:found->second;
            for(int i=0;i<2;++i)b.world[i]=toePoint(b.pose,b.local[i]);
        }
    }
    bool submit(NativeFrame& frame) {
        const auto count=scene_.batches.size();
        if(frame.uiLayer && !frame.uiLayer->empty())appendUiBatches(*frame.uiLayer,camera_,scene_);
        frame.interfaceInFrame=frame.uiLayer && !frame.uiLayer->empty();frame.wantsBgra=true;frame.rgb.clear();
        const bool ok=renderGpuScene(scene_,frame);scene_.batches.resize(count);return ok;
    }
    struct Point {double x,y;};
    std::optional<Point> project(Vec3 p) const {
        const Vec3 v=camera_.view(p);if(v.z<=.001)return std::nullopt;
        return Point{camera_.width*.5+camera_.focal()*v.x/v.z,camera_.height*.5-camera_.focal()*v.y/v.z};
    }
    static Key key(const ToeEdit& e){return {e.male,e.blue,e.side,e.digit,e.segment};}
    static bool same(const Key& a,const ToeEdit& e){return a==key(e);}
    static void button(NativeFrame& f,int x,int y,int w,const char* s) {
        f.blendRectangle(x,y,w,24,colorHex("#44657b"),.78);drawTextPixels(f,x+4,y+2,s,colorHex("#e0effb"),20);
    }
    bool save() const {
        std::ofstream out(path_,std::ios::trunc);if(!out)return false;
        out<<"toe-axes-v1\n"<<std::setprecision(17);
        for(const auto& [k,p]:axes_)out<<k.male<<' '<<k.blue<<' '<<k.side<<' '<<k.digit<<' '<<k.segment<<' '
            <<p[0].x<<' '<<p[0].y<<' '<<p[0].z<<' '<<p[1].x<<' '<<p[1].y<<' '<<p[1].z<<'\n';
        return bool(out);
    }
    bool load() {
        std::ifstream in(path_);std::string version;if(!(in>>version) || version!="toe-axes-v1")return false;
        std::map<Key,std::array<Vec3,2>> result;int male,blue;Key k{};std::array<Vec3,2> p;
        while(in>>male) {
            if(!(in>>blue>>k.side>>k.digit>>k.segment>>p[0].x>>p[0].y>>p[0].z>>p[1].x>>p[1].y>>p[1].z))return false;
            ToeEdit e;e.male=male==1;e.blue=blue==1;e.side=k.side;e.digit=k.digit;e.segment=k.segment;
            if((male!=0&&male!=1)||(blue!=0&&blue!=1)||!validToeEdit(e)||!joint_detail::finite(p[0])||!joint_detail::finite(p[1])||result.size()>=112)return false;
            k.male=e.male;k.blue=e.blue;result[k]=p;
        }
        if(!in.eof())return false;axes_=std::move(result);return true;
    }
    std::string path_,status_;
    std::map<Key,std::array<Vec3,2>> axes_;
    std::vector<Bone> bones_;
    Camera camera_{};GpuScene scene_;
    std::uint64_t revision_=0,cachedPose_=0,cachedComparison_=0,cachedEdits_=0;
    Key cachedSelection_{};bool cached_=false,cachedTop_=true,cachedPanel_=true;
    Key dragKey_{};int dragEnd_=0,focus_=0,cachedFocus_=0;double lastX_=0,lastY_=0;
    double azimuth_=0,elevation_=1.56,zoom_=1;
    bool top_=true,preset_=true,dragging_=false,orbiting_=false;
};
} // namespace gtc::native
