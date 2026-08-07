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

#include <juce_audio_processors/juce_audio_processors.h>

namespace collapse::gui
{

/** Bypass, drawn as an observer's aperture: a ring that is open and glowing while the
    plug-in is doing something, and shut and dark while it is not.

    Lit means active, which is the opposite of what the bypass parameter stores - hosts
    expect a parameter called Bypass to be true when bypassed, and users expect a lit
    button to mean the thing is on. The inversion lives here, in one place. */
class PowerToggle final : public juce::Component,
                          public juce::SettableTooltipClient,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    PowerToggle (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID);
    ~PowerToggle() override;

    void setPalette (const skin::Palette* p) noexcept { palette = p; }

    bool isActive() const noexcept { return ! bypassed; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void parameterChanged (const juce::String&, float) override;

    juce::AudioProcessorValueTreeState& state;
    juce::String parameterID;
    const skin::Palette* palette = nullptr;

    bool bypassed = false;
    bool hovered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerToggle)
};

} // namespace collapse::gui
