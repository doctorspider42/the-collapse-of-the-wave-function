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

/*  Shared, verbatim, with the sibling plug-in Voxy Drive by the same author: the same
    GPLv3 code in two projects rather than a library neither of them needs.          */

#pragma once

#include <cmath>

namespace collapse::dsp
{

/** coeff = 1 - exp(-1 / (tau * fs)). The time constant is the 63 % point; roughly
    4.6 time constants reach 99 %. */
inline float onePoleCoeff (double sampleRate, double timeConstantSeconds) noexcept
{
    if (timeConstantSeconds <= 0.0 || sampleRate <= 0.0)
        return 1.0f;

    return (float) (1.0 - std::exp (-1.0 / (timeConstantSeconds * sampleRate)));
}

/** Asymmetric one-pole follower over a rectified signal - the attack/release pair
    that gives supply sag and meter ballistics their feel. */
struct EnvelopeFollower
{
    float attack = 1.0f, release = 1.0f, value = 0.0f;

    void setTimes (double sampleRate, double attackSeconds, double releaseSeconds) noexcept
    {
        attack  = onePoleCoeff (sampleRate, attackSeconds);
        release = onePoleCoeff (sampleRate, releaseSeconds);
    }

    void reset() noexcept { value = 0.0f; }

    inline float process (float rectified) noexcept
    {
        value += (rectified > value ? attack : release) * (rectified - value);
        return value;
    }
};

/** One-pole high-pass, the cheapest correct place to put the DC that an asymmetric
    non-linearity leaves behind. */
struct DcBlocker
{
    float r = 0.999f;
    float x1 = 0.0f, y1 = 0.0f;

    void prepare (double sampleRate, double cornerHz = 12.0) noexcept
    {
        r = (float) (1.0 - 2.0 * 3.14159265358979323846 * cornerHz / sampleRate);

        if (r < 0.0f) r = 0.0f;
        if (r > 0.99999f) r = 0.99999f;
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    inline float process (float x) noexcept
    {
        const auto y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

} // namespace collapse::dsp
