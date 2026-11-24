// Main.cpp
// Entry point of the AETHER CPU research simulator.
//
// Runs research experiments by name:
//   Aether.Simulator.exe              runs every experiment
//   Aether.Simulator.exe reference    runs only the "reference" experiment
// Earlier experiments stay runnable, so every result we have reported can be reproduced.
//
// The numbers printed here count regions or describe the simulation model.
// They are NOT performance or image-quality measurements.

#include <algorithm>    // std::max, std::min, std::ranges::sort
#include <array>
#include <cmath>        // std::sqrt
#include <cstddef>      // std::size_t
#include <cstdint>      // std::uint32_t, std::uint64_t
#include <format>       // std::format (C++20)
#include <functional>   // std::greater
#include <iostream>     // std::cout
#include <limits>
#include <string_view>
#include <vector>

#include "ChangeDetector.h"
#include "CostEstimator.h"
#include "GroundTruthEvaluator.h"
#include "ImportanceEstimator.h"
#include "ProjectInfo.h"
#include "ScenarioGenerator.h"
#include "SceneState.h"
#include "ScreenSpace.h"
#include "TemporalStore.h"
#include "VisualErrorEstimator.h"

// Compile-time checks. If either condition is false, the build stops with our message.
static_assert(sizeof(void*) == 8,
    "AETHER must be built as x64. Set the platform dropdown to x64.");
static_assert(_MSVC_LANG >= 202002L,
    "AETHER requires C++20. Set C++ Language Standard to ISO C++20 (/std:c++20).");

namespace
{
    constexpr std::uint64_t kFrameCount = 600;   // 10 seconds at 60 frames per second
    constexpr std::uint64_t kWindowFrames = 60;  // one table row per second

    double perFrame(std::uint64_t windowTotal)
    {
        return static_cast<double>(windowTotal) / static_cast<double>(kWindowFrames);
    }

    // "Re-renders" every region with a significant change: for now that just means
    // recording its current state in the store. Returns how many regions were re-rendered.
    std::uint32_t renderSignificantChanges(aether::TemporalStore& store,
                                           const std::vector<aether::RegionChange>& changes,
                                           const aether::SceneState& current)
    {
        std::uint32_t updates = 0;
        for (const aether::RegionChange& change : changes)
        {
            if (change.isSignificant())
            {
                store.recordRender(current.regions[change.regionId], current.frameIndex);
                ++updates;
            }
        }
        return updates;
    }

    // How far what is on screen (the store's rendered states) is from the ground truth.
    struct Staleness
    {
        std::uint32_t staleRegions = 0;  // regions beyond tolerance
        float worstScore = 0.0f;         // largest change score
    };

    Staleness measureStaleness(const aether::ChangeDetector& detector,
                               const aether::TemporalStore& store,
                               const aether::SceneState& previous,
                               const aether::SceneState& current)
    {
        Staleness result{};
        for (const aether::RegionChange& change : detector.analyse(store.renderedStates(), previous, current))
        {
            if (change.isSignificant())
            {
                ++result.staleRegions;
            }
            result.worstScore = std::max(result.worstScore, change.changeScore);
        }
        return result;
    }

