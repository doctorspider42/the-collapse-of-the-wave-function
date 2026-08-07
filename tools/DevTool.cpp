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

/*  Offline developer tool.

        devtool check            - DSP assertions, measurements, CPU cost
        devtool sweep            - the level curve of each state, for Shapers.h
        devtool cab              - the cabinet's magnitude response, decade by decade
        devtool shot <file.png>  - render the editor to a PNG

    None of the modes needs a host, an audio device or a display, so all of them run in CI
    and all of them run in a couple of seconds on a laptop. Every claim the README makes
    about this plug-in is checked by something in here.                              */

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/dsp/Shapers.h"

#include <iostream>

using Processor = collapse::CollapseProcessor;

namespace
{

constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;

void setParam (Processor& proc, const char* id, float value)
{
    if (auto* p = proc.getState().getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

struct Stats
{
    float rms = 0.0f;
    float peak = 0.0f;
    bool  finite = true;
};

Stats analyse (const juce::AudioBuffer<float>& buffer, int start = 0, int length = -1)
{
    Stats s;
    const auto n = length < 0 ? buffer.getNumSamples() - start : length;
    double sum = 0.0;
    int count = 0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* d = buffer.getReadPointer (ch);

        for (int i = start; i < start + n; ++i)
        {
            if (! std::isfinite (d[i]))
                s.finite = false;

            sum += (double) d[i] * d[i];
            s.peak = juce::jmax (s.peak, std::abs (d[i]));
            ++count;
        }
    }

    s.rms = count > 0 ? (float) std::sqrt (sum / count) : 0.0f;
    return s;
}

void fillSine (juce::AudioBuffer<float>& buffer, double freq, float amplitude,
               int start = 0, int length = -1)
{
    const auto n = length < 0 ? buffer.getNumSamples() - start : length;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);

        for (int i = 0; i < n; ++i)
            d[start + i] = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                             * freq * (double) i / kSampleRate);
    }
}

void fillNoise (juce::AudioBuffer<float>& buffer, float amplitude, int start, int length)
{
    juce::Random rng (0x5eed);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);

        for (int i = 0; i < length; ++i)
            d[start + i] = amplitude * (rng.nextFloat() * 2.0f - 1.0f);
    }
}

/** A bass guitar, near enough: a fundamental and three harmonics falling away, which is
    what the low E of a passive P-bass looks like on an analyser. Peak-normalised, so the
    level going into the shaper is a number and not an accident. */
void fillBassTone (juce::AudioBuffer<float>& buffer, double fundamental, float peak)
{
    const auto n = buffer.getNumSamples();
    const double amps[] = { 1.0, 0.55, 0.30, 0.16 };

    for (int i = 0; i < n; ++i)
    {
        auto v = 0.0;

        for (int h = 0; h < 4; ++h)
            v += amps[h] * std::sin (2.0 * juce::MathConstants<double>::pi
                                         * fundamental * (double) (h + 1) * (double) i
                                         / kSampleRate);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, (float) v);
    }

    const auto measured = analyse (buffer).peak;

    if (measured > 1.0e-9f)
        buffer.applyGain (peak / measured);
}

/** Noise shaped like a bass DI: a one-pole roll-off from 700 Hz and a 45 Hz high-pass,
    which lands within a couple of dB of a real passive P-bass track across the band that
    matters.

    The level sweep needs this rather than a tone. Measured on a single 82 Hz note, the RMS
    is almost entirely the fundamental, and the three-stage voicings high-pass that away in
    their coupling stages - so the makeup gain that came out was compensating for a tonal
    choice rather than for a level, and it asked for 13 dB where it should have asked for
    four. Spreading the energy over the band the plug-in actually works in fixes it. */
