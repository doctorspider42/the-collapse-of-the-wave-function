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
#include "CabinetIr.h"
#include "Envelopes.h"
#include "Shapers.h"

#include <array>
#include <algorithm>
#include <cmath>

namespace collapse::dsp
{

enum class Cabinet { off = 0, cone, steel, numCabinets };

/** Speaker, cabinet and microphone, as three separable problems.

    **The measured part.** Everything from roughly 90 Hz upwards is a real impulse
    response - two microphone captures of one speaker, blended at a fixed position per
    voicing. That band is what a cabinet capture is actually good for, and no filter model
    reproduces the ragged 2-5 kHz region that decides whether a distorted bass sounds like
    an amp or like a plug-in. Source and licence are in CabinetIr.h.

    **The modelled part.** The captures are of a guitar 4x12, and a guitar 4x12 has
    nothing below about 80 Hz - which is most of a bass. Boosting the convolution's
    residual down there would be amplifying 30 dB of nothing, so instead a parallel band
    is taken from the cabinet's *input*, low-passed at fourth order, given the port
    resonance a ported bass cabinet has, and added back. It is honest about being a model:
    the low end is synthesised, the voice is measured, and the two meet around 100 Hz.

    **The non-linear part.** A real cone is level dependent - excursion compresses the
    bass and voice-coil heating pulls sustained level down - and on bass that behaviour is
    not a subtlety, it is half the sound of a loud rig. It runs on a low-passed
    displacement signal before the linear stages, so the non-linearity cannot generate
    anything high enough to alias.

    The convolution is direct and zero latency. At 48 kHz a response is 1024 taps, which
    is where truncation error across the instrument's band drops below about 1 dB; the
    history is kept in a double-written ring so each output sample is one contiguous dot
    product the compiler can vectorise. Nothing here allocates or locks, and the blended
    coefficients are rebuilt at prepare time, not per sample.
*/
class CabSim
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        // Above 96 kHz the response is capped rather than scaled: a full-length 192 kHz
        // convolution costs four times a 48 kHz one for detail that lives below 60 Hz,
        // where the modelled low end has taken over anyway.
        const auto ratio = sampleRate / ir::kSourceSampleRate;
        numTaps = std::max (1, std::min (kMaxTaps,
                                         (int) std::ceil ((double) ir::kLength * ratio)));

        resample (ir::vintageEdge,   coneEdgeIr);
        resample (ir::vintageCentre, coneCentreIr);
        resample (ir::modernEdge,    steelEdgeIr);
        resample (ir::modernCentre,  steelCentreIr);

        // Excursion follows the note, heating follows the passage. Both are slower than
        // the equivalent numbers would be on a guitar cabinet, because the cone is bigger
        // and the frequencies that move it are lower.
        for (auto& e : excursionEnv)
            e.setTimes (sampleRate, 0.0035, 0.110);

        for (auto& e : heatEnv)
            e.setTimes (sampleRate, 0.080, 1.100);

