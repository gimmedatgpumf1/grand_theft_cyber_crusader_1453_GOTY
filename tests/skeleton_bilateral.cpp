#include "client/skeleton_bilateral.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
using namespace gtc::native;
int main() {
    // Differently triangulated copies are not required to have matching indices.
    const std::vector<std::array<int,3>> faces{{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    const std::vector<Vec3> templatePoints{{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    std::vector<Vec3> a=templatePoints,b=templatePoints;
    for (Vec3& p:b) p+=Vec3{0.4,0.2,-0.6};
    assert(bilateral::average(a,b,faces));
    for (std::size_t i=0;i<a.size();++i)
        assert(length(a[i]-(templatePoints[i]+Vec3{0.2,0.1,-0.3}))<1e-12);
    // Moving only the opposite side changes the average by half as much.
    std::vector<Vec3> c=templatePoints; for (Vec3& p:b) p.x+=0.2;
    assert(bilateral::average(c,b,faces));
    for (std::size_t i=0;i<a.size();++i) assert(std::abs(c[i].x-a[i].x-0.1)<1e-12);
    // A varying surface changes the result too; centroid-only averaging
    // would incorrectly leave this symmetric-height bulge completely flat.
    std::vector<Vec3> grid,bulged; std::vector<std::array<int,3>> triangles;
    constexpr int n=20;
    for (int y=0;y<n;++y) for (int x=0;x<n;++x) {
        grid.push_back({x*0.01,y*0.01,0});
        bulged.push_back({x*0.01,y*0.01,0.04*std::sin(3.141592653589793*x/(n-1))*std::sin(3.141592653589793*y/(n-1))});
        if (x && y) { const int i=y*n+x; triangles.push_back({i,i-1,i-n}); triangles.push_back({i-1,i-n-1,i-n}); }
    }
    assert(bilateral::average(grid,bulged,triangles));
    assert(grid[(n/2)*n+n/2].z>0.005);
    // The local guard detects real non-adjacent crossings, while not treating
    // triangles sharing a vertex as an intersection defect.
    const std::vector<Vec3> crossing{{-1,-1,0},{1,-1,0},{0,1,0},
                                      {0,-0.5,-1},{0,-0.5,1},{0,0.5,0}};
    const std::vector<std::array<int,3>> crossedFaces{{0,1,2},{3,4,5}};
    assert(bilateral::selfIntersections(crossing,crossedFaces).size()==1);
    const std::vector<std::array<int,3>> neighbours{{0,1,2},{0,4,5}};
    assert(bilateral::selfIntersections(crossing,neighbours).empty());
    // Clip and mirror a closed cube. Seam vertices must be shared; otherwise
    // mirrored sacra can develop a visible crack or an open collision mesh.
    std::vector<Vec3> cube{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                           {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    std::vector<std::array<int,3>> cf{{0,2,1},{0,3,2},{4,5,6},{4,6,7},
        {0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
    assert(bilateral::mirrorAxial(cube,cf,0));
    std::map<std::pair<int,int>,int> edges;
    for (auto f:cf) for (int j=0;j<3;++j) {
        const int a=f[j],b=f[(j+1)%3]; ++edges[{std::min(a,b),std::max(a,b)}];
    }
    for (const auto& edge:edges) assert(edge.second==2);
    bilateral::PointTree cubeTree(cube);
    for (Vec3 p:cube) assert(length(cubeTree.nearest({-p.x,p.y,p.z})-Vec3{-p.x,p.y,p.z})<1e-12);
    std::puts("Bilateral source and closed axial-mesh averaging: PASS");
}
