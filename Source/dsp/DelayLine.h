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
#include <vector>

namespace collapse::dsp
{

/** Whole-sample delay, used to hold the dry and bypassed paths back by exactly the
    latency the oversampler adds. Without it, bypassing shifts the audio against every
    other track in the session and the mix control combs. */
class IntegerDelay
{
public:
    void prepare (int numChannelsToUse, int delayInSamples)
    {
        numChannels = numChannelsToUse;
        delay       = std::max (0, delayInSamples);
        buffer.assign ((size_t) (numChannels * std::max (1, delay)), 0.0f);
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    int getDelay() const noexcept { return delay; }

    void process (const float* const* input, float* const* output, int numSamples) noexcept
    {
        if (delay == 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                std::copy (input[ch], input[ch] + numSamples, output[ch]);

            return;
        }

        auto pos = writePos;

        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* line = buffer.data() + (size_t) (ch * delay);
                const auto out = line[pos];
                line[pos] = input[ch][n];
                output[ch][n] = out;
            }

            pos = (pos + 1 == delay ? 0 : pos + 1);
        }

        writePos = pos;
    }

private:
    int numChannels = 0, delay = 0, writePos = 0;
    std::vector<float> buffer;
};

} // namespace collapse::dsp