void fillBassNoise (juce::AudioBuffer<float>& buffer, float peak)
{
    juce::Random rng (0xba55);

    const auto lpCoeff = (float) std::exp (-2.0 * juce::MathConstants<double>::pi
                                                * 700.0 / kSampleRate);
    const auto hpCoeff = (float) std::exp (-2.0 * juce::MathConstants<double>::pi
                                                * 45.0 / kSampleRate);

    float low = 0.0f, hpState = 0.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto white = rng.nextFloat() * 2.0f - 1.0f;
        low = white + lpCoeff * (low - white);
        hpState = low + hpCoeff * (hpState - low);
        const auto v = low - hpState;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, v);
    }

    const auto measured = analyse (buffer).peak;

    if (measured > 1.0e-9f)
        buffer.applyGain (peak / measured);
}

/** Amplitude of one frequency component by direct correlation. Exact when the window
    holds a whole number of periods, so no windowing is needed. */
float magnitudeAt (const juce::AudioBuffer<float>& buffer, double freq,
                   int startSample, int numSamples)
{
    double re = 0.0, im = 0.0;
    const auto* d = buffer.getReadPointer (0);

    for (int n = 0; n < numSamples; ++n)
    {
        const auto phase = 2.0 * juce::MathConstants<double>::pi * freq * (double) n / kSampleRate;
        re += (double) d[startSample + n] * std::cos (phase);
        im += (double) d[startSample + n] * std::sin (phase);
    }

    return (float) (2.0 * std::sqrt (re * re + im * im) / (double) numSamples);
}

void runThrough (Processor& proc, juce::AudioBuffer<float>& buffer, int blockSize = kBlockSize)
{
    juce::MidiBuffer midi;

    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const auto len = juce::jmin (blockSize, buffer.getNumSamples() - start);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(),
                                        buffer.getNumChannels(), start, len);
        proc.processBlock (slice, midi);
    }
}

/** A known state: nothing engaged that a check has not asked for. Weight sits at its
    neutral 50 % - the shelf is then exactly 0 dB - rather than at the shipping default. */
void neutral (Processor& proc)
{
    using namespace collapse::pid;

    setParam (proc, drive, 0.0f);
    setParam (proc, blend, 0.0f);
    setParam (proc, split, 110.0f);
    setParam (proc, weight, 50.0f);
    setParam (proc, tone, 50.0f);
    setParam (proc, grit, 35.0f);       // Grit's own zero, not the middle of its travel
    setParam (proc, level, 0.0f);
    setParam (proc, state, 1.0f);
    setParam (proc, cabinet, 0.0f);
    setParam (proc, bypass, 0.0f);
}

/** Gain through the plug-in at one frequency, in dB. Measured over the second half of a
    two-second render, so nothing transient is in the window. */
float responseAtDb (Processor& proc, double freq)
{
    proc.reset();

    const auto total = (int) (kSampleRate * 2.0);
    juce::AudioBuffer<float> buffer (2, total);
    fillSine (buffer, freq, 0.25f);

    const auto window = (int) (kSampleRate * 0.5);
    const auto start  = total - window;
    const auto before = magnitudeAt (buffer, freq, start, window);

    runThrough (proc, buffer);

    const auto after = magnitudeAt (buffer, freq, start, window);
    return juce::Decibels::gainToDecibels (juce::jmax (1.0e-9f, after / juce::jmax (1.0e-9f, before)));
}

