// SceneState.h
// The complete ground-truth state of the simulated scene for one frame.
//
// Copying a SceneState is how the simulator keeps "the previous frame": with 100
// regions of 80 bytes each, a copy is about 8 KB, which is negligible.

#pragma once

#include <cstdint>
#include <vector>

#include "SceneRegion.h"

namespace aether
{
    struct SceneState
    {
        std::uint64_t frameIndex = 0;      // 0 = the first frame
        std::uint32_t screenWidth = 1920;  // output resolution in pixels; the camera is fixed,
        std::uint32_t screenHeight = 1080; // so this never changes during a run
        std::vector<SceneRegion> regions;  // regions[i].id == i
    };
}
