// ChangeDetector.h
// Change Detection Engine: measures how much each region has changed.
//
// Two different comparisons, on purpose:
//  - Geometry, material and lighting are compared with a REFERENCE: the state the region
//    had when it was last rendered. Comparing only with the previous frame misses slow
//    changes: a sun that brightens 0.05 % per frame never crosses a per-frame threshold,
//    yet adds up to 5 % over 100 frames.
//  - Motion is measured from the PREVIOUS frame to the current one (frame-to-frame
//    velocity), because that is what motion tracking needs.
//
// Every change is divided by a tolerance, giving a score where 1.0 means "just large
// enough to matter". The tolerances are rough perceptual heuristics, NOT validated
// thresholds:
//  - displacement: half a pixel (sub-pixel movement is treated as invisible);
//  - lighting: about 2 % change in brightness (roughly the Weber fraction for brightness
//    under good viewing conditions; real visibility depends on adaptation, contrast and
//    the content around it);
//  - material: 1 % of the 0..1 range of any albedo channel or of roughness.
//
// SIMPLIFICATIONS (research model, not reality):
//  - Screen-space distances assume a region looks like a disc the size of its bounding sphere.
//  - Lighting is compared by luminance only, so a change of colour at constant brightness is missed.
//  - Material change is a plain largest-channel difference, not a perceptual colour difference.
//  - A mesh change has unknown size, so it scores exactly 1.0 ("just significant").
//  - The overall score is the largest category score. Whether the maximum or a weighted sum
//    works better is an open question for the ablation experiments.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "SceneRegion.h"
#include "SceneState.h"
#include "Vec3.h"

namespace aether
{
    struct ChangeDetectorConfig
    {
        float displacementTolerancePixels = 0.5f;
        float lightingTolerance = 0.02f;   // as a log-ratio, which is about a 2 % change
        float materialTolerance = 0.01f;
        float minimumLuminance = 0.001f;   // keeps near-black regions from dividing by ~0
    };

    // What the detector found for one region.
    struct RegionChange
    {
        std::uint32_t regionId = 0;

        // Since the reference (the last rendered state):
        bool meshChanged = false;
        float displacementPixels = 0.0f;   // approximate on-screen distance moved
        float materialDelta = 0.0f;        // largest change of any albedo channel or roughness
        float lightingDelta = 0.0f;        // |ln(current / reference luminance)|, about the relative change

        // Since the previous frame (motion tracking):
        Vec3 velocity{};                   // world units per frame
        float screenSpeedPixels = 0.0f;    // approximate on-screen pixels per frame

        // Each change divided by its tolerance. 1.0 = exactly at the threshold.
        float geometryScore = 0.0f;
        float materialScore = 0.0f;
        float lightingScore = 0.0f;
        float changeScore = 0.0f;          // the largest of the three

        [[nodiscard]] bool isSignificant() const { return changeScore >= 1.0f; }
    };

    class ChangeDetector
    {
    public:
        explicit ChangeDetector(const ChangeDetectorConfig& config);

        [[nodiscard]] const ChangeDetectorConfig& config() const { return m_config; }

        // Analyses one region. screenPixelCount = screen width * height.
        [[nodiscard]] RegionChange analyseRegion(const SceneRegion& reference,
                                                 const SceneRegion& previous,
                                                 const SceneRegion& current,
                                                 float screenPixelCount) const;

        // Analyses every region. reference[i], previous.regions[i] and current.regions[i]
        // must all describe region i, so all three must have the same size.
        [[nodiscard]] std::vector<RegionChange> analyse(std::span<const SceneRegion> reference,
                                                        const SceneState& previous,
                                                        const SceneState& current) const;

    private:
        ChangeDetectorConfig m_config;
    };
}
