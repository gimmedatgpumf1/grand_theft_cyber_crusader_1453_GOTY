#pragma once

#include "client/skeleton_room.h"
#include "client/window.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace gtc::native {

// A momentary pointer interaction, not a camera or gameplay input mode. The
// mouse is released with Esc and recaptured by clicking outside this panel.
class SkeletonComparisonUi {
public:
    void draw(NativeFrame& frame, const SkeletonRoom& room) const {
        const Layout l=layout(frame.width,frame.height);
        if (!open_) {
            const int x=(frame.width-330)/2, y=std::max(0,frame.height-37);
            frame.blendRectangle(x,y,330,29,colorHex("#142632"),0.78);
            drawTextPixels(frame,x+10,y+4,"P: sliders   Esc: cursor",colorHex("#d4e9ff"),20);
            return;
        }
        frame.blendRectangle(l.x,l.y,l.w,l.h,colorHex("#10212c"),0.83);
        drawTextPixels(frame,l.x+10,l.y+3,"BLUE: VISUAL PROXY",colorHex("#9dceff"),20);
        if (l.wide) drawTextPixels(frame,l.x+260,l.y+3,"Esc: cursor   P: hide   click outside: look",colorHex("#c2cfda"),20);
        button(frame,l.x+10,l.y+30,85,male_?"MALE *":"MALE");
        button(frame,l.x+100,l.y+30,110,!male_?"FEMALE *":"FEMALE");
        button(frame,l.x+l.w-84,l.y+30,74,"RESET");
        const auto& c=room.comparison(male_);
        static const char* names[]={"Height","Head","Neck","Torso","Arm","Leg","Hip width","Shoulder"};
        for (int i=0;i<SkeletonComparison::Count;++i) {
            if (l.compact && i/4!=page_) continue;
            const int col=l.wide ? i/4 : 0, row=(l.wide || l.compact) ? i%4 : i;
            const int left=l.x+col*l.colWidth, y=l.y+59+row*27;
            drawTextPixels(frame,left+10,y+2,names[i],colorHex("#e6edf2"),20);
            const auto& p=c.profile();
            const double fraction=i==0 ? (p.height-40)/190 : (p.multiplier[i]-0.5);
            const int start=left+130, width=l.colWidth-230;
            frame.blendRectangle(start,y+10,width,4,colorHex("#63839c"),0.8);
            const int x=start+static_cast<int>(std::clamp(fraction,0.0,1.0)*width);
            frame.rectangle(x-3,y+4,7,17,colorHex("#53aaff"));
            button(frame,left+103,y,22,"-"); button(frame,left+l.colWidth-94,y,22,"+");
            char value[32]; const double v=i==0?p.height:c.targets()[i];
            std::snprintf(value,sizeof value,(v<10 || v>=1000)?"%.4g":"%.1f",v);
            drawTextPixels(frame,left+l.colWidth-68,y+2,value,colorHex("#e8f5ff"),20);
        }
        const std::string text=l.compact ?
            (std::string(c.exact()?"cm | ":"Partial fit | ")+(page_==0?"More sliders >":"First sliders >")) :
            !status_.empty()?status_ : !c.exact()?"Partial fit: values are fitted cm.":
            (l.wide ? "cm  |  Surface targets; skeletal proxies." : "cm  |  Esc: cursor   P: hide");
        drawTextPixels(frame,l.x+10,l.y+l.h-24,text,colorHex("#c0d7e7"),20);
    }

    bool handle(const WindowEvent& e, bool captured, int width, int height, SkeletonRoom& room) {
        if (e.type==WindowEvent::Type::FocusLoss) { dragging_=-1; return false; }
        if (e.type==WindowEvent::Type::KeyDown && e.key=='p' && !e.repeat) {
            open_=!open_; dragging_=-1; return true;
        }
        if (captured) { dragging_=-1; return false; }
        const Layout l=layout(width,height);
        if (e.type==WindowEvent::Type::MouseUp && e.button==1 && dragging_>=0) {
            dragging_=-1; return true;
        }
        if (e.type==WindowEvent::Type::MouseMove && dragging_>=0) {
            change(room,l,dragging_,e.x,0); return true;
        }
        if (e.type!=WindowEvent::Type::MouseDown || e.button!=1) return false;
        if (!open_) {
            const int x=(width-330)/2, y=std::max(0,height-37);
            if (e.x>=x && e.x<x+330 && e.y>=y && e.y<y+29) { open_=true; return true; }
            return false;
        }
        if (e.x<l.x || e.x>=l.x+l.w || e.y<l.y || e.y>=l.y+l.h) return false;
        const double x=e.x-l.x,y=e.y-l.y;
        if (y>=30 && y<55) {
            if (x>=10 && x<95) { male_=true; status_.clear(); }
            else if (x>=100 && x<210) { male_=false; status_.clear(); }
            else if (x>=l.w-84) { room.resetComparison(male_); status_.clear(); }
            return true;
        }
        if (l.compact && y>=l.h-26) { page_=1-page_; status_.clear(); return true; }
        if (y>=59) {
            const int column=l.wide && x>=l.colWidth ? 1 : 0;
            const double localX=x-column*l.colWidth;
            const int line=static_cast<int>((y-59)/27);
            const int row=l.wide ? column*4+line : l.compact ? page_*4+line : line;
            if (line<((l.wide || l.compact)?4:8) && row<SkeletonComparison::Count) {
                if (localX>=103 && localX<125) change(room,l,row,e.x,-1);
                else if (localX>=l.colWidth-94 && localX<l.colWidth-72) change(room,l,row,e.x,1);
                else if (localX>=130 && localX<=l.colWidth-100) { dragging_=row; change(room,l,row,e.x,0); }
            }
        }
        return true;
    }
private:
    struct Layout { int x,y,w,h,colWidth; bool wide,compact; };
    static Layout layout(int width,int height) {
        const bool wide=width>=760, compact=!wide && height<330;
        const int w=std::max(300,std::min(wide?940:440,width-16)), h=(wide||compact)?195:303;
        return {(width-w)/2,std::max(4,height-h-12),w,h,wide?w/2:w,wide,compact};
    }
    static void button(NativeFrame& f,int x,int y,int w,const char* text) {
        f.blendRectangle(x,y,w,24,colorHex("#44657b"),0.72);
        drawTextPixels(f,x+4,y+2,text,colorHex("#e0effb"),20);
    }
    void change(SkeletonRoom& room,const Layout& l,int row,double x,int step) {
        auto profile=room.comparison(male_).profile();
        if (step) {
            if (row==0) profile.height+=step;
            else profile.multiplier[row]+=step*0.02;
        } else {
            const int column=l.wide ? row/4 : 0;
            const double start=l.x+column*l.colWidth+130;
            const double fraction=std::clamp((x-start)/(l.colWidth-230),0.0,1.0);
            if (row==0) profile.height=40+190*fraction;
            else profile.multiplier[row]=0.5+fraction;
        }
        std::string error;
        if (!room.setComparison(male_,profile,error)) status_="Cannot fit; shape unchanged.";
        else status_.clear();
    }
    bool male_=true,open_=false;
    int dragging_=-1, page_=0;
    std::string status_;
};
} // namespace gtc::native
