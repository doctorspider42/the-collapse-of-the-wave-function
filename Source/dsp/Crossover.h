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

#pragma once

#include "Biquad.h"

namespace collapse::dsp
{

/** Linkwitz-Riley 4th-order two-way split: two cascaded Butterworth sections per band.

    The whole plug-in rests on this. Distortion is a low-frequency signal's enemy - two
    strong fundamentals an octave apart intermodulate into everything between them - so
    the bottom of the instrument is taken out of the drive path entirely and put back
    afterwards, rather than being distorted and then EQ'd back into shape.

    Fourth order specifically, and not a pair of one-pole filters: at 24 dB/octave the
    band that reaches the shaper genuinely excludes the fundamental instead of merely
    leaning away from it, and the two outputs sum to an allpass - flat magnitude at every
    frequency, whatever the crossover is set to. That last property is what lets Blend
    sit at 0 and be a straight wire as far as the frequency response is concerned.

    (Allpass, not identity: summing the bands returns the signal with its phase rotated
    around the crossover. Audible on its own it is not; against a parallel untouched copy
    of the same take it will comb, same as any analogue crossover would.)
*/
class LinkwitzRiley4
{
public:
    void setCrossover (double sampleRate, double frequency) noexcept
    {
        // Two identical Butterworth sections in series make a Linkwitz-Riley response:
        // -6 dB at the corner from each band, which is what makes the sum flat.
        constexpr auto butterworthQ = 0.70710678118654752;

        lowCoeffs .setLowpass  (sampleRate, frequency, butterworthQ);
        highCoeffs.setHighpass (sampleRate, frequency, butterworthQ);
    }

    void reset() noexcept
    {
        for (auto& s : lowA)  s.reset();
        for (auto& s : lowB)  s.reset();
        for (auto& s : highA) s.reset();
        for (auto& s : highB) s.reset();
    }

    inline void process (int channel, float x, float& low, float& high) noexcept
    {
        const auto c = (std::size_t) channel;

        low  = lowB[c] .process (lowCoeffs,  lowA[c] .process (lowCoeffs,  x));
        high = highB[c].process (highCoeffs, highA[c].process (highCoeffs, x));
    }

private:
    BiquadCoeffs lowCoeffs, highCoeffs;
    BiquadState  lowA[2], lowB[2], highA[2], highB[2];
};

} // namespace collapse::dsp
