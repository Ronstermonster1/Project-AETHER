// Main.cpp
// Entry point of the AETHER CPU research simulator.
// Currently: builds the default scenario, runs it for 600 frames and reports how much
// of the ground-truth scene changes from one frame to the next.

#include <algorithm>  // std::max
#include <cstddef>    // std::size_t
#include <cstdint>    // std::uint32_t, std::uint64_t
#include <format>     // std::format (C++20)
#include <iostream>   // std::cout

#include "ProjectInfo.h"
#include "ScenarioGenerator.h"
#include "SceneState.h"
#include "Vec3.h"

// Compile-time checks. If either condition is false, the build stops with our message.
static_assert(sizeof(void*) == 8,
    "AETHER must be built as x64. Set the platform dropdown to x64.");
static_assert(_MSVC_LANG >= 202002L,
    "AETHER requires C++20. Set C++ Language Standard to ISO C++20 (/std:c++20).");

namespace
{
    // Exact (bit-for-bit) differences between two consecutive ground-truth frames.
    //
    // Exact comparison is right HERE because the question is "did the scenario touch this
    // value at all?". AETHER's change detector will ask a different question, "did it change
    // enough to matter?", and that needs tolerances.
    struct RawChangeCounts
    {
        std::uint32_t moved = 0;
        std::uint32_t meshChanged = 0;
        std::uint32_t materialChanged = 0;
        std::uint32_t lightingChanged = 0;
        float largestLightingChange = 0.0f;  // biggest change in direct light (length of the RGB difference)
    };

    RawChangeCounts countRawChanges(const aether::SceneState& previous, const aether::SceneState& current)
    {
        RawChangeCounts counts{};
        for (std::size_t i = 0; i < current.regions.size(); ++i)
        {
            const aether::SceneRegion& before = previous.regions[i];
            const aether::SceneRegion& after = current.regions[i];

            if (!aether::exactlyEqual(before.geometry.position, after.geometry.position))
            {
                ++counts.moved;
            }
            if (before.geometry.meshVersion != after.geometry.meshVersion)
            {
                ++counts.meshChanged;
            }
            if (!aether::exactlyEqual(before.material.albedo, after.material.albedo)
                || before.material.roughness != after.material.roughness)
            {
                ++counts.materialChanged;
            }
            if (!aether::exactlyEqual(before.lighting.direct, after.lighting.direct)
                || !aether::exactlyEqual(before.lighting.indirect, after.lighting.indirect))
            {
                ++counts.lightingChanged;
            }

            const float lightingChange = aether::length(after.lighting.direct - before.lighting.direct);
            counts.largestLightingChange = std::max(counts.largestLightingChange, lightingChange);
        }
        return counts;
    }
}

int main()
{
    std::cout << std::format("{} v{}\n\n", aether::kProjectName, aether::kSimulatorVersion);

    const aether::ScenarioConfig config{};  // the defaults: 100 regions, seed 42
    aether::ScenarioGenerator scenario(config);

    const aether::ScenarioSummary setup = scenario.summary();
    float totalCoverage = 0.0f;
    for (const aether::SceneRegion& region : scenario.scene().regions)
    {
        totalCoverage += region.screenCoverage;
    }

    std::cout << std::format("Scenario: {} regions, seed {}\n", config.regionCount, config.seed);
    std::cout << std::format("  moving:            {}\n", setup.movingRegions);
    std::cout << std::format("  deforming:         {}\n", setup.deformingRegions);
    std::cout << std::format("  animated material: {}\n", setup.animatedMaterialRegions);
    std::cout << std::format("  total coverage:    {:.4f}  (1.0 = the whole screen)\n\n", totalCoverage);

    std::cout << "Raw changes since the previous frame (exact comparison of ground truth)\n";
    std::cout << " frame | lamp | moved | mesh | material | lighting | largest light change\n";
    std::cout << "-------+------+-------+------+----------+----------+---------------------\n";

    constexpr std::uint64_t kFrameCount = 600;  // 10 seconds at 60 frames per second
    constexpr std::uint64_t kPrintEvery = 30;

    aether::SceneState previous = scenario.scene();
    for (std::uint64_t frame = 1; frame <= kFrameCount; ++frame)
    {
        scenario.advance();
        const aether::SceneState& current = scenario.scene();

        if (frame % kPrintEvery == 0)
        {
            const RawChangeCounts changes = countRawChanges(previous, current);
            std::cout << std::format(" {:5} | {:>4} | {:5} | {:4} | {:8} | {:8} | {:.6f}\n",
                current.frameIndex,
                scenario.summary().localLightOn ? "on" : "off",
                changes.moved,
                changes.meshChanged,
                changes.materialChanged,
                changes.lightingChanged,
                changes.largestLightingChange);
        }

        // Copy-assignment reuses previous's existing memory, so no allocation per frame.
        previous = current;
    }

    std::cout << std::format("\nMaterial swaps over {} frames: {}\n",
        kFrameCount, scenario.summary().materialSwapsSoFar);
    return 0;
}
