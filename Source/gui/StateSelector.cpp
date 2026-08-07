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

#include "StateSelector.h"

namespace collapse::gui
{

namespace
{
    constexpr float kGap = 6.0f;
    constexpr float kCaptionHeight = 14.0f;
}

StateSelector::StateSelector (juce::AudioProcessorValueTreeState& s,
                              const juce::String& id,
                              const juce::StringArray& opts,
                              const juce::String& tooltip)
    : state (s), parameterID (id), options (opts)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    if (auto* p = state.getParameter (parameterID))
        selected = juce::roundToInt (p->convertFrom0to1 (p->getValue()));

    state.addParameterListener (parameterID, this);
}

StateSelector::~StateSelector()
{
    state.removeParameterListener (parameterID, this);
}

void StateSelector::setCaptions (juce::StringArray newCaptions)
{
    captions = std::move (newCaptions);
    repaint();
}

void StateSelector::parameterChanged (const juce::String&, float newValue)
{
    const auto index = juce::roundToInt (newValue);

    if (index == selected)
        return;

    selected = index;

    // The host's automation thread is not the message thread, and repaint() must not be
    // called from anywhere else.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StateSelector> (this)]
    {
        if (safe != nullptr)
            safe->repaint();
    });
}

juce::Rectangle<float> StateSelector::pillBounds (int index) const
{
    auto area = getLocalBounds().toFloat();

    if (captions.isEmpty())
        area = area.reduced (0.0f, 1.0f);
    else
        area.removeFromBottom (kCaptionHeight);

    const auto count = juce::jmax (1, options.size());
    const auto width = (area.getWidth() - kGap * (float) (count - 1)) / (float) count;

    return area.withWidth (width)
               .translated ((width + kGap) * (float) index, 0.0f);
}

int StateSelector::indexAt (juce::Point<float> position) const
{
    for (int i = 0; i < options.size(); ++i)
        if (pillBounds (i).contains (position))
            return i;

    return -1;
}

void StateSelector::mouseDown (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.position);

    if (index < 0 || index == selected)
        return;

    // Through the parameter, so the host records the change, undo works and any other
    // attached view follows.
    if (auto* p = state.getParameter (parameterID))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }

    selected = index;
    repaint();
}

void StateSelector::mouseMove (const juce::MouseEvent& e)
{
    const auto index = indexAt (e.position);

    if (index == hovered)
        return;

    hovered = index;
    repaint();
}

void StateSelector::mouseExit (const juce::MouseEvent&)
{
    if (hovered < 0)
        return;

    hovered = -1;
    repaint();
}

void StateSelector::paint (juce::Graphics& g)
{
    const auto accent = palette != nullptr ? palette->accent : skin::colour::cold;
    const auto heat   = palette != nullptr ? palette->heat : 0.0f;

    for (int i = 0; i < options.size(); ++i)
    {
        const auto pill = pillBounds (i).reduced (0.5f);
        const auto radius = pill.getHeight() * 0.5f;
        const auto isSelected = i == selected;
        const auto isHovered = i == hovered;

        // Recessed and dark unless lit. The unselected pills have to stay quiet or the
        // panel turns into a row of buttons competing with the wave.
        juce::ColourGradient fill (juce::Colour (isSelected ? 0xff10182a : 0xff080c15),
                                   pill.getCentreX(), pill.getY(),
                                   juce::Colour (isSelected ? 0xff060a14 : 0xff04060b),
                                   pill.getCentreX(), pill.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (pill, radius);

        if (isSelected)
        {
            // The lit pill glows from behind rather than being filled with the accent -
            // a solid block of colour at this size reads as a warning, not a selection.
            for (int layer = 3; layer >= 1; --layer)
            {
                const auto t = (float) layer / 3.0f;
                g.setColour (accent.withAlpha (0.13f * (1.0f - t) * (1.0f + 0.5f * heat)));
                g.drawRoundedRectangle (pill.expanded (t * 3.0f), radius + t * 3.0f,
                                        1.6f + t * 1.4f);
            }

            g.setColour (accent.withAlpha (0.78f));
            g.drawRoundedRectangle (pill, radius, 1.2f);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (isHovered ? 0.16f : 0.07f));
            g.drawRoundedRectangle (pill, radius, 1.0f);
        }

        const auto colour = isSelected ? accent.brighter (0.45f)
                                       : (isHovered ? skin::colour::text.withAlpha (0.80f)
                                                    : skin::colour::textDim);

        skin::drawTracked (g, options[i].toUpperCase(), skin::labelFont (9.5f, isSelected),
                           pill, juce::Justification::centred, colour, 1.7f);
    }

    if (! captions.isEmpty() && juce::isPositiveAndBelow (selected, captions.size()))
    {
        auto caption = getLocalBounds().toFloat().removeFromBottom (kCaptionHeight);
        skin::drawTracked (g, captions[selected], skin::labelFont (8.5f), caption,
                           juce::Justification::centred,
                           skin::colour::textFaint.withAlpha (0.95f), 0.9f);
    }
}

} // namespace collapse::gui
