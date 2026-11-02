#pragma once

#include "rigid_solver_data.h"
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace gtc::native::gpu_physics {

// The joint graph's connected components do not share writable bodies. Each
// component can therefore execute its coloured joint batches in one workgroup,
// with a workgroup memory/control barrier between colours. Contacts between
// components remain separate global dispatches.
struct IslandPlan {
    std::uint32_t count=0;
    std::vector<std::uint32_t> words;
    std::vector<std::uint32_t> bodyIsland;
};

inline bool makeIslandPlan(std::size_t bodyCount, std::span<const Joint> joints,
                           std::span<const JointBatch> batches, IslandPlan& plan) {
    plan={};
    std::vector<std::uint32_t> parent(bodyCount), component(bodyCount);
    std::iota(parent.begin(),parent.end(),0u);
    const auto root=[&](std::uint32_t i) {
        while (parent[i]!=i) { parent[i]=parent[parent[i]]; i=parent[i]; }
        return i;
    };
    for (const Joint& j:joints) {
        auto a=root(j.bodies[0]), b=root(j.bodies[1]);
        if (a!=b) parent[a]=b;
    }
    std::vector<std::uint32_t> groupOfRoot(bodyCount,~0u);
    for (std::uint32_t i=0;i<bodyCount;++i) {
        auto r=root(i);
        if (groupOfRoot[r]==~0u) groupOfRoot[r]=plan.count++;
        component[i]=groupOfRoot[r];
    }
    // Avoid an enormous table for pathological inputs; callers can retain the
    // ordinary one-dispatch-per-colour implementation instead.
    if (std::uint64_t(plan.count)*(5u+2u*batches.size())>1048576u) { plan={}; return false; }
    plan.bodyIsland=component;
    std::vector<std::vector<std::uint32_t>> bodyLists(plan.count);
    std::vector<std::vector<std::uint32_t>> jointLists(std::size_t(plan.count)*batches.size());
    for (std::uint32_t i=0;i<bodyCount;++i) bodyLists[component[i]].push_back(i);
    for (std::size_t colour=0;colour<batches.size();++colour) {
        const auto& batch=batches[colour];
        for (std::uint32_t j=batch.first;j<batch.first+batch.count;++j)
            jointLists[std::size_t(component[joints[j].bodies[0]])*batches.size()+colour].push_back(j);
    }
    // Each record ends with elapsed time, a wake decision and quiet windows.
    const std::size_t recordWords=5+2*batches.size();
    plan.words.resize(2+std::size_t(plan.count)*recordWords,0);
    plan.words[0]=static_cast<std::uint32_t>(batches.size());
    for (std::size_t island=0;island<plan.count;++island) {
        const std::size_t record=2+island*recordWords;
        plan.words[record]=static_cast<std::uint32_t>(plan.words.size());
        plan.words[record+1]=static_cast<std::uint32_t>(bodyLists[island].size());
        plan.words.insert(plan.words.end(),bodyLists[island].begin(),bodyLists[island].end());
        for (std::size_t colour=0;colour<batches.size();++colour) {
            auto& list=jointLists[island*batches.size()+colour];
            plan.words[record+2+colour*2]=static_cast<std::uint32_t>(plan.words.size());
            plan.words[record+3+colour*2]=static_cast<std::uint32_t>(list.size());
            plan.words.insert(plan.words.end(),list.begin(),list.end());
        }
    }
    // An eight-word reference pose per body tracks real displacement over a
    // full second, rather than mistaking solver jitter for useful motion.
    plan.words[1]=static_cast<std::uint32_t>(plan.words.size());
    plan.words.resize(plan.words.size()+8u*bodyCount+20u,0);
    // Four indirect commands and eight drag-state words follow the poses.
    return true;
}
} // namespace gtc::native::gpu_physics
