#pragma once

#include "client/skeleton_metatarsal_registration.h"
#include "client/skeleton_foot_joint_clearance.h"
#include <limits>

namespace gtc::native {
// Preserve foot shape as stature changes, while joining the ankle to the
// separately scaled shin. The two landmarks are in the comparison frame.
template<class Part>
bool comparisonFootLandmarks(const std::vector<Part>& parts,double floor,double& ankle,double& knee) {
    const Part *tibia=nullptr,*talus=nullptr;
    for (const auto& p:parts) {if(p.name=="Tibia_L") tibia=&p; if(p.name=="Tal_L") talus=&p;}
    if (!tibia || !talus || tibia->points.empty() || talus->points.empty()) return false;
    knee=-1e30; for (Vec3 p:tibia->points) knee=std::max(knee,p.y);
    const auto ts=std::max<std::size_t>(1,tibia->points.size()/192),as=std::max<std::size_t>(1,talus->points.size()/192);
    double best=1e30; ankle=0;
    for (std::size_t i=0;i<tibia->points.size();i+=ts) for(std::size_t j=0;j<talus->points.size();j+=as) {
        Vec3 p=tibia->points[i],q=talus->points[j],d=p-q;double v=dot(d,d);
        if(v<best){best=v;ankle=(p.y+q.y)*.5;}
    }
    return knee>ankle && ankle>floor;
}
template<class MapY>
double comparisonFootHeight(const std::string& n,Vec3 p,double mapped,double floor,double ankle,double knee,double scale,MapY mapY) {
    const bool foot=n.rfind("Foot",0)==0 || n.rfind("Tal_",0)==0 || n.rfind("Cal_",0)==0 ||
        n.rfind("Nav_",0)==0 || n.rfind("Cub_",0)==0 || n.rfind("MedCun_",0)==0 ||
        n.rfind("InterCun_",0)==0 || n.rfind("LatCun_",0)==0;
    if (foot) return mapY(floor)+(p.y-floor)*scale;
    if (n.rfind("Tibia_",0)==0 || n.rfind("Fibula_",0)==0) {
        const double delta=mapY(floor)+(ankle-floor)*scale-mapY(ankle);
        return mapped+delta*std::clamp((knee-p.y)/(knee-ankle),0.0,1.0);
    }
    return mapped;
}

// A constrained approximation of a neutral loaded foot, not a reconstruction
// of the original subject's weight-bearing CT. Reference declinations vary by
// measurement method and individual, so these guide rather than force a fit.
// https://musculoskeletalkey.com/anatomy-and-biomechanics-of-the-foot-and-ankle/
// Pivot the midfoot at its measured joints, then fit each metatarsal about
// its tarsal contact. Sesamoids follow the first ray; toes rotate at the MTP.
template<class Part, class Map>
bool fitStandingComparisonFeet(std::vector<Part>& parts, std::vector<JointRigidTransform>& frames, Map map) {
    auto find=[&](const std::string& name)->Part* {
        auto it=std::find_if(parts.begin(),parts.end(),[&](const Part& p){return p.name==name;});
        return it==parts.end() ? nullptr : &*it;
    };
    const auto point=[](const JointRigidTransform& t,Vec3 p) {return frameVector(t,p)+t.translation;};
    const auto rotation=[](Vec3 pivot,double angle) {
        auto t=joint_detail::rotate({1,0,0},angle);
        t.translation=pivot-frameVector(t,pivot); return t;
    };
    const auto contact=[](const Part& a,const Part& b) {
        Vec3 result{}; double best=1e100;
        const auto as=std::max<std::size_t>(1,a.points.size()/192),bs=std::max<std::size_t>(1,b.points.size()/192);
        for (std::size_t i=0;i<a.points.size();i+=as) for (std::size_t j=0;j<b.points.size();j+=bs) {
            Vec3 d=a.points[i]-b.points[j]; double n=dot(d,d);
            if (n<best) {best=n;result=(a.points[i]+b.points[j])*.5;}
        }
        return result;
    };
    const auto closest=[](const Part& a,const Part& b) {
        Vec3 result{};double best=1e100;
        for(Vec3 x:a.points) for(Vec3 y:b.points) {
            const Vec3 d=x-y;const double n=dot(d,d);
            if(n<best) {best=n;result=d;}
        }
        return result;
    };
    const auto apply=[&](Part* p,const JointRigidTransform& t) {
        if (!p) return;
        const std::size_t i=static_cast<std::size_t>(p-parts.data());
        frames[i]=joint_detail::compose(t,frames[i]);
        for (Vec3& v:p->points) v=point(t,v);
    };
    constexpr double pi=3.14159265358979323846;
    // A WBCT control group reported first-metatarsal declination 24.7°;
    // another reported a first/second ratio of 0.87. Third/fourth values are
    // interpolated, and fifth is an approximate textbook value.
    // https://pmc.ncbi.nlm.nih.gov/articles/PMC13053976/
    // https://link.springer.com/article/10.1007/s00402-021-04168-5
    const std::array<double,5> target{-24.7*pi/180,-28*pi/180,-20*pi/180,-12*pi/180,-5*pi/180};
    for (const std::string suffix:{"_L","_R"}) {
        std::array<Part*,5> meta{},tarsal{};
        std::array<Vec3,5> axes{},centers{},pivots{};
        std::array<std::vector<Vec3>,5> supportPoints{};
        Part* nav=find("Nav"+suffix), *talus=find("Tal"+suffix), *cub=find("Cub"+suffix), *heel=find("Cal"+suffix), *tibia=find("Tibia"+suffix);
        if (!nav || !talus || !cub || !heel || !tibia) return false;
        for (int i=0;i<5;++i) {
            const std::string n=std::to_string(i+1);
            meta[i]=find("FootMeta"+n+suffix);
            tarsal[i]=find(std::string(i==0?"MedCun":i==1?"InterCun":i==2?"LatCun":"Cub")+suffix);
            if (!meta[i] || !tarsal[i] || meta[i]->points.empty() || tarsal[i]->points.empty()) return false;
            for (Vec3 p:meta[i]->points) centers[i]+=p;
            centers[i]=centers[i]/double(meta[i]->points.size());
            // Bisect the actual projected shaft outline at two cross sections.
            // Vertex centroids depend on how densely an OBJ happens to be
            // tessellated and can bias the estimated declination by degrees.
            double s0=1e100,s1=-1e100;
            for (Vec3 p:meta[i]->points) {s0=std::min(s0,p.z);s1=std::max(s1,p.z);}
            if (s1-s0<1e-9) return false;
            const auto bisect=[&](double fraction) {
                const double z=s0+(s1-s0)*fraction;double low=1e100,high=-1e100;
                for (const auto& face:meta[i]->faces) for (int edge=0;edge<3;++edge) {
                    Vec3 a=meta[i]->points[face[edge]],b=meta[i]->points[face[(edge+1)%3]];
                    if ((a.z<z)==(b.z<z) || std::abs(b.z-a.z)<1e-12) continue;
                    const double y=a.y+(b.y-a.y)*(z-a.z)/(b.z-a.z);
                    low=std::min(low,y);high=std::max(high,y);
                }
                return low<=high ? (low+high)*.5 : std::numeric_limits<double>::quiet_NaN();
            };
            const double a=bisect(.35),b=bisect(.65);
            if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a)>1e20 || std::abs(b)>1e20) return false;
            axes[i]=normalized(Vec3{0,b-a,(s1-s0)*.3});
            pivots[i]=contact(*meta[i],*tarsal[i]);
            double z0=1e100,z1=-1e100;
            for (Vec3 p:meta[i]->points) {p=map(p,*meta[i]);z0=std::min(z0,p.z);z1=std::max(z1,p.z);}
            for (Vec3 p:meta[i]->points) {
                const Vec3 q=map(p,*meta[i]);
                if (q.z>=z0+.65*(z1-z0)) supportPoints[i].push_back(p);
            }
            if (supportPoints[i].empty()) return false;
        }
        // Approximate loaded heel and forefoot pad thicknesses. These are
        // population estimates, not measurements of the source subject.
        // https://www.jfasap.com/doi/10.5005/jp-journals-10040-1246
        // https://onlinelibrary.wiley.com/doi/full/10.1002/jor.21328
        // The first ray bears through the sesamoids, not its metatarsal
        // head. Keep the actual points so support is recomputed when the ray
        // turns instead of freezing its original lowest vertex.
        supportPoints[0].clear();
        for (const char* name:{"FootSesaMed","FootSesaLat"}) {
            const Part* bone=find(std::string(name)+suffix); if (!bone || bone->points.empty()) return false;
            supportPoints[0].insert(supportPoints[0].end(),bone->points.begin(),bone->points.end());
        }
        // Only vertices on the lower sagittal convex hull can become the
        // support point under the bounded pitch rotations used below.
        for (auto& points:supportPoints) {
            std::sort(points.begin(),points.end(),[](Vec3 a,Vec3 b) {return a.z==b.z?a.y<b.y:a.z<b.z;});
            std::vector<Vec3> hull;
            for (Vec3 p:points) {
                while (hull.size()>=2) {
                    const Vec3 a=hull[hull.size()-2],b=hull.back();
                    if ((b.z-a.z)*(p.y-a.y)-(b.y-a.y)*(p.z-a.z)>0) break;
                    hull.pop_back();
                }
                hull.push_back(p);
            }
            points=std::move(hull);
        }
        double foreMin=1e100,foreMax=-1e100;
        for (Vec3 p:meta[0]->points) {p=map(p,*meta[0]);foreMin=std::min(foreMin,p.z);foreMax=std::max(foreMax,p.z);}
        const double padDifference=.004*std::clamp((foreMax-foreMin)/.065,.5,2.0);
        // Approximate lateral radiographic landmarks from the bone outlines:
        // talar body/neck midpoints and the plantar calcaneal tuberosity to
        // the inferior calcaneocuboid surface. A mesh is not a radiograph, so
        // these are soft guides, not clinical measurements.
        // https://www.nature.com/articles/s41598-022-16995-6
        double tz0=1e100,tz1=-1e100,hz0=1e100,hz1=-1e100;
        for (Vec3 p:talus->points) {tz0=std::min(tz0,p.z);tz1=std::max(tz1,p.z);}
        for (Vec3 p:heel->points) {hz0=std::min(hz0,p.z);hz1=std::max(hz1,p.z);}
        if (tz1-tz0<1e-9 || hz1-hz0<1e-9) return false;
        const auto talarMiddle=[&](double fraction) {
            const double z=tz0+fraction*(tz1-tz0);double low=1e100,high=-1e100;
            for (const auto& face:talus->faces) for(int e=0;e<3;++e) {
                const Vec3 a=talus->points[face[e]],b=talus->points[face[(e+1)%3]];
                if ((a.z<z)==(b.z<z) || std::abs(b.z-a.z)<1e-12) continue;
                const double y=a.y+(b.y-a.y)*(z-a.z)/(b.z-a.z);
                low=std::min(low,y); high=std::max(high,y);
            }
            return low<=high ? (low+high)*.5 : std::numeric_limits<double>::quiet_NaN();
        };
        const double body=talarMiddle(.50),neck=talarMiddle(.75);
        if (!std::isfinite(body) || !std::isfinite(neck)) return false;
        const Vec3 talarCenter{0,body,tz0+.50*(tz1-tz0)};
        const Vec3 talarAxis=normalized(Vec3{0,neck-body,.25*(tz1-tz0)});
        const Vec3 ankleContact=contact(*talus,*tibia),subtalar=contact(*talus,*heel);
        const Vec3 navJoint=contact(*nav,*talus),cuneiformJoint=contact(*nav,*tarsal[1]);
        const Vec3 cubJoint=contact(*cub,*heel),crossJoint=contact(*cub,*tarsal[2]);
        const double originalCrossGap=length(closest(*tarsal[2],*cub));
        using Transform=JointRigidTransform;
        struct Chain { Transform heel,talus,nav,cuneiform,cuboid; };
        const auto chain=[&](const std::array<double,5>& angles) {
            Chain c;
            c.heel=rotation(ankleContact,angles[0]);
            c.talus=joint_detail::compose(rotation(point(c.heel,subtalar),angles[1]),c.heel);
            c.nav=joint_detail::compose(rotation(point(c.talus,navJoint),angles[2]),c.talus);
            c.cuneiform=joint_detail::compose(rotation(point(c.nav,cuneiformJoint),angles[3]),c.nav);
            c.cuboid=joint_detail::compose(rotation(point(c.heel,cubJoint),angles[4]),c.heel);
            return c;
        };
        const auto heelHeight=[&](const Transform& t) {
            double y=1e100;
            for (Vec3 p:heel->points) y=std::min(y,map(point(t,p),*heel).y);
            return y;
        };
        const auto heelPitch=[&](const Transform& t) {
            Vec3 back{},front{};double low=1e100,high=1e100,z0=1e100,z1=-1e100;
            for (Vec3 p:heel->points) {const Vec3 q=map(point(t,p),*heel);if(q.y<low){low=q.y;back=q;}z0=std::min(z0,q.z);z1=std::max(z1,q.z);}
            for (Vec3 p:heel->points) {const Vec3 q=map(point(t,p),*heel);if(q.z>=z0+.95*(z1-z0) && q.y<high){high=q.y;front=q;}}
            return std::atan2(front.y-back.y,front.z-back.z);
        };
        const auto headHeight=[&](int i,const Transform& t) {
            double y=1e100;
            for (Vec3 p:supportPoints[i]) y=std::min(y,map(point(t,p),*meta[i]).y);
            return y;
        };
        // Limit each local rotation rather than flattening or scaling the
        // metatarsals. Mean excursions are only regularising bounds here.
        constexpr double limits[5]={3.5,0.6,1.6,9.6,10.2};
        const auto metaTurn=[&](int i,const Transform& parent,double ground) {
            const Vec3 center=point(parent,centers[i]),axis=frameVector(parent,axes[i]),pivot=point(parent,pivots[i]);
            const double limit=limits[i]*pi/180;
            const auto cost=[&](double radians) {
                const auto t=rotation(pivot,radians);
                const Vec3 a=map(point(t,center),*meta[i]),b=map(point(t,center+axis*.02),*meta[i]);
                const double angle=(std::atan2(b.y-a.y,b.z-a.z)-target[i])*.09;
                const double goal=ground-padDifference;
                const double height=headHeight(i,joint_detail::compose(t,parent))-goal;
                return angle*angle+height*height;
            };
            double lo=-limit,hi=limit;
            for (int k=0;k<28;++k) {double a=(2*lo+hi)/3,b=(lo+2*hi)/3;if(cost(a)<cost(b))hi=b;else lo=a;}
            return rotation(pivot,(lo+hi)*.5);
        };
        const auto cost=[&](const std::array<double,5>& angles) {
            const Chain c=chain(angles); const double ground=heelHeight(c.heel); double sum=0;
            for (int i=0;i<5;++i) {
                const auto parent=i<3?c.cuneiform:c.cuboid;
                const auto turn=metaTurn(i,parent,ground);
                const Vec3 center=point(parent,centers[i]),axis=frameVector(parent,axes[i]);
                const Vec3 a=map(point(turn,center),*meta[i]),b=map(point(turn,center+axis*.02),*meta[i]);
                const double angle=(std::atan2(b.y-a.y,b.z-a.z)-target[i])*.09;
                const double goal=ground-padDifference;
                const double height=headHeight(i,joint_detail::compose(turn,parent))-goal;
                sum+=angle*angle+height*height;
            }
            const double pitch=heelPitch(c.heel);
            const double pitchError=pitch-std::clamp(pitch,18*pi/180,30*pi/180);
            sum+=pitchError*pitchError*.25*.25;
            const Vec3 ta=map(point(c.talus,talarCenter),*talus),tb=map(point(c.talus,talarCenter+talarAxis*.02),*talus);
            const auto ray=joint_detail::compose(metaTurn(0,c.cuneiform,ground),c.cuneiform);
            const Vec3 ma=map(point(ray,centers[0]),*meta[0]),mb=map(point(ray,centers[0]+axes[0]*.02),*meta[0]);
            const double alignment=std::atan2(tb.y-ta.y,tb.z-ta.z)-std::atan2(mb.y-ma.y,mb.z-ma.z);
            const double archError=alignment-std::clamp(alignment,-4*pi/180,4*pi/180);
            sum+=archError*archError*.06*.06;
            // Limit separation where the medial and lateral columns meet.
            const Vec3 gap=point(c.cuneiform,crossJoint)-point(c.cuboid,crossJoint);
            sum+=dot(gap,gap)*.5;
            for (double a:angles) sum+=.000004*a*a;
            return sum;
        };
        // Ankle pitch lowers the forefoot while allowing the arch joints to
        // declinate the rays. Optimize all joints together so the heel and
        // both columns stay connected, instead of rotating rays in isolation.
        const std::array<double,5> bounds{14*pi/180,6*pi/180,7*pi/180,5*pi/180,2.3*pi/180};
        std::array<double,5> angles{};double value=cost(angles);
        for (double step=4*pi/180;step>pi/720;step*=.5) {
            for (int pass=0;pass<8;++pass) {
                bool changed=false;
                for (int i=0;i<5;++i) for (int sign:{-1,1}) {
                    auto trial=angles;trial[i]=std::clamp(trial[i]+sign*step,-bounds[i],bounds[i]);
                    const double next=cost(trial);
                    if(next<value) {angles=trial;value=next;changed=true;}
                }
                if (!changed) break;
            }
        }
        const Chain c=chain(angles);const double ground=heelHeight(c.heel);
        // Resolve pivots and turns before mutating any of the source meshes.
        std::array<Transform,5> full{};
        for(int i=0;i<5;++i) {
            const auto parent=i<3?c.cuneiform:c.cuboid;
            full[i]=joint_detail::compose(metaTurn(i,parent,ground),parent);
        }
        apply(heel,c.heel);apply(talus,c.talus);apply(nav,c.nav);
        for(int i=0;i<3;++i) apply(tarsal[i],c.cuneiform);
        apply(cub,c.cuboid);
        for(int i=0;i<5;++i) {
            const std::string n=std::to_string(i+1);
            const auto& ray=full[i];
            Part* proximal=find("FootProxP"+n+suffix);
            if (!proximal) return false;
            const Vec3 mtp=point(ray,contact(*meta[i],*proximal));
            // Keep the distal foot's original resting direction as the ray
            // declinates. A small MTP rotation carries the intact toe rather
            // than pitching every phalanx up with the metatarsal.
            const double rayAngle=std::atan2(ray.rotation[7],ray.rotation[4]);
            const auto toe=joint_detail::compose(rotation(mtp,std::clamp(-rayAngle,-12*pi/180,12*pi/180)),ray);
            apply(meta[i],ray);
            for (const char* prefix:{"FootProxP","FootMidP","FootDistP"}) apply(find(std::string(prefix)+n+suffix),toe);
            if(i==0) for(const char* name:{"FootSesaMed","FootSesaLat"}) apply(find(std::string(name)+suffix),ray);
        }
        // The source CT can contain intersecting surfaces, and a rotation
        // about a single closest point can introduce further overlap. Resolve
        // only actual triangle crossings, translating entire distal branches
        // by the smallest bounded amount; preserve each bone and toe frame.
        const auto center=[](const Part& p) {
            Vec3 c{}; for(Vec3 v:p.points) c+=v; return c/double(p.points.size());
        };
        const auto move=[&](Part* p,Vec3 offset) {
            auto t=rotation({},0);t.translation=offset;apply(p,t);
        };
        const auto moveRay=[&](int i,Vec3 offset) {
            move(meta[i],offset);const std::string n=std::to_string(i+1);
            for(const char* prefix:{"FootProxP","FootMidP","FootDistP"}) move(find(std::string(prefix)+n+suffix),offset);
            if(i==0) for(const char* name:{"FootSesaMed","FootSesaLat"}) move(find(std::string(name)+suffix),offset);
        };
        const double limit=.002*std::clamp((foreMax-foreMin)/.065,.5,2.0);
        const Vec3 tn=foot_joint_clearance::separation<Part>({{talus,nav}},center(*nav)-center(*talus),limit);
        if(length(tn)>0) {
            move(nav,tn);for(int i=0;i<3;++i) {move(tarsal[i],tn);moveRay(i,tn);}
        }
        const Vec3 nc=foot_joint_clearance::separation<Part>({{nav,tarsal[0]},{nav,tarsal[1]},{nav,tarsal[2]}},
                                                              center(*tarsal[1])-center(*nav),limit);
        if(length(nc)>0) for(int i=0;i<3;++i) {move(tarsal[i],nc);moveRay(i,nc);}
        // Keep the lateral column near its original connection to the
        // cuneiforms after medial joint clearance, without moving it through
        // the heel or navicular. Carry rays four and five with the cuboid.
        const Vec3 crossGap=closest(*tarsal[2],*cub);
        const double excess=length(crossGap)-originalCrossGap;
        if(excess>0 && length(crossGap)>1e-10) {
            const Vec3 desired=normalized(crossGap)*std::min(excess,limit*.7);
            for(double f:{1.0,.75,.5,.25}) {
                const Vec3 step=desired*f;
                if(foot_joint_clearance::clear<Part>({{heel,cub},{nav,cub},{tarsal[2],cub}},step)) {
                    move(cub,step);moveRay(3,step);moveRay(4,step);break;
                }
            }
        }
        for(int i=0;i<5;++i) {
            const Vec3 tm=foot_joint_clearance::separation<Part>({{tarsal[i],meta[i]}},center(*meta[i])-center(*tarsal[i]),limit);
            if(length(tm)>0) moveRay(i,tm);
        }
    }
    return true;
}
} // namespace gtc::native
