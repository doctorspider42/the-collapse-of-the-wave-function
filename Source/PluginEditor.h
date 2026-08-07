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

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"
#include "gui/Backdrop.h"
#include "gui/CollapseKnob.h"
#include "gui/PowerToggle.h"
#include "gui/Skin.h"
#include "gui/StateSelector.h"
#include "gui/WaveView.h"

namespace collapse
{

class CollapseEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit CollapseEditor (CollapseProcessor&);
    ~CollapseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Everything is laid out once at this size inside `content`, which is then scaled by
        a transform. Layout code never deals with scaling and the proportions cannot
        drift. */
    static constexpr int kLogicalWidth  = 880;
    static constexpr int kLogicalHeight = 580;

private:
    /** The preset row: two arrows and a name that opens the whole list. Small enough that
        a file of its own would be filing rather than organising. */
    class PresetBar final : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        explicit PresetBar (CollapseProcessor&);

        void setPalette (const skin::Palette* p) noexcept { palette = p; }
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        enum class Zone { none, previous, next, name };

        Zone zoneAt (juce::Point<float>) const;
        juce::Rectangle<float> arrowBounds (bool previous) const;
        void step (int delta);

        CollapseProcessor& processor;
        const skin::Palette* palette = nullptr;
        Zone hovered = Zone::none;
    };

    void timerCallback() override;
    void layOutContent();
    void pushPalette (bool force);

    static constexpr int kNumKnobs = 7;

    CollapseProcessor& processor;

    skin::PanelLookAndFeel panelLookAndFeel;

    juce::Component content;
    gui::Backdrop backdrop;
    gui::WaveView wave;

    std::unique_ptr<gui::CollapseKnob> knobs[kNumKnobs];
    std::unique_ptr<gui::StateSelector> stateSelector;
    std::unique_ptr<gui::StateSelector> cabinetSelector;
    std::unique_ptr<gui::PowerToggle> power;
    std::unique_ptr<PresetBar> presetBar;

    juce::TooltipWindow tooltips { this, 700 };

    skin::Palette palette;
    float smoothedHeat = 0.0f;
    float pushedHeat = -1.0f;
    int   pushedState = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollapseEditor)
};

} // namespace collapse
