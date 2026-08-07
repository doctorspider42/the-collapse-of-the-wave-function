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

#include "Skin.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace collapse::gui
{

/** The panel's one moving picture, and a literal reading of what the DSP is doing.

    A superposition of four standing modes is the coherent state: smooth, wide, spread
    across the whole box. As the drive path is worked and Blend brings it into the sum,
    that wave morphs into a handful of localised packets - the measurement - each of them
    oscillating hard inside a narrow envelope. The strip along the bottom is |psi|^2, the
    probability of finding the particle at all, and it goes from one broad hump to a row
    of spikes as the same thing happens.

    Underneath both of them runs a slow, wide, cold ribbon: the low band that never enters
    the drive path. Its size follows the engine's coherence figure, so when the crossover
    is high and the bottom of the instrument is being held back, you can see it being held
    back.

    Nothing here reaches into the audio thread. The editor polls four floats at frame rate
    and hands them over; this component smooths them and draws.
*/
class WaveView final : public juce::Component,
                       private juce::Timer
{
public:
    WaveView();
    ~WaveView() override = default;

    void setPalette (const skin::Palette& p) { palette = p; }

    /** All four are 0..1. `collapse` is how hard the stages are being hit, `blend` how
        much of that reaches the output, `coherence` the share of the output that came
        down the clean low path, `level` the output envelope. */
    void setEnergy (float level, float collapse, float blend, float coherence);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    /** psi(x) at the current phase, for x in [0, 1]. */
    float waveAt (float x) const;

    static constexpr int kPoints  = 224;
    static constexpr int kPackets = 5;

    skin::Palette palette;

    // Where the packets land once the wave has collapsed. Fixed for the life of the
    // component and derived from a fixed seed, because a picture that rearranges itself
    // every time the editor opens reads as a bug rather than as physics.
    std::array<float, kPackets> packetX {};
    std::array<float, kPackets> packetPhase {};
    std::array<float, kPackets> packetSign {};

    float phase = 0.0f;
    float smoothedLevel = 0.0f, smoothedCollapse = 0.0f;
    float smoothedBlend = 0.0f, smoothedCoherence = 1.0f;
    float targetLevel = 0.0f, targetCollapse = 0.0f;
    float targetBlend = 0.0f, targetCoherence = 1.0f;

    /** The followers start empty, so the first frame after the editor opens would show a
        flat line easing up towards whatever is actually playing. Snapping the first update
        skips that - and it is what makes the offline `devtool shot`, where no timer ever
        fires, render the plug-in rather than a dead panel. */
    bool seeded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveView)
};

} // namespace collapse::gui