    // -------------------------------------------------------------------------------------
    // Experiment "reference": what should change detection compare against?
    //   A: the previous frame.
    //   B: the state each region had when it was last rendered.
    // -------------------------------------------------------------------------------------
    void runReferenceExperiment()
    {
        struct WindowStats
        {
            std::uint64_t updates = 0;
            std::uint32_t peakUpdates = 0;
            std::uint32_t peakStale = 0;
            float worstStaleness = 0.0f;
        };

        aether::ScenarioGenerator scenario(aether::ScenarioConfig{});
        const aether::ChangeDetector detector(aether::ChangeDetectorConfig{});
        aether::TemporalStore storeA(aether::TemporalStoreConfig{}, scenario.scene());
        aether::TemporalStore storeB(aether::TemporalStoreConfig{}, scenario.scene());
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
            const std::vector<aether::RegionChange> changesB = detector.analyse(storeB.renderedStates(), previous, current);

            const std::uint32_t updatesA = renderSignificantChanges(storeA, changesA, current);
            const std::uint32_t updatesB = renderSignificantChanges(storeB, changesB, current);
            const Staleness staleA = measureStaleness(detector, storeA, previous, current);
            const Staleness staleB = measureStaleness(detector, storeB, previous, current);

            windowA.updates += updatesA;
            windowA.peakUpdates = std::max(windowA.peakUpdates, updatesA);
            windowA.peakStale = std::max(windowA.peakStale, staleA.staleRegions);
            windowA.worstStaleness = std::max(windowA.worstStaleness, staleA.worstScore);
            windowB.updates += updatesB;
            windowB.peakUpdates = std::max(windowB.peakUpdates, updatesB);
            windowB.peakStale = std::max(windowB.peakStale, staleB.staleRegions);
            windowB.worstStaleness = std::max(windowB.worstStaleness, staleB.worstScore);

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

        const std::uint64_t fullUpdates = storeB.regionCount() * kFrameCount;
        std::cout << std::format("\nRegion updates over {} frames: A = {}, B = {}, re-rendering everything = {}\n",
            kFrameCount, totalUpdatesA, totalUpdatesB, fullUpdates);
        std::cout << std::format("Most regions stale beyond tolerance in a single frame: A = {}, B = {}\n",
            worstPeakStaleA, worstPeakStaleB);
        std::cout << "Note: B is judged with the same measure it uses to decide, so it cannot look stale here.\n";
    }

    // -------------------------------------------------------------------------------------
    // Experiment "temporal": how old do displayed results get, and how much confidence do
    // they keep, when regions are re-rendered only on a significant change (strategy B)?
    // -------------------------------------------------------------------------------------
    void runTemporalExperiment()
    {
        struct WindowStats
        {
            std::uint64_t updates = 0;
            double confidenceSum = 0.0;
            std::uint64_t confidenceSamples = 0;
            float lowestConfidence = 1.0f;
            std::uint64_t oldestResult = 0;
        };

        aether::ScenarioGenerator scenario(aether::ScenarioConfig{});
        const aether::ChangeDetector detector(aether::ChangeDetectorConfig{});
        const aether::TemporalStoreConfig storeConfig{};
        aether::TemporalStore store(storeConfig, scenario.scene());
        aether::SceneState previous = scenario.scene();

        std::cout << std::format("Confidence time constant: {} frames\n\n", storeConfig.confidenceTimeConstantFrames);
        std::cout << "   frames    | updates/frame | mean confidence | lowest confidence | oldest result | never refreshed\n";
        std::cout << "-------------+---------------+-----------------+-------------------+---------------+----------------\n";

        WindowStats window{};
        for (std::uint64_t frame = 1; frame <= kFrameCount; ++frame)
        {
            scenario.advance();
            const aether::SceneState& current = scenario.scene();

            const std::vector<aether::RegionChange> changes = detector.analyse(store.renderedStates(), previous, current);
            window.updates += renderSignificantChanges(store, changes, current);

            // Confidence in what is on screen now, after this frame's re-renders.
            for (const aether::RegionChange& shown : detector.analyse(store.renderedStates(), previous, current))
            {
                const float confidence = store.confidence(shown, current.frameIndex);
                window.confidenceSum += confidence;
                ++window.confidenceSamples;
                window.lowestConfidence = std::min(window.lowestConfidence, confidence);
                window.oldestResult = std::max(window.oldestResult, store.age(shown.regionId, current.frameIndex));
            }

            if (frame % kWindowFrames == 0)
            {
                std::uint32_t neverRefreshed = 0;
                for (std::uint32_t id = 0; id < store.regionCount(); ++id)
                {
                    if (store.renderCount(id) == 1)  // only the initial render at frame 0
                    {
                        ++neverRefreshed;
                    }
                }

                std::cout << std::format(" {:4} - {:4} | {:13.1f} | {:15.3f} | {:17.3f} | {:7} frames | {:15}\n",
                    frame - kWindowFrames + 1, frame,
                    perFrame(window.updates),
                    window.confidenceSum / static_cast<double>(window.confidenceSamples),
                    window.lowestConfidence,
                    window.oldestResult,
                    neverRefreshed);
                window = {};
            }

            previous = current;
        }

        std::cout << "\nNote: confidence is a heuristic. No decision uses it yet; the controller will.\n";
    }

