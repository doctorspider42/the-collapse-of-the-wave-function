/*
    The Collapse of The Wave Function - bass drive, blend and cabinet
    Copyright (C) 2026 doctorspider42

    This program is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later version.

    It links against JUCE 8 under the AGPLv3, so every distributed binary additionally
    carries AGPLv3 section 13. See LICENSE and LICENSE.AGPLv3.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
    PARTICULAR PURPOSE. See the GNU General Public License for more details.
*/

/*  The few odds and ends both the engine and the panel need. Deliberately free of any
    JUCE include, so it can be used from either side.                                */

#pragma once

#include <cmath>
#include <cstdint>

namespace collapse::dsp
{

inline constexpr float kPi    = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;

template <typename T>
inline T clamp (T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

/** Hermite ease. Used everywhere a control should stop feeling like a switch. */
inline float smoothstep (float t) noexcept
{
    t = clamp (t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/** Cheap deterministic noise, for the panel grain and the positions of the collapsed
    packets. Deterministic matters: a user who notices the artwork rearranging itself
    between sessions will find it distracting. */
class Xorshift
{
public:
    explicit Xorshift (std::uint32_t seed = 0x9e3779b9u) noexcept : s (seed | 1u) {}

    std::uint32_t nextInt() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }

    /** Uniform in [0, 1). */
    float nextFloat() noexcept { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }

private:
    std::uint32_t s;
};

} // namespace collapse::dsp
