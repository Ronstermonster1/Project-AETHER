// SceneRegion.h
// The basic unit of the AETHER simulation.
//
// A SceneRegion is one part of the scene (an object or small cluster of objects)
// together with the area it covers on screen. Every AETHER component reasons about
// regions: change detection compares a region with its previous-frame copy, the cost
// estimator predicts what updating it would cost, and the controller decides per region
// whether to recompute it or reuse earlier results.
//
// DESIGN RULE: this struct holds only the scene's actual state ("ground truth").
// Values AETHER derives (change scores, importance, cost, confidence...) are produced
// by the analysis components and stored in their own structs, never in here.
//
// SIMPLIFICATIONS (research model, not reality):
//  - Regions are independent: they do not overlap or occlude each other on screen.
//  - The camera is fixed, so screenCoverage changes only when the scenario changes it.
//  - triangleCount and shadingComplexity feed an invented cost model,
//    not measurements of a real GPU.

#pragma once

#include <cstdint>
#include <type_traits>

#include "Vec3.h"

namespace aether
{
    // Shape and placement of the region.
    struct GeometryState
    {
        Vec3 position{};                   // world-space centre, in world units
        float boundingRadius = 1.0f;       // radius of a sphere enclosing the region (world units);
        // used later to convert world motion into on-screen motion
        std::uint32_t triangleCount = 1000;
        std::uint32_t meshVersion = 0;     // increases whenever the vertex data changes
        // (deformation, animation, level-of-detail switch)
    };

    // Surface appearance of the region.
    struct MaterialState
    {
        Vec3 albedo{ 0.5f, 0.5f, 0.5f };   // base colour, linear RGB, each channel 0..1
        float roughness = 0.5f;            // 0 = mirror-like, 1 = fully matte
        float shadingComplexity = 1.0f;    // relative per-pixel shading cost (1 = standard material)
    };

    // Light arriving at the region, split the way many real renderers split it.
    struct LightingState
    {
        Vec3 direct{ 1.0f, 1.0f, 1.0f };   // light straight from light sources (linear RGB)
        Vec3 indirect{ 0.1f, 0.1f, 0.1f }; // light bounced off other surfaces (linear RGB)
    };

    struct SceneRegion
    {
        std::uint32_t id = 0;
        GeometryState geometry{};
        MaterialState material{};
        LightingState lighting{};
        float screenCoverage = 0.01f;      // fraction of the screen's pixels covered, 0..1
        float authoredImportance = 0.5f;   // importance assigned by the scene designer, 0..1
    };

    // SceneRegion must stay plain data so per-frame snapshots are cheap to copy.
    // If someone later adds a member such as std::string or std::vector,
    // this check fails and forces a deliberate design decision.
    static_assert(std::is_trivially_copyable_v<SceneRegion>,
        "SceneRegion must remain plain data (trivially copyable).");
}