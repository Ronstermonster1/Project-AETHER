// Random.h
// Deterministic pseudo-random numbers for reproducible experiments.
//
// Why not std::uniform_real_distribution or std::shuffle? The C++ standard defines
// exactly which numbers std::mt19937 produces for a given seed, but it does NOT define
// how the standard distributions turn those numbers into floats or indices. MSVC, GCC
// and Clang can give different results from the same seed. Converting the raw engine
// output ourselves means every AETHER experiment can be reproduced from its seed alone.

#pragma once

#include <cstdint>
#include <random>

namespace aether
{
    class Random
    {
    public:
        explicit Random(std::uint32_t seed);

        // Uniform float in [0, 1).
        [[nodiscard]] float uniform01();

        // Uniform float between minValue and maxValue.
        [[nodiscard]] float range(float minValue, float maxValue);

        // True with the given probability (0 = never, 1 = always).
        [[nodiscard]] bool chance(float probability);

        // Uniform integer in [0, count). count must be greater than zero.
        // Precise enough for counts up to a few million.
        [[nodiscard]] std::uint32_t index(std::uint32_t count);

    private:
        std::mt19937 m_engine;
    };
}
