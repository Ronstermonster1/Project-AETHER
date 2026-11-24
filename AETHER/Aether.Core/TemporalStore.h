// TemporalStore.h
// Temporal Information Store: remembers, for every region, what was last rendered.
//
// For each region it keeps:
//  - the region's state at the moment it was last rendered. This is the reference the
//    ChangeDetector compares against;
//  - the frame it was rendered in, so we know how old the displayed result is;
//  - how many times it has been rendered, so we can spot regions that are never refreshed.
//
// It also estimates TEMPORAL CONFIDENCE: how much we trust a stored result if we reuse
// it now. This is a heuristic, not a measured quantity:
//
//     confidence = exp(-age / timeConstant) * 1 / (1 + changeScore)
//
//  - Age term: even when no change is detected, older results are trusted less, because
//    the detector can miss things (for example a colour shift at constant brightness).
//    With the default 120-frame time constant, a 2-second-old result keeps about 37 %.
//  - Change term: 1.0 with no change, 0.5 at the detection threshold, falling towards 0
//    as the change grows.
// Whether either term actually predicts visible error must be tested against the ground truth.
//
// Related established work: the render cache (Walter et al., 1999) and reverse reprojection
// caching (Nehab et al., 2007) store per-pixel results for reuse, and denoisers such as
// SVGF (Schied et al., 2017) track how long each pixel's history is. This store is much
// coarser (one entry per region). In the simulator it stores each region's STATE rather
// than rendered pixels, because the "rendered result" can be recomputed from that state.
// A GPU version (Phase 3) must store actual rendered output, which costs far more memory.
//
// Layout: "structure of arrays". The rendered states sit in one contiguous vector, so they
// can be passed straight to ChangeDetector::analyse as a std::span.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ChangeDetector.h"
#include "SceneRegion.h"
#include "SceneState.h"

namespace aether
{
    struct TemporalStoreConfig
    {
        float confidenceTimeConstantFrames = 120.0f;  // age at which the age term falls to about 37 %
    };

    class TemporalStore
    {
    public:
        // Starts with every region of initialScene rendered in initialScene.frameIndex.
        TemporalStore(const TemporalStoreConfig& config, const SceneState& initialScene);

        [[nodiscard]] std::size_t regionCount() const { return m_renderedStates.size(); }

        // Every region's state when it was last rendered, indexed by region id.
        [[nodiscard]] std::span<const SceneRegion> renderedStates() const { return m_renderedStates; }

        [[nodiscard]] const SceneRegion& renderedState(std::uint32_t regionId) const;
        [[nodiscard]] std::uint64_t renderedFrame(std::uint32_t regionId) const;
        [[nodiscard]] std::uint64_t renderCount(std::uint32_t regionId) const;

        // Frames since the region was last rendered.
        [[nodiscard]] std::uint64_t age(std::uint32_t regionId, std::uint64_t currentFrame) const;

        // Stores a freshly rendered region. Its id says which entry to replace.
        void recordRender(const SceneRegion& state, std::uint64_t frame);

        // Trust in reusing the stored result now: 1 = full, 0 = none.
        // 'change' must compare the CURRENT state with this store's rendered state.
        [[nodiscard]] float confidence(const RegionChange& change, std::uint64_t currentFrame) const;

    private:
        TemporalStoreConfig m_config;
        std::vector<SceneRegion> m_renderedStates;    // [i] = region i
        std::vector<std::uint64_t> m_renderedFrames;  // [i] = region i
        std::vector<std::uint64_t> m_renderCounts;    // [i] = region i
    };
}
