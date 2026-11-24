// ImportanceEstimator.h
// Visual importance: how much a viewer is likely to care about each PIXEL of a region.
//
// Importance is deliberately per pixel. A region's size (screenCoverage) is accounted for
// separately, in the cost and error estimates; putting it in here as well would count it twice.
//
// Model (a heuristic, not a validated perceptual model):
//     importance     = clamp(authoredImportance + motionWeight * motionSaliency, 0, 1)
//     motionSaliency = speed / (speed + halfSaturationSpeed)   (0 when still, towards 1 when fast)
//  - authoredImportance: the scene designer's hint (player character, objective, ...).
//  - Motion attracts attention, so moving regions gain importance, up to motionWeight.
//
// What this ignores:
//  - Motion also HIDES detail: people see less detail on fast-moving objects, and motion
//    blur hides even more. That effect pushes the other way and belongs in the error model.
//  - Where the viewer is looking (gaze, screen centre) is unknown: the simulator has no
//    camera projection yet.
//  - In the default scenario, authoredImportance is random and unrelated to behaviour.
//
// Related established work: saliency models (Itti, Koch & Niebur, 1998); Yee, Pattanaik &
// Greenberg (2001) used saliency and spatiotemporal sensitivity to decide where to spend
// rendering effort; foveated rendering (Guenter et al., 2012) and variable rate shading
// spend less effort where the viewer is less sensitive. Spending rendering work according
// to importance is an established idea, not a new one.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "ChangeDetector.h"
#include "SceneRegion.h"
#include "SceneState.h"

namespace aether
{
    struct ImportanceEstimatorConfig
    {
        float motionWeight = 0.3f;                // most importance motion can add (0 = ignore motion)
        float motionHalfSaturationPixels = 4.0f;  // on-screen speed at which motion adds half of motionWeight
    };

    struct RegionImportance
    {
        std::uint32_t regionId = 0;
        float authored = 0.0f;        // the designer's hint, 0..1
        float motionSaliency = 0.0f;  // 0..1
        float importance = 0.0f;      // final per-pixel importance, 0..1
    };

    class ImportanceEstimator
    {
    public:
        explicit ImportanceEstimator(const ImportanceEstimatorConfig& config);

        [[nodiscard]] const ImportanceEstimatorConfig& config() const { return m_config; }

        // 'change' supplies the region's frame-to-frame motion (RegionChange::screenSpeedPixels).
        [[nodiscard]] RegionImportance estimateRegion(const SceneRegion& region, const RegionChange& change) const;

        // changes[i] must belong to current.regions[i].
        [[nodiscard]] std::vector<RegionImportance> estimate(const SceneState& current,
                                                             std::span<const RegionChange> changes) const;

    private:
        ImportanceEstimatorConfig m_config;
    };
}
