// ChangeDetector.cpp

#include "ChangeDetector.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "ScreenSpace.h"

namespace aether
{
    namespace
    {
        // Perceived brightness of a linear RGB value (Rec. 709 / sRGB primaries).
        float luminance(Vec3 rgb)
        {
            return 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z;
        }
    }

    ChangeDetector::ChangeDetector(const ChangeDetectorConfig& config)
        : m_config(config)
    {
        assert(m_config.displacementTolerancePixels > 0.0f && "tolerances must be positive");
        assert(m_config.lightingTolerance > 0.0f && "tolerances must be positive");
        assert(m_config.materialTolerance > 0.0f && "tolerances must be positive");
    }

    RegionChange ChangeDetector::analyseRegion(const SceneRegion& reference,
                                               const SceneRegion& previous,
                                               const SceneRegion& current,
                                               float screenPixelCount) const
    {
        RegionChange change{};
        change.regionId = current.id;

        const float pixelsPerUnit = pixelsPerWorldUnit(current, screenPixelCount);

        // Geometry, since the reference.
        change.meshChanged = current.geometry.meshVersion != reference.geometry.meshVersion;
        change.displacementPixels = length(current.geometry.position - reference.geometry.position) * pixelsPerUnit;

        // Material, since the reference.
        const float albedoDelta = maxAbsComponent(current.material.albedo - reference.material.albedo);
        const float roughnessDelta = std::abs(current.material.roughness - reference.material.roughness);
        change.materialDelta = std::max(albedoDelta, roughnessDelta);

        // Lighting, since the reference. A log-ratio is symmetric: brightening by 5 % and
        // darkening by 5 % give (almost) the same value, and for small changes it is
        // approximately the relative change.
        const float luminanceFloor = m_config.minimumLuminance;
        const float referenceLuminance = luminance(reference.lighting.direct + reference.lighting.indirect);
        const float currentLuminance = luminance(current.lighting.direct + current.lighting.indirect);
        change.lightingDelta = std::abs(std::log((currentLuminance + luminanceFloor) / (referenceLuminance + luminanceFloor)));

        // Motion, since the previous frame.
        change.velocity = current.geometry.position - previous.geometry.position;
        change.screenSpeedPixels = length(change.velocity) * pixelsPerUnit;

        // Scores.
        change.geometryScore = change.displacementPixels / m_config.displacementTolerancePixels;
        if (change.meshChanged)
        {
            change.geometryScore = std::max(change.geometryScore, 1.0f);
        }
        change.materialScore = change.materialDelta / m_config.materialTolerance;
        change.lightingScore = change.lightingDelta / m_config.lightingTolerance;
        change.changeScore = std::max({ change.geometryScore, change.materialScore, change.lightingScore });

        return change;
    }

    std::vector<RegionChange> ChangeDetector::analyse(std::span<const SceneRegion> reference,
                                                      const SceneState& previous,
                                                      const SceneState& current) const
    {
        assert(reference.size() == current.regions.size() && "one reference per region");
        assert(previous.regions.size() == current.regions.size() && "frames must have the same regions");

        const float screenPixelCount = static_cast<float>(current.screenWidth) * static_cast<float>(current.screenHeight);

        std::vector<RegionChange> changes;
        changes.reserve(current.regions.size());
        for (std::size_t i = 0; i < current.regions.size(); ++i)
        {
            changes.push_back(analyseRegion(reference[i], previous.regions[i], current.regions[i], screenPixelCount));
        }
        return changes;
    }
}
