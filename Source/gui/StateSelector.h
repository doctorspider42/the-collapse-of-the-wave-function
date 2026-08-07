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

/** A row of pills bound to an AudioParameterChoice.

    A drop-down would hide three quarters of what the plug-in can do behind a click, and
    these are the two decisions worth making first. Every option is on the panel, the
    selected one is lit, and choosing is one click rather than three.

    It draws its own text and does not use juce::Button, because the lit pill needs a glow
    that follows the palette and nothing in LookAndFeel_V4 has a hook for that. */
class StateSelector final : public juce::Component,
                            public juce::SettableTooltipClient,
                            private juce::AudioProcessorValueTreeState::Listener
{
public:
    StateSelector (juce::AudioProcessorValueTreeState& state,
                   const juce::String& parameterID,
                   const juce::StringArray& options,
                   const juce::String& tooltip);

    ~StateSelector() override;

    void setPalette (const skin::Palette* p) noexcept { palette = p; }

    /** An optional line under the row, describing whatever is selected. */
    void setCaptions (juce::StringArray newCaptions);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void parameterChanged (const juce::String&, float) override;
    juce::Rectangle<float> pillBounds (int index) const;
    int indexAt (juce::Point<float> position) const;

    juce::AudioProcessorValueTreeState& state;
    juce::String parameterID;
    juce::StringArray options, captions;
    const skin::Palette* palette = nullptr;

    int selected = 0;
    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StateSelector)
};

} // namespace collapse::gui