    // -------------------------------------------------------------------------------------
    // Experiment "estimators": what does a frame cost in the model, how does that change
    // when only significantly changed regions are re-rendered (strategy B), and how does
    // motion affect importance?
    // -------------------------------------------------------------------------------------
    void runEstimatorExperiment()
    {
        aether::ScenarioGenerator scenario(aether::ScenarioConfig{});
        const aether::ChangeDetectorConfig detectorConfig{};
        const aether::ChangeDetector detector(detectorConfig);
        const aether::ImportanceEstimator importanceEstimator(aether::ImportanceEstimatorConfig{});
        const aether::CostEstimatorConfig costConfig{};
        const aether::CostEstimator costEstimator(costConfig);
        aether::TemporalStore store(aether::TemporalStoreConfig{}, scenario.scene());
        aether::SceneState previous = scenario.scene();

        // Part 1: one full frame, broken down.
        const std::vector<aether::RegionCost> frame0Costs = costEstimator.estimate(scenario.scene());
        double overhead = 0.0;
        double geometry = 0.0;
        double shading = 0.0;
        double reuseEverything = costConfig.frameFixedCost;
        std::vector<float> renderCosts;
        for (const aether::RegionCost& cost : frame0Costs)
        {
            overhead += cost.overhead;
            geometry += cost.geometry;
            shading += cost.shading;
            reuseEverything += cost.reuse;
            renderCosts.push_back(cost.render);
        }
        const double fullFrame = costEstimator.fullRenderFrameCost(frame0Costs);

        // Cost concentration: how much of the region rendering cost sits in the 10 most expensive regions?
        std::ranges::sort(renderCosts, std::greater<>{});
        double topTen = 0.0;
        for (std::size_t i = 0; i < renderCosts.size() && i < 10; ++i)
        {
            topTen += renderCosts[i];
        }

        std::cout << "Full frame, modelled milliseconds (NOT measured on any GPU):\n";
        std::cout << std::format("  fixed (post-processing, UI)  {:6.2f}\n", costConfig.frameFixedCost);
        std::cout << std::format("  region overhead              {:6.2f}\n", overhead);
        std::cout << std::format("  geometry                     {:6.2f}\n", geometry);
        std::cout << std::format("  shading                      {:6.2f}\n", shading);
        std::cout << std::format("  total, re-rendering all      {:6.2f}\n", fullFrame);
        std::cout << std::format("  total, reusing all           {:6.2f}  (the floor: fixed cost + reuse cost)\n", reuseEverything);
        std::cout << std::format("  10 most expensive regions:   {:5.1f} % of region rendering cost\n\n",
            100.0 * topTen / (fullFrame - costConfig.frameFixedCost));

        // Part 2: modelled frame cost over time with change-driven updates (strategy B).
        struct WindowStats
        {
            std::uint64_t updates = 0;
            double costSum = 0.0;
            double lowestCost = std::numeric_limits<double>::max();
            double highestCost = 0.0;
        };

        std::cout << std::format("Change-driven updates vs re-rendering everything ({:.2f} ms every frame):\n", fullFrame);
        std::cout << "   frames    | updates/frame | mean ms | lowest ms | highest ms | highest vs full\n";
        std::cout << "-------------+---------------+---------+-----------+------------+----------------\n";

        WindowStats window{};
        double totalCost = 0.0;
        double highestOverall = 0.0;
        double movingImportanceSum = 0.0;
        double stillImportanceSum = 0.0;
        std::uint64_t movingSamples = 0;
        std::uint64_t stillSamples = 0;

        for (std::uint64_t frame = 1; frame <= kFrameCount; ++frame)
        {
            scenario.advance();
            const aether::SceneState& current = scenario.scene();

            const std::vector<aether::RegionChange> changes = detector.analyse(store.renderedStates(), previous, current);
            const std::vector<aether::RegionCost> costs = costEstimator.estimate(current);
            const std::vector<aether::RegionImportance> importance = importanceEstimator.estimate(current, changes);

            double frameCost = costConfig.frameFixedCost;
            for (std::size_t i = 0; i < changes.size(); ++i)
            {
                if (changes[i].isSignificant())
                {
                    frameCost += costs[i].render;
                    store.recordRender(current.regions[i], current.frameIndex);
                    ++window.updates;
                }
                else
                {
                    frameCost += costs[i].reuse;
                }

                // "Moving" here means visibly moving on screen this frame, as the detector measures it.
                if (changes[i].screenSpeedPixels > detectorConfig.displacementTolerancePixels)
                {
                    movingImportanceSum += importance[i].importance;
                    ++movingSamples;
                }
                else
                {
                    stillImportanceSum += importance[i].importance;
                    ++stillSamples;
                }
            }

            window.costSum += frameCost;
            window.lowestCost = std::min(window.lowestCost, frameCost);
            window.highestCost = std::max(window.highestCost, frameCost);

            if (frame % kWindowFrames == 0)
            {
                std::cout << std::format(" {:4} - {:4} | {:13.1f} | {:7.2f} | {:9.2f} | {:10.2f} | {:13.0f} %\n",
                    frame - kWindowFrames + 1, frame,
                    perFrame(window.updates),
                    window.costSum / static_cast<double>(kWindowFrames),
                    window.lowestCost,
                    window.highestCost,
                    100.0 * window.highestCost / fullFrame);
                totalCost += window.costSum;
                highestOverall = std::max(highestOverall, window.highestCost);
                window = {};
            }

            previous = current;
        }

        const double meanCost = totalCost / static_cast<double>(kFrameCount);
        std::cout << std::format("\nModelled frame cost over {} frames: mean {:.2f} ms ({:.0f} % of full), highest {:.2f} ms ({:.0f} % of full)\n",
            kFrameCount, meanCost, 100.0 * meanCost / fullFrame, highestOverall, 100.0 * highestOverall / fullFrame);
        std::cout << std::format("Mean importance: moving regions {:.3f} ({} region-frames), still regions {:.3f} ({} region-frames)\n",
            movingImportanceSum / static_cast<double>(std::max<std::uint64_t>(movingSamples, 1)), movingSamples,
            stillImportanceSum / static_cast<double>(std::max<std::uint64_t>(stillSamples, 1)), stillSamples);
        std::cout << "\nThese are outputs of an invented cost model on a scenario where 80 % of regions are static.\n";
        std::cout << "They show how the model behaves, not how a real renderer would perform.\n";
    }

