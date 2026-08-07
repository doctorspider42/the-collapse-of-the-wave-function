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

/*  The waveshapers, and the four states as data. Keeping the voicings in a table rather
    than four code paths means there is one processing routine to reason about, test and
    optimise, and that adding a fifth state is a row rather than a branch.           */

#pragma once

#include <cmath>

namespace collapse::dsp
{

/** x / sqrt(1 + x^2) - monotonic, infinitely differentiable, unity slope at the origin,
    cheaper than tanh and with none of the divergence that polynomial tanh approximations
    bring to loud transients. */
inline float softTight (float x) noexcept
{
    return x / std::sqrt (1.0f + x * x);
}

/** x / (1 + |x|) - the same unity slope at zero but a much longer knee. */
inline float softLong (float x) noexcept
{
    return x / (1.0f + std::abs (x));
}

/** Both shapers share unity slope at zero, so any blend of them does too and `knee` is a
    continuous "how gradual is the compression" control rather than a switch. */
inline float softBlend (float x, float knee) noexcept
{
    return (1.0f - knee) * softTight (x) + knee * softLong (x);
}

/** Compressing the negative half harder is what a single-ended valve stage does, and is
    where the even harmonics come from. Dividing by negK after scaling keeps the
    derivative at the origin equal on both sides, so the curve stays C1-continuous and
    only the curvature differs. */
inline float shapeAsymmetric (float x, float negK, float knee) noexcept
{
    return x >= 0.0f ? softBlend (x, knee)
                     : softBlend (x * negK, knee) / negK;
}

/** Fourth-order soft ceiling: transparent well below the limit (0.13 dB at half of it),
    asymptotic to +/-limit however hard it is driven. This is a safety net at the end of
    the chain, not a tone control - it exists so that no combination of drive, weight and
    cabinet resonance can hand the host an unbounded signal. */
inline float softCeiling (float x, float limit) noexcept
{
    const auto t  = x / limit;
    const auto t2 = t * t;
    return x / std::sqrt (std::sqrt (1.0f + t2 * t2));
}

/** How the four states differ.

    Every frequency here is lower than the equivalent number would be in a guitar drive,
    and deliberately: a bass guitar's fundamentals live between 31 and 400 Hz, so a
    pre-clip emphasis at 600 Hz works the second harmonic rather than the note. The
    inter-stage high-pass matters more than anywhere else too - it is what stops each
    successive stage intermodulating the fundamental into mud.
*/
struct StateSpec
{
    const char* name;
    const char* blurb;        // one line, shown in the panel under the selector

    int   numStages;          // cascaded gain stages, 2 or 3
    float driveDbMin;         // total small-signal gain across all stages at drive = 0
    float driveDbMax;         // ... and at drive = 1
    float stageTrim[3];       // relative gain of each stage (product is normalised)
    float driveShare[3];      // share of total drive dB assigned to each stage
    float negK;               // asymmetry, > 1
    float knee;               // 0 = tight/sqrt, 1 = long/abs
    float bias;               // static offset into the shaper
    float biasShift;          // dynamic bias excursion under sustained level
    float cathode;            // local cathode feedback / gain compression
    float detail;             // first-stage articulation mixed around later stages
    float power;              // push-pull output stage contribution
    float preHz;              // pre-clip emphasis: which band generates the harmonics
    float preDb;
    float preQ;
    float interHpHz;          // coupling capacitor between stages
    float interLpHz;          // Miller-capacitance rolloff between stages
    float sag;                // supply sag depth, 0..1
    float octave;             // squaring term - octave-up fizz, the fuzz voice

    /** Output makeup in dB at drive = 0, 25, 50, 75 and 100 %, interpolated between.

        A pair of constants cannot do this job: below saturation the level tracks the
        pre-gain, above it the stages sit on the rail and the level stops rising, so the
        curve needed is steep and then flat. These five numbers per state are read
        straight off `devtool sweep`, which drives the collapsed path alone with a
        bass-band signal and reports what came out. */
    float makeupDb[5];

    float tightenHz;          // extra input high-pass, keeps high gain from flubbing
};

enum class State { drift = 0, phase, decay, collapse, numStates };

/** Voicings set by ear, levels trimmed against `devtool sweep`. */
inline const StateSpec& stateSpec (int index) noexcept
{
    // `driveDbMax` is total gain across the cascade, not gain repeated at every stage.
    // Splitting it deliberately prevents the final stages from spending the upper half of
    // the Drive knob pinned to one identical rail.
    static const StateSpec specs[] =
    {
        // DRIFT - the wave function still coherent. Two low-gain stages, gentle
        // asymmetry, almost no voicing: at Drive 0 this is a clean bass preamp that
        // rounds peaks, and at Drive 100 it is still only a warm growl.
        { "DRIFT", "coherent - valve warmth, barely dirty",
          2, 0.0f, 40.0f, { 1.00f, 0.82f, 0.0f },
          { 0.60f, 0.40f, 0.0f },
          1.14f, 0.42f, 0.018f, 0.016f, 0.09f, 0.030f, 0.18f,
          190.0f, 2.0f, 0.60f,
          38.0f, 11000.0f, 0.20f, 0.00f,
          { 2.1f, -3.9f, -9.0f, -12.1f, -13.4f }, 24.0f },

        // PHASE - the workhorse. Enough asymmetry for a real second harmonic and a push
        // just above the fundamental range, so a low B still reads as a note rather than
        // as a rumble with fur on it.
        { "PHASE", "shifted - the growl that still holds a note",
          2, 0.0f, 54.0f, { 1.00f, 1.00f, 0.0f },
          { 0.57f, 0.43f, 0.0f },
          1.52f, 0.50f, 0.050f, 0.032f, 0.15f, 0.060f, 0.30f,
          430.0f, 4.0f, 0.75f,
          58.0f, 8200.0f, 0.38f, 0.03f,
          { 2.8f, -4.6f, -9.7f, -11.8f, -12.4f }, 38.0f },

        // DECAY - three stages, a firmer input and a harder knee. This is where the
        // plug-in stops being an overdrive: percussive, mid-forward, and tight enough
        // that fast lines stay legible.
        { "DECAY", "decoherent - tight, mid-forward, aggressive",
          3, 0.0f, 68.0f, { 1.00f, 0.86f, 0.76f },
          { 0.47f, 0.32f, 0.21f },
          1.36f, 0.26f, 0.028f, 0.048f, 0.20f, 0.105f, 0.44f,
          700.0f, 5.5f, 0.95f,
          82.0f, 6600.0f, 0.48f, 0.00f,
          { 3.8f, -4.6f, -9.2f, -10.8f, -11.1f }, 52.0f },

        // COLLAPSE - the state the plug-in is named for. Heavy asymmetry, a long knee and
        // a squaring term for the octave-up fizz. Squashy even at Drive 0, which is the
        // point of it; the Blend control is what keeps it usable on a bass at all.
        { "COLLAPSE", "observed - fuzz, octave fizz, all of it",
          3, 0.0f, 92.0f, { 1.00f, 1.10f, 1.02f },
          { 0.50f, 0.30f, 0.20f },
          2.60f, 0.66f, 0.140f, 0.066f, 0.21f, 0.095f, 0.50f,
          520.0f, 5.0f, 0.60f,
          88.0f, 6000.0f, 0.55f, 0.26f,
          { 8.4f, -2.3f, -6.4f, -7.7f, -7.7f }, 60.0f },
    };

    const auto count = (int) (sizeof (specs) / sizeof (specs[0]));
    return specs[index < 0 ? 0 : (index >= count ? count - 1 : index)];
}

} // namespace collapse::dsp
