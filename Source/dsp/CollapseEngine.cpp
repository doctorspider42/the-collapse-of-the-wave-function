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

#include "CollapseEngine.h"

#include <algorithm>
#include <cmath>

namespace collapse::dsp
{

void CollapseEngine::prepare (double newSampleRate, int numChannelsToUse, int maxBlockToUse)
{
    sampleRate  = newSampleRate;
    numChannels = numChannelsToUse < 1 ? 1 : (numChannelsToUse > 2 ? 2 : numChannelsToUse);
    maxBlock    = std::max (1, maxBlockToUse);

    // 4x below 88.2 kHz, 2x above it: the shaper's harmonics scale with the base rate, so
    // a session already running at 96 kHz does not need the same factor to keep folding
    // inaudible - and halving it there halves the cost.
    oversampler.prepare (sampleRate >= 88200.0 ? 2 : 4, numChannels, maxBlock);
    osSampleRate = sampleRate * (double) oversampler.getFactor();

    cab.prepare (sampleRate);

    // Three stereo scratch bands, sized once, here, where allocating is allowed.
    buffers.assign ((std::size_t) (3 * 2 * maxBlock), 0.0f);

    const auto latency = oversampler.getLatencySamples();
    lowDelay .prepare (numChannels, latency);
    highDelay.prepare (numChannels, latency);

    // 8 Hz in, 14 Hz out. The input blocker is deliberately far below anything the
    // instrument plays: it exists to stop a DC-offset interface from biasing the shaper,
    // not to shape the bottom of a low B.
    for (auto& b : dcIn)     b.prepare (sampleRate, 8.0);
    for (auto& b : dcOut)    b.prepare (sampleRate, 14.0);
    for (auto& b : octaveDc) b.prepare (osSampleRate, 48.0);

    sagEnv     .setTimes (osSampleRate, 0.018, 0.220);
    collapseEnv.setTimes (osSampleRate, 0.020, 0.300);

    for (auto& stage : stageEnv)
        for (auto& e : stage)
            e.setTimes (osSampleRate, 0.0015, 0.050);

    for (auto& e : powerEnv)
        e.setTimes (osSampleRate, 0.010, 0.190);

    levelEnv      .setTimes (sampleRate, 0.001, 0.220);
    lowEnergyEnv  .setTimes (sampleRate, 0.030, 0.300);
    totalEnergyEnv.setTimes (sampleRate, 0.030, 0.300);

    dirtyFlag = true;
    applyControls();
    reset();
}

void CollapseEngine::reset() noexcept
{
    oversampler.reset();
    cab.reset();
    split.reset();
    lowDelay.reset();
    highDelay.reset();

    std::fill (buffers.begin(), buffers.end(), 0.0f);

    inputHp.reset();
    preEmphasis.reset();
    toneLow.reset();
    toneHigh.reset();
    gritPeak.reset();
    gritShelf.reset();
    dirtyHpA.reset();
    dirtyHpB.reset();
    weightSub.reset();
    weightShelf.reset();

    for (auto& b : dcIn)     b.reset();
    for (auto& b : dcOut)    b.reset();
    for (auto& b : octaveDc) b.reset();

    for (auto& stage : interHp) for (auto& s : stage) s.reset();
    for (auto& stage : interLp) for (auto& s : stage) s.reset();

    sagEnv.reset();
    collapseEnv.reset();
    levelEnv.reset();
    lowEnergyEnv.reset();
    totalEnergyEnv.reset();

    for (auto& stage : stageEnv) for (auto& e : stage) e.reset();
    for (auto& e : powerEnv) e.reset();

    makeupCurrent = makeupTarget;
    coherence = 1.0f;
}

void CollapseEngine::setControls (const Controls& c) noexcept
{
    constexpr auto eps = 1.0e-4f;

    const auto moved = std::abs (c.drive  - controls.drive)  > eps
                    || std::abs (c.blend  - controls.blend)  > eps
                    || std::abs (c.weight - controls.weight) > eps
                    || std::abs (c.tone   - controls.tone)   > eps
                    || std::abs (c.grit   - controls.grit)   > eps
                    || std::abs (c.splitHz - controls.splitHz) > 0.05f
                    || c.state   != controls.state
                    || c.cabinet != controls.cabinet;

    if (! moved)
        return;

    controls  = c;
    dirtyFlag = true;
    applyControls();
}

void CollapseEngine::applyControls()
{
    if (! dirtyFlag)
        return;

    dirtyFlag = false;

    const auto& spec = stateSpec (controls.state);
    const auto d = controls.drive;

    split.setCrossover (sampleRate, (double) controls.splitHz);

    // Total gain is distributed deliberately across the stages rather than applied to
    // each: give every stage the full figure and at the top of the knob stages two and
    // three both see hundreds of times full scale and collapse onto the same rail.
    // Normalising by the trims' geometric mean is what keeps drive = 0 at unity.
    const auto driveCurve = d * (0.72f + 0.28f * d);
    const auto driveDb = spec.driveDbMin + driveCurve * (spec.driveDbMax - spec.driveDbMin);
    auto trimProduct = 1.0f;

    for (int s = 0; s < spec.numStages; ++s)
        trimProduct *= std::max (0.05f, spec.stageTrim[s]);

    const auto trimMean = std::pow (trimProduct, 1.0f / (float) spec.numStages);

    for (int s = 0; s < 3; ++s)
    {
        if (s < spec.numStages)
        {
            const auto gainDb = driveDb * spec.driveShare[s];
            stageGain[(std::size_t) s] = (spec.stageTrim[s] / trimMean)
                                       * std::pow (10.0f, gainDb / 20.0f);
        }
        else
        {
            stageGain[(std::size_t) s] = 0.0f;
        }
    }

    // High gain needs a firmer input or low notes turn to flub. The crossover has already
    // taken most of that band away, so this is a second line of defence rather than the
    // first - which is why it can stay gentle enough to leave the growl alone.
    const auto tightHz = (double) spec.tightenHz * (0.75 + 0.55 * (double) d);
    inputHp.coeffs.setHighpass (sampleRate, std::max (18.0, tightHz), 0.72);

    // Pre-clip emphasis decides which band generates the harmonics. Letting it grow with
    // drive is why a real amp gets more mid-forward as it is pushed.
    preEmphasis.coeffs.setPeak (sampleRate, spec.preHz,
                                (double) spec.preDb * (0.25 + 0.75 * (double) d), spec.preQ);

    interHpCoeffs.setHighpass (osSampleRate, spec.interHpHz, 0.70);
    interLpCoeffs.setLowpass  (osSampleRate, spec.interLpHz, 0.70);

    // Tone is a tilt: one control, no way to end up with an unusable curve. Hinged low,
    // at 380 Hz, because on a bass the interesting argument is between the body of the
    // note and the string, not between mid and treble.
    const auto tiltDb = ((double) controls.tone - 0.5) * 2.0 * 7.0;
    toneLow .coeffs.setLowShelf  (sampleRate, 380.0,  -tiltDb, 0.72);
    toneHigh.coeffs.setHighShelf (sampleRate, 1700.0,  tiltDb, 0.72);

    // Grit is the fret buzz and pick attack, which on an amplified bass live in a narrow
    // band around 2.3 kHz. Its zero is at 35 % rather than 50 %, so most of the knob's
    // travel is spent adding rather than taking away.
    const auto gritAmount = (double) controls.grit - 0.35;
    gritPeak .coeffs.setPeak      (sampleRate, 2300.0, gritAmount * 12.0, 0.85);
    gritShelf.coeffs.setHighShelf (sampleRate, 4800.0, gritAmount *  6.0, 0.70);

    // The second half of the split, on the way out. Fourth order, like the crossover, but
    // hinged well below it: at 0.62 of the corner this costs the drive path about 1.2 dB
    // where the two bands meet, which is small enough not to leave a hole in the sum, and
    // is 29 dB down an octave and a half lower - where the clean band lives and where
    // nothing the shaper regenerated has any business being.
    dirtyHpA.coeffs.setHighpass (sampleRate, 0.62 * (double) controls.splitHz, 0.70710678);
    dirtyHpB.coeffs = dirtyHpA.coeffs;

    // Weight acts on the sum, not on the low band, so it still does something when the
    // crossover is parked at the bottom of its range and there is no low band to speak of.
    // The neutral position costs 0.7 dB at a low E, which is as close to nothing as a
    // subsonic filter that is actually doing something can be.
    const auto weightHz = 40.0 - 24.0 * (double) controls.weight;
    weightSub  .coeffs.setHighpass (sampleRate, weightHz, 0.72);
    weightShelf.coeffs.setLowShelf (sampleRate, 92.0,
                                    ((double) controls.weight - 0.5) * 2.0 * 4.5, 0.75);

    cab.setMode ((Cabinet) controls.cabinet);

    // Makeup is interpolated between the five measured breakpoints in the spec.
    const auto pos   = std::min (3.999f, std::max (0.0f, d) * 4.0f);
    const auto index = (int) pos;
    const auto frac  = pos - (float) index;
    const auto mkDb  = spec.makeupDb[index]
                         + frac * (spec.makeupDb[index + 1] - spec.makeupDb[index]);

    makeupTarget = std::pow (10.0f, mkDb / 20.0f);
}

void CollapseEngine::process (float* const* io, int numSamples) noexcept
{
    const auto& spec = stateSpec (controls.state);

    float* low[2]   = { scratch (kLowSlot,   0), scratch (kLowSlot,   1) };
    float* high[2]  = { scratch (kHighSlot,  0), scratch (kHighSlot,  1) };
    float* dirty[2] = { scratch (kDirtySlot, 0), scratch (kDirtySlot, 1) };

    // ---- the split, and the pre-drive voicing on what came out of the top ---------
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* in = io[ch];

        for (int n = 0; n < numSamples; ++n)
        {
            const auto x = dcIn[(std::size_t) ch].process (in[n]);

            float lo = 0.0f, hi = 0.0f;
            split.process (ch, x, lo, hi);

            low[ch][n]  = lo;
            high[ch][n] = hi;

            auto pre = inputHp.process (ch, hi);
            pre = preEmphasis.process (ch, pre);
            dirty[ch][n] = pre;
        }
    }

