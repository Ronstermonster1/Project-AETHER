// GroundTruthEvaluator.h
// Measures the TRUE visual error of what is on screen. For experiments only.
//
// AETHER's components must NEVER call this. The controller has to decide from estimates, as
// a real renderer would; this class is the referee that checks those decisions afterwards.
//
// It is still a MODEL, but a more detailed one than anything the controller sees:
//  - A tiny "renderer" turns a region's state into a displayed colour:
//        radiance = albedo * (direct + indirect) + specularStrength * (1 - roughness)^2 * direct
//        display  = radiance / (1 + radiance)            (Reinhard tone mapping, per channel)
//    Because of gloss and tone mapping, the same change in light can look very different on
//    different surfaces. The ChangeDetector does not know that.
//  - A reused result of a region that has MOVED is drawn in its old place. Treating both the
//    old and the new image as discs, the pixels where they don't overlap are wrong twice (a
//    ghost where it was, a hole where it is now), each with an assumed contrast against the
//    background (edgeContrast).
//  - A mesh that has deformed since the stored result makes deformationPerMeshVersion of its
//    pixels wrong for every mesh version it is behind (an assumption: the scene stores no
//    deformation size).
//
// Frame metrics:
//  - meanError:       average display error over all screen pixels (0 = perfect).
//  - visibleFraction: share of the screen whose error is at least visibilityThreshold.
//  - maxRegionError:  the worst single region, because one obvious artefact matters even
//                     when the average is low.
//
// The constants are assumptions. Real ground truth needs real rendered images compared with
// a perceptual metric such as FLIP; that is a Phase 4 task.

#pragma once

#include <span>

#include "SceneRegion.h"
#include "SceneState.h"
#include "Vec3.h"

namespace aether
{
    struct GroundTruthConfig
    {
        float specularStrength = 0.5f;
        float edgeContrast = 0.5f;               // average display difference between a region and what is behind it
        float deformationPerMeshVersion = 0.05f; // share of pixels wrong per mesh version behind
        float visibilityThreshold = 0.02f;       // per-pixel error counted as visible (about 5 of 255 levels)
    };

    struct FrameError
    {
        double meanError = 0.0;
        double visibleFraction = 0.0;
        float maxRegionError = 0.0f;
    };

    class GroundTruthEvaluator
    {
    public:
        explicit GroundTruthEvaluator(const GroundTruthConfig& config);

        [[nodiscard]] const GroundTruthConfig& config() const { return m_config; }

        // The colour the region appears on screen, 0..1 per channel.
        [[nodiscard]] Vec3 displayedColour(const SceneRegion& region) const;

        // Average display error over the region's pixels when 'shown' is on screen but 'truth' is correct.
        [[nodiscard]] float regionError(const SceneRegion& shown, const SceneRegion& truth, float pixelCount) const;

        // shown[i] is what is on screen for region i (for example a TemporalStore's rendered states).
        [[nodiscard]] FrameError evaluate(std::span<const SceneRegion> shown, const SceneState& truth) const;

    private:
        GroundTruthConfig m_config;
    };
}
