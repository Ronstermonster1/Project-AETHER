// VisualErrorEstimator.h
// Visual Error Estimator: predicts how visible the error would be if a region's stored
// result were reused this frame. It uses only cheap information a real renderer has
// BEFORE rendering: the ChangeDetector's score, the region's size and its importance.
//
// Model (deliberately simple; the "error" experiment measures how accurate it is):
//     pixelError   = min(1, justVisibleError * changeScore)    // display units per pixel, 0..1
//     visibleError = pixelError * screenCoverage * importance  // what a controller should rank by
// The detector's tolerances are meant to mark "just visible" changes, so a change score of
// 1.0 maps to justVisibleError (0.02, about 5 of 255 display levels). Above that, error is
// assumed to grow in proportion to the score. That is a strong assumption: real error
// saturates (a ghost can't be more than 100 % wrong) and depends on brightness, tone
// mapping and surface gloss, none of which this estimator sees.
//
// visibleError is roughly "the share of the screen that is wrong, weighted by attention".
//
// Related established work: perceptual image metrics (Daly's Visible Differences Predictor,
// 1993; HDR-VDP; FLIP, Andersson et al., 2020) predict visible differences from rendered
// IMAGES. This estimator predicts them from scene changes instead, which is far cheaper and
// far less accurate.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "ChangeDetector.h"
#include "ImportanceEstimator.h"
#include "SceneRegion.h"
#include "SceneState.h"

namespace aether
{
    struct VisualErrorEstimatorConfig
    {
        float justVisibleError = 0.02f;  // predicted per-pixel error at change score 1.0
    };

    struct ErrorEstimate
    {
        std::uint32_t regionId = 0;
        float pixelError = 0.0f;    // predicted display error per pixel if reused, 0..1
        float visibleError = 0.0f;  // pixelError * screenCoverage * importance
    };

    class VisualErrorEstimator
    {
    public:
        explicit VisualErrorEstimator(const VisualErrorEstimatorConfig& config);

        [[nodiscard]] const VisualErrorEstimatorConfig& config() const { return m_config; }

        [[nodiscard]] ErrorEstimate estimateRegion(const SceneRegion& region,
                                                   const RegionChange& change,
                                                   const RegionImportance& importance) const;

        // changes[i] and importance[i] must belong to current.regions[i].
        [[nodiscard]] std::vector<ErrorEstimate> estimate(const SceneState& current,
                                                          std::span<const RegionChange> changes,
                                                          std::span<const RegionImportance> importance) const;

    private:
        VisualErrorEstimatorConfig m_config;
    };
}
