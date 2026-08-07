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
#include <cmath>
#include <vector>

namespace collapse::dsp
{

/** Linear-phase polyphase FIR oversampler for a non-linear stage.

    Only the non-linearity needs oversampling, so this wraps just that: upsample,
    shape, downsample. Both directions use the same Kaiser-windowed sinc kernel.

    Latency is deliberately an integer number of base-rate samples. A round trip
    delays by 2 * order / factor samples, and `order` is chosen so that divides
    exactly - which is what lets the host compensate, and what lets the dry and
    bypassed paths be delayed by a plain integer delay line and null perfectly
    against the wet path.

    All allocation happens in prepare(); upsample()/downsample() are allocation and
    branch free.
*/
class Oversampler
{
public:
    /** factor must be 1, 2 or 4. maxBaseBlock is the largest number of base-rate
        samples that will be passed to upsample() in one call. */
    void prepare (int factorToUse, int numChannelsToUse, int maxBaseBlock)
    {
        factor      = (factorToUse == 4 ? 4 : (factorToUse == 2 ? 2 : 1));
        numChannels = numChannelsToUse;
        maxBase     = maxBaseBlock;

        // Order is a multiple of factor / 2, so the round-trip latency
        // 2 * order / factor is a whole number of base-rate samples.
        order = (factor == 4 ? 64 : (factor == 2 ? 48 : 0));

        const auto numTaps = 2 * order + 1;
        buildKernel (numTaps);

        osBuffer.assign ((size_t) (numChannels * factor * maxBase), 0.0f);
        upHistory.assign ((size_t) (numChannels * 2 * tapsPerPhase), 0.0f);
        downHistory.assign ((size_t) (numChannels * 2 * numTaps), 0.0f);
        upPos.assign ((size_t) numChannels, 0);
        downPos.assign ((size_t) numChannels, 0);
    }

    /** Round-trip latency in base-rate samples. */
    int getLatencySamples() const noexcept { return factor > 1 ? 2 * order / factor : 0; }

    int getFactor() const noexcept { return factor; }

    void reset() noexcept
    {
        std::fill (upHistory.begin(), upHistory.end(), 0.0f);
        std::fill (downHistory.begin(), downHistory.end(), 0.0f);
        std::fill (osBuffer.begin(), osBuffer.end(), 0.0f);
        std::fill (upPos.begin(), upPos.end(), 0);
        std::fill (downPos.begin(), downPos.end(), 0);
    }

    /** Oversampled scratch buffer for one channel: factor * numSamples valid samples
        after upsample(), and the place to write the shaped signal before
        downsample(). */
    float* upsampled (int channel) noexcept
    {
        return osBuffer.data() + (size_t) (channel * factor * maxBase);
    }

    void upsample (const float* const* input, int numSamples) noexcept
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* x   = input[ch];
            auto* out       = upsampled (ch);
            auto* hist      = upHistory.data() + (size_t) (ch * 2 * tapsPerPhase);
            const auto taps = tapsPerPhase;
            auto pos        = upPos[(size_t) ch];

            if (factor == 1)
            {
                for (int n = 0; n < numSamples; ++n)
                    out[n] = x[n];

                continue;
            }

            for (int n = 0; n < numSamples; ++n)
            {
                // Written twice so the history reads back as one descending run.
                hist[pos]        = x[n];
                hist[pos + taps] = x[n];

                const auto* h = hist + pos + taps;

                for (int p = 0; p < factor; ++p)
                {
                    const auto* tap = upTaps.data() + (size_t) (p * taps);
                    auto acc = 0.0f;

                    for (int j = 0; j < taps; ++j)
                        acc += tap[j] * h[-j];

                    out[n * factor + p] = acc;
                }

                pos = (pos + 1 == taps ? 0 : pos + 1);
            }

