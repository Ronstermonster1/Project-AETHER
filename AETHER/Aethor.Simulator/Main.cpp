// Main.cpp
// Entry point of the AETHER CPU research simulator.
//
// Current experiment: what should change detection compare against?
//   A: the previous frame.
//   B: the state each region had when it was last rendered.
// Both strategies "re-render" a region (for now: record its current state as its new
// last-rendered state) whenever the detector reports a significant change. We then
// measure how stale each strategy's displayed results really are, i.e. how far the
// last-rendered state is from the current ground truth.
//
// These numbers count regions. They are NOT performance or image-quality results.

#include <algorithm>  // std::max
#include <cstddef>    // std::size_t
#include <cstdint>    // std::uint32_t, std::uint64_t
#include <format>     // std::format (C++20)
#include <iostream>   // std::cout
#include <vector>

#include "ChangeDetector.h"
#include "ProjectInfo.h"
#include "ScenarioGenerator.h"
#include "SceneRegion.h"
#include "SceneState.h"

// Compile-time checks. If either condition is false, the build stops with our message.
static_assert(sizeof(void*) == 8,
    "AETHER must be built as x64. Set the platform dropdown to x64.");
static_assert(_MSVC_LANG >= 202002L,
    "AETHER requires C++20. Set C++ Language Standard to ISO C++20 (/std:c++20).");

namespace
{
    constexpr std::uint64_t kFrameCount = 600;  // 10 seconds at 60 frames per second
    constexpr std::uint64_t kWindowFrames = 60; // one table row per second

    // Statistics for one strategy over one reporting window.
    struct WindowStats
    {
        std::uint64_t updates = 0;      // region re-renders in this window
        std::uint32_t peakUpdates = 0;  // most regions re-rendered in a single frame
        std::uint32_t peakStale = 0;    // most regions stale beyond tolerance in a single frame
        float worstStaleness = 0.0f;    // largest change score between a displayed result and the truth
    };

    // Re-renders every region the detector flagged, then measures true staleness.
    void renderFlaggedRegions(std::vector<aether::SceneRegion>& lastRendered,
                              const std::vector<aether::RegionChange>& changes,
                              const aether::ChangeDetector& detector,
                              const aether::SceneState& previous,
                              const aether::SceneState& current,
                              WindowStats& stats)
    {
        std::uint32_t updates = 0;
        for (std::size_t i = 0; i < changes.size(); ++i)
        {
            if (changes[i].isSignificant())
            {
                lastRendered[i] = current.regions[i];
                ++updates;
            }
        }

        // Staleness: compare what is on screen (the last-rendered state) with the truth.
        std::uint32_t stale = 0;
        for (const aether::RegionChange& staleness : detector.analyse(lastRendered, previous, current))
        {
            if (staleness.isSignificant())
            {
                ++stale;
            }
            stats.worstStaleness = std::max(stats.worstStaleness, staleness.changeScore);
        }

        stats.updates += updates;
        stats.peakUpdates = std::max(stats.peakUpdates, updates);
        stats.peakStale = std::max(stats.peakStale, stale);
    }

    double perFrame(std::uint64_t total)
    {
        return static_cast<double>(total) / static_cast<double>(kWindowFrames);
    }
}

int main()
{
    std::cout << std::format("{} v{}\n\n", aether::kProjectName, aether::kSimulatorVersion);

    const aether::ScenarioConfig scenarioConfig{};
    aether::ScenarioGenerator scenario(scenarioConfig);
    const aether::ChangeDetectorConfig detectorConfig{};
    const aether::ChangeDetector detector(detectorConfig);

    const aether::ScenarioSummary setup = scenario.summary();
    std::cout << std::format("Scenario: {} regions, seed {} ({} moving, {} deforming, {} animated material)\n",
        scenarioConfig.regionCount, scenarioConfig.seed,
        setup.movingRegions, setup.deformingRegions, setup.animatedMaterialRegions);
    std::cout << std::format("Tolerances: {} px movement, {:.0f} % lighting, {:.0f} % material\n\n",
        detectorConfig.displacementTolerancePixels,
        detectorConfig.lightingTolerance * 100.0f,
        detectorConfig.materialTolerance * 100.0f);

    // Frame 0 is fully rendered by both strategies.
    std::vector<aether::SceneRegion> lastRenderedA = scenario.scene().regions;
    std::vector<aether::SceneRegion> lastRenderedB = lastRenderedA;
    aether::SceneState previous = scenario.scene();

    WindowStats windowA{};
    WindowStats windowB{};
    std::uint64_t totalUpdatesA = 0;
    std::uint64_t totalUpdatesB = 0;
    std::uint32_t worstPeakStaleA = 0;
    std::uint32_t worstPeakStaleB = 0;

    std::cout << "             | A: compare with previous frame        | B: compare with last rendered state\n";
    std::cout << "   frames    | updates/frame | max stale | worst   | updates/frame | peak updates | max stale\n";
    std::cout << "-------------+---------------+-----------+---------+---------------+--------------+----------\n";

    for (std::uint64_t frame = 1; frame <= kFrameCount; ++frame)
    {
        scenario.advance();
        const aether::SceneState& current = scenario.scene();

        const std::vector<aether::RegionChange> changesA = detector.analyse(previous.regions, previous, current);
        const std::vector<aether::RegionChange> changesB = detector.analyse(lastRenderedB, previous, current);

        renderFlaggedRegions(lastRenderedA, changesA, detector, previous, current, windowA);
        renderFlaggedRegions(lastRenderedB, changesB, detector, previous, current, windowB);

        if (frame % kWindowFrames == 0)
        {
            std::cout << std::format(" {:4} - {:4} | {:13.1f} | {:9} | {:7.2f} | {:13.1f} | {:12} | {:9}\n",
                frame - kWindowFrames + 1, frame,
                perFrame(windowA.updates), windowA.peakStale, windowA.worstStaleness,
                perFrame(windowB.updates), windowB.peakUpdates, windowB.peakStale);

            totalUpdatesA += windowA.updates;
            totalUpdatesB += windowB.updates;
            worstPeakStaleA = std::max(worstPeakStaleA, windowA.peakStale);
            worstPeakStaleB = std::max(worstPeakStaleB, windowB.peakStale);
            windowA = {};
            windowB = {};
        }

        // Copy-assignment reuses previous's existing memory, so no allocation per frame.
        previous = current;
    }

    const std::uint64_t fullUpdates = static_cast<std::uint64_t>(scenarioConfig.regionCount) * kFrameCount;
    std::cout << std::format("\nRegion updates over {} frames: A = {}, B = {}, re-rendering everything = {}\n",
        kFrameCount, totalUpdatesA, totalUpdatesB, fullUpdates);
    std::cout << std::format("Most regions stale beyond tolerance in a single frame: A = {}, B = {}\n",
        worstPeakStaleA, worstPeakStaleB);
    std::cout << "Note: B is judged with the same measure it uses to decide, so it cannot look stale here.\n";
    std::cout << "Whether that measure matches what a viewer would see is a separate question.\n";
    return 0;
}
