#pragma once

#include "client/skeleton_toe_editor_ui.h"
#include "client/skeleton_toe_surface.h"
#include "client/ui_layer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>
#include <vector>

namespace gtc::native {
// The user edits individual bones and places landmarks on their surfaces.
// Marks stay in the original CT mesh frame when the displayed bone is moved.
class SkeletonToeWorkspace {
    struct Key {
        bool male,blue; char side; int digit,segment;
        auto operator<=>(const Key&) const = default;
    };
    struct Bone {
        Key key; std::string name; std::array<Vec3,2> local,world,baseLocal;
        std::array<double,16> pose; bool blueMarked=false; std::vector<Vec3> points;
        std::vector<std::array<int,3>> faces;
    };
public:
    explicit SkeletonToeWorkspace(std::string root) : path_(std::move(root)+"/toe-bone-axes.txt") { load(); }
    void syncWhiteAxes(SkeletonRoom& room) {
        // Files from older editors were free-floating. Never treat them (or a
        // damaged file) as surface attachments without validating each point.
        if(!validated_) {
            for(auto& [k,mask]:marks_) {
                if(k.blue || !mask)continue;
                ToeEdit e;e.male=k.male;e.side=k.side;e.digit=k.digit;e.segment=k.segment;
                const auto& model=k.male?room.male():room.female();const int index=model.sourceStructure(toeEditName(e,k.segment));
                const auto found=axes_.find(k);if(index<0 || found==axes_.end()){mask=0;continue;}
                const auto& part=model.sourceStructures()[std::size_t(index)];const auto& group=model.bones()[part.group];
                std::vector<Vec3> points;std::vector<std::array<int,3>> faces;
                for(std::size_t j=0;j<part.pointCount;++j)points.push_back(group.points[part.firstPoint+j]);
                for(std::size_t j=0;j<part.triangleCount;++j) {const auto f=group.triangles[part.firstTriangle+j];
                    faces.push_back({f[0]-int(part.firstPoint),f[1]-int(part.firstPoint),f[2]-int(part.firstPoint)});}
                for(int j=0;j<2;++j)if(mask&(1u<<j)) {
                    const auto p=toeClosestSurface(found->second[j],points,faces);
                    if(!p || length(*p-found->second[j])>1e-6)mask&=~(1u<<j);
                }
                if(mask==3 && length(found->second[1]-found->second[0])<1e-5)mask=0;
            }
            validated_=true;
        }
        room.resetDisplayToeAxes();
        for(const auto& [k,p]:axes_)if(!k.blue) {
            ToeEdit e;e.male=k.male;e.side=k.side;e.digit=k.digit;e.segment=k.segment;
            const auto mark=marks_.find(k);const unsigned mask=mark==marks_.end()?0:mark->second;
            room.setDisplayToeAxis(k.male,toeEditName(e,k.segment),p,mask);
        }
    }
    void drawUi(NativeFrame& f,const SkeletonToeEditorUi& ui) const {
        if(!ui.open())return;
        f.blendRectangle(8,8,630,94,colorHex("#10212c"),.88);
        button(f,14,13,62,preset_ && top_?"TOP *":"TOP");button(f,80,13,64,preset_ && !top_?"SIDE *":"SIDE");
        button(f,150,13,108,"SAVE AXES");button(f,263,13,108,"LOAD AXES");button(f,376,13,122,"RESET AXIS");
        button(f,503,13,130,focus_==0?"ALL BONES":focus_==1?"ONE TOE":"ONE BONE");
        drawTextPixels(f,14,43,status_.empty()?"Green: start. Red: tip. Dim means unset.":status_,colorHex("#e0effb"),20);
        std::string hint="Mark end faces. Right: orbit. H: controls.";
        const auto selected=std::find_if(bones_.begin(),bones_.end(),[&](const Bone& b){return same(b.key,ui.selection());});
        if(selected!=bones_.end() && !selected->key.blue && selected->key.segment>=0) {
            Key parent=selected->key;parent.segment=parent.segment==0?-1:parent.segment==2 && parent.digit==1?0:parent.segment-1;
            const auto neighbour=std::find_if(bones_.begin(),bones_.end(),[&](const Bone& b){return b.key==parent;});
            const auto a=marks_.find(parent),b=marks_.find(selected->key);
            if(neighbour!=bones_.end() && a!=marks_.end() && b!=marks_.end() && (a->second&2) && (b->second&1)) {
                char gap[96];std::snprintf(gap,sizeof gap,"Joint gap: %.2f mm. Move/rotate the bone.",1000*length(neighbour->world[1]-selected->world[0]));hint=gap;
            }
        }
        drawTextPixels(f,14,67,hint,colorHex("#e0effb"),20);
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
            const auto mark=marks_.find(b.key);const unsigned mask=b.blueMarked?3:mark==marks_.end()?0:mark->second;
            f.blendRectangle(int(a->x)-5,int(a->y)-5,10,10,colorHex(mask&1?"#58f5ad":"#3b8c69"),1);
            f.blendRectangle(int(c->x)-5,int(c->y)-5,10,10,colorHex(mask&2?"#ff7664":"#a34b40"),1);
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
        for(int digit=1;digit<=5;++digit) for(int segment=-1;segment<3;++segment) {
            if((digit==1 && segment==1) || (focus_>=1 && digit!=selection.digit) ||
               (focus_>=2 && segment!=selection.segment))continue;
            ToeEdit e=selection;e.digit=digit;e.segment=segment;
            const std::string name=toeEditName(e,segment);const int idx=model.sourceStructure(name);
            auto ends=room.toeAxis(selection.male,name);
            if(idx<0)continue;
            const auto i=std::size_t(idx);
            if(!ends && segment==-1) {
                const auto& part=model.sourceStructures()[i];const auto& group=model.bones()[part.group];
                const int proximal=model.sourceStructure(toeEditName(e,0));
                if(!part.pointCount || proximal<0)continue;
                const auto& tip=model.sourceStructures()[std::size_t(proximal)];if(!tip.pointCount)continue;
                Vec3 center{},next{};
                for(std::size_t j=0;j<part.pointCount;++j)center+=group.points[part.firstPoint+j];
                for(std::size_t j=0;j<tip.pointCount;++j)next+=model.bones()[tip.group].points[tip.firstPoint+j];
                center=center/double(part.pointCount);next=next/double(tip.pointCount);
                const Vec3 direction=normalized(next-center);double low=1e30,high=-1e30;std::array<Vec3,2> pair{};
                for(std::size_t j=0;j<part.pointCount;++j) {const Vec3 p=group.points[part.firstPoint+j];const double d=dot(p-center,direction);
                    if(d<low){low=d;pair[0]=p;}if(d>high){high=d;pair[1]=p;}}
                ends=pair;
            }
            if(!ends)continue;
            if(segment>=0) {
                // B-line files do not consistently order proximal and distal
                // ends. Order the *unconfirmed handles* from heel to tip;
                // the user's own surface picks remain authoritative.
                const int prev=segment==0?-1:segment==2 && digit==1?0:segment-1;
                const int next=segment==0?(digit==1?2:1):segment==1?2:3;
                const auto center=[&](int s)->std::optional<Vec3> {
                    if(s>2)return std::nullopt;
                    const int n=model.sourceStructure(toeEditName(e,s));
                    if(n<0)return std::nullopt;
                    const auto& p=model.sourceStructures()[std::size_t(n)];if(!p.pointCount)return std::nullopt;
                    Vec3 c{};for(std::size_t j=0;j<p.pointCount;++j)c+=model.bones()[p.group].points[p.firstPoint+j];
                    return c/double(p.pointCount);
                };
                const auto a=center(prev),b=center(segment),c=center(next);
                if(b && (c || a)) {const Vec3 forward=c?*c-*b:*b-*a;
                    if(dot((*ends)[1]-(*ends)[0],forward)<0)std::swap((*ends)[0],(*ends)[1]);}
            }
            Bone b;b.key={selection.male,selection.blue,selection.side,digit,segment};b.name=name;
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
                if(auto calibrated=room.blueToeAxis(selection.male,selection.side,digit,segment)){b.local=*calibrated;b.blueMarked=true;}
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
            // Unconfirmed handles start on the displayed surface, but do not
            // become calibration data until explicitly placed by the user.
            const auto inverse=joint_detail::inverse(toeRigid(b.pose));
            for(int k=0;k<2;++k)if(auto surface=toeClosestSurface(toePoint(b.pose,b.local[k]),b.points,b.faces))
                b.local[k]=frameVector(inverse,*surface)+inverse.translation;
            b.baseLocal=b.local;
            if(auto found=axes_.find(b.key);!b.key.blue && found!=axes_.end())for(int k=0;k<2;++k)
                if(marks_[b.key]&(1u<<k))b.local[k]=found->second[k];
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
            const Vec3 ray=normalized(camera_.world({(e.x-camera_.width*.5)/camera_.focal(),
                (camera_.height*.5-e.y)/camera_.focal(),1})-camera_.position);
            if(auto point=toeSurfaceRay(camera_.position,ray,it->points,it->faces)) {
                auto pair=it->local;
                if(auto found=axes_.find(it->key);found!=axes_.end())for(int i=0;i<2;++i)
                    if(marks_[it->key]&(1u<<i))pair[i]=found->second[i];
                const auto inv=joint_detail::inverse(toeRigid(it->pose));
                pair[dragEnd_]=frameVector(inv,*point)+inv.translation;
                axes_[it->key]=pair;marks_[it->key]|=1u<<dragEnd_;
                if(length(pair[1]-pair[0])<1e-5) {marks_[it->key]&=~(1u<<dragEnd_);status_="Place the two ends apart on the bone.";}
                else status_.clear();
                syncWhiteAxes(room);
            } else status_="Move over the selected bone surface.";
            lastX_=e.x;lastY_=e.y;return true;
        }
        if(e.type!=WindowEvent::Type::MouseDown || e.button!=1)return false;
        if(e.y>=13 && e.y<37) {
            if(e.x>=14 && e.x<76){top_=true;preset_=true;azimuth_=0;elevation_=1.56;cached_=false;return true;}
            if(e.x>=80 && e.x<144){top_=false;preset_=true;azimuth_=1.5707963267948966;elevation_=0;cached_=false;return true;}
            if(e.x>=150 && e.x<258){status_=save()?"Axes saved to toe-bone-axes.txt":"Could not save axes.";return true;}
            if(e.x>=263 && e.x<371){if(load()){syncWhiteAxes(room);if(!room.originalToePose())room.restoreReferencePose();status_="Axes loaded.";}else status_="No valid axes file.";return true;}
            if(e.x>=376 && e.x<498){if(ui.selection().blue){status_="Reset the axis on the white source.";return true;}
                axes_.erase(key(ui.selection()));marks_.erase(key(ui.selection()));syncWhiteAxes(room);
                if(!room.originalToePose())room.restoreReferencePose();
                status_="Selected axis reset.";return true;}
            if(e.x>=503 && e.x<633){focus_=(focus_+1)%3;return true;}
        }
        double best=15*15;const Bone* hit=nullptr;int end=0;
        for(const auto& b:bones_)for(int k=0;k<2;++k)if(auto p=project(b.world[k])) {
            const double d=(p->x-e.x)*(p->x-e.x)+(p->y-e.y)*(p->y-e.y);
            if(d<best){best=d;hit=&b;end=k;}
        }
        if(hit) {ui.select(hit->key.male,hit->key.blue,hit->key.side,hit->key.digit,hit->key.segment);
            if(hit->key.blue || !room.originalToePose()) {status_="Mark on WHITE in B; STAND is a preview.";return true;}
            dragKey_=hit->key;dragEnd_=end;lastX_=e.x;lastY_=e.y;dragging_=true;status_.clear();}
        return true;
    }
private:
    void refreshAxes() {
        for(auto& b:bones_) {
            const auto found=axes_.find(b.key);b.local=b.baseLocal;
            if(!b.key.blue && found!=axes_.end())for(int i=0;i<2;++i)if(marks_[b.key]&(1u<<i))b.local[i]=found->second[i];
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
        out<<"toe-axes-v2\n"<<std::setprecision(17);
        for(const auto& [k,p]:axes_) {const auto mark=marks_.find(k);const unsigned mask=mark==marks_.end()?0:mark->second;
            out<<k.male<<' '<<k.blue<<' '<<k.side<<' '<<k.digit<<' '<<k.segment<<' '<<mask<<' '
            <<p[0].x<<' '<<p[0].y<<' '<<p[0].z<<' '<<p[1].x<<' '<<p[1].y<<' '<<p[1].z<<'\n';}
        return bool(out);
    }
    bool load() {
        std::ifstream in(path_);std::string version;if(!(in>>version) || (version!="toe-axes-v1" && version!="toe-axes-v2"))return false;
        std::map<Key,std::array<Vec3,2>> result;std::map<Key,unsigned> placed;int male,blue;Key k{};std::array<Vec3,2> p;
        while(in>>male) {
            unsigned mask=0;
            if(!(in>>blue>>k.side>>k.digit>>k.segment))return false;
            if(version=="toe-axes-v2" && !(in>>mask))return false;
            if(!(in>>p[0].x>>p[0].y>>p[0].z>>p[1].x>>p[1].y>>p[1].z))return false;
            if((male!=0&&male!=1)||(blue!=0&&blue!=1)||(k.side!='L'&&k.side!='R')||k.digit<1||k.digit>5||
               k.segment< -1||k.segment>2||(k.digit==1&&k.segment==1)||mask>3||!joint_detail::finite(p[0])||!joint_detail::finite(p[1])||result.size()>=152)return false;
            k.male=male==1;k.blue=blue==1;result[k]=p;placed[k]=mask;
        }
        if(!in.eof())return false;
        axes_=std::move(result);marks_=std::move(placed);validated_=false;return true;
    }
    std::string path_,status_;
    std::map<Key,std::array<Vec3,2>> axes_;
    std::map<Key,unsigned> marks_;
    std::vector<Bone> bones_;
    Camera camera_{};GpuScene scene_;
    std::uint64_t revision_=0,cachedPose_=0,cachedComparison_=0,cachedEdits_=0;
    Key cachedSelection_{};bool cached_=false,cachedTop_=true,cachedPanel_=true;
    Key dragKey_{};int dragEnd_=0,focus_=0,cachedFocus_=0;double lastX_=0,lastY_=0;
    double azimuth_=0,elevation_=1.56,zoom_=1;
    bool top_=true,preset_=true,dragging_=false,orbiting_=false,validated_=false;
};
} // namespace gtc::native