int runCheck()
{
    using namespace collapse::pid;

    Processor proc;
    proc.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    proc.prepareToPlay (kSampleRate, kBlockSize);

    int failures = 0;
    auto expect = [&failures] (bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  ok    " : "  FAIL  ") << what << std::endl;
        if (! condition) ++failures;
    };

    std::cout << "offline check @ " << kSampleRate << " Hz, block " << kBlockSize << std::endl;
    std::cout << "reported latency: " << proc.getLatencySamples() << " samples" << std::endl;
    std::cout << "tail length: " << proc.getTailLengthSeconds() << " s\n" << std::endl;

    // ---- 1. flat with nothing collapsed --------------------------------------
    // Blend at 0 leaves the crossover summing its own two halves, and a Linkwitz-Riley
    // pair sums to an allpass. Magnitude flat is the promise; identity is not, and the
    // README says so.
    {
        neutral (proc);

        auto worst = 0.0f;
        const double points[] = { 60, 90, 125, 180, 250, 400, 700, 1200, 2500, 5000, 10000 };

        for (auto f : points)
            worst = juce::jmax (worst, std::abs (responseAtDb (proc, f)));

        std::cout << "  worst deviation, 60 Hz .. 10 kHz at Blend 0: "
                  << juce::String (worst, 2) << " dB" << std::endl;
        expect (worst < 0.8f, "the crossover sums flat when nothing is collapsed");
    }

    // ---- 2. the whole point of the plug-in -----------------------------------
    // A low E, fully collapsed, full drive, split above the fundamental. If the crossover
    // is doing its job the fundamental comes out at the level it went in at, whatever is
    // happening to everything above it.
    {
        neutral (proc);
        setParam (proc, blend, 100.0f);
        setParam (proc, drive, 100.0f);
        setParam (proc, split, 150.0f);
        setParam (proc, state, 3.0f);       // Collapse, the most destructive voicing
        setParam (proc, cabinet, 1.0f);
        proc.reset();

        const auto total = (int) (kSampleRate * 2.0);
        juce::AudioBuffer<float> buffer (2, total);
        fillSine (buffer, 41.2, 0.4f);      // low E

        const auto window = (int) (kSampleRate * 0.5);
        const auto start  = total - window;
        const auto before = magnitudeAt (buffer, 41.2, start, window);
        runThrough (proc, buffer);
        const auto after = magnitudeAt (buffer, 41.2, start, window);

        const auto delta = juce::Decibels::gainToDecibels (after / juce::jmax (1.0e-9f, before));
        std::cout << "  low E through full Collapse fuzz: " << juce::String (delta, 2)
                  << " dB at the fundamental" << std::endl;
        expect (std::abs (delta) < 2.0f, "the fundamental survives the drive path intact");
    }

    // ---- 3. and it does not, without the split -------------------------------
    // The counterexample. With the crossover parked at the bottom, the same fuzz eats the
    // same note - which is why the control exists.
    {
        neutral (proc);
        setParam (proc, blend, 100.0f);
        setParam (proc, drive, 100.0f);
        setParam (proc, split, 20.0f);
        setParam (proc, state, 3.0f);
        setParam (proc, cabinet, 1.0f);
        proc.reset();

        const auto total = (int) (kSampleRate * 2.0);
        juce::AudioBuffer<float> buffer (2, total);
        fillSine (buffer, 41.2, 0.4f);

        const auto window = (int) (kSampleRate * 0.5);
        const auto start  = total - window;
        const auto before = magnitudeAt (buffer, 41.2, start, window);
        runThrough (proc, buffer);
        const auto after = magnitudeAt (buffer, 41.2, start, window);

        const auto delta = juce::Decibels::gainToDecibels (
                               juce::jmax (1.0e-9f, after) / juce::jmax (1.0e-9f, before));
        std::cout << "  ... and with Split at 20 Hz: " << juce::String (delta, 2) << " dB"
                  << std::endl;
        expect (delta < -3.0f, "without the split, the same fuzz does eat the fundamental");
    }

    // ---- 4. every parameter reaches the DSP ----------------------------------
    // The single highest-value assertion here: a smoothed parameter needs both halves, the
    // target read and the smoother advanced. Miss the first and every knob is silently
    // inert while the plug-in still looks like it works.
    {
        auto renderWith = [&proc] (const char* id, float value)
        {
            neutral (proc);
            setParam (proc, blend, 100.0f);
            setParam (proc, drive, 45.0f);
            setParam (proc, cabinet, 1.0f);
            setParam (proc, id, value);
            proc.reset();

            juce::AudioBuffer<float> buffer (2, (int) kSampleRate);
            fillBassTone (buffer, 82.4, 0.45f);
            runThrough (proc, buffer);

            juce::AudioBuffer<float> tail;
            tail.makeCopyOf (buffer);
            return tail;
        };

        struct Case { const char* id; float low, high; };
        static const Case cases[] =
        {
            { drive,  5.0f,  95.0f },
            { blend,  0.0f, 100.0f },
            { split, 20.0f, 700.0f },
            { weight, 0.0f, 100.0f },
            { tone,   0.0f, 100.0f },
            { grit,   0.0f, 100.0f },
            { level, -12.0f,  6.0f },
        };

        for (const auto& c : cases)
        {
            const auto a = renderWith (c.id, c.low);
            const auto b = renderWith (c.id, c.high);

            auto worst = 0.0f;
            for (int n = 0; n < a.getNumSamples(); ++n)
                worst = juce::jmax (worst, std::abs (a.getSample (0, n) - b.getSample (0, n)));

            expect (worst > 1.0e-3f, juce::String (c.id) + " changes the output");
        }

        // The two choices, the same way.
        for (const char* id : { state, cabinet })
        {
            const auto a = renderWith (id, 0.0f);
            const auto b = renderWith (id, id == state ? 3.0f : 2.0f);

            auto worst = 0.0f;
            for (int n = 0; n < a.getNumSamples(); ++n)
                worst = juce::jmax (worst, std::abs (a.getSample (0, n) - b.getSample (0, n)));

            expect (worst > 1.0e-3f, juce::String (id) + " changes the output");
        }
    }

    // ---- 5. bypass -----------------------------------------------------------
    {
        neutral (proc);
        setParam (proc, blend, 80.0f);
        setParam (proc, drive, 70.0f);
        setParam (proc, bypass, 1.0f);
        proc.reset();

        juce::AudioBuffer<float> buffer (2, (int) kSampleRate);
        fillSine (buffer, 220.0, 0.4f);
        juce::AudioBuffer<float> reference;
        reference.makeCopyOf (buffer);
        runThrough (proc, buffer);

        const auto latency = proc.getLatencySamples();
        auto worst = 0.0f;

        // Skip the first block: the bypass smoother starts at its stored value, and the
        // very first samples are the ramp onto it.
        for (int n = kBlockSize; n < buffer.getNumSamples() - latency - 8; ++n)
            worst = juce::jmax (worst, std::abs (buffer.getSample (0, n + latency)
                                                     - reference.getSample (0, n)));

        std::cout << "  bypass error against the reported latency: "
                  << juce::Decibels::gainToDecibels (juce::jmax (1.0e-9f, worst))
                  << " dBFS" << std::endl;
        expect (latency > 0, "the oversampler's latency is reported");
        expect (worst < 1.0e-5f, "bypass passes audio through, delayed by exactly that");
    }

    // ---- 6. silence in, silence out ------------------------------------------
    // Two asymmetric shapers, a squaring term and a bias that moves with level: every one
    // of those is a way to leave DC behind on a signal that has stopped.
    {
        neutral (proc);
        setParam (proc, blend, 100.0f);
        setParam (proc, drive, 100.0f);
        setParam (proc, state, 3.0f);
        setParam (proc, cabinet, 1.0f);
        proc.reset();

        juce::AudioBuffer<float> buffer (2, (int) (kSampleRate * 2.0));
        buffer.clear();
        runThrough (proc, buffer);

        const auto after = analyse (buffer);
        expect (after.finite && after.peak < 1.0e-6f, "silence stays silent");
    }

    // ---- 7. bounded under everything -----------------------------------------
    {
        auto worstPeak = 0.0f;
        auto allFinite = true;

        for (int s = 0; s < (int) collapse::dsp::State::numStates; ++s)
            for (int cabIndex = 0; cabIndex < 3; ++cabIndex)
            {
                neutral (proc);
                setParam (proc, blend, 100.0f);
                setParam (proc, drive, 100.0f);
                setParam (proc, weight, 100.0f);
                setParam (proc, grit, 100.0f);
                setParam (proc, state, (float) s);
                setParam (proc, cabinet, (float) cabIndex);
                proc.reset();

                juce::AudioBuffer<float> buffer (2, (int) kSampleRate);
                fillNoise (buffer, 0.99f, 0, (int) kSampleRate);
                runThrough (proc, buffer);

                const auto stats = analyse (buffer);
                worstPeak = juce::jmax (worstPeak, stats.peak);
                allFinite = allFinite && stats.finite;
            }

        std::cout << "  worst peak over every state and cabinet at full drive: "
                  << juce::String (juce::Decibels::gainToDecibels (worstPeak), 2)
                  << " dBFS" << std::endl;
        expect (allFinite, "no state produces a non-finite sample");
        expect (worstPeak < 4.0f, "the output stays bounded");
    }

    // ---- 8. the cabinet is a voice, not a volume control ---------------------
    {
        auto bandLevel = [&proc] (int cabinetIndex)
        {
            neutral (proc);
            setParam (proc, blend, 100.0f);
            setParam (proc, drive, 8.0f);
            setParam (proc, split, 20.0f);
            setParam (proc, cabinet, (float) cabinetIndex);

            auto total = 0.0f;
            const double points[] = { 180, 260, 380, 550, 800, 1200, 1800, 2600, 3800 };

            for (auto f : points)
                total += responseAtDb (proc, f);

            return total / (float) (sizeof (points) / sizeof (points[0]));
        };

        const auto off   = bandLevel (0);
        const auto cone  = bandLevel (1);
        const auto steel = bandLevel (2);

        std::cout << "  mean gain 180 Hz .. 3.8 kHz - off: " << juce::String (off, 2)
                  << " dB, Cone: " << juce::String (cone, 2)
                  << " dB, Steel: " << juce::String (steel, 2) << " dB" << std::endl;

        expect (std::abs (cone - off) < 2.5f && std::abs (steel - off) < 2.5f,
                "switching a cabinet on is not also a volume change");
        expect (std::abs (cone - steel) < 2.5f, "the two cabinets are level-matched");
    }

    // ---- 9. the modelled low end is actually there ---------------------------
    // A guitar 4x12 capture has nothing below 80 Hz, so switching one of these cabinets on
    // would normally cost the bottom octave outright. The parallel modelled port in CabSim
    // is what stops that, and this is the check that would notice if it broke: measured
    // against the same path with the cabinet off, not against the input, because the drive
    // path's own coupling stages set that corner and are supposed to.
    {
        auto lowEndOf = [&proc] (int cabinetIndex, double freq)
        {
            neutral (proc);
            setParam (proc, blend, 100.0f);
            setParam (proc, drive, 8.0f);
            setParam (proc, split, 20.0f);
            setParam (proc, cabinet, (float) cabinetIndex);
            return responseAtDb (proc, freq);
        };

        const auto cone45  = lowEndOf (1, 45.0) - lowEndOf (0, 45.0);
        const auto cone70  = lowEndOf (1, 70.0) - lowEndOf (0, 70.0);
        const auto steel45 = lowEndOf (2, 45.0) - lowEndOf (0, 45.0);

        std::cout << "  cabinet against no cabinet - Cone 45 Hz: " << juce::String (cone45, 2)
                  << " dB, Cone 70 Hz: " << juce::String (cone70, 2)
                  << " dB, Steel 45 Hz: " << juce::String (steel45, 2) << " dB" << std::endl;
        expect (cone45 > -1.5f && cone70 > -1.5f && steel45 > -2.5f,
                "the modelled port replaces the bottom octave the captures do not have");
    }

    // ---- 10. aliasing --------------------------------------------------------
    // Everything not sitting on a harmonic of the input is either aliasing or numerical
    // noise, and at full drive on a three-stage cascade there is plenty of opportunity for
    // the first one.
    {
        neutral (proc);
        setParam (proc, blend, 100.0f);
        setParam (proc, drive, 100.0f);
        setParam (proc, split, 20.0f);
        setParam (proc, state, 2.0f);
        setParam (proc, cabinet, 0.0f);
        proc.reset();

        constexpr int order = 14;
        constexpr int size  = 1 << order;
        const double f0 = 2093.0;   // high C, and not a neat divisor of anything

        juce::AudioBuffer<float> buffer (2, size * 2);
        fillSine (buffer, f0, 0.5f);
        runThrough (proc, buffer);

        juce::dsp::FFT fft (order);
        std::vector<float> data ((std::size_t) (size * 2), 0.0f);

        for (int n = 0; n < size; ++n)
        {
            // Hann, so a component that is not exactly on a bin does not smear its energy
            // across the whole spectrum and drown the thing being measured.
            const auto w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                       * (float) n / (float) (size - 1));
            data[(std::size_t) n] = buffer.getSample (0, size + n) * w;
        }

        fft.performFrequencyOnlyForwardTransform (data.data());

        double harmonic = 0.0, other = 0.0;

        for (int bin = 1; bin < size / 2; ++bin)
        {
            const auto f = (double) bin * kSampleRate / (double) size;
            const auto power = (double) data[(std::size_t) bin] * data[(std::size_t) bin];

            if (f < 150.0)
                continue;   // the DC blocker's corner, not a signal

            auto onHarmonic = false;

            for (int h = 1; h * f0 < kSampleRate * 0.5; ++h)
                if (std::abs (f - (double) h * f0) < 5.0 * kSampleRate / (double) size)
                    onHarmonic = true;

            (onHarmonic ? harmonic : other) += power;
        }

        const auto ratio = 10.0 * std::log10 ((other + 1.0e-30) / (harmonic + 1.0e-30));
        std::cout << "  everything off-harmonic at full Decay drive: "
                  << juce::String (ratio, 1) << " dB below the harmonic series" << std::endl;
        expect (ratio < -45.0, "aliasing stays below -45 dB");
    }

    // ---- 11. changing things mid-stream --------------------------------------
    {
        neutral (proc);
        setParam (proc, blend, 100.0f);
        setParam (proc, drive, 60.0f);
        proc.reset();

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        juce::MidiBuffer midi;
        auto finite = true;

        for (int i = 0; i < 240; ++i)
        {
            fillBassTone (buffer, 55.0 + (double) (i % 7) * 30.0, 0.5f);

            setParam (proc, state,   (float) (i % 4));
            setParam (proc, cabinet, (float) (i % 3));
            setParam (proc, split,   20.0f + (float) (i % 20) * 35.0f);

            proc.processBlock (buffer, midi);
            finite = finite && analyse (buffer).finite;
        }

        expect (finite, "switching state, cabinet and crossover mid-stream stays finite");
    }

    // ---- 12. sample rates ----------------------------------------------------
    {
        auto finite = true;

        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            Processor other;
            other.setPlayConfigDetails (2, 2, rate, kBlockSize);
            other.prepareToPlay (rate, kBlockSize);
            neutral (other);
            setParam (other, blend, 100.0f);
            setParam (other, drive, 85.0f);
            setParam (other, cabinet, 1.0f);
            other.reset();

            juce::AudioBuffer<float> buffer (2, (int) rate / 2);
            fillNoise (buffer, 0.6f, 0, buffer.getNumSamples());
            runThrough (other, buffer);

            const auto stats = analyse (buffer);
            finite = finite && stats.finite;

            std::cout << "  " << (int) rate << " Hz: latency "
                      << other.getLatencySamples() << " samples, peak "
                      << juce::String (juce::Decibels::gainToDecibels (stats.peak), 1)
                      << " dBFS" << std::endl;
        }

        expect (finite, "every supported sample rate stays finite");
    }

    // ---- 13. mono ------------------------------------------------------------
    {
        Processor mono;
        mono.setPlayConfigDetails (1, 1, kSampleRate, kBlockSize);
        mono.prepareToPlay (kSampleRate, kBlockSize);
        neutral (mono);
        setParam (mono, blend, 100.0f);
        setParam (mono, drive, 70.0f);
        setParam (mono, cabinet, 1.0f);
        mono.reset();

        juce::AudioBuffer<float> buffer (1, (int) kSampleRate);
        fillBassTone (buffer, 61.7, 0.5f);
        runThrough (mono, buffer);

        const auto stats = analyse (buffer);
        expect (stats.finite && stats.peak > 0.01f, "a mono instantiation works");
    }

    // ---- CPU -----------------------------------------------------------------
    {
        constexpr double seconds = 30.0;

        auto costOf = [&] (int cabinetIndex, int stateIndex)
        {
            neutral (proc);
            setParam (proc, blend, 70.0f);
            setParam (proc, drive, 65.0f);
            setParam (proc, state, (float) stateIndex);
            setParam (proc, cabinet, (float) cabinetIndex);
            proc.reset();

            juce::AudioBuffer<float> buffer (2, kBlockSize);
            fillSine (buffer, 110.0, 0.3f);

            juce::MidiBuffer midi;
            const auto start = juce::Time::getHighResolutionTicks();

            for (int done = 0; done < (int) (seconds * kSampleRate); done += kBlockSize)
                proc.processBlock (buffer, midi);

            return juce::Time::highResolutionTicksToSeconds (
                       juce::Time::getHighResolutionTicks() - start);
        };

        const auto bare = costOf (0, 1);
        const auto full = costOf (1, 3);

        std::cout << "\n" << seconds << " s of stereo processed in "
                  << juce::String (bare, 3) << " s  ("
                  << juce::String (bare / seconds * 100.0, 2)
                  << " % of one core at " << kSampleRate << " Hz) with no cabinet, and in "
                  << juce::String (full, 3) << " s  ("
                  << juce::String (full / seconds * 100.0, 2)
                  << " %) with three stages and a cabinet" << std::endl;
    }

    std::cout << (failures == 0 ? "\nall checks passed"
                                : "\n" + juce::String (failures) + " check(s) failed")
              << std::endl;

    return failures == 0 ? 0 : 1;
}

