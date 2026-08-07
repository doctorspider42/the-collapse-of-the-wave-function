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

/** A dial drawn as a dark sphere in a ring of graduations, with a glowing value arc.

    The juce::Slider underneath keeps every behaviour hosts and users expect - drag, wheel,
    fine drag with ctrl or cmd, double-click to default, host automation - and this
    component only takes over the painting and the lettering. */
class CollapseKnob final : public juce::Component
{
public:
    CollapseKnob (juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterID,
                  const juce::String& caption,
                  const juce::String& tooltip,
                  bool bipolar = false);

    ~CollapseKnob() override;

    /** The palette lives in the editor and changes with the State selector, so it is read
        through a pointer: a colour change costs a repaint rather than a rebuild. */
    void setPalette (const skin::Palette* p) noexcept { palette = p; }

    void paint (juce::Graphics&) override;
    void resized() override;

    void setKnobDiameter (float d) { diameter = d; resized(); }

    static constexpr float captionHeight = 15.0f;
    static constexpr float readoutHeight = 17.0f;

private:
    class LookAndFeel;

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<LookAndFeel> lookAndFeel;

    const skin::Palette* palette = nullptr;
    juce::String caption;
    float diameter = 74.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollapseKnob)
};

} // namespace collapse::gui
