// Vec3.cpp
// Definitions of the Vec3 functions that are declared (but not defined) in Vec3.h.

#include "Vec3.h"

#include <algorithm>
#include <cmath>

namespace aether
{
    float length(Vec3 v)
    {
        return std::sqrt(dot(v, v));
    }

    float maxAbsComponent(Vec3 v)
    {
        return std::max({ std::abs(v.x), std::abs(v.y), std::abs(v.z) });
    }
}
