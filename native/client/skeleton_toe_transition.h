#pragma once

#include "client/skeleton_metatarsal_plane.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace gtc::native {
namespace toe_transition {
struct Root {
    int owner=-1;
    std::array<Vec3,8> ring{}, rear{};
    Vec3 center{};
    double left=1e30,right=-1e30;
};
template<class Comparison>
std::vector<Root> roots(const Comparison& comparison,const std::string& suffix,Vec3 origin) {
    std::vector<Root> result;
    for(std::size_t i=0;i<comparison.parts().size();++i) {
        const auto& part=comparison.parts()[i];
        if(!part.name.ends_with(suffix) || part.name.rfind("FootProxP",0)!=0) continue;
        auto prism=comparison.toePrism(i,origin);
        if(prism.size()!=16) return {};
        comparison.resizeGreatToe(i,origin,prism);
        Root root;root.owner=static_cast<int>(i);
        for(int k=0;k<8;++k) {
            Vec3 p=(prism[std::size_t(k)]+prism[std::size_t(k+8)])*.5;
            root.ring[std::size_t(k)]=p;root.rear[std::size_t(k)]=prism[std::size_t(k)];root.center+=p/8;
            root.left=std::min(root.left,p.x);root.right=std::max(root.right,p.x);
        }
        result.push_back(root);
    }
    std::sort(result.begin(),result.end(),[](const Root& a,const Root& b){return a.center.x<b.center.x;});
    return result;
}
struct Point { Vec3 p{}; int owner=-1; };
// The two values are the upper and lower outline of the union of the toe
// root rings. In a gap they meet along a semicircular web rather than at a
// square transverse wall.
inline std::array<Point,2> front(const std::vector<Root>& roots,double x,int footOwner) {
    std::array<Point,2> result{};result[0].p.y=-1e30;result[1].p.y=1e30;
    bool found=false;
    for(const auto& root:roots) {
        if(x<root.left-1e-9 || x>root.right+1e-9) continue;
        for(int k=0;k<8;++k) {
            Vec3 a=root.ring[std::size_t(k)],b=root.ring[std::size_t((k+1)%8)];
            if(x<std::min(a.x,b.x)-1e-9 || x>std::max(a.x,b.x)+1e-9) continue;
            const double t=std::abs(b.x-a.x)>1e-12 ? std::clamp((x-a.x)/(b.x-a.x),0.0,1.0) : 0;
            Vec3 p=a+(b-a)*t;
            if(!found || p.y>result[0].p.y) result[0]={p,root.owner};
            if(!found || p.y<result[1].p.y) result[1]={p,root.owner};
            found=true;
        }
    }
    if(found) return result;
    if(x<=roots.front().left || x>=roots.back().right) {
        const Root& root=x<=roots.front().left ? roots.front() : roots.back();
        const double edge=x<=roots.front().left ? root.left : root.right;
        return front(roots,edge,footOwner);
    }
    for(std::size_t i=0;i+1<roots.size();++i) if(x>roots[i].right && x<roots[i+1].left) {
        const auto a=front(roots,roots[i].right,footOwner)[0];
        const auto b=front(roots,roots[i+1].left,footOwner)[0];
        const double gap=roots[i+1].left-roots[i].right,t=(x-roots[i].right)/gap;
        Vec3 p=a.p+(b.p-a.p)*t;
        p.z-=gap*.5*std::sqrt(std::max(0.0,1-(2*t-1)*(2*t-1)));
        return {Point{p,footOwner},Point{p,footOwner}};
    }
    // An overlapping pair has no empty slot; use its nearest ring instead.
    const Root* best=&roots.front();
    for(const auto& root:roots) if(std::abs(root.center.x-x)<std::abs(best->center.x-x)) best=&root;
    return front(roots,std::clamp(x,best->left,best->right),footOwner);
}
inline void add(MetatarsalPlane& mesh,Point a,Point b,Point c) {
    if(length(cross(b.p-a.p,c.p-a.p))<1e-14) return;
    const int base=static_cast<int>(mesh.points.size());
    for(Point p:{a,b,c}) {mesh.points.push_back(p.p);mesh.owners.push_back(p.owner);}
    mesh.faces.push_back({base,base+1,base+2});
}
template<class Back>
void band(MetatarsalPlane& mesh,const std::vector<Root>& roots,std::vector<double> samples,
          double left,double right,int footOwner,bool upper,const Back& back) {
    samples.push_back(left);samples.push_back(right);
    for(const auto& root:roots) for(Vec3 p:root.ring) samples.push_back(p.x);
    for(std::size_t i=0;i+1<roots.size();++i) {
        const double a=roots[i].right,b=roots[i+1].left;
        if(b>a+1e-8) for(int k=1;k<8;++k) samples.push_back(a+(b-a)*k/8.0);
    }
    samples.erase(std::remove_if(samples.begin(),samples.end(),[&](double x){return x<left-1e-8 || x>right+1e-8;}),samples.end());
    std::sort(samples.begin(),samples.end());
    samples.erase(std::unique(samples.begin(),samples.end(),[](double a,double b){return std::abs(a-b)<1e-8;}),samples.end());
    for(std::size_t i=0;i+1<samples.size();++i) {
        const double x=samples[i],y=samples[i+1];
        Point a{back(x),footOwner},b{back(y),footOwner};
        Point c=front(roots,y,footOwner)[upper?0:1],d=front(roots,x,footOwner)[upper?0:1];
        if(upper) {add(mesh,a,b,c);add(mesh,a,c,d);} else {add(mesh,a,c,b);add(mesh,a,d,c);}
    }
}
} // namespace toe_transition
} // namespace gtc::native
