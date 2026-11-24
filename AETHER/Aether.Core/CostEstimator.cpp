// CostEstimator.cpp

#include "CostEstimator.h"

#include <cassert>

namespace aether
{
    CostEstimator::CostEstimator(const CostEstimatorConfig& config)
        : m_config(config)
    {
        assert(m_config.frameFixedCost >= 0.0f && m_config.regionOverheadCost >= 0.0f
            && m_config.costPerTriangle >= 0.0f && m_config.costPerShadedPixel >= 0.0f
            && m_config.reuseCostPerPixel >= 0.0f && "costs cannot be negative");
    }

    RegionCost CostEstimator::estimateRegion(const SceneRegion& region, float screenPixelCount) const
    {
        const float pixels = region.screenCoverage * screenPixelCount;

        RegionCost cost{};
        cost.regionId = region.id;
        cost.overhead = m_config.regionOverheadCost;
        cost.geometry = static_cast<float>(region.geometry.triangleCount) * m_config.costPerTriangle;
        cost.shading = pixels * region.material.shadingComplexity * m_config.costPerShadedPixel;
        cost.render = cost.overhead + cost.geometry + cost.shading;
        cost.reuse = pixels * m_config.reuseCostPerPixel;
        return cost;
    }

    std::vector<RegionCost> CostEstimator::estimate(const SceneState& scene) const
    {
        const float screenPixelCount = static_cast<float>(scene.screenWidth) * static_cast<float>(scene.screenHeight);

        std::vector<RegionCost> costs;
        costs.reserve(scene.regions.size());
        for (const SceneRegion& region : scene.regions)
        {
            costs.push_back(estimateRegion(region, screenPixelCount));
        }
        return costs;
    }

    double CostEstimator::fullRenderFrameCost(std::span<const RegionCost> costs) const
    {
        // Sum in double: adding many small floats in float loses precision.
        double total = m_config.frameFixedCost;
        for (const RegionCost& cost : costs)
        {
            total += cost.render;
        }
        return total;
    }
}
