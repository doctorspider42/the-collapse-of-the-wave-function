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

#include "WaveView.h"

#include "../dsp/Utils.h"

namespace collapse::gui
{

namespace
{
    // 30 Hz. Fast enough to read as motion, slow enough that a full-screen editor on a
    // laptop does not spend a core on it.
    constexpr int   kFrameMs = 33;
    constexpr float kFrameSeconds = 0.033f;

    // The four standing modes of the coherent state, and how fast each of them turns.
    // Deliberately irrational-ish ratios, so the superposition never repeats visibly.
    constexpr float kModeAmp[4]  = { 1.00f, 0.52f, 0.30f, 0.17f };
    constexpr float kModeRate[4] = { 0.115f, 0.191f, 0.307f, 0.463f };

    /** One-pole follower for a display value. Different rise and fall times, because a
        meter that falls as fast as it rises is unreadable. */
    float follow (float current, float target, float rise, float fall) noexcept
    {
        const auto k = target > current ? rise : fall;
        return current + k * (target - current);
    }
}

WaveView::WaveView()
{
    setInterceptsMouseClicks (false, false);

    dsp::Xorshift rng (0x7a3f19c5u);

    for (int i = 0; i < kPackets; ++i)
    {
        // Spread over the box with a little jitter, rather than uniformly at random: five
        // random numbers in [0,1] cluster often enough to look like a mistake.
        packetX[(std::size_t) i] = (float) (i + 0.5f) / (float) kPackets
                                     + rng.nextBipolar() * 0.055f;
        packetPhase[(std::size_t) i] = rng.nextFloat() * dsp::kTwoPi;
        packetSign[(std::size_t) i]  = rng.nextFloat() > 0.5f ? 1.0f : -1.0f;
    }

    // Not zero: at phase zero all four modes are in step and the superposition is a single
    // symmetrical hump, which is both the least interesting thing this can draw and the
    // first thing every user would see. Starting part-way through puts the modes out of
    // register so the editor opens on a wave rather than on a bulge.
    phase = 3.7f;

    startTimer (kFrameMs);
}

void WaveView::setEnergy (float level, float collapse, float blend, float coherence)
{
    targetLevel     = juce::jlimit (0.0f, 1.0f, level);
    targetCollapse  = juce::jlimit (0.0f, 1.0f, collapse);
    targetBlend     = juce::jlimit (0.0f, 1.0f, blend);
    targetCoherence = juce::jlimit (0.0f, 1.0f, coherence);

    if (! seeded)
    {
        seeded = true;
        smoothedLevel     = targetLevel;
        smoothedCollapse  = targetCollapse;
        smoothedBlend     = targetBlend;
        smoothedCoherence = targetCoherence;
        repaint();
    }
}

void WaveView::timerCallback()
{
    phase += kFrameSeconds;

    smoothedLevel     = follow (smoothedLevel,     targetLevel,     0.42f, 0.10f);
    smoothedCollapse  = follow (smoothedCollapse,  targetCollapse,  0.22f, 0.07f);
    smoothedBlend     = follow (smoothedBlend,     targetBlend,     0.18f, 0.18f);
    smoothedCoherence = follow (smoothedCoherence, targetCoherence, 0.10f, 0.10f);

    repaint();
}

float WaveView::waveAt (float x) const
{
    // ---- the coherent state: four standing modes in a box ---------------------
    auto coherent = 0.0f;

    for (int k = 0; k < 4; ++k)
        coherent += kModeAmp[k]
                      * std::sin ((float) (k + 1) * dsp::kPi * x)
                      * std::cos (dsp::kTwoPi * kModeRate[k] * phase
                                    + (float) k * 1.7f);

    coherent *= 0.68f;

    // ---- the collapsed state: localised packets -------------------------------
    // Only the part of the drive path that Blend actually lets through has been observed,
    // so the morph is driven by the product rather than by Drive alone. A wall of fuzz
    // sitting at Blend 0 is still, physically and audibly, a superposition.
    const auto c = dsp::smoothstep (smoothedCollapse * smoothedBlend);

    if (c <= 0.001f)
        return coherent;

    // The packet narrows as the measurement sharpens, and the wave inside it speeds up:
    // localising a particle is exactly the trade that costs you its momentum.
    const auto sigma = 0.088f - 0.052f * c;
    const auto inner = 34.0f + 46.0f * c;

    auto collapsed = 0.0f;

    for (int j = 0; j < kPackets; ++j)
    {
        const auto d = (x - packetX[(std::size_t) j]) / sigma;
        const auto envelope = std::exp (-d * d);

        if (envelope < 0.002f)
            continue;

        collapsed += packetSign[(std::size_t) j] * envelope
                       * std::cos (inner * (x - packetX[(std::size_t) j])
                                     + packetPhase[(std::size_t) j]
                                     + dsp::kTwoPi * 0.31f * phase);
    }

    return (1.0f - c) * coherent + c * collapsed * 0.86f;
}

void WaveView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    if (bounds.getWidth() < 8.0f || bounds.getHeight() < 8.0f)
        return;

