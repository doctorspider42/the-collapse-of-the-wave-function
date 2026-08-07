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

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace collapse::dsp
{

/** RBJ bi-quad coefficients as plain doubles, normalised so a0 == 1.

    Coefficients are separate from state so one set can drive several channels, and
    so computing them never allocates - which is what makes it safe to recompute at
    control rate on the audio thread.
*/
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    void setBypass() noexcept { b0 = 1.0; b1 = b2 = a1 = a2 = 0.0; }

    void setLowpass (double fs, double f, double q) noexcept
    {
        const auto w = omega (fs, f), c = std::cos (w);
        const auto alpha = std::sin (w) / (2.0 * std::max (0.05, q));
        const auto a0 = 1.0 + alpha;
        b0 = (1.0 - c) * 0.5 / a0;
        b1 = (1.0 - c)       / a0;
        b2 = (1.0 - c) * 0.5 / a0;
        a1 = (-2.0 * c)      / a0;
        a2 = (1.0 - alpha)   / a0;
    }

    void setHighpass (double fs, double f, double q) noexcept
    {
        const auto w = omega (fs, f), c = std::cos (w);
        const auto alpha = std::sin (w) / (2.0 * std::max (0.05, q));
        const auto a0 = 1.0 + alpha;
        b0 =  (1.0 + c) * 0.5 / a0;
        b1 = -(1.0 + c)       / a0;
        b2 =  (1.0 + c) * 0.5 / a0;
        a1 =  (-2.0 * c)      / a0;
        a2 =  (1.0 - alpha)   / a0;
    }

    void setPeak (double fs, double f, double gainDb, double q) noexcept
    {
        if (std::abs (gainDb) < 1.0e-4) return setBypass();

        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = omega (fs, f), c = std::cos (w);
        const auto alpha = std::sin (w) / (2.0 * std::max (0.05, q));
        const auto a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0;
        b1 = (-2.0 * c)        / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = (-2.0 * c)        / a0;
        a2 = (1.0 - alpha / A) / a0;
    }

    /** Slope 0.7-0.8 gives the wide, gentle curve of a console tone control, which is
        what people usually mean by "musical". 1.0 is the steepest non-resonant shelf. */
    void setLowShelf (double fs, double f, double gainDb, double slope = 0.8) noexcept
    {
        if (std::abs (gainDb) < 1.0e-4) return setBypass();

        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = omega (fs, f), c = std::cos (w), s = std::sin (w);
        const auto beta = shelfBeta (A, s, slope);
        const auto Ap1 = A + 1.0, Am1 = A - 1.0;
        const auto a0 = Ap1 + Am1 * c + beta;
        b0 =  A * (Ap1 - Am1 * c + beta) / a0;
        b1 =  2.0 * A * (Am1 - Ap1 * c)  / a0;
        b2 =  A * (Ap1 - Am1 * c - beta) / a0;
        a1 = -2.0 * (Am1 + Ap1 * c)      / a0;
        a2 =  (Ap1 + Am1 * c - beta)     / a0;
    }

    void setHighShelf (double fs, double f, double gainDb, double slope = 0.8) noexcept
    {
        if (std::abs (gainDb) < 1.0e-4) return setBypass();

        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = omega (fs, f), c = std::cos (w), s = std::sin (w);
        const auto beta = shelfBeta (A, s, slope);
        const auto Ap1 = A + 1.0, Am1 = A - 1.0;
        const auto a0 = Ap1 - Am1 * c + beta;
        b0 =  A * (Ap1 + Am1 * c + beta) / a0;
        b1 = -2.0 * A * (Am1 + Ap1 * c)  / a0;
        b2 =  A * (Ap1 + Am1 * c - beta) / a0;
        a1 =  2.0 * (Am1 - Ap1 * c)      / a0;
        a2 =  (Ap1 - Am1 * c - beta)     / a0;
    }

private:
    /** Clamping below Nyquist matters: a user (or a sample-rate change) can otherwise
        ask for a shelf above Nyquist and the coefficients blow up. */
    static double omega (double fs, double f) noexcept
    {
        return 2.0 * 3.14159265358979323846 * std::min (std::max (f, 5.0), fs * 0.49) / fs;
    }

    static double shelfBeta (double A, double s, double slope) noexcept
    {
        return s * std::sqrt (std::max (0.0, (A * A + 1.0) / std::max (0.05, slope)
                                                 - (A - 1.0) * (A - 1.0)));
    }
};

/** Transposed direct form II - good numerical behaviour in float, two state variables. */
struct BiquadState
{
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    inline float process (const BiquadCoeffs& c, float x) noexcept
    {
        const auto y = (float) c.b0 * x + z1;
        z1 = (float) c.b1 * x - (float) c.a1 * y + z2;
        z2 = (float) c.b2 * x - (float) c.a2 * y;
        return y;
    }
};

/** One coefficient set, N channels of state.

    The channel count is a std::size_t rather than an int so that neither the array bound
    nor the subscript needs a signed-to-unsigned conversion, which -Wsign-conversion is
    right to complain about. */
template <std::size_t NumChannels>
struct Biquad
{
    BiquadCoeffs coeffs;
    BiquadState  state[NumChannels];

    void reset() noexcept
    {
        for (auto& s : state)
            s.reset();
    }

    inline float process (int channel, float x) noexcept
    {
        return state[(std::size_t) channel].process (coeffs, x);
    }
};

} // namespace collapse::dsp
