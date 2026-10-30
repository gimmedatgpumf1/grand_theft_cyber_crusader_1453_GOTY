// The loaded OBJ must retain the source-bone partition even when many bones
// share a coarse renderer group. This does not test or claim joint animation.
#include "client/skeleton_body.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <set>
#include <map>
#include <sstream>
#include <queue>
#include <string>
#include <vector>

using namespace gtc::native;

static void verify(const SkeletonModel& model) {
    const auto& groups = model.bones();
    const auto& structures = model.sourceStructures();
    assert(groups.size() == 27 && structures.size() == 182);
    std::vector<std::size_t> pointTotals(groups.size()), faceTotals(groups.size());
    std::set<std::string> names;
    for (std::size_t n = 0; n < structures.size(); ++n) {
        const auto& part = structures[n];
        assert(part.group < groups.size() && names.insert(part.name).second);
        assert(model.sourceStructure(part.name) == static_cast<int>(n));
        assert(part.pointCount > 0 && part.triangleCount > 0);
        assert(part.firstPoint == pointTotals[part.group]);
        assert(part.firstTriangle == faceTotals[part.group]);
        const auto& mesh = groups[part.group];
        assert(part.firstPoint + part.pointCount <= mesh.points.size());
        assert(part.firstTriangle + part.triangleCount <= mesh.triangles.size());
        for (std::size_t i = part.firstTriangle; i < part.firstTriangle + part.triangleCount; ++i)
            for (int vertex : mesh.triangles[i])
                assert(vertex >= 0 && static_cast<std::size_t>(vertex) >= part.firstPoint &&
                       static_cast<std::size_t>(vertex) < part.firstPoint + part.pointCount);
        pointTotals[part.group] += part.pointCount;
        faceTotals[part.group] += part.triangleCount;
    }
    for (std::size_t i = 0; i < groups.size(); ++i) {
        assert(pointTotals[i] == groups[i].points.size());
        assert(faceTotals[i] == groups[i].triangles.size());
    }
    for (const char* side : {"_L", "_R"}) {
        for (int digit = 1; digit <= 5; ++digit) {
            for (const char* prefix : {"HandMeta", "HandProxP", "HandDistP", "FootMeta", "FootProxP", "FootDistP"})
                assert(names.count(std::string(prefix) + std::to_string(digit) + side));
            if (digit != 1)
                for (const char* prefix : {"HandMidP", "FootMidP"})
                    assert(names.count(std::string(prefix) + std::to_string(digit) + side));
        }
    }
    assert(model.sourceStructure("not a source structure") == -1);
    const auto& links = model.sourceLinks();
    assert(links.size() == 209);
    std::map<std::pair<std::string,std::string>, std::string> connections;
    for (const auto& link : links) {
        assert(link.first < structures.size() && link.second < structures.size() && link.first != link.second);
        std::string a = structures[link.first].name, b = structures[link.second].name;
        if (a > b) std::swap(a,b);
        assert(connections.emplace(std::make_pair(a,b), link.mobility).second);
    }
    std::vector<std::vector<std::size_t>> neighbours(structures.size());
    for (const auto& link : links) {
        neighbours[link.first].push_back(link.second);
        neighbours[link.second].push_back(link.first);
    }
    std::vector<bool> visited(structures.size());
    std::queue<std::size_t> queue;
    queue.push(0); visited[0] = true;
    std::size_t reached = 0;
    while (!queue.empty()) {
        const auto node = queue.front(); queue.pop(); ++reached;
        for (const auto next : neighbours[node])
            if (!visited[next]) { visited[next] = true; queue.push(next); }
    }
    assert(reached == 182 && links.size() - reached + 1 == 28);
    auto connected = [&](std::string a, std::string b, const std::string& type) {
        if (a > b) std::swap(a,b);
        const auto it = connections.find({a,b});
        assert(it != connections.end() && it->second == type);
    };
    for (const char* side : {"_L", "_R"}) {
        for (int digit = 1; digit <= 5; ++digit) {
            const auto suffix = std::to_string(digit) + side;
            connected("HandMeta" + suffix, "HandProxP" + suffix, digit == 1 ? "thumb_mcp" : "finger_mcp_condyloid");
            if (digit == 1) connected("HandProxP" + suffix, "HandDistP" + suffix, "thumb_ip_hinge");
            else {
                connected("HandProxP" + suffix, "HandMidP" + suffix, "finger_pip_hinge");
                connected("HandMidP" + suffix, "HandDistP" + suffix, "finger_dip_hinge");
            }
        }
    }
    std::ifstream input("tools/skeleton-source/skeleton-joint-links.txt");
    std::string row;
    std::size_t count = 0;
    while (std::getline(input,row)) {
        if (row.empty() || row[0] == '#') continue;
        std::istringstream fields(row);
        std::string kind, a, b, type;
        fields >> kind >> a >> b >> type;
        assert(fields && count < links.size());
        const auto& actual = links[count++];
        assert(actual.kind == kind && actual.mobility == type &&
               structures[actual.first].name == a && structures[actual.second].name == b);
    }
    assert(count == links.size());
}

int main() {
    SkeletonModel model;
    std::string error;
    assert(model.load("tools/skeleton-source/skeleton-male.obj", error));
    assert(model.loadSourceLinks("tools/skeleton-source/skeleton-joint-links.txt", error));
    verify(model);
    // Failed reload is transactional: it must not replace the valid graph.
    { std::ofstream bad("build/invalid-joint-links.txt"); bad << "joint Head ImaginaryBone elbow_hinge\n"; }
    assert(!model.loadSourceLinks("build/invalid-joint-links.txt", error));
    assert(model.sourceLinks().size() == 209);
    std::remove("build/invalid-joint-links.txt");
    // Reload the same instance, ensuring no source ranges from the first model
    // survive; the female has different source geometry and face counts.
    assert(model.load("tools/skeleton-source/skeleton-female.obj", error));
    assert(model.sourceLinks().empty());
    assert(model.loadSourceLinks("tools/skeleton-source/skeleton-joint-links.txt", error));
    verify(model);
    std::puts("OpenSkeleton runtime source ranges and link graph: PASS (182 structures, 209 links each; no joint rig implied)");
}