    constexpr float corner = 8.0f;
    const auto c = dsp::smoothstep (smoothedCollapse * smoothedBlend);

    // ---- the box --------------------------------------------------------------
    {
        juce::ColourGradient well (juce::Colour (0xff03050c), bounds.getCentreX(), bounds.getY(),
                                   juce::Colour (0xff010205), bounds.getCentreX(),
                                   bounds.getBottom(), false);
        g.setGradientFill (well);
        g.fillRoundedRectangle (bounds, corner);
    }

    // A position basis to measure against. Faint, evenly spaced, and the reason the
    // collapsed packets read as being *somewhere* rather than merely being spiky.
    {
        g.setColour (skin::colour::textFaint.withAlpha (0.16f));

        for (int i = 1; i < 16; ++i)
        {
            const auto x = bounds.getX() + bounds.getWidth() * (float) i / 16.0f;
            g.drawLine (x, bounds.getY() + 8.0f, x, bounds.getBottom() - 8.0f, 0.6f);
        }
    }

    const auto plot = bounds.reduced (10.0f, 12.0f);
    const auto densityHeight = plot.getHeight() * 0.24f;
    const auto axisY = plot.getY() + (plot.getHeight() - densityHeight) * 0.5f;
    const auto swing = (plot.getHeight() - densityHeight) * 0.44f;

    g.setColour (skin::colour::textFaint.withAlpha (0.30f));
    g.drawLine (plot.getX(), axisY, plot.getRight(), axisY, 0.7f);

    // Always some motion, even in silence: an idle panel that looks switched off is a
    // support question waiting to happen.
    const auto amplitude = 0.30f + 0.70f * std::sqrt (smoothedLevel);

    // ---- the coherent ribbon: the band that never enters the drive path --------
    if (smoothedCoherence > 0.02f)
    {
        juce::Path ribbon;

        for (int i = 0; i <= kPoints; ++i)
        {
            const auto t = (float) i / (float) kPoints;
            const auto x = plot.getX() + plot.getWidth() * t;
            const auto y = axisY - swing * 0.55f * amplitude * smoothedCoherence
                                     * std::sin (dsp::kPi * t)
                                     * std::sin (dsp::kTwoPi * 0.083f * phase + 0.6f);

            if (i == 0) ribbon.startNewSubPath (x, y);
            else        ribbon.lineTo (x, y);
        }

        skin::glowPath (g, ribbon, skin::colour::cold, 1.3f,
                        0.16f + 0.20f * smoothedCoherence, 2);
    }

    // ---- psi ------------------------------------------------------------------
    juce::Path wave;
    std::array<float, kPoints + 1> psi {};

    for (int i = 0; i <= kPoints; ++i)
    {
        const auto t = (float) i / (float) kPoints;
        const auto v = waveAt (t) * amplitude;
        psi[(std::size_t) i] = v;

        const auto x = plot.getX() + plot.getWidth() * t;
        const auto y = axisY - swing * juce::jlimit (-1.6f, 1.6f, v);

        if (i == 0) wave.startNewSubPath (x, y);
        else        wave.lineTo (x, y);
    }

