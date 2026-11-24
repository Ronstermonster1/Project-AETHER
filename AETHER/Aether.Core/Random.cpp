// Random.cpp

#include "Random.h"

#include <cassert>

namespace aether
{
    Random::Random(std::uint32_t seed)
        : m_engine(seed)
    {
    }

    float Random::uniform01()
    {
        // Keep the top 24 bits of the 32-bit engine output. A float has a 24-bit
        // significand, so every value k / 2^24 (k = 0 .. 2^24 - 1) is represented
        // exactly and the result can never reach 1.0.
        const std::uint32_t bits = static_cast<std::uint32_t>(m_engine()) >> 8;
        return static_cast<float>(bits) * (1.0f / 16777216.0f);
    }

    float Random::range(float minValue, float maxValue)
    {
        return minValue + (maxValue - minValue) * uniform01();
    }

    bool Random::chance(float probability)
    {
        return uniform01() < probability;
    }

    std::uint32_t Random::index(std::uint32_t count)
    {
        assert(count > 0 && "Random::index needs a non-empty range");

        const auto value = static_cast<std::uint32_t>(uniform01() * static_cast<float>(count));
        // Float rounding can, very rarely, produce exactly 'count'. Clamp it.
        return value < count ? value : count - 1;
    }
}