    // -------------------------------------------------------------------------------------
    // Experiment "error": what is the TRUE error (judged by the GroundTruthEvaluator, which no
    // strategy can see) of four update strategies, and how well does the VisualErrorEstimator
    // predict it?
    // -------------------------------------------------------------------------------------

    // Compares predicted with actual per-region error.
    class EstimatorAccuracy
    {
    public:
        void add(double predicted, double actual, double visibleThreshold)
        {
            ++m_count;
            m_sumPredicted += predicted;
            m_sumActual += actual;
            m_sumPredictedSquared += predicted * predicted;
            m_sumActualSquared += actual * actual;
            m_sumProduct += predicted * actual;

            const bool predictedVisible = predicted >= visibleThreshold;
            const bool actuallyVisible = actual >= visibleThreshold;
            if (predictedVisible && actuallyVisible) { ++m_bothVisible; }
            else if (predictedVisible) { ++m_falseAlarms; }
            else if (actuallyVisible)
            {
                ++m_missed;
                m_largestMissed = std::max(m_largestMissed, actual);
            }
            else { ++m_bothInvisible; }
        }

        // Pearson correlation: +1 = predictions rise and fall exactly with the truth, 0 = unrelated.
        [[nodiscard]] double correlation() const
        {
            const double n = static_cast<double>(m_count);
            const double covariance = n * m_sumProduct - m_sumPredicted * m_sumActual;
            const double spreadPredicted = n * m_sumPredictedSquared - m_sumPredicted * m_sumPredicted;
            const double spreadActual = n * m_sumActualSquared - m_sumActual * m_sumActual;
            const double denominator = std::sqrt(spreadPredicted * spreadActual);
            return denominator > 0.0 ? covariance / denominator : 0.0;
        }