/** The measurement behind StateSpec::makeupDb.

    Drives each state at the five breakpoints with a bass tone, fully collapsed, and
    reports how far the output has drifted from what the same signal does at Drive 0. The
    negated numbers go straight into Shapers.h; run it again afterwards and the residual
    should be a fraction of a dB. */
int runSweep()
{
    using namespace collapse::pid;

    Processor proc;
    proc.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    proc.prepareToPlay (kSampleRate, kBlockSize);

    std::cout << "level sweep @ " << kSampleRate << " Hz - fully collapsed, Cone, "
              << "bass-shaped noise at -6 dBFS peak\n" << std::endl;

    // The reference: a level to aim at that does not itself move when a spec changes.
    // Straight through the plug-in with nothing collapsed, which is unity by construction.
    auto renderRms = [&proc] (int stateIndex, float drivePercent, bool collapsed)
    {
        neutral (proc);
        setParam (proc, blend, collapsed ? 100.0f : 0.0f);
        setParam (proc, split, 20.0f);
        setParam (proc, cabinet, 1.0f);
        setParam (proc, state, (float) stateIndex);
        setParam (proc, drive, drivePercent);
        proc.reset();

        juce::AudioBuffer<float> buffer (2, (int) (kSampleRate * 1.5));
        fillBassNoise (buffer, 0.5f);
        runThrough (proc, buffer);

        // Second half only, well after the sag and bias envelopes have settled.
        return analyse (buffer, (int) (kSampleRate * 0.75), (int) (kSampleRate * 0.75)).rms;
    };

    const auto reference = renderRms (0, 0.0f, false);

    std::cout << "reference (Blend 0): "
              << juce::Decibels::gainToDecibels (reference) << " dBFS RMS\n" << std::endl;

    for (int s = 0; s < (int) collapse::dsp::State::numStates; ++s)
    {
        const auto& spec = collapse::dsp::stateSpec (s);
        juce::String line, correction;

        for (int i = 0; i < 5; ++i)
        {
            const auto drivePercent = (float) i * 25.0f;
            const auto rms = renderRms (s, drivePercent, true);
            const auto delta = juce::Decibels::gainToDecibels (
                                   juce::jmax (1.0e-9f, rms / juce::jmax (1.0e-9f, reference)));

            line += juce::String (delta, 1).paddedLeft (' ', 7);

            // What is already in the spec, minus what is still left over.
            correction += juce::String (spec.makeupDb[i] - delta, 1) + "f, ";
        }

        std::cout << juce::String (spec.name).paddedRight (' ', 10) << line << " dB"
                  << std::endl;
        std::cout << "           { " << correction.dropLastCharacters (2) << " }\n"
                  << std::endl;
    }

    std::cout << "paste each brace into that state's makeupDb in Source/dsp/Shapers.h"
              << std::endl;
    return 0;
}

