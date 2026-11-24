// VisualErrorEstimator.cpp

#include "VisualErrorEstimator.h"

#include <algorithm>
#include <cassert>

namespace aether
{
    VisualErrorEstimator::VisualErrorEstimator(const VisualErrorEstimatorConfig& config)
        : m_config(config)
    {
        assert(m_config.justVisibleError > 0.0f && "justVisibleError must be positive");
    }

    ErrorEstimate VisualErrorEstimator::estimateRegion(const SceneRegion& region,
                                                       const RegionChange& change,
                                                       const RegionImportance& importance) const
    {
        ErrorEstimate estimate{};
        estimate.regionId = region.id;
        estimate.pixelError = std::min(1.0f, m_config.justVisibleError * change.changeScore);
        estimate.visibleError = estimate.pixelError * region.screenCoverage * importance.importance;
        return estimate;
    }

    std::vector<ErrorEstimate> VisualErrorEstimator::estimate(const SceneState& current,
                                                              std::span<const RegionChange> changes,
                                                              std::span<const RegionImportance> importance) const
    {
        assert(changes.size() == current.regions.size() && "one change per region");
        assert(importance.size() == current.regions.size() && "one importance per region");

        std::vector<ErrorEstimate> estimates;
        estimates.reserve(current.regions.size());
        for (std::size_t i = 0; i < current.regions.size(); ++i)
        {
            estimates.push_back(estimateRegion(current.regions[i], changes[i], importance[i]));
        }
        return estimates;
    }
}
