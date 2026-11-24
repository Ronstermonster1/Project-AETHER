// ScreenSpace.h
// Converting between world units and approximate on-screen pixels.
//
// Model: the simulator has no camera projection yet, so a region is assumed to appear on
// screen as a disc whose area is screenCoverage of the screen and whose radius corresponds
// to the radius of the region's bounding sphere.
//
// Shared by the ChangeDetector and the GroundTruthEvaluator, so both use the same geometry.

#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

#include "SceneRegion.h"
#include "SceneState.h"

namespace aether
{
    // Total number of pixels on screen.
    [[nodiscard]] inline float screenPixelCount(const SceneState& scene)
    {
        return static_cast<float>(scene.screenWidth) * static_cast<float>(scene.screenHeight);
    }

    // Radius of the region's on-screen disc, in pixels.
    [[nodiscard]] inline float onScreenRadiusPixels(const SceneRegion& region, float pixelCount)
    {
        const float areaPixels = region.screenCoverage * pixelCount;
        return std::sqrt(areaPixels / std::numbers::pi_v<float>);
    }

    // How many on-screen pixels one world unit spans for this region.
    [[nodiscard]] inline float pixelsPerWorldUnit(const SceneRegion& region, float pixelCount)
    {
        const float radiusWorld = std::max(region.geometry.boundingRadius, 1e-6f);
        return onScreenRadiusPixels(region, pixelCount) / radiusWorld;
    }
}