/** The cabinet's magnitude response, measured through the plug-in with the drive path as
    close to a wire as it gets. This is how the modelled low end in CabSim was tuned, and
    it is the only way to see where the measurement stops and the model starts. */
int runCab()
{
    using namespace collapse::pid;

    Processor proc;
    proc.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    proc.prepareToPlay (kSampleRate, kBlockSize);

    const double points[] = { 30, 40, 50, 60, 70, 85, 100, 125, 160, 200, 260, 340,
                              450, 600, 800, 1100, 1500, 2000, 2800, 3800, 5000, 7000 };

    std::cout << "cabinet response @ " << kSampleRate
              << " Hz, through the collapsed path at Drive 8 %\n" << std::endl;
    std::cout << "     Hz      OFF     CONE    STEEL" << std::endl;

    for (auto f : points)
    {
        juce::String row = juce::String ((int) f).paddedLeft (' ', 7);

        for (int cabinetIndex = 0; cabinetIndex < 3; ++cabinetIndex)
        {
            neutral (proc);
            setParam (proc, blend, 100.0f);
            setParam (proc, drive, 8.0f);
            setParam (proc, split, 20.0f);
            setParam (proc, cabinet, (float) cabinetIndex);

            row += juce::String (responseAtDb (proc, f), 1).paddedLeft (' ', 9);
        }

        std::cout << row << std::endl;
    }

    return 0;
}