        configure();
        reset();
    }

    void reset() noexcept
    {
        coneHp.reset();
        displacement.reset();
        lowA.reset();
        lowB.reset();
        lowPort.reset();
        lowSub.reset();

        for (auto& channel : history)
            channel.fill (0.0f);

        historyPos.fill (0);

        for (auto& e : excursionEnv) e.reset();
        for (auto& e : heatEnv)      e.reset();
    }

    void setMode (Cabinet newMode) noexcept
    {
        if (mode == newMode)
            return;

        mode = newMode;
        configure();
    }

    Cabinet getMode() const noexcept { return mode; }

    inline float process (int channel, float x) noexcept
    {
        auto& ring = history[(std::size_t) channel];
        auto& pos  = historyPos[(std::size_t) channel];

        if (mode == Cabinet::off)
        {
            // Keeping the history warm means switching a cabinet on never reveals stale
            // samples from the last time one was active.
            ring[(std::size_t) pos] = x;
            ring[(std::size_t) (pos + numTaps)] = x;
            pos = (pos + 1 == numTaps ? 0 : pos + 1);
            return x;
        }

        const auto& v = voicing();

        auto y = coneHp.process (channel, x);

        // ---- the cone, before anything linear ---------------------------------
        // Only the low-frequency displacement is bent, so the harmonics this generates
        // stay in the instrument's band and then meet the response's own roll-off.
        const auto cone      = displacement.process (channel, y);
        const auto excursion = excursionEnv[(std::size_t) channel].process (std::abs (cone));
        const auto heat      = heatEnv[(std::size_t) channel].process (std::abs (y));
        const auto bentCone  = softTight (cone * v.coneDrive) / v.coneDrive;

        y += v.coneAmount * (bentCone - cone);
        y *= 1.0f - v.excursionDepth * (excursion / (0.22f + excursion));
        y *= 1.0f - v.heatDepth      * (heat      / (0.30f + heat));

        // ---- the measured voice ------------------------------------------------
        // The newest sample is written twice, numTaps apart, so the window of the last
        // numTaps samples is always contiguous however far the ring has wrapped.
        ring[(std::size_t) pos] = y;
        ring[(std::size_t) (pos + numTaps)] = y;

        const auto* window = ring.data() + pos + 1;
        const auto* taps   = blendedIr.data();

        // Four independent accumulators over adjacent taps. A single running sum is a
        // serial dependency the compiler is not allowed to reassociate - and without that
        // permission it cannot vectorise, which here costs a factor of four. Splitting
        // the sum in the source grants it explicitly and deterministically.
        auto s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        auto tap = 0;

        for (; tap + 4 <= numTaps; tap += 4)
        {
            s0 += taps[tap]     * window[tap];
            s1 += taps[tap + 1] * window[tap + 1];
            s2 += taps[tap + 2] * window[tap + 2];
            s3 += taps[tap + 3] * window[tap + 3];
        }

        auto out = (s0 + s1) + (s2 + s3);

        for (; tap < numTaps; ++tap)
            out += taps[tap] * window[tap];

        pos = (pos + 1 == numTaps ? 0 : pos + 1);

        // ---- the modelled low end ----------------------------------------------
        // Taken from the cone's output rather than the convolution's, because the
        // convolution has already thrown this band away. Fourth-order, so it does not
        // pile onto the bottom of the measured response, and subsonic-limited so a DC
        // wander in the drive path cannot turn into cone travel that is not there.
        auto low = lowB.process (channel, lowA.process (channel, y));
        low = lowPort.process (channel, low);
        low = lowSub .process (channel, low);

        return (out + v.lowGain * low) * trim;
    }

    /** Fixed voicing constants per cabinet. Public so the offline harness can name what
        it is measuring instead of repeating the numbers. */
    struct Voicing
    {
        const char* name;
        float  micBlend;         // 0 = the darker capture, 1 = the brighter one
        double coneHpHz;         // subsonic protection in front of the cone model
        double displacementHz;   // what counts as "cone travel"
        float  coneDrive, coneAmount, excursionDepth, heatDepth;
        double lowSplitHz;       // corner of the modelled low end
        double portHz, portDb, portQ;
        double subHpHz;          // below the port, a real cabinet unloads fast
        float  lowGain;
        float  trimDb;           // so switching cabinet is not a volume change
    };

    static const Voicing& voicingFor (Cabinet m) noexcept
    {
        // Both low ends are the same weight on purpose: the cabinets differ in their
        // midrange voice and in how hard the cone gives way, not in how loud they are.
        static const Voicing voicings[] =
        {
            { "OFF",   0.50f, 20.0, 140.0, 1.0f, 0.0f, 0.00f, 0.000f,
              100.0, 60.0, 0.0, 1.0, 30.0, 0.0f, 0.0f },

            // CONE - the Celestion V30 pair. Darker capture weighted, a cone that gives
            // way early, and a low port tuned where a big sealed 4x10 sits.
            { "CONE",  0.30f, 26.0, 150.0, 2.20f, 0.20f, 0.17f, 0.075f,
              104.0, 58.0, 2.6, 1.05, 29.0, 1.00f, 0.0f },

            // STEEL - the Eminence DV-77 pair. Brighter capture weighted, a stiffer cone,
            // and a port an octave-and-a-bit up from the low B so fast lines stay tight.
            { "STEEL", 0.68f, 30.0, 132.0, 1.65f, 0.11f, 0.10f, 0.048f,
              96.0, 67.0, 1.6, 1.35, 34.0, 1.00f, 0.0f },
        };

        const auto index = (int) m;
        const auto count = (int) (sizeof (voicings) / sizeof (voicings[0]));
        return voicings[index < 0 ? 0 : (index >= count ? count - 1 : index)];
    }