    // ---- the gain stages, oversampled --------------------------------------------
    oversampler.upsample (dirty, numSamples);

    const auto numOs = numSamples * oversampler.getFactor();
    const auto invChannels = 1.0f / (float) numChannels;

    // The articulation path only opens once the cascade is compressing enough to have
    // lost something worth putting back; smoothstep keeps it from switching in.
    const auto detailRise = std::max (0.0f, std::min (1.0f, (controls.drive - 0.28f) / 0.72f));
    const auto detailMix  = spec.detail * detailRise * detailRise * (3.0f - 2.0f * detailRise);
    const auto powerMix   = spec.power * (0.20f + 0.80f * controls.drive);

    float* os[2] = { oversampler.upsampled (0),
                     numChannels > 1 ? oversampler.upsampled (1) : nullptr };

    for (int n = 0; n < numOs; ++n)
    {
        // One supply feeds every stage and both channels, so the sag envelope is shared -
        // and that shared compression is most of what "amp feel" means.
        const auto sagGain = 1.0f - spec.sag * 0.65f * sagEnv.value;
        auto push = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto y = os[ch][n];
            auto firstStageVoice = 0.0f;

            for (int s = 0; s < spec.numStages; ++s)
            {
                auto& cathode = stageEnv[(std::size_t) s][(std::size_t) ch];

                // Local cathode feedback moves on a much faster time scale than the shared
                // supply. Alternating the dynamic bias direction from stage to stage
                // avoids the "same clipper three times" sound and keeps even harmonics
                // alive when the cascade is driven hard.
                const auto localCompression = 1.0f / (1.0f + spec.cathode * cathode.value);
                const auto polarity = (s & 1) == 0 ? 1.0f : -0.62f;
                const auto dynamicBias = spec.bias * ((s & 1) == 0 ? 1.0f : -0.45f)
                                       - polarity * spec.biasShift * cathode.value;
                const auto g = stageGain[(std::size_t) s] * localCompression
                                         * (s == 0 ? 1.0f : sagGain);
                const auto u = g * y + dynamicBias;

                if (s == 0)
                    push += std::abs (u);

                // Subtracting the shaper's moving operating point keeps silence exactly
                // silent even while the bias memory is returning to rest.
                y = shapeAsymmetric (u, spec.negK, spec.knee)
                      - shapeAsymmetric (dynamicBias, spec.negK, spec.knee);

                const auto rectified = std::abs (u);
                cathode.process (rectified / (1.0f + rectified));

                // Every valve stage rolls off its own top end, and that rolloff is what
                // stops a cascade of clipping stages turning into a square wave full of
                // fold-back. It is inside the oversampled region on purpose: the
                // harmonics are killed here, before they can alias on the way back down.
                y = interLp[(std::size_t) s][(std::size_t) ch].process (interLpCoeffs, y);

                if (s + 1 < spec.numStages)
                    y = interHp[(std::size_t) s][(std::size_t) ch].process (interHpCoeffs, y);

                if (s == 0)
                    firstStageVoice = y;
            }

            // At high gain the first stage still holds the pick and the fret noise that
            // the final stage has compressed away. A small, drive-dependent parallel path
            // is the equivalent of carefully voiced bypass capacitance around the later
            // stages, and on bass it is the difference between a line you can follow and
            // a wall you cannot.
            y = (1.0f - detailMix) * y + detailMix * firstStageVoice;
            auto octaveVoice = 0.0f;

            // A squaring term is octave-up content: the fizz that makes a fuzz sound like
            // it has collapsed rather than merely got loud. Written so that zero maps to
            // zero - a constant offset here would put a DC step into an otherwise silent
            // output, which the blocker downstream would only remove after an audible
            // tail. The level-dependent DC it does produce is removed by octaveDc, before
            // the output stage, so it cannot pull the bias around under a low note.
            if (spec.octave > 0.0f)
            {
                const auto rageDrive = 2.5f + 6.5f * controls.drive;
                const auto rage = softTight (firstStageVoice * rageDrive);
                const auto harmonicBase = softTight (firstStageVoice * 1.65f);
                const auto rage3 = 4.0f * harmonicBase * harmonicBase * harmonicBase
                                     - 3.0f * harmonicBase;
                const auto thirdAmount = 0.45f + 1.15f * controls.drive;
                octaveVoice = octaveDc[(std::size_t) ch].process (y * y + 0.72f * rage
                                                                       + thirdAmount * rage3);
            }

            // The output stage is a biased push-pull pair. Taking the difference of
            // mirrored valve curves cancels DC and most even harmonics, while the small
            // operating-point offset leaves enough imbalance to feel alive. It is
            // parallel-blended so Drift stays an overdrive rather than quietly becoming a
            // limiter.
            auto& power = powerEnv[(std::size_t) ch];
            const auto powerSag    = 1.0f - 0.24f * powerMix * power.value;
            const auto powerDrive  = 1.0f + 3.8f * powerMix;
            const auto powerBias   = 0.018f + 0.035f * spec.power;
            const auto powerIn     = y * powerDrive * powerSag;
            const auto positive    = shapeAsymmetric ( powerIn + powerBias, 1.12f, 0.24f);
            const auto negative    = shapeAsymmetric (-powerIn + powerBias, 1.12f, 0.24f);
            const auto normaliser  = 1.0f + 0.72f * (powerDrive - 1.0f);
            const auto powerVoice  = (positive - negative) / (2.0f * normaliser);

            y = (1.0f - powerMix) * y + powerMix * powerVoice;
            y += spec.octave * octaveVoice;
            power.process (std::abs (y) / (0.35f + std::abs (y)));

            // The musical ceiling lives here, inside the oversampled region, so that its
            // own harmonics are anti-aliased like everything else. A soft limiter at base
            // rate would be a non-linearity with nothing oversampling it, and it folds
            // enough back into the top octave to dominate the whole aliasing figure.
            os[ch][n] = softCeiling (y, 1.25f);
        }

