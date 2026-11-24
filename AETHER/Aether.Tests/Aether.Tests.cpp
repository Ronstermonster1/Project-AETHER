// Aether.Tests.cpp
// Self-checking tests for Aether.Core.
//
// No external test framework yet: each check prints PASS or FAIL, and the program
// returns exit code 1 if any check failed. That is easy to run by hand now and easy
// to run automatically later (for example from GitHub Actions).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <string_view>
#include <vector>

#include "ChangeDetector.h"
#include "CostEstimator.h"
#include "GroundTruthEvaluator.h"
#include "ImportanceEstimator.h"
#include "Random.h"
#include "ScenarioGenerator.h"
#include "SceneState.h"
#include "ScreenSpace.h"
#include "TemporalStore.h"
#include "Vec3.h"
#include "VisualErrorEstimator.h"

namespace
{
    class TestReport
    {
    public:
        void check(bool passed, std::string_view name)
        {
            std::cout << std::format("[{}] {}\n", passed ? "PASS" : "FAIL", name);
            if (!passed)
            {
                ++m_failures;
            }
        }

        [[nodiscard]] int failures() const { return m_failures; }

    private:
        int m_failures = 0;
    };

    // True if both scenes hold exactly the same bytes. SceneRegion is trivially copyable
    // and has no padding (every member is 4 bytes wide), so comparing raw bytes is a
    // valid "bit-for-bit identical" check.
    bool identical(const aether::SceneState& a, const aether::SceneState& b)
    {
        return a.frameIndex == b.frameIndex
            && a.regions.size() == b.regions.size()
            && std::memcmp(a.regions.data(), b.regions.data(),
                           a.regions.size() * sizeof(aether::SceneRegion)) == 0;
    }

    void testVec3(TestReport& report)
    {
        const float length = aether::length(aether::Vec3{ 3.0f, 4.0f, 0.0f });
        report.check(std::abs(length - 5.0f) < 1e-6f, "Vec3: length of (3, 4, 0) is 5");
    }

