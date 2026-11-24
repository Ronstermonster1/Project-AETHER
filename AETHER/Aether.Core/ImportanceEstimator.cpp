// ImportanceEstimator.cpp

#include "ImportanceEstimator.h"

#include <algorithm>
#include <cassert>

namespace aether
{
    ImportanceEstimator::ImportanceEstimator(const ImportanceEstimatorConfig& config)
        : m_config(config)
    {
        assert(m_config.motionHalfSaturationPixels > 0.0f && "the half-saturation speed must be positive");
    }

    RegionImportance ImportanceEstimator::estimateRegion(const SceneRegion& region, const RegionChange& change) const
    {
        const float speed = std::max(change.screenSpeedPixels, 0.0f);

        RegionImportance result{};
        result.regionId = region.id;
        result.authored = region.authoredImportance;
        result.motionSaliency = speed / (speed + m_config.motionHalfSaturationPixels);
        result.importance = std::clamp(result.authored + m_config.motionWeight * result.motionSaliency, 0.0f, 1.0f);
        return result;
    }

    std::vector<RegionImportance> ImportanceEstimator::estimate(const SceneState& current,
                                                                std::span<const RegionChange> changes) const
    {
        assert(changes.size() == current.regions.size() && "one change per region");

        std::vector<RegionImportance> results;
        results.reserve(current.regions.size());
        for (std::size_t i = 0; i < current.regions.size(); ++i)
        {
            results.push_back(estimateRegion(current.regions[i], changes[i]));
        }
        return results;
    }
}
