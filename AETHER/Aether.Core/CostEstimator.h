// CostEstimator.h
// Cost Estimator: predicts how expensive it is to render, or to reuse, each region.
//
// UNITS: "modelled milliseconds". The constants are chosen so that the default scene costs
// a plausible amount at 1920x1080 (roughly 14 ms for a full frame). They are NOT measured
// on any GPU, so absolute numbers mean nothing; only ratios inside the model do.
// Phase 3 should calibrate them with GPU timestamp queries on real hardware.
//
// Model:
//     render = overhead + triangles * costPerTriangle + pixels * shadingComplexity * costPerShadedPixel
//     reuse  = pixels * reuseCostPerPixel     (reading and compositing a stored result is not free)
//     frame  = frameFixedCost + sum over regions of (render or reuse)
// frameFixedCost covers work no reuse can remove (post-processing, UI, presenting the frame).
// It caps the best possible saving, in the same way as Amdahl's law.
//
// SIMPLIFICATIONS (research model, not reality):
//  - Costs simply add up. Real GPUs overlap work, and cost depends on overdraw, occlusion,
//    cache behaviour, how busy the GPU is, and the order things are drawn in.
//  - Shading cost is linear in pixels and complexity. Lighting is not costed separately,
//    so "update only the lighting" is not an option yet.
//  - In the simulator the estimate is also the "true" cost, so estimation error cannot
//    happen yet. A later experiment should add deliberate estimation error to test how
//    sensitive the controller is to it.
//
// Related established work: Funkhouser & Sequin (1993) predicted per-object rendering cost
// and benefit to choose levels of detail that hold a target frame rate. That is very close
// to what AETHER's controller will do, so it is an important baseline to discuss.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "SceneRegion.h"
#include "SceneState.h"

namespace aether
{
    struct CostEstimatorConfig
    {
        float frameFixedCost = 3.0f;           // per frame, never reusable
        float regionOverheadCost = 0.01f;      // per rendered region: draw-call and state setup
        float costPerTriangle = 0.5e-6f;       // 0.5 ms per million triangles
        float costPerShadedPixel = 3.0e-6f;    // per pixel at shadingComplexity 1 (~6 ms for a full 1080p screen)
        float reuseCostPerPixel = 0.3e-6f;     // reading and compositing a stored pixel (10 % of standard shading)
    };

    // Modelled cost of one region, in modelled milliseconds.
    struct RegionCost
    {
        std::uint32_t regionId = 0;
        float overhead = 0.0f;
        float geometry = 0.0f;
        float shading = 0.0f;
        float render = 0.0f;   // overhead + geometry + shading
        float reuse = 0.0f;    // cost of showing the stored result instead
    };

    class CostEstimator
    {
    public:
        explicit CostEstimator(const CostEstimatorConfig& config);

        [[nodiscard]] const CostEstimatorConfig& config() const { return m_config; }

        // screenPixelCount = screen width * height.
        [[nodiscard]] RegionCost estimateRegion(const SceneRegion& region, float screenPixelCount) const;

        // One cost per region, in region order.
        [[nodiscard]] std::vector<RegionCost> estimate(const SceneState& scene) const;

        // frameFixedCost + every region's render cost: the "re-render everything" baseline.
        [[nodiscard]] double fullRenderFrameCost(std::span<const RegionCost> costs) const;

    private:
        CostEstimatorConfig m_config;
    };
}
