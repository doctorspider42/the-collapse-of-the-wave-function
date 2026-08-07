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

#include "CabSim.h"
#include "Crossover.h"
#include "DelayLine.h"
#include "Envelopes.h"
#include "Oversampler.h"
#include "Shapers.h"

#include <vector>

namespace collapse::dsp
{

/** The whole signal path.

                             +--- low band ------------------------------+
        in -> LR4 split -----+                                            +-> Weight
                             +--- high band -+-- (1 - Blend) ------------+     -> ceiling
                                             |                           |
                                             +-- pre-EQ -> [oversampled  |
                                                 2-3 asymmetric stages,  |
                                                 sag, push-pull, octave] |
                                                 -> tone -> grit -> cab -+-- Blend

    Three things about that diagram are the whole design.

    **The low band never enters the drive path.** Distortion of two low fundamentals
    produces sum and difference tones between them, and on a bass those land right in the
    middle of the note. Splitting first is why this can be driven hard and still be a
    bass rather than a buzz.

    **The clean high band is delayed, not merely mixed.** The oversampler reports an
    integer latency and both clean paths are held back by exactly that, so Blend is a
    crossfade rather than a comb filter, and the plug-in's own bypass nulls.

    **The drive path is high-passed again on the way out.** Taking the low band away in
    front of a clipper is only half the job: whatever residual gets through is then handed
    92 dB of gain, saturates, and comes out as a full-scale square wave with a fundamental
    of its own - one that arrives at the sum in whatever phase the input filter left it in,
    and subtracts from the clean band it was supposed to leave alone. Measured at -4.2 dB
    on a low E before this filter existed. It is the reason for `dirtyHpA/B`.

    **The cabinet only sees the collapsed path.** That is how the rig this imitates is
    wired - a DI holding the bottom and a driven amp holding the shape - and it means the
    modelled low end in CabSim never has to fight the real one coming down the clean side.

    No JUCE, no allocation outside prepare(), no locks. The maximum block passed to
    process() is fixed at prepare() time.
*/
class CollapseEngine
{
public:
    struct Controls
    {
        float drive  = 0.30f;   // 0..1
        float blend  = 0.55f;   // 0..1, coherent .. collapsed
        float splitHz = 110.0f; // crossover; below this the signal is never distorted
        float weight = 0.55f;   // 0..1, output low end
        float tone   = 0.50f;   // 0..1, dark .. bright, collapsed path only
        float grit   = 0.45f;   // 0..1, upper-mid bite, collapsed path only
        int   state   = 1;      // index into stateSpec()
        int   cabinet = 1;      // Cabinet
    };

    void prepare (double newSampleRate, int numChannelsToUse, int maxBlockToUse);
    void reset() noexcept;

    /** Round-trip latency of the oversampler, in samples. Settled at prepare time and
        constant while audio runs, so the host is told once and never again. */
    int getLatencySamples() const noexcept { return oversampler.getLatencySamples(); }

    /** Call once per control block with the smoothed parameter values. Coefficients are
        only rebuilt when something actually moved, so a static setting costs nothing. */
    void setControls (const Controls& c) noexcept;

    void process (float* const* io, int numSamples) noexcept;

    /** Values the editor polls at frame rate. Written by the audio thread as plain
        relaxed stores by the processor - no locks, no queues. */
    float getCollapse() const noexcept { return collapseEnv.value; }
    float getSag()      const noexcept { return sagEnv.value; }
    float getLevel()    const noexcept { return levelEnv.value; }

    /** How much of what is coming out is coming out of the clean low band, 0..1. The
        visualiser uses it to decide how much of the wave stays whole. */
    float getCoherence() const noexcept { return coherence; }

private:
    void applyControls();

    float* scratch (int slot, int channel) noexcept
    {
        return buffers.data() + (std::size_t) ((slot * 2 + channel) * maxBlock);
    }

    static constexpr int kLowSlot = 0, kHighSlot = 1, kDirtySlot = 2;

    double sampleRate   = 48000.0;
    double osSampleRate = 192000.0;
    int    numChannels  = 2;
    int    maxBlock     = 32;
    bool   dirtyFlag    = true;

    Controls controls {};

    Oversampler    oversampler;
    CabSim         cab;
    LinkwitzRiley4 split;
    IntegerDelay   lowDelay, highDelay;

    std::vector<float> buffers;

    DcBlocker dcIn[2], dcOut[2], octaveDc[2];
    Biquad<2> inputHp, preEmphasis;
    Biquad<2> toneLow, toneHigh, gritPeak, gritShelf;
    Biquad<2> dirtyHpA, dirtyHpB;
    Biquad<2> weightSub, weightShelf;

    // Shared coefficients, one state per stage per channel.
    BiquadCoeffs interHpCoeffs, interLpCoeffs;
    BiquadState  interHp[3][2], interLp[3][2];

    EnvelopeFollower sagEnv, collapseEnv, levelEnv;
    EnvelopeFollower stageEnv[3][2], powerEnv[2];
    EnvelopeFollower lowEnergyEnv, totalEnergyEnv;

    float stageGain[3]  = { 1.0f, 1.0f, 1.0f };
    float makeupTarget  = 1.0f;
    float makeupCurrent = 1.0f;
    float coherence     = 1.0f;
};

} // namespace collapse::dsp