        push *= invChannels;
        const auto normalised = push / (1.0f + push);   // 0..1, how hard it is hit
        sagEnv     .process (normalised);
        collapseEnv.process (normalised);
    }

    oversampler.downsample (dirty, numSamples);

    // ---- post EQ on the collapsed path, base rate ---------------------------------
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* d = dirty[ch];

        for (int n = 0; n < numSamples; ++n)
        {
            auto y = dcOut[(std::size_t) ch].process (d[n]);

            y = toneLow  .process (ch, y);
            y = toneHigh .process (ch, y);
            y = gritPeak .process (ch, y);
            y = gritShelf.process (ch, y);

            // Last thing on this path, and it has to be: everything upstream of it is a
            // way to put energy back under the crossover that the clean band is holding.
            y = dirtyHpB.process (ch, dirtyHpA.process (ch, y));

            d[n] = y;
        }
    }

    // ---- both clean paths, held back by exactly the reported latency ---------------
    lowDelay .process (low,  low,  numSamples);
    highDelay.process (high, high, numSamples);

    // ---- the sum ------------------------------------------------------------------
    // Makeup is ramped across the block rather than stepped, because it is the one value
    // derived from `drive` that is applied as a raw multiply.
    const auto makeupStep = (makeupTarget - makeupCurrent) / (float) numSamples;
    const auto blend = controls.blend;
    auto peak = 0.0f;
    auto lowSum = 0.0f, totalSum = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* out = io[ch];
        auto makeup = makeupCurrent;

        for (int n = 0; n < numSamples; ++n)
        {
            makeup += makeupStep;

            const auto lo = low[ch][n];
            auto y = lo + (1.0f - blend) * high[ch][n] + blend * makeup * dirty[ch][n];

            // One cabinet, at the end, fed by everything - both bands, clean and
            // collapsed. The cone model in front of it is level dependent, so this is also
            // the only position where it sees the low end that actually moves a cone.
            y = cab.process (ch, y);

            y = weightSub  .process (ch, y);
            y = weightShelf.process (ch, y);

            // A structural guarantee rather than a tone control: at full scale this is
            // algebraically inert (0.008 dB), and it only starts to bite four times above
            // it - but it means no combination of drive, weight and cabinet resonance can
            // hand the host an unbounded sample.
            y = softCeiling (y, 4.0f);

            out[n] = y;

            const auto a = std::abs (y);
            if (a > peak) peak = a;

            lowSum   += std::abs (lo);
            totalSum += a;
        }
    }

    makeupCurrent = makeupTarget;

    const auto scale = invChannels / (float) std::max (1, numSamples);
    const auto lowLevel   = lowEnergyEnv  .process (lowSum   * scale);
    const auto totalLevel = totalEnergyEnv.process (totalSum * scale);

    // Below the noise floor there is nothing to be coherent about, and a ratio of two
    // tiny numbers is noise - so the display holds at fully coherent instead of flickering.
    coherence = totalLevel > 1.0e-5f
                  ? std::min (1.0f, lowLevel / totalLevel)
                  : 1.0f;

    for (int n = 0; n < numSamples; ++n)
        levelEnv.process (peak);
}

} // namespace collapse::dsp