    // The fill under the curve, closed back along the axis. A gradient rather than a flat
    // alpha, so the wave sits in the box instead of on top of it.
    {
        auto filled = wave;
        filled.lineTo (plot.getRight(), axisY);
        filled.lineTo (plot.getX(), axisY);
        filled.closeSubPath();

        const auto tint = palette.accent.interpolatedWith (palette.hot, c * 0.65f);
        juce::ColourGradient fill (tint.withAlpha (0.030f + 0.10f * amplitude),
                                   plot.getCentreX(), axisY - swing,
                                   tint.withAlpha (0.0f), plot.getCentreX(), axisY, false);
        g.setGradientFill (fill);
        g.fillPath (filled);
    }

    {
        const auto tint = palette.accent.interpolatedWith (palette.hot, c * 0.8f);
        skin::glowPath (g, wave, tint, 1.8f + 0.7f * amplitude,
                        0.55f + 0.42f * amplitude, 4);
    }

    // ---- |psi|^2, along the floor ---------------------------------------------
    // The probability of finding the thing anywhere in particular. One broad hump while
    // the wave is whole; a row of spikes once it is not.
    {
        const auto floorY = plot.getBottom();
        juce::Path density;
        density.startNewSubPath (plot.getX(), floorY);

        auto highest = 1.0e-6f;

        for (int i = 0; i <= kPoints; ++i)
            highest = juce::jmax (highest, psi[(std::size_t) i] * psi[(std::size_t) i]);

        for (int i = 0; i <= kPoints; ++i)
        {
            const auto t = (float) i / (float) kPoints;
            const auto v = psi[(std::size_t) i] * psi[(std::size_t) i] / highest;
            density.lineTo (plot.getX() + plot.getWidth() * t,
                            floorY - densityHeight * v * (0.25f + 0.75f * amplitude));
        }

        density.lineTo (plot.getRight(), floorY);
        density.closeSubPath();

        const auto tint = palette.accent.interpolatedWith (palette.hot, 0.35f + 0.5f * c);
        juce::ColourGradient fill (tint.withAlpha (0.50f), plot.getCentreX(),
                                   floorY - densityHeight,
                                   tint.withAlpha (0.10f), plot.getCentreX(), floorY, false);
        g.setGradientFill (fill);
        g.fillPath (density);

        g.setColour (tint.withAlpha (0.55f + 0.35f * c));
        g.strokePath (density, juce::PathStrokeType (0.9f));
    }

    // ---- where the measurement landed -----------------------------------------
    if (c > 0.05f)
    {
        const auto floorY = plot.getBottom() + 1.0f;

        for (int j = 0; j < kPackets; ++j)
        {
            const auto x = plot.getX() + plot.getWidth() * packetX[(std::size_t) j];
            skin::glowEllipse (g, { x, floorY }, 1.3f + 1.1f * c, palette.hot,
                               0.55f * c * (0.4f + 0.6f * amplitude), 4);
        }
    }

    // ---- the label -------------------------------------------------------------
    // Two words crossfading rather than a Greek ket: the panel asks the operating system
    // for whatever sans-serif it has, and there is no promise that face carries U+27E9.
    {
        auto label = bounds.reduced (13.0f, 0.0f).withHeight (14.0f).translated (0.0f, 6.0f);
        const auto font = skin::labelFont (8.0f, true);

        skin::drawTracked (g, "SUPERPOSED", font, label, juce::Justification::centredLeft,
                           skin::colour::cold.withAlpha (0.20f + 0.42f * (1.0f - c)), 2.2f);
        skin::drawTracked (g, "OBSERVED", font, label, juce::Justification::centredRight,
                           palette.hot.withAlpha (0.16f + 0.52f * c), 2.2f);
    }

    // ---- the frame -------------------------------------------------------------
    g.setColour (juce::Colours::black.withAlpha (0.60f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
    g.setColour (palette.accent.withAlpha (0.20f + 0.16f * amplitude));
    g.drawRoundedRectangle (bounds.reduced (1.5f), corner - 1.0f, 1.0f);
}

} // namespace collapse::gui