        void print() const
        {
            const double n = static_cast<double>(std::max<std::uint64_t>(m_count, 1));
            std::cout << std::format("  region-frames compared:       {}\n", m_count);
            std::cout << std::format("  mean predicted error:         {:.5f}\n", m_sumPredicted / n);
            std::cout << std::format("  mean actual error:            {:.5f}\n", m_sumActual / n);
            std::cout << std::format("  correlation (Pearson r):      {:.3f}\n", correlation());
            std::cout << "  at the visibility threshold:\n";
            std::cout << std::format("    predicted visible, is visible       {:6}\n", m_bothVisible);
            std::cout << std::format("    predicted visible, is NOT visible   {:6}   (false alarms: updates that were not needed)\n", m_falseAlarms);
            std::cout << std::format("    predicted invisible, IS visible     {:6}   (misses; largest actual error {:.4f})\n", m_missed, m_largestMissed);
            std::cout << std::format("    predicted invisible, is invisible   {:6}\n", m_bothInvisible);
        }

    private:
        std::uint64_t m_count = 0;
        double m_sumPredicted = 0.0;
        double m_sumActual = 0.0;
        double m_sumPredictedSquared = 0.0;
        double m_sumActualSquared = 0.0;
        double m_sumProduct = 0.0;
        std::uint64_t m_bothVisible = 0;
        std::uint64_t m_falseAlarms = 0;
        std::uint64_t m_missed = 0;
        std::uint64_t m_bothInvisible = 0;
        double m_largestMissed = 0.0;
    };

    void runErrorExperiment()
    {
        enum class Policy { RenderEverything, PreviousFrame, LastRendered, NeverUpdate };

        struct Strategy
        {
            std::string_view name;
            Policy policy;
            aether::TemporalStore store;
            std::uint64_t updates = 0;
            double meanErrorSum = 0.0;
            double worstFrameError = 0.0;
            double visibleFractionSum = 0.0;
            double worstVisibleFraction = 0.0;
            float worstRegionError = 0.0f;
        };

        aether::ScenarioGenerator scenario(aether::ScenarioConfig{});
        const aether::ChangeDetector detector(aether::ChangeDetectorConfig{});
        const aether::ImportanceEstimator importanceEstimator(aether::ImportanceEstimatorConfig{});
        const aether::VisualErrorEstimator errorEstimator(aether::VisualErrorEstimatorConfig{});
        const aether::GroundTruthConfig truthConfig{};
        const aether::GroundTruthEvaluator groundTruth(truthConfig);
        const aether::TemporalStoreConfig storeConfig{};
        aether::SceneState previous = scenario.scene();

        std::vector<Strategy> strategies;
        strategies.push_back({ "re-render everything", Policy::RenderEverything, aether::TemporalStore(storeConfig, previous) });
        strategies.push_back({ "A: vs previous frame", Policy::PreviousFrame, aether::TemporalStore(storeConfig, previous) });
        strategies.push_back({ "B: vs last rendered",  Policy::LastRendered, aether::TemporalStore(storeConfig, previous) });
        strategies.push_back({ "never update",         Policy::NeverUpdate, aether::TemporalStore(storeConfig, previous) });

        EstimatorAccuracy accuracy;

        for (std::uint64_t frame = 1; frame <= kFrameCount; ++frame)
        {
            scenario.advance();
            const aether::SceneState& current = scenario.scene();
            const float pixelCount = aether::screenPixelCount(current);

            for (Strategy& strategy : strategies)
            {
                switch (strategy.policy)
                {
                case Policy::RenderEverything:
                    for (const aether::SceneRegion& region : current.regions)
                    {
                        strategy.store.recordRender(region, current.frameIndex);
                    }
                    strategy.updates += current.regions.size();
                    break;

                case Policy::PreviousFrame:
                    strategy.updates += renderSignificantChanges(
                        strategy.store, detector.analyse(previous.regions, previous, current), current);
                    break;

                case Policy::LastRendered:
                {
                    const std::vector<aether::RegionChange> changes =
                        detector.analyse(strategy.store.renderedStates(), previous, current);

                    // Before deciding: for EVERY region, the predicted error if it were reused
                    // vs the true error if it were reused.
                    const std::vector<aether::RegionImportance> importance = importanceEstimator.estimate(current, changes);
                    const std::vector<aether::ErrorEstimate> estimates = errorEstimator.estimate(current, changes, importance);
                    for (std::size_t i = 0; i < current.regions.size(); ++i)
                    {
                        const float actual = groundTruth.regionError(strategy.store.renderedStates()[i], current.regions[i], pixelCount);
                        accuracy.add(estimates[i].pixelError, actual, truthConfig.visibilityThreshold);
                    }

                    strategy.updates += renderSignificantChanges(strategy.store, changes, current);
                    break;
                }

                case Policy::NeverUpdate:
                    break;
                }

                // Judge what is on screen now.
                const aether::FrameError error = groundTruth.evaluate(strategy.store.renderedStates(), current);
                strategy.meanErrorSum += error.meanError;
                strategy.worstFrameError = std::max(strategy.worstFrameError, error.meanError);
                strategy.visibleFractionSum += error.visibleFraction;
                strategy.worstVisibleFraction = std::max(strategy.worstVisibleFraction, error.visibleFraction);
                strategy.worstRegionError = std::max(strategy.worstRegionError, error.maxRegionError);
            }

            previous = current;
        }

        const double frames = static_cast<double>(kFrameCount);
        std::cout << std::format("Ground-truth error of what is on screen over {} frames (a model: see GroundTruthEvaluator.h)\n", kFrameCount);
        std::cout << std::format("'visibly wrong' = per-pixel error of at least {}\n\n", truthConfig.visibilityThreshold);
        std::cout << " strategy             | updates/frame | mean error | worst frame | visibly wrong: mean | worst frame | worst region\n";
        std::cout << "----------------------+---------------+------------+-------------+---------------------+-------------+-------------\n";
        for (const Strategy& strategy : strategies)
        {
            std::cout << std::format(" {:<20} | {:13.1f} | {:10.5f} | {:11.5f} | {:17.1f} % | {:9.1f} % | {:11.3f}\n",
                strategy.name,
                static_cast<double>(strategy.updates) / frames,
                strategy.meanErrorSum / frames,
                strategy.worstFrameError,
                100.0 * strategy.visibleFractionSum / frames,
                100.0 * strategy.worstVisibleFraction,
                strategy.worstRegionError);
        }

        std::cout << "\nVisualErrorEstimator vs ground truth (strategy B, every region, every frame, before deciding):\n";
        accuracy.print();
        std::cout << "\nBoth sides of this comparison are models with assumed constants. Agreement or disagreement\n";
        std::cout << "between them says nothing yet about real images; it shows where the two models differ.\n";
    }

