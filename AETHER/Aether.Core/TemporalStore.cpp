// TemporalStore.cpp

#include "TemporalStore.h"

#include <cassert>
#include <cmath>

namespace aether
{
    TemporalStore::TemporalStore(const TemporalStoreConfig& config, const SceneState& initialScene)
        : m_config(config)
        , m_renderedStates(initialScene.regions)
        // Parentheses, not braces: vector(count, value) makes 'count' copies of 'value',
        // while vector{count, value} would make a vector holding just those two numbers.
        , m_renderedFrames(initialScene.regions.size(), initialScene.frameIndex)
        , m_renderCounts(initialScene.regions.size(), 1)
    {
        assert(m_config.confidenceTimeConstantFrames > 0.0f && "the time constant must be positive");
    }

    const SceneRegion& TemporalStore::renderedState(std::uint32_t regionId) const
    {
        assert(regionId < m_renderedStates.size() && "unknown region id");
        return m_renderedStates[regionId];
    }

    std::uint64_t TemporalStore::renderedFrame(std::uint32_t regionId) const
    {
        assert(regionId < m_renderedFrames.size() && "unknown region id");
        return m_renderedFrames[regionId];
    }

    std::uint64_t TemporalStore::renderCount(std::uint32_t regionId) const
    {
        assert(regionId < m_renderCounts.size() && "unknown region id");
        return m_renderCounts[regionId];
    }

    std::uint64_t TemporalStore::age(std::uint32_t regionId, std::uint64_t currentFrame) const
    {
        const std::uint64_t rendered = renderedFrame(regionId);
        // Unsigned subtraction would wrap around to a huge number if currentFrame were earlier.
        return currentFrame >= rendered ? currentFrame - rendered : 0;
    }

    void TemporalStore::recordRender(const SceneRegion& state, std::uint64_t frame)
    {
        const std::uint32_t id = state.id;
        assert(id < m_renderedStates.size() && "unknown region id");
        m_renderedStates[id] = state;
        m_renderedFrames[id] = frame;
        ++m_renderCounts[id];
    }

    float TemporalStore::confidence(const RegionChange& change, std::uint64_t currentFrame) const
    {
        const auto ageFrames = static_cast<float>(age(change.regionId, currentFrame));
        const float ageTerm = std::exp(-ageFrames / m_config.confidenceTimeConstantFrames);
        const float changeTerm = 1.0f / (1.0f + change.changeScore);
        return ageTerm * changeTerm;
    }
}
