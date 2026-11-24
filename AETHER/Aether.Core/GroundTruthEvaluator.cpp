// GroundTruthEvaluator.cpp

#include "GroundTruthEvaluator.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "ScreenSpace.h"

namespace aether
{
    namespace
    {
        float toneMap(float radiance)
        {
            return radiance / (1.0f + radiance);
        }

        // Share of a disc's area (radius r) still covered after moving it by distance d.
        // Standard circle-circle overlap formula, computed in double because acos and
        // sqrt lose too much precision in float when d is tiny compared with r.
        float discOverlapShare(float distance, float radius)
        {
            if (radius <= 0.0f || distance <= 0.0f)
            {
                return 1.0f;
            }
            if (distance >= 2.0f * radius)
            {
                return 0.0f;
            }
            const double r = radius;
            const double d = distance;
            const double overlap = 2.0 * r * r * std::acos(d / (2.0 * r)) - 0.5 * d * std::sqrt(4.0 * r * r - d * d);
            return static_cast<float>(overlap / (std::numbers::pi * r * r));
        }
    }

    GroundTruthEvaluator::GroundTruthEvaluator(const GroundTruthConfig& config)
        : m_config(config)
    {
    }

    Vec3 GroundTruthEvaluator::displayedColour(const SceneRegion& region) const
    {
        const Vec3 incoming = region.lighting.direct + region.lighting.indirect;
        const Vec3 diffuse = multiplyComponents(region.material.albedo, incoming);
        const float smoothness = 1.0f - region.material.roughness;
        const Vec3 specular = region.lighting.direct * (m_config.specularStrength * smoothness * smoothness);
        const Vec3 radiance = diffuse + specular;
        return { toneMap(radiance.x), toneMap(radiance.y), toneMap(radiance.z) };
    }

    float GroundTruthEvaluator::regionError(const SceneRegion& shown, const SceneRegion& truth, float pixelCount) const
    {
        // Colour error where the stale image still overlaps the true one.
        const float colourError = maxAbsComponent(displayedColour(truth) - displayedColour(shown));

        // How much of the stale image still lines up with where the region really is.
        const float radius = onScreenRadiusPixels(truth, pixelCount);
        const float distance = length(truth.geometry.position - shown.geometry.position)
                             * pixelsPerWorldUnit(truth, pixelCount);
        const float overlapShare = discOverlapShare(distance, radius);
        const float ghostShare = 1.0f - overlapShare;

        // Deformation since the stored result.
        const std::uint32_t newer = std::max(truth.geometry.meshVersion, shown.geometry.meshVersion);
        const std::uint32_t older = std::min(truth.geometry.meshVersion, shown.geometry.meshVersion);
        const float deformedShare = std::min(1.0f, m_config.deformationPerMeshVersion * static_cast<float>(newer - older));

        const float overlapError = (1.0f - deformedShare) * colourError + deformedShare * m_config.edgeContrast;
        const float error = overlapShare * overlapError + 2.0f * ghostShare * m_config.edgeContrast;
        return std::min(error, 1.0f);
    }

    FrameError GroundTruthEvaluator::evaluate(std::span<const SceneRegion> shown, const SceneState& truth) const
    {
        assert(shown.size() == truth.regions.size() && "one shown state per region");

        const float pixelCount = screenPixelCount(truth);
        FrameError result{};
        for (std::size_t i = 0; i < truth.regions.size(); ++i)
        {
            const float error = regionError(shown[i], truth.regions[i], pixelCount);
            const double coverage = truth.regions[i].screenCoverage;
            result.meanError += coverage * error;
            if (error >= m_config.visibilityThreshold)
            {
                result.visibleFraction += coverage;
            }
            result.maxRegionError = std::max(result.maxRegionError, error);
        }
        return result;
    }
}