    void testRandom(TestReport& report)
    {
        aether::Random a(123);
        aether::Random b(123);
        aether::Random c(124);

        bool sameSequence = true;
        bool inRange = true;
        bool otherSeedDiffers = false;
        for (int i = 0; i < 10'000; ++i)
        {
            const float va = a.uniform01();
            const float vb = b.uniform01();
            const float vc = c.uniform01();
            sameSequence = sameSequence && va == vb;
            inRange = inRange && va >= 0.0f && va < 1.0f;
            otherSeedDiffers = otherSeedDiffers || va != vc;
        }
        report.check(sameSequence, "Random: same seed gives the same sequence");
        report.check(inRange, "Random: uniform01() stays in [0, 1)");
        report.check(otherSeedDiffers, "Random: a different seed gives a different sequence");
    }

    void testScenarioSetup(TestReport& report)
    {
        const aether::ScenarioConfig config{};
        const aether::ScenarioGenerator scenario(config);
        const aether::SceneState& scene = scenario.scene();

        report.check(scene.frameIndex == 0, "Scenario: starts at frame 0");
        report.check(scene.regions.size() == config.regionCount, "Scenario: creates regionCount regions");

        bool idsMatchIndex = true;
        double coverage = 0.0;
        for (std::size_t i = 0; i < scene.regions.size(); ++i)
        {
            idsMatchIndex = idsMatchIndex && scene.regions[i].id == i;
            coverage += scene.regions[i].screenCoverage;
        }
        report.check(idsMatchIndex, "Scenario: region ids equal their index");
        report.check(std::abs(coverage - 1.0) < 1e-4, "Scenario: screen coverage sums to 1");

        // Defaults: 10 %, 5 % and 5 % of 100 regions.
        const aether::ScenarioSummary summary = scenario.summary();
        report.check(summary.movingRegions == 10
                  && summary.deformingRegions == 5
                  && summary.animatedMaterialRegions == 5,
            "Scenario: behaviour counts match the configured fractions");
    }

    void testDeterminism(TestReport& report)
    {
        const aether::ScenarioConfig config{};
        aether::ScenarioGenerator first(config);
        aether::ScenarioGenerator second(config);

        bool alwaysIdentical = identical(first.scene(), second.scene());
        for (int frame = 0; frame < 500; ++frame)
        {
            first.advance();
            second.advance();
            alwaysIdentical = alwaysIdentical && identical(first.scene(), second.scene());
        }
        report.check(alwaysIdentical, "Scenario: same config gives bit-identical frames (500 frames)");

        aether::ScenarioConfig otherConfig = config;
        otherConfig.seed = config.seed + 1;
        const aether::ScenarioGenerator fresh(config);
        const aether::ScenarioGenerator other(otherConfig);
        report.check(!identical(fresh.scene(), other.scene()), "Scenario: a different seed gives a different scene");
    }

    void testChangesMatchBehaviours(TestReport& report)
    {
        aether::ScenarioGenerator scenario(aether::ScenarioConfig{});
        const aether::ScenarioSummary summary = scenario.summary();

        bool movedMatches = true;
        bool meshMatches = true;
        bool coverageNeverChanges = true;
        for (int frame = 0; frame < 60; ++frame)
        {
            const aether::SceneState before = scenario.scene();  // a copy
            scenario.advance();
            const aether::SceneState& after = scenario.scene();

            std::uint32_t moved = 0;
            std::uint32_t meshChanged = 0;
            for (std::size_t i = 0; i < after.regions.size(); ++i)
            {
                const aether::SceneRegion& b = before.regions[i];
                const aether::SceneRegion& a = after.regions[i];
                if (!aether::exactlyEqual(b.geometry.position, a.geometry.position)) { ++moved; }
                if (b.geometry.meshVersion != a.geometry.meshVersion) { ++meshChanged; }
                coverageNeverChanges = coverageNeverChanges && b.screenCoverage == a.screenCoverage;
            }
            movedMatches = movedMatches && moved == summary.movingRegions;
            meshMatches = meshMatches && meshChanged == summary.deformingRegions;
        }
        report.check(movedMatches, "Scenario: exactly the moving regions move each frame");
        report.check(meshMatches, "Scenario: exactly the deforming regions change mesh each frame");
        report.check(coverageNeverChanges, "Scenario: screen coverage never changes");
    }

    void testLampToggles(TestReport& report)
    {
        aether::ScenarioGenerator scenario(aether::ScenarioConfig{});  // toggles every 120 frames
        const bool offAtStart = !scenario.summary().localLightOn;
        for (int i = 0; i < 120; ++i) { scenario.advance(); }
        const bool onAt120 = scenario.summary().localLightOn;
        for (int i = 0; i < 120; ++i) { scenario.advance(); }
        const bool offAt240 = !scenario.summary().localLightOn;
        report.check(offAtStart && onAt120 && offAt240, "Scenario: lamp is off at 0, on at 120, off at 240");
    }

    // A region covering 1 % of a 1920x1080 screen with radius 1: about 162 pixels across,
    // so one world unit is about 81 pixels.
    aether::SceneRegion makeTestRegion()
    {
        aether::SceneRegion region{};
        region.screenCoverage = 0.01f;
        region.geometry.boundingRadius = 1.0f;
        return region;
    }

    aether::SceneRegion withLightingScaled(aether::SceneRegion region, float factor)
    {
        region.lighting.direct = region.lighting.direct * factor;
        region.lighting.indirect = region.lighting.indirect * factor;
        return region;
    }

    void testChangeDetector(TestReport& report)
    {
        const aether::ChangeDetector detector(aether::ChangeDetectorConfig{});
        constexpr float kScreenPixels = 1920.0f * 1080.0f;
        const aether::SceneRegion base = makeTestRegion();

        const aether::RegionChange unchanged = detector.analyseRegion(base, base, base, kScreenPixels);
        report.check(unchanged.changeScore == 0.0f && !unchanged.isSignificant(),
            "ChangeDetector: identical regions score 0");

        aether::SceneRegion nudged = base;
        nudged.geometry.position.x += 0.001f;  // about 0.08 pixels
        report.check(!detector.analyseRegion(base, base, nudged, kScreenPixels).isSignificant(),
            "ChangeDetector: sub-pixel movement is not significant");

        aether::SceneRegion moved = base;
        moved.geometry.position.x += 0.1f;     // about 8 pixels
        const aether::RegionChange movedChange = detector.analyseRegion(base, base, moved, kScreenPixels);
        report.check(movedChange.isSignificant()
                  && movedChange.displacementPixels > 7.5f && movedChange.displacementPixels < 8.5f,
            "ChangeDetector: moving 0.1 units is about 8 pixels and significant");

        aether::SceneRegion deformed = base;
        ++deformed.geometry.meshVersion;
        const aether::RegionChange deformedChange = detector.analyseRegion(base, base, deformed, kScreenPixels);
        report.check(deformedChange.meshChanged && deformedChange.isSignificant(),
            "ChangeDetector: a mesh change is significant");

        report.check(!detector.analyseRegion(base, base, withLightingScaled(base, 1.01f), kScreenPixels).isSignificant(),
            "ChangeDetector: 1 % brighter is not significant");
        report.check(detector.analyseRegion(base, base, withLightingScaled(base, 1.05f), kScreenPixels).isSignificant(),
            "ChangeDetector: 5 % brighter is significant");
        report.check(detector.analyseRegion(base, base, withLightingScaled(base, 0.95f), kScreenPixels).isSignificant(),
            "ChangeDetector: 5 % darker is significant too");

        aether::SceneRegion tinted = base;
        tinted.material.albedo.x += 0.005f;
        report.check(!detector.analyseRegion(base, base, tinted, kScreenPixels).isSignificant(),
            "ChangeDetector: a 0.005 albedo change is not significant");
        tinted.material.albedo.x = base.material.albedo.x + 0.05f;
        report.check(detector.analyseRegion(base, base, tinted, kScreenPixels).isSignificant(),
            "ChangeDetector: a 0.05 albedo change is significant");

        // Motion is measured from the previous frame, displacement from the reference.
        aether::SceneRegion previous = base;
        previous.geometry.position.x += 0.5f;
        aether::SceneRegion current = base;
        current.geometry.position.x += 0.6f;
        const aether::RegionChange motion = detector.analyseRegion(base, previous, current, kScreenPixels);
        report.check(std::abs(motion.velocity.x - 0.1f) < 1e-5f && motion.displacementPixels > 40.0f,
            "ChangeDetector: velocity uses the previous frame, displacement uses the reference");

        // The slow-drift trap: +0.5 % light per frame for 10 frames.
        aether::SceneRegion drifting = base;
        aether::SceneRegion lastFrame = base;
        for (int frame = 0; frame < 10; ++frame)
        {
            lastFrame = drifting;
            drifting = withLightingScaled(drifting, 1.005f);
        }
        const bool perFrameMisses = !detector.analyseRegion(lastFrame, lastFrame, drifting, kScreenPixels).isSignificant();
        const bool referenceCatches = detector.analyseRegion(base, lastFrame, drifting, kScreenPixels).isSignificant();
        report.check(perFrameMisses && referenceCatches,
            "ChangeDetector: slow drift is missed frame-to-frame but caught against the reference");

        // Whole-scene analysis returns one result per region, in order.
        const aether::ScenarioGenerator scenario(aether::ScenarioConfig{});
        const aether::SceneState& scene = scenario.scene();
        const std::vector<aether::RegionChange> changes = detector.analyse(scene.regions, scene, scene);
        bool inOrder = changes.size() == scene.regions.size();
        for (std::size_t i = 0; inOrder && i < changes.size(); ++i)
        {
            inOrder = changes[i].regionId == scene.regions[i].id && !changes[i].isSignificant();
        }
        report.check(inOrder, "ChangeDetector: analyse() returns one unchanged result per region, in order");
    }

    void testTemporalStore(TestReport& report)
    {
        aether::ScenarioGenerator scenario(aether::ScenarioConfig{});
        const aether::SceneState frame0 = scenario.scene();
        aether::TemporalStore store(aether::TemporalStoreConfig{}, frame0);

        bool startsRendered = store.regionCount() == frame0.regions.size()
            && std::memcmp(store.renderedStates().data(), frame0.regions.data(),
                           frame0.regions.size() * sizeof(aether::SceneRegion)) == 0;
        for (std::uint32_t id = 0; startsRendered && id < store.regionCount(); ++id)
        {
            startsRendered = store.renderedFrame(id) == 0 && store.renderCount(id) == 1 && store.age(id, 0) == 0;
        }
        report.check(startsRendered, "TemporalStore: starts with every region rendered at frame 0");

        for (int i = 0; i < 10; ++i) { scenario.advance(); }
        const aether::SceneState& frame10 = scenario.scene();
        store.recordRender(frame10.regions[3], frame10.frameIndex);

        report.check(store.age(3, 10) == 0 && store.age(4, 10) == 10
                  && store.renderCount(3) == 2 && store.renderCount(4) == 1,
            "TemporalStore: recordRender resets the age and counts the render");
        report.check(std::memcmp(&store.renderedState(3), &frame10.regions[3], sizeof(aether::SceneRegion)) == 0,
            "TemporalStore: recordRender stores the region's state");
        report.check(store.age(3, 5) == 0, "TemporalStore: age never wraps around below zero");

        const aether::ChangeDetector detector(aether::ChangeDetectorConfig{});
        const std::vector<aether::RegionChange> changes = detector.analyse(store.renderedStates(), frame10, frame10);
        report.check(changes[3].changeScore == 0.0f,
            "TemporalStore: renderedStates() feeds the ChangeDetector (fresh region shows no change)");

        aether::RegionChange noChange{};
        noChange.regionId = 3;  // rendered at frame 10
        aether::RegionChange atThreshold = noChange;
        atThreshold.changeScore = 1.0f;

        report.check(std::abs(store.confidence(noChange, 10) - 1.0f) < 1e-6f,
            "TemporalStore: a fresh, unchanged result has confidence 1");
        report.check(std::abs(store.confidence(noChange, 10 + 120) - std::exp(-1.0f)) < 1e-4f,
            "TemporalStore: confidence falls to about 37 % after 120 frames");
        report.check(std::abs(store.confidence(atThreshold, 10) - 0.5f) < 1e-6f,
            "TemporalStore: a change exactly at the threshold halves confidence");
    }

    void testImportanceEstimator(TestReport& report)
    {
        const aether::ImportanceEstimator estimator(aether::ImportanceEstimatorConfig{});  // weight 0.3, half-saturation 4 px
        aether::SceneRegion region{};
        region.authoredImportance = 0.5f;

        const aether::RegionChange still{};
        report.check(estimator.estimateRegion(region, still).importance == 0.5f,
            "Importance: a still region keeps its authored importance");

        aether::RegionChange moving{};
        moving.screenSpeedPixels = 4.0f;  // exactly the half-saturation speed
        report.check(std::abs(estimator.estimateRegion(region, moving).importance - (0.5f + 0.3f * 0.5f)) < 1e-6f,
            "Importance: motion at the half-saturation speed adds half the motion weight");

        region.authoredImportance = 0.9f;
        moving.screenSpeedPixels = 1000.0f;
        report.check(estimator.estimateRegion(region, moving).importance <= 1.0f,
            "Importance: never exceeds 1");

        const aether::ImportanceEstimator motionIgnored(aether::ImportanceEstimatorConfig{ .motionWeight = 0.0f });
        report.check(motionIgnored.estimateRegion(region, moving).importance == 0.9f,
            "Importance: motionWeight 0 ignores motion");
    }

    void testCostEstimator(TestReport& report)
    {
        const aether::CostEstimatorConfig config{};
        const aether::CostEstimator estimator(config);
        constexpr float kScreenPixels = 1920.0f * 1080.0f;

        aether::SceneRegion region{};
        region.screenCoverage = 0.01f;
        region.geometry.triangleCount = 10'000;
        region.material.shadingComplexity = 2.0f;

        const aether::RegionCost cost = estimator.estimateRegion(region, kScreenPixels);
        const float pixels = 0.01f * kScreenPixels;
        const float expectedRender = config.regionOverheadCost
                                   + 10'000.0f * config.costPerTriangle
                                   + pixels * 2.0f * config.costPerShadedPixel;
        report.check(std::abs(cost.render - expectedRender) < 1e-5f,
            "Cost: render = overhead + geometry + shading");
        report.check(cost.reuse < cost.render, "Cost: reusing a region is cheaper than rendering it");

        aether::SceneRegion bigger = region;
        bigger.screenCoverage = 0.02f;
        report.check(std::abs(estimator.estimateRegion(bigger, kScreenPixels).shading - 2.0f * cost.shading) < 1e-5f,
            "Cost: twice the pixels costs twice the shading");

        const aether::ScenarioGenerator scenario(aether::ScenarioConfig{});
        const std::vector<aether::RegionCost> costs = estimator.estimate(scenario.scene());
        double expectedFrame = config.frameFixedCost;
        for (const aether::RegionCost& regionCost : costs)
        {
            expectedFrame += regionCost.render;
        }
        report.check(costs.size() == scenario.scene().regions.size()
                  && std::abs(estimator.fullRenderFrameCost(costs) - expectedFrame) < 1e-9,
            "Cost: full frame = fixed cost + every region's render cost");
    }

    void testScreenSpace(TestReport& report)
    {
        const aether::SceneRegion region = makeTestRegion();  // 1 % of 1920x1080, radius 1
        constexpr float kScreenPixels = 1920.0f * 1080.0f;
        report.check(std::abs(aether::onScreenRadiusPixels(region, kScreenPixels) - 81.24f) < 0.01f
                  && std::abs(aether::pixelsPerWorldUnit(region, kScreenPixels) - 81.24f) < 0.01f,
            "ScreenSpace: 1 % of a 1080p screen is a disc of radius ~81 px");
    }

    void testVisualErrorEstimator(TestReport& report)
    {
        const aether::VisualErrorEstimator estimator(aether::VisualErrorEstimatorConfig{});  // 0.02 per score point
        aether::SceneRegion region{};
        region.screenCoverage = 0.01f;
        aether::RegionImportance importance{};
        importance.importance = 0.5f;

        aether::RegionChange change{};
        report.check(estimator.estimateRegion(region, change, importance).pixelError == 0.0f,
            "ErrorEstimate: no change predicts no error");

        change.changeScore = 1.0f;
        const aether::ErrorEstimate atThreshold = estimator.estimateRegion(region, change, importance);
        report.check(std::abs(atThreshold.pixelError - 0.02f) < 1e-7f
                  && std::abs(atThreshold.visibleError - 0.02f * 0.01f * 0.5f) < 1e-9f,
            "ErrorEstimate: score 1 predicts 0.02, weighted by coverage and importance");

        change.changeScore = 1000.0f;
        report.check(estimator.estimateRegion(region, change, importance).pixelError == 1.0f,
            "ErrorEstimate: predicted error never exceeds 1");
    }

    void testGroundTruthEvaluator(TestReport& report)
    {
        const aether::GroundTruthConfig config{};
        const aether::GroundTruthEvaluator truth(config);
        constexpr float kScreenPixels = 1920.0f * 1080.0f;
        const aether::SceneRegion base = makeTestRegion();

        report.check(truth.regionError(base, base, kScreenPixels) == 0.0f,
            "GroundTruth: showing the correct state has zero error");
        report.check(truth.regionError(base, withLightingScaled(base, 1.5f), kScreenPixels) > 0.0f,
            "GroundTruth: a lighting change creates error");

        aether::SceneRegion farAway = base;
        farAway.geometry.position.x += 10.0f;  // ~800 px, far more than the disc's diameter
        report.check(std::abs(truth.regionError(base, farAway, kScreenPixels) - 2.0f * config.edgeContrast) < 1e-6f,
            "GroundTruth: a region moved completely off its old image is wrong twice over (ghost + hole)");

        aether::SceneRegion nudged = base;
        nudged.geometry.position.x += 0.001f;  // ~0.08 px
        report.check(truth.regionError(base, nudged, kScreenPixels) < 0.01f,
            "GroundTruth: a sub-pixel move creates almost no error");

        aether::SceneRegion deformed = base;
        deformed.geometry.meshVersion += 4;    // 4 versions behind: 20 % of pixels wrong
        report.check(std::abs(truth.regionError(base, deformed, kScreenPixels) - 0.2f * config.edgeContrast) < 1e-6f,
            "GroundTruth: each mesh version behind makes 5 % of pixels wrong");

        const aether::ScenarioGenerator scenario(aether::ScenarioConfig{});
        const aether::SceneState& scene = scenario.scene();
        const aether::FrameError perfect = truth.evaluate(scene.regions, scene);
        report.check(perfect.meanError == 0.0 && perfect.visibleFraction == 0.0 && perfect.maxRegionError == 0.0f,
            "GroundTruth: a frame showing the truth has no error");
    }
}

int main()
{
    TestReport report;
    testVec3(report);
    testRandom(report);
    testScenarioSetup(report);
    testDeterminism(report);
    testChangesMatchBehaviours(report);
    testLampToggles(report);
    testChangeDetector(report);
    testTemporalStore(report);
    testImportanceEstimator(report);
    testCostEstimator(report);
    testScreenSpace(report);
    testVisualErrorEstimator(report);
    testGroundTruthEvaluator(report);

    std::cout << std::format("\n{} check(s) failed.\n", report.failures());
    return report.failures() == 0 ? 0 : 1;
}