            upPos[(size_t) ch] = pos;
        }
    }

    void downsample (float* const* output, int numSamples) noexcept
    {
        const auto numTaps = 2 * order + 1;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* y          = output[ch];
            const auto* in   = upsampled (ch);
            auto* hist       = downHistory.data() + (size_t) (ch * 2 * numTaps);
            auto pos         = downPos[(size_t) ch];

            if (factor == 1)
            {
                for (int n = 0; n < numSamples; ++n)
                    y[n] = in[n];

                continue;
            }

            for (int n = 0; n < numSamples; ++n)
            {
                // The output sample is centred on high-rate index n * factor, so that
                // sample is pushed first and the remainder of the frame afterwards.
                // Centring it anywhere else would make the latency fractional.
                for (int p = 0; p < factor; ++p)
                {
                    hist[pos]           = in[n * factor + p];
                    hist[pos + numTaps] = in[n * factor + p];

                    if (p == 0)
                    {
                        const auto* h = hist + pos + numTaps;
                        auto acc = 0.0f;

                        for (int j = 0; j < numTaps; ++j)
                            acc += downTaps[(size_t) j] * h[-j];

                        y[n] = acc;
                    }

                    pos = (pos + 1 == numTaps ? 0 : pos + 1);
                }
            }

            downPos[(size_t) ch] = pos;
        }
    }

private:
    void buildKernel (int numTaps)
    {
        upTaps.clear();
        downTaps.assign ((size_t) numTaps, 0.0f);
        tapsPerPhase = 1;

        if (factor == 1)
        {
            upTaps.assign (1, 1.0f);
            downTaps.assign (1, 1.0f);
            return;
        }

        // 76 dB stopband. At 48 kHz base and 4x this puts the transition between
        // roughly 17 kHz and 31 kHz - flat across the guitar band, and everything the
        // shaper generates above base Nyquist is 76 dB down before it can fold back.
        constexpr double stopbandDb = 76.0;
        const auto beta = 0.1102 * (stopbandDb - 8.7);

        std::vector<double> h ((size_t) numTaps, 0.0);
        const auto centre = (double) order;

        for (int n = 0; n < numTaps; ++n)
        {
            const auto k = (double) n - centre;
            const auto s = sinc (k / (double) factor);           // gain = factor in band
            const auto t = 2.0 * (double) n / (double) (numTaps - 1) - 1.0;
            const auto w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - t * t)))
                               / besselI0 (beta);
            h[(size_t) n] = s * w;
        }

        // Normalise so the interpolator's passband gain is exactly `factor`: with
        // zero stuffing only every factor-th tap contributes to any one output, so
        // each polyphase branch must sum to 1.
        for (int p = 0; p < factor; ++p)
        {
            auto sum = 0.0;

            for (int j = p; j < numTaps; j += factor)
                sum += h[(size_t) j];

            if (std::abs (sum) > 1.0e-12)
                for (int j = p; j < numTaps; j += factor)
                    h[(size_t) j] /= sum;
        }

        tapsPerPhase = (numTaps + factor - 1) / factor;
        upTaps.assign ((size_t) (factor * tapsPerPhase), 0.0f);

        for (int p = 0; p < factor; ++p)
            for (int j = 0; j < tapsPerPhase; ++j)
            {
                const auto index = factor * j + p;

                if (index < numTaps)
                    upTaps[(size_t) (p * tapsPerPhase + j)] = (float) h[(size_t) index];
            }

        // The decimator wants unity passband gain, so it uses the same kernel scaled
        // down by the factor.
        for (int n = 0; n < numTaps; ++n)
            downTaps[(size_t) n] = (float) (h[(size_t) n] / (double) factor);
    }

    static double sinc (double x) noexcept
    {
        constexpr auto pi = 3.14159265358979323846;

        if (std::abs (x) < 1.0e-12)
            return 1.0;

        return std::sin (pi * x) / (pi * x);
    }

    static double besselI0 (double x) noexcept
    {
        auto sum = 1.0, term = 1.0;

        for (int k = 1; k < 64; ++k)
        {
            term *= (x * x) / (4.0 * (double) k * (double) k);
            sum  += term;

            if (term < 1.0e-17 * sum)
                break;
        }

        return sum;
    }

    int factor = 1, numChannels = 0, maxBase = 0, order = 0, tapsPerPhase = 1;

    std::vector<float> upTaps, downTaps;
    std::vector<float> osBuffer, upHistory, downHistory;
    std::vector<int>   upPos, downPos;
};

} // namespace collapse::dsp
