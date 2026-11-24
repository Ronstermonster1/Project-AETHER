// ScenarioGenerator.cpp

#include "ScenarioGenerator.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <utility>

namespace aether
{
    namespace
    {
        constexpr float kRegionSpacing = 4.0f;      // world units between neighbouring regions
        constexpr float kSceneDepth = -20.0f;       // the camera looks down -z; regions start 20 units away
        constexpr float kLampHeight = 3.0f;         // the lamp hangs above the middle of the scene
        constexpr Vec3 kSunColour{ 1.0f, 0.95f, 0.9f };
        constexpr Vec3 kLampColour{ 1.0f, 0.8f, 0.6f };
        constexpr float kIndirectFraction = 0.2f;   // indirect light = 20 % of direct light
        constexpr double kMaterialPulsePeriodFrames = 90.0;
        constexpr double kTwoPi = 2.0 * std::numbers::pi;

        // How many of 'total' regions a fraction such as 0.1 refers to (rounded).
        std::uint32_t countFromFraction(float fraction, std::uint32_t total)
        {
            const float clamped = std::clamp(fraction, 0.0f, 1.0f);
            return static_cast<std::uint32_t>(std::lround(clamped * static_cast<float>(total)));
        }

        // Separate statements on purpose: the order of random draws must never depend
        // on the compiler. (C++ does not fix the evaluation order of function arguments.)
        Vec3 randomAlbedo(Random& random)
        {
            Vec3 albedo{};
            albedo.x = random.range(0.1f, 0.9f);
            albedo.y = random.range(0.1f, 0.9f);
            albedo.z = random.range(0.1f, 0.9f);
            return albedo;
        }
    }

