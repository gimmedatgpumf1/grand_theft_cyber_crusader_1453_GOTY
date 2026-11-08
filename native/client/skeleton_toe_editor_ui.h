#pragma once

#include "client/skeleton_room.h"
#include "client/window.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace gtc::native {
// Manual, per-phalanx editor. No fitting or automatic alignment is performed.
class SkeletonToeEditorUi {
public:
    bool open() const { return open_; }
    bool panelVisible() const { return panelVisible_; }
    const ToeEdit& selection() const { return edit_; }
    void select(bool male,bool blue,char side,int digit,int segment) {
        edit_.male=male;edit_.blue=blue;edit_.side=side;edit_.digit=digit;edit_.segment=segment;
    }
    void draw(NativeFrame& f,const SkeletonRoom& room) const {
        if(!open_ || !panelVisible_)return;
        const auto l=layout(f.width,f.height);
        f.blendRectangle(l.x,l.y,l.w,l.h,colorHex("#10212c"),0.88);
        text(f,l,10,5,"TOE BONE EDITOR  |  T: close");
        button(f,l,10,31,90,edit_.male?"MALE *":"MALE");
        button(f,l,105,31,105,!edit_.male?"FEMALE *":"FEMALE");
        button(f,l,218,31,90,!edit_.blue?"WHITE *":"WHITE");
        button(f,l,313,31,85,edit_.blue?"BLUE *":"BLUE");
        button(f,l,10,59,75,edit_.side=='L'?"LEFT *":"LEFT");
        button(f,l,90,59,80,edit_.side=='R'?"RIGHT *":"RIGHT");
        for(int i=1;i<=5;++i) {
            char name[8];std::snprintf(name,sizeof name,"%d%s",i,edit_.digit==i?" *":"");
            button(f,l,178+(i-1)*43,59,40,name);
        }
        button(f,l,10,87,66,edit_.segment==-1?"META *":"META");
        button(f,l,80,87,74,edit_.segment==0?"PROX *":"PROX");
        button(f,l,158,87,70,edit_.segment==1?"MID *":"MID");
        button(f,l,232,87,75,edit_.segment==2?"DIST *":"DIST");
        button(f,l,312,87,80,fine_?"FINE *":"FINE");
        text(f,l,10,116,"ROTATE XYZ");
        axisRow(f,l,140,"X",0);axisRow(f,l,166,"Y",1);axisRow(f,l,192,"Z",2);
        text(f,l,205,116,"MOVE XYZ");
        axisRow(f,l,140,"X",3);axisRow(f,l,166,"Y",4);axisRow(f,l,192,"Z",5);
        char step[96];std::snprintf(step,sizeof step,"Step: %.0f deg / %.1f mm  |  %zu edits",fine_?1.:5.,fine_?.2:1.,room.toeEditCount());
        text(f,l,10,222,step);
        button(f,l,10,248,80,"UNDO");button(f,l,94,248,108,"RESET");
        button(f,l,206,248,65,"SAVE");button(f,l,275,248,65,"LOAD");button(f,l,344,248,70,"STAND");
        button(f,l,10,276,90,"APPLY B");
        text(f,l,110,279,room.originalToePose()?"B: ORIGINAL POSE":"STANDING POSE");
        const auto message=!status_.empty()?status_:!room.toeEditError().empty()?room.toeEditError():
            room.toeEditingAvailable()?"B: original pose   STAND: resting pose":"Press STAND to exit ragdoll.";
        text(f,l,10,305,message);
        text(f,l,10,329,"Reset affects the selected bone.");
    }
    bool handle(const WindowEvent& e,bool captured,int width,int height,SkeletonRoom& room) {
        if(e.type==WindowEvent::Type::KeyDown && e.key=='t' && !e.repeat) {open_=!open_;if(open_)panelVisible_=width>=1000;return true;}
        if(open_ && e.type==WindowEvent::Type::KeyDown && e.key=='h' && !e.repeat){panelVisible_=!panelVisible_;return true;}
        if(!open_ || !panelVisible_ || captured || e.type!=WindowEvent::Type::MouseDown || e.button!=1)return false;
        const auto l=layout(width,height);const int x=int(e.x)-l.x,y=int(e.y)-l.y;
        if(x<0 || x>=l.w || y<0 || y>=l.h)return false;
        status_.clear();
        const auto hit=[&](int bx,int by,int bw){return x>=bx && x<bx+bw && y>=by && y<by+24;};
        if(hit(10,31,90))edit_.male=true;else if(hit(105,31,105))edit_.male=false;
        else if(hit(218,31,90))edit_.blue=false;else if(hit(313,31,85))edit_.blue=true;
        else if(hit(10,59,75))edit_.side='L';else if(hit(90,59,80))edit_.side='R';
        else if(y>=59 && y<83 && x>=178 && x<393) {edit_.digit=(x-178)/43+1;if(edit_.digit==1 && edit_.segment==1)edit_.segment=2;}
        else if(hit(10,87,66))edit_.segment=-1;
        else if(hit(80,87,74))edit_.segment=0;
        else if(hit(158,87,70)) {if(edit_.digit!=1)edit_.segment=1;else status_="The big toe has no middle phalanx.";}
        else if(hit(232,87,75))edit_.segment=2;
        else if(hit(312,87,80))fine_=!fine_;
        else if(y>=140 && y<216) {
            const int row=(y-140)/26, local=(y-140)%26;
            if(row<3 && local<24) {
                int axis=-1,sign=0;
                if(x>=54 && x<92){axis=row;sign=-1;}else if(x>=96 && x<134){axis=row;sign=1;}
                else if(x>=249 && x<287){axis=row+3;sign=-1;}else if(x>=291 && x<329){axis=row+3;sign=1;}
                if(axis>=0) {auto edit=edit_;edit.axis=axis;edit.amount=sign*(axis<3?(fine_?1.:5.):(fine_?.0002:.001));
                    if(!room.editToe(edit))status_="Cannot edit bone (check ragdoll).";}
            }
        } else if(hit(10,248,80)) {if(!room.undoToeEdit())status_="Nothing to undo in this pose.";}
        else if(hit(94,248,108)) {if(!room.resetToeEdits(edit_))status_="Cannot reset during ragdoll.";}
        else if(hit(206,248,65)) status_=room.saveToeEdits()?"Saved toe-bone-edits.txt":"Save failed: check folder permissions.";
        else if(hit(275,248,65)) status_=room.loadToeEdits()?"Loaded toe-bone-edits.txt":"Load failed: check file or ragdoll.";
        else if(hit(344,248,70)) {if(!room.restoreReferencePose())status_="Could not restore standing pose.";}
        else if(hit(10,276,90)) {if(!room.showOriginalToeEdits())status_="Press B first to show original edits.";}
        return true;
    }
private:
    struct Layout {int x,y,w,h;};
    static Layout layout(int w,int h) {return {std::max(0,w-430),std::max(0,(h-354)/2),420,354};}
    static void text(NativeFrame& f,Layout l,int x,int y,const std::string& s) {
        // The Vulkan glyph atlas starts at 20 px; smaller sizes render as empty boxes.
        drawTextPixels(f,l.x+x,l.y+y,s,colorHex("#e0effb"),20);
    }
    static void button(NativeFrame& f,Layout l,int x,int y,int w,const char* s) {
        f.blendRectangle(l.x+x,l.y+y,w,24,colorHex("#44657b"),0.78);text(f,l,x+4,y+3,s);
    }
    static void axisRow(NativeFrame& f,Layout l,int y,const char* name,int axis) {
        const int x=axis<3?10:205;text(f,l,x,y+3,name);
        button(f,l,x+44,y,38,"-");button(f,l,x+86,y,38,"+");
    }
    ToeEdit edit_{};
    bool open_=false,fine_=false,panelVisible_=true;
    std::string status_;
};
} // namespace gtc::native
