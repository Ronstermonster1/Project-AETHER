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

#include "Random.h"
#include "ScenarioGenerator.h"
#include "SceneState.h"
#include "Vec3.h"

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

    std::cout << std::format("\n{} check(s) failed.\n", report.failures());
    return report.failures() == 0 ? 0 : 1;
}