    ScenarioGenerator::ScenarioGenerator(const ScenarioConfig& config)
        : m_config(config)
        , m_random(config.seed)
    {
        const std::uint32_t count = m_config.regionCount;
        m_behaviours.resize(count);
        m_scene.frameIndex = 0;
        m_scene.regions.resize(count);
        if (count == 0)
        {
            return;
        }

        // Regions sit on a square-ish grid in front of the camera.
        const auto columns = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<float>(count))));
        const std::uint32_t rows = (count + columns - 1) / columns;

        // Screen coverage: random weights, normalised so that all regions together
        // cover exactly the whole screen.
        std::vector<float> coverageWeights(count);
        float weightSum = 0.0f;
        for (float& weight : coverageWeights)
        {
            weight = m_random.range(0.5f, 1.5f);
            weightSum += weight;
        }

        for (std::uint32_t i = 0; i < count; ++i)
        {
            SceneRegion& region = m_scene.regions[i];
            RegionBehaviour& behaviour = m_behaviours[i];

            const auto column = static_cast<float>(i % columns);
            const auto row = static_cast<float>(i / columns);
            behaviour.basePosition = { column * kRegionSpacing, 0.0f, kSceneDepth - row * kRegionSpacing };

            region.id = i;
            region.geometry.position = behaviour.basePosition;
            region.geometry.boundingRadius = m_random.range(0.5f, 2.0f);
            region.geometry.triangleCount = static_cast<std::uint32_t>(m_random.range(1'000.0f, 50'000.0f));

            behaviour.baseAlbedo = randomAlbedo(m_random);
            region.material.albedo = behaviour.baseAlbedo;
            region.material.roughness = m_random.range(0.1f, 0.9f);
            // About 1 in 5 regions uses an expensive material (2-4x the standard shading cost).
            region.material.shadingComplexity = m_random.chance(0.2f) ? m_random.range(2.0f, 4.0f) : 1.0f;

            behaviour.shadowFactor = m_random.range(0.3f, 1.0f);
            region.screenCoverage = coverageWeights[i] / weightSum;
            region.authoredImportance = m_random.range(0.2f, 0.8f);
        }

        // Choose which regions get which behaviour: shuffle the region indices
        // (Fisher-Yates, using our own Random so it is reproducible) and hand out
        // behaviours in that order, so the groups never overlap.
        std::vector<std::uint32_t> order(count);
        std::iota(order.begin(), order.end(), 0u);
        for (std::uint32_t remaining = count; remaining > 1; --remaining)
        {
            const std::uint32_t pick = m_random.index(remaining);
            std::swap(order[remaining - 1], order[pick]);
        }

        const std::uint32_t movingEnd = std::min(count, countFromFraction(m_config.movingFraction, count));
        const std::uint32_t deformingEnd = std::min(count, movingEnd + countFromFraction(m_config.deformingFraction, count));
        const std::uint32_t animatedEnd = std::min(count, deformingEnd + countFromFraction(m_config.animatedMaterialFraction, count));

        for (std::uint32_t k = 0; k < animatedEnd; ++k)
        {
            RegionBehaviour& behaviour = m_behaviours[order[k]];
            if (k < movingEnd)
            {
                behaviour.moving = true;
                // At least 1 unit of movement, so a moving region visibly moves every frame.
                behaviour.motionAmplitude.x = m_random.range(1.0f, 3.0f);
                const float zDirection = m_random.chance(0.5f) ? 1.0f : -1.0f;
                behaviour.motionAmplitude.z = zDirection * m_random.range(1.0f, 3.0f);
                behaviour.motionPeriodFrames = m_random.range(60.0f, 240.0f);
                behaviour.motionPhase = m_random.range(0.0f, static_cast<float>(kTwoPi));
            }
            else if (k < deformingEnd)
            {
                behaviour.deforming = true;
            }
            else
            {
                behaviour.animatedMaterial = true;
            }
        }

        // The lamp hangs above the centre of the grid.
        m_localLightPosition = {
            static_cast<float>(columns - 1) * kRegionSpacing * 0.5f,
            kLampHeight,
            kSceneDepth - static_cast<float>(rows - 1) * kRegionSpacing * 0.5f,
        };

        applyTimeDependentState();
    }

    ScenarioSummary ScenarioGenerator::summary() const
    {
        ScenarioSummary result{};
        for (const RegionBehaviour& behaviour : m_behaviours)
        {
            if (behaviour.moving) { ++result.movingRegions; }
            if (behaviour.deforming) { ++result.deformingRegions; }
            if (behaviour.animatedMaterial) { ++result.animatedMaterialRegions; }
        }
        result.materialSwapsSoFar = m_materialSwaps;
        result.localLightOn = isLocalLightOn();
        return result;
    }

    void ScenarioGenerator::advance()
    {
        ++m_scene.frameIndex;

        // Discrete events: things that happen at a moment rather than smoothly over time.
        for (std::size_t i = 0; i < m_scene.regions.size(); ++i)
        {
            SceneRegion& region = m_scene.regions[i];
            RegionBehaviour& behaviour = m_behaviours[i];

            if (behaviour.deforming)
            {
                ++region.geometry.meshVersion;
            }

            // Every region draws exactly one chance() per frame, so the random sequence
            // stays the same no matter what happened in earlier frames.
            if (m_random.chance(m_config.materialSwapChance))
            {
                behaviour.baseAlbedo = randomAlbedo(m_random);
                region.material.roughness = m_random.range(0.1f, 0.9f);
                ++m_materialSwaps;
            }
        }

        applyTimeDependentState();
    }

    void ScenarioGenerator::applyTimeDependentState()
    {
        // double for time: float loses precision once frame counts and angles grow large.
        const auto t = static_cast<double>(m_scene.frameIndex);

        double sunFactor = 1.0;
        if (m_config.sunDriftPeriodFrames > 0)
        {
            sunFactor += m_config.sunDriftAmplitude * std::sin(kTwoPi * t / m_config.sunDriftPeriodFrames);
        }
        const bool lampOn = isLocalLightOn();

        for (std::size_t i = 0; i < m_scene.regions.size(); ++i)
        {
            SceneRegion& region = m_scene.regions[i];
            const RegionBehaviour& behaviour = m_behaviours[i];

            if (behaviour.moving)
            {
                const double angle = kTwoPi * t / behaviour.motionPeriodFrames + behaviour.motionPhase;
                region.geometry.position =
                    behaviour.basePosition + behaviour.motionAmplitude * static_cast<float>(std::sin(angle));
            }

            if (behaviour.animatedMaterial)
            {
                const double pulse = 0.85 + 0.15 * std::sin(kTwoPi * t / kMaterialPulsePeriodFrames);
                region.material.albedo = behaviour.baseAlbedo * static_cast<float>(pulse);
            }
            else
            {
                region.material.albedo = behaviour.baseAlbedo;
            }

            Vec3 direct = kSunColour * static_cast<float>(sunFactor * behaviour.shadowFactor);
            if (lampOn)
            {
                // Uses the region's CURRENT position, so moving regions drift in and out of the light.
                const float distance = length(region.geometry.position - m_localLightPosition);
                if (distance < m_config.localLightRadius)
                {
                    const float falloff = 1.0f - distance / m_config.localLightRadius;  // linear: a simple model
                    direct = direct + kLampColour * (m_config.localLightIntensity * falloff);
                }
            }

            region.lighting.direct = direct;
            region.lighting.indirect = direct * kIndirectFraction;
        }
    }

    bool ScenarioGenerator::isLocalLightOn() const
    {
        const std::uint32_t period = m_config.localLightTogglePeriodFrames;
        if (period == 0)
        {
            return false;
        }
        // Off for the first period, on for the second, off for the third, and so on.
        return (m_scene.frameIndex / period) % 2 == 1;
    }
}