private:
    // 1024 taps at 48 kHz, doubled at 96 kHz, capped there.
    static constexpr int kMaxTaps = 2048;
    using Impulse = std::array<float, kMaxTaps>;

    const Voicing& voicing() const noexcept { return voicingFor (mode); }

    void configure() noexcept
    {
        const auto& v = voicing();

        coneHp      .coeffs.setHighpass (sampleRate, v.coneHpHz, 0.70);
        displacement.coeffs.setLowpass  (sampleRate, v.displacementHz, 0.72);

        // Two Butterworth sections make the low feed fourth order, matching the slope the
        // measured response rolls off at and keeping the overlap region narrow.
        lowA.coeffs.setLowpass (sampleRate, v.lowSplitHz, 0.70710678);
        lowB.coeffs = lowA.coeffs;

        lowPort.coeffs.setPeak     (sampleRate, v.portHz, v.portDb, v.portQ);
        lowSub .coeffs.setHighpass (sampleRate, v.subHpHz, 0.72);

        trim = std::pow (10.0f, v.trimDb / 20.0f);

        // Coefficients are stored time reversed so the audio thread walks the impulse and
        // the history window forwards together.
        const auto& edge   = mode == Cabinet::steel ? steelEdgeIr   : coneEdgeIr;
        const auto& centre = mode == Cabinet::steel ? steelCentreIr : coneCentreIr;

        for (int n = 0; n < numTaps; ++n)
            blendedIr[(std::size_t) (numTaps - 1 - n)] =
                edge[(std::size_t) n]
                    + v.micBlend * (centre[(std::size_t) n] - edge[(std::size_t) n]);
    }

    /** Windowed-sinc conversion from the stored 48 kHz measurement to the host rate,
        then a trim to exactly unity gain across the judging band. Prepare-time only.

        Unity, not the generator's stored level: the Cabinet selector has an Off position,
        and if the responses carried their own in-band gain then reaching for a cabinet
        would also be reaching for a volume control. Below 48 kHz the kernel's cutoff
        follows the ratio, otherwise resampling to 44.1 kHz would fold the top of the
        response back down. */
    void resample (const float* source, Impulse& result) const noexcept
    {
        result.fill (0.0f);

        const auto ratio     = sampleRate / ir::kSourceSampleRate;
        const auto cutoff    = std::min (1.0, ratio);
        const auto halfWidth = (int) std::ceil (16.0 / cutoff);

        for (int n = 0; n < numTaps; ++n)
        {
            const auto t = (double) n / ratio;
            const auto nearest = (int) std::floor (t);
            auto sum = 0.0;

            for (int k = nearest - halfWidth + 1; k <= nearest + halfWidth; ++k)
            {
                if (k < 0 || k >= ir::kLength)
                    continue;

                sum += (double) source[k] * kernel (t - (double) k, cutoff, halfWidth);
            }

            result[(std::size_t) n] = (float) (sum * cutoff / ratio);
        }

        // Only reached when the cap above truncated the response. Fading the cut keeps the
        // step out of the frequency domain.
        if ((double) numTaps < (double) ir::kLength * ratio - 0.5)
        {
            constexpr int fadeSamples = 64;

            for (int n = std::max (0, numTaps - fadeSamples); n < numTaps; ++n)
            {
                const auto p = (double) (n - (numTaps - fadeSamples))
                             / (double) (fadeSamples - 1);
                result[(std::size_t) n] *= (float) (0.5 + 0.5 * std::cos (kPi * p));
            }
        }

        // Measuring the band level and matching it is exact, and accounts for both the
        // change in tap density and the truncation fade that a correction derived from
        // the ratio alone would miss. It costs nothing that matters outside prepare().
        const auto correction = std::pow (10.0, -bandLevelDb (result) / 20.0);

        for (int n = 0; n < numTaps; ++n)
            result[(std::size_t) n] *= (float) correction;
    }

    double bandLevelDb (const Impulse& impulse) const noexcept
    {
        auto total = 0.0;

        for (int i = 0; i < ir::kBandPoints; ++i)
        {
            const auto f = ir::kBandLowHz
                             * std::pow (ir::kBandHighHz / ir::kBandLowHz,
                                         (double) i / (double) (ir::kBandPoints - 1));
            auto re = 0.0, im = 0.0;

            for (int n = 0; n < numTaps; ++n)
            {
                const auto phase = 2.0 * kPi * f * (double) n / sampleRate;
                re += (double) impulse[(std::size_t) n] * std::cos (phase);
                im -= (double) impulse[(std::size_t) n] * std::sin (phase);
            }

            total += 20.0 * std::log10 (std::sqrt (re * re + im * im) + 1.0e-12);
        }

        return total / (double) ir::kBandPoints;
    }

    static double kernel (double distanceInSamples, double cutoff, int halfWidth) noexcept
    {
        const auto x = distanceInSamples / (double) halfWidth;
        const auto window = 0.42 + 0.5 * std::cos (kPi * x) + 0.08 * std::cos (2.0 * kPi * x);
        const auto arg = kPi * cutoff * distanceInSamples;

        return window * (std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (arg) / arg);
    }

    static constexpr double kPi = 3.14159265358979323846;

    double  sampleRate = 48000.0;
    int     numTaps = ir::kLength;
    Cabinet mode = Cabinet::cone;
    float   trim = 1.0f;

    Biquad<2> coneHp, displacement;
    Biquad<2> lowA, lowB, lowPort, lowSub;
    EnvelopeFollower excursionEnv[2], heatEnv[2];

    Impulse coneEdgeIr {}, coneCentreIr {}, steelEdgeIr {}, steelCentreIr {};
    Impulse blendedIr {};
    std::array<std::array<float, 2 * kMaxTaps>, 2> history {};
    std::array<int, 2> historyPos {};
};

} // namespace collapse::dsp