    struct Experiment
    {
        std::string_view name;
        std::string_view description;
        void (*run)();  // pointer to the function that runs the experiment
    };

    constexpr std::array kExperiments{
        Experiment{ "reference", "change detection against the previous frame vs the last rendered state", &runReferenceExperiment },
        Experiment{ "temporal",  "age and confidence of displayed results",                                  &runTemporalExperiment },
        Experiment{ "estimators", "modelled frame cost and importance under change-driven updates",           &runEstimatorExperiment },
        Experiment{ "error",      "true (ground-truth) error of four strategies, and estimator accuracy",     &runErrorExperiment },
    };
}

int main(int argc, char* argv[])
{
    std::cout << std::format("{} v{}\n", aether::kProjectName, aether::kSimulatorVersion);
    std::cout << "Scenario: defaults (100 regions, seed 42). Detector: default tolerances.\n\n";

    const std::string_view requested = argc > 1 ? argv[1] : "all";

    bool ranAny = false;
    for (const Experiment& experiment : kExperiments)
    {
        if (requested == "all" || requested == experiment.name)
        {
            std::cout << std::format("=== Experiment '{}': {}\n\n", experiment.name, experiment.description);
            experiment.run();
            std::cout << '\n';
            ranAny = true;
        }
    }

    if (!ranAny)
    {
        std::cout << std::format("Unknown experiment '{}'. Available experiments:\n", requested);
        for (const Experiment& experiment : kExperiments)
        {
            std::cout << std::format("  {:<10} {}\n", experiment.name, experiment.description);
        }
        return 1;
    }
    return 0;
}