int writePng (const juce::Image& image, const juce::File& destination)
{
    destination.deleteFile();

    if (auto stream = destination.createOutputStream())
    {
        juce::PNGImageFormat png;

        if (png.writeImageToStream (image, *stream))
        {
            std::cout << "wrote " << destination.getFullPathName()
                      << " (" << image.getWidth() << "x" << image.getHeight() << ")"
                      << std::endl;
            return 0;
        }
    }

    std::cerr << "could not write " << destination.getFullPathName() << std::endl;
    return 1;
}

/** Renders the editor with no window and no display server, so a panel that fails to
    construct or paint fails the build instead of shipping. */
int renderShot (const juce::File& destination, int presetIndex)
{
    Processor proc;
    proc.setPlayConfigDetails (2, 2, kSampleRate, kBlockSize);
    proc.prepareToPlay (kSampleRate, kBlockSize);

    if (presetIndex >= 0)
        proc.setCurrentProgram (presetIndex);

    // Push audio through first, so the wave has something to show. Component timers never
    // fire here: there is no message loop to fire them.
    {
        juce::AudioBuffer<float> buffer (2, (int) kSampleRate);
        fillBassTone (buffer, 61.7, 0.55f);
        runThrough (proc, buffer);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

    if (editor == nullptr)
    {
        std::cerr << "no editor" << std::endl;
        return 1;
    }

    editor->setSize (collapse::CollapseEditor::kLogicalWidth,
                     collapse::CollapseEditor::kLogicalHeight);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

    if (image.isNull())
    {
        std::cerr << "snapshot failed" << std::endl;
        return 1;
    }

    return writePng (image, destination);
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String command = argc > 1 ? juce::String (argv[1]) : "check";
    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (command == "check") return runCheck();
    if (command == "sweep") return runSweep();
    if (command == "cab")   return runCab();

    if (command == "shot")
        return renderShot (cwd.getChildFile (argc > 2 ? juce::String (argv[2]) : "panel.png"),
                           argc > 3 ? juce::String (argv[3]).getIntValue() : -1);

    std::cerr << "usage: devtool [check | sweep | cab | shot <file.png> [presetIndex]]"
              << std::endl;
    return 2;
}
