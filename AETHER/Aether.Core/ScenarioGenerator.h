// ScenarioGenerator.h
// Produces the ground-truth scene for every frame of an experiment.
//
// The generator owns the scene and moves it forward one frame at a time. Every change
// comes from the config and a seeded random generator, so the same config always
// produces exactly the same sequence of frames. That is what makes AETHER experiments
// repeatable.
//
// The generator also decides HOW each region behaves (static, moving, deforming...).
// Those behaviours are private on purpose: AETHER's change detector must discover
// changes from the scene state alone, just as a real renderer would.
//
// SIMPLIFICATIONS (research model, not reality):
//  - Motion is a smooth back-and-forth oscillation: no physics, no collisions.
//  - screenCoverage never changes, even when a region moves (fixed camera, no perspective).
//  - Each region has at most one behaviour (a region never both moves and deforms).
//  - Indirect light is a fixed fraction of direct light. Real indirect light depends on
//    the whole scene and reacts to changes with a delay.

#pragma once

#include <cstdint>
#include <vector>

#include "Random.h"
#include "SceneState.h"
#include "Vec3.h"

namespace aether
{
    // Every knob that defines one scenario. Record these with each experiment's results.
    struct ScenarioConfig
    {
        std::uint32_t regionCount = 100;
        std::uint32_t seed = 42;

        // Fraction of regions given each behaviour, assigned once at frame 0.
        float movingFraction = 0.10f;            // oscillate around their starting position
        float deformingFraction = 0.05f;         // mesh changes every frame (e.g. animated characters)
        float animatedMaterialFraction = 0.05f;  // colour pulses every frame (e.g. animated textures)

        // Chance, per region per frame, that its material is swapped for a new one.
        float materialSwapChance = 0.002f;

        // Sun: slow, smooth brightness variation (clouds, time of day).
        float sunDriftAmplitude = 0.05f;         // +/- 5 % brightness
        std::uint32_t sunDriftPeriodFrames = 600;

        // A lamp in the middle of the scene that switches on and off.
        std::uint32_t localLightTogglePeriodFrames = 120;  // 0 = always off
        float localLightRadius = 12.0f;          // world units
        float localLightIntensity = 2.0f;
    };

    // Counts describing the scenario, for reports and tests.
    struct ScenarioSummary
    {
        std::uint32_t movingRegions = 0;
        std::uint32_t deformingRegions = 0;
        std::uint32_t animatedMaterialRegions = 0;
        std::uint64_t materialSwapsSoFar = 0;
        bool localLightOn = false;
    };

    class ScenarioGenerator
    {
    public:
        // Builds frame 0 of the scenario.
        explicit ScenarioGenerator(const ScenarioConfig& config);

        // The ground-truth scene for the current frame.
        [[nodiscard]] const SceneState& scene() const { return m_scene; }

        [[nodiscard]] const ScenarioConfig& config() const { return m_config; }

        [[nodiscard]] ScenarioSummary summary() const;

        // Moves the scene forward by exactly one frame.
        void advance();

    private:
        // How one region behaves over time. Never visible outside this class.
        struct RegionBehaviour
        {
            bool moving = false;
            bool deforming = false;
            bool animatedMaterial = false;
            Vec3 basePosition{};
            Vec3 motionAmplitude{};          // largest offset from basePosition (world units)
            float motionPeriodFrames = 120.0f;
            float motionPhase = 0.0f;        // radians, so regions don't all move in sync
            Vec3 baseAlbedo{};
            float shadowFactor = 1.0f;       // 0 = always in shadow, 1 = fully sunlit
        };

        // Recomputes everything that is a pure function of the frame index:
        // positions of moving regions, animated colours and lighting.
        void applyTimeDependentState();

        [[nodiscard]] bool isLocalLightOn() const;

        // Members are initialised in this order, so m_config must come before m_random.
        ScenarioConfig m_config;
        Random m_random;
        std::vector<RegionBehaviour> m_behaviours;  // m_behaviours[i] belongs to m_scene.regions[i]
        SceneState m_scene;
        Vec3 m_localLightPosition{};
        std::uint64_t m_materialSwaps = 0;
    };
}
