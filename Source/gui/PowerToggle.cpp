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

#include "PowerToggle.h"

namespace collapse::gui
{

PowerToggle::PowerToggle (juce::AudioProcessorValueTreeState& s, const juce::String& id)
    : state (s), parameterID (id)
{
    setTooltip ("Bypass. Lit while the plug-in is in the signal path.");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    if (auto* raw = state.getRawParameterValue (parameterID))
        bypassed = raw->load() > 0.5f;

    state.addParameterListener (parameterID, this);
}

PowerToggle::~PowerToggle()
{
    state.removeParameterListener (parameterID, this);
}

void PowerToggle::parameterChanged (const juce::String&, float newValue)
{
    const auto now = newValue > 0.5f;

    if (now == bypassed)
        return;

    bypassed = now;

    // The host's automation thread is not the message thread.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PowerToggle> (this)]
    {
        if (safe != nullptr)
            safe->repaint();
    });
}

void PowerToggle::mouseDown (const juce::MouseEvent&)
{
    if (auto* p = state.getParameter (parameterID))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (bypassed ? 0.0f : 1.0f);
        p->endChangeGesture();
    }

    bypassed = ! bypassed;
    repaint();
}

void PowerToggle::mouseEnter (const juce::MouseEvent&) { hovered = true;  repaint(); }
void PowerToggle::mouseExit  (const juce::MouseEvent&) { hovered = false; repaint(); }

void PowerToggle::paint (juce::Graphics& g)
{
    const auto accent = palette != nullptr ? palette->accent : skin::colour::cold;
    const auto heat   = palette != nullptr ? palette->heat : 0.0f;

    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const auto d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto circle = juce::Rectangle<float> (d, d).withCentre (bounds.getCentre());
    const auto centre = circle.getCentre();
    const auto radius = d * 0.5f;

    const auto active = ! bypassed;
    const auto colour = active ? accent : skin::colour::textFaint;

    // ---- the well --------------------------------------------------------------
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (circle.translated (0.0f, 1.5f).expanded (1.5f));

    juce::ColourGradient body (juce::Colour (0xff141c2c),
                               centre.x - radius * 0.45f, centre.y - radius * 0.6f,
                               juce::Colour (0xff03050b),
                               centre.x + radius * 0.4f, centre.y + radius * 0.8f, true);
    g.setGradientFill (body);
    g.fillEllipse (circle);

    // ---- the aperture ----------------------------------------------------------
    // Open when active, closed to a slit when bypassed. Two arcs and a gap at the top -
    // the standard power glyph, which nobody has to be taught.
    const auto ringRadius = radius * 0.52f;
    const auto opening = active ? 0.62f : 0.10f;

    juce::Path ring;
    ring.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                        opening, juce::MathConstants<float>::twoPi - opening, true);

    juce::Path stem;
    stem.startNewSubPath (centre.x, centre.y - ringRadius * (active ? 1.32f : 0.98f));
    stem.lineTo (centre.x, centre.y - ringRadius * 0.24f);

    const auto intensity = active ? (0.85f + 0.15f * heat) * (hovered ? 1.0f : 0.90f)
                                  : (hovered ? 0.42f : 0.30f);

    skin::glowPath (g, ring, colour, 2.0f, intensity, active ? 4 : 2);
    skin::glowPath (g, stem, colour, 2.0f, intensity, active ? 4 : 2);

    if (active)
        skin::glowEllipse (g, centre, radius * 0.30f, accent, 0.16f + 0.12f * heat, 4);

    g.setColour (colour.withAlpha (active ? 0.34f : 0.16f));
    g.drawEllipse (circle, 1.0f);
}

} // namespace collapse::gui
