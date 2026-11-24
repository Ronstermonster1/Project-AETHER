// Vec3.h
// A minimal 3-component vector used for positions, displacements and linear RGB colours.
// Deliberately small: new operations are added only when a component actually needs them.

#pragma once

namespace aether
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    [[nodiscard]] constexpr Vec3 operator+(Vec3 a, Vec3 b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    [[nodiscard]] constexpr Vec3 operator-(Vec3 a, Vec3 b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    [[nodiscard]] constexpr Vec3 operator*(Vec3 v, float s)
    {
        return { v.x * s, v.y * s, v.z * s };
    }

    [[nodiscard]] constexpr Vec3 operator*(float s, Vec3 v)
    {
        return v * s;
    }

    [[nodiscard]] constexpr float dot(Vec3 a, Vec3 b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // Multiplies matching components: (a.x * b.x, a.y * b.y, a.z * b.z).
    // Used for colours, e.g. albedo times incoming light.
    [[nodiscard]] constexpr Vec3 multiplyComponents(Vec3 a, Vec3 b)
    {
        return { a.x * b.x, a.y * b.y, a.z * b.z };
    }

    // Bit-for-bit comparison. Correct for ground-truth questions such as
    // "did the scenario touch this value at all?" or "are two runs identical?".
    // NOT for change detection: tiny float differences would count as changes.
    // That is why this is a named function and Vec3 has no operator==.
    [[nodiscard]] constexpr bool exactlyEqual(Vec3 a, Vec3 b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    // Declared here, defined in Vec3.cpp (compiled into Aether.Core.lib).
    [[nodiscard]] float length(Vec3 v);

    // The largest of |x|, |y| and |z|. For a colour difference: the worst channel.
    [[nodiscard]] float maxAbsComponent(Vec3 v);
}
