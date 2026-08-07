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

#include "PluginEditor.h"
#include "Presets.h"
#include "dsp/Shapers.h"

namespace collapse
{

namespace
{
    constexpr float kMargin      = 22.0f;
    constexpr float kHeaderY     = 14.0f,  kHeaderH   = 34.0f;
    constexpr float kWaveY       = 58.0f,  kWaveH     = 192.0f;
    constexpr float kSelectorY   = 262.0f, kSelectorH = 84.0f;
    constexpr float kRowY        = 358.0f, kRowH      = 168.0f;
    constexpr float kFooterY     = 536.0f, kFooterH   = 22.0f;

    constexpr float kSectionTitleH = 30.0f;
    constexpr float kKnobWidth  = 88.0f;
    constexpr float kKnobHeight = 104.0f;
    constexpr float kKnobDial   = 72.0f;
    constexpr float kGap        = 12.0f;

    juce::String versionText()
    {
       #ifdef COLLAPSE_BUILD_ID
        return juce::String ("V") + JucePlugin_VersionString + "  " + COLLAPSE_BUILD_ID;
       #else
        return juce::String ("V") + JucePlugin_VersionString;
       #endif
    }
}

// ---------------------------------------------------------------------------------
//  Preset bar
// ---------------------------------------------------------------------------------

CollapseEditor::PresetBar::PresetBar (CollapseProcessor& p)
    : processor (p)
{
    setTooltip ("Presets. The arrows step through them; the name opens the list.");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void CollapseEditor::PresetBar::refresh()
{
    repaint();
}

juce::Rectangle<float> CollapseEditor::PresetBar::arrowBounds (bool previous) const
{
    auto area = getLocalBounds().toFloat();
    const auto w = area.getHeight();

    return previous ? area.removeFromLeft (w) : area.removeFromRight (w);
}

CollapseEditor::PresetBar::Zone
CollapseEditor::PresetBar::zoneAt (juce::Point<float> position) const
{
    if (arrowBounds (true) .contains (position)) return Zone::previous;
    if (arrowBounds (false).contains (position)) return Zone::next;
    if (getLocalBounds().toFloat().contains (position)) return Zone::name;

    return Zone::none;
}

void CollapseEditor::PresetBar::step (int delta)
{
    const auto count = processor.getNumPrograms();

    if (count <= 0)
        return;

    // Wrapping rather than clamping: a seven-item list is quicker to walk round than to
    // walk back through, and there is no meaning to either end of it.
    const auto next = (processor.getCurrentProgram() + delta + count) % count;
    processor.setCurrentProgram (next);
    repaint();
}

void CollapseEditor::PresetBar::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case Zone::previous: step (-1); return;
        case Zone::next:     step (+1); return;

        case Zone::name:
        {
            juce::PopupMenu menu;
            const auto current = processor.getCurrentProgram();

            for (int i = 0; i < processor.getNumPrograms(); ++i)
                menu.addItem (i + 1, processor.getProgramName (i), true, i == current);

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                [safe = juce::Component::SafePointer<PresetBar> (this)] (int result)
                                {
                                    if (safe != nullptr && result > 0)
                                    {
                                        safe->processor.setCurrentProgram (result - 1);
                                        safe->repaint();
                                    }
                                });
            return;
        }

        case Zone::none:
        default:
            return;
    }
}

void CollapseEditor::PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const auto zone = zoneAt (e.position);

    if (zone == hovered)
        return;

    hovered = zone;
    repaint();
}

void CollapseEditor::PresetBar::mouseExit (const juce::MouseEvent&)
{
    if (hovered == Zone::none)
        return;

    hovered = Zone::none;
    repaint();
}

void CollapseEditor::PresetBar::paint (juce::Graphics& g)
{
    const auto accent = palette != nullptr ? palette->accent : skin::colour::cold;
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto radius = bounds.getHeight() * 0.5f;

    juce::ColourGradient fill (juce::Colour (0xff0b1220), bounds.getCentreX(), bounds.getY(),
                               juce::Colour (0xff05070e), bounds.getCentreX(),
                               bounds.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (bounds, radius);

    g.setColour (accent.withAlpha (hovered == Zone::none ? 0.16f : 0.30f));
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    // Two triangles, drawn rather than typed: an ASCII "<" sits on the baseline and looks
    // wrong next to centred text, and the pointing characters are not in every font.
    const auto arrow = [&g, accent] (juce::Rectangle<float> area, bool pointsLeft, bool lit)
    {
        const auto c = area.getCentre();
        const auto w = area.getHeight() * 0.15f;
        const auto h = area.getHeight() * 0.20f;
        const auto sign = pointsLeft ? -1.0f : 1.0f;

        juce::Path p;
        p.startNewSubPath (c.x - sign * w, c.y - h);
        p.lineTo (c.x + sign * w, c.y);
        p.lineTo (c.x - sign * w, c.y + h);

        g.setColour (lit ? accent.brighter (0.4f) : skin::colour::textDim);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    };

    arrow (arrowBounds (true),  true,  hovered == Zone::previous);
    arrow (arrowBounds (false), false, hovered == Zone::next);

    auto nameArea = bounds.reduced (bounds.getHeight(), 0.0f);
    skin::drawTracked (g, processor.getProgramName (processor.getCurrentProgram()).toUpperCase(),
                       skin::labelFont (9.0f, true), nameArea, juce::Justification::centred,
                       hovered == Zone::name ? accent.brighter (0.3f) : skin::colour::text,
                       1.5f);
}

// ---------------------------------------------------------------------------------
//  Editor
// ---------------------------------------------------------------------------------

CollapseEditor::CollapseEditor (CollapseProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&panelLookAndFeel);

    addAndMakeVisible (content);
    content.addAndMakeVisible (backdrop);
    content.addAndMakeVisible (wave);

    struct KnobSpec { const char* id; const char* caption; const char* tip; bool bipolar; };

    static const KnobSpec specs[kNumKnobs] =
    {
        { pid::drive,  "Drive",
          "How hard the gain stages are hit. Only the band above Split ever reaches them.",
          false },
        { pid::blend,  "Blend",
          "Coherent to collapsed. The clean path is delay-matched, so this is a crossfade "
          "rather than a comb filter.", false },
        { pid::split,  "Split",
          "The crossover. Everything below it stays clean no matter what Blend and Drive "
          "are doing.", false },
        { pid::weight, "Weight",
          "The output low end: subsonic corner and a shelf at 92 Hz.", true },
        { pid::tone,   "Tone",
          "A tilt from dark to bright across the collapsed path.", true },
        { pid::grit,   "Grit",
          "Pick attack and fret noise, around 2.3 kHz, on the collapsed path.", false },
        { pid::level,  "Level",
          "Output trim, -18 to +18 dB.", true },
    };

    for (int i = 0; i < kNumKnobs; ++i)
    {
        knobs[i] = std::make_unique<gui::CollapseKnob> (processor.getState(), specs[i].id,
                                                        specs[i].caption, specs[i].tip,
                                                        specs[i].bipolar);
        knobs[i]->setKnobDiameter (kKnobDial);
        knobs[i]->setPalette (&palette);
        content.addAndMakeVisible (*knobs[i]);
    }

    stateSelector = std::make_unique<gui::StateSelector> (
        processor.getState(), pid::state, stateNames(),
        "The voicing of the drive path: four gain structures, from a valve preamp to a fuzz.");

    juce::StringArray stateCaptions;

    for (int i = 0; i < (int) dsp::State::numStates; ++i)
        stateCaptions.add (juce::String (dsp::stateSpec (i).blurb).toUpperCase());

    stateSelector->setCaptions (stateCaptions);
    stateSelector->setPalette (&palette);
    content.addAndMakeVisible (*stateSelector);

    cabinetSelector = std::make_unique<gui::StateSelector> (
        processor.getState(), pid::cabinet, cabinetNames(),
        "Speaker and cabinet on the collapsed path only - the clean side keeps its bottom.");
    cabinetSelector->setCaptions ({ "no speaker - straight out of the stages",
                                    "warmer cone, lower port, gives way earlier",
                                    "stiffer cone, higher port, stays tight" });
    cabinetSelector->setPalette (&palette);
    content.addAndMakeVisible (*cabinetSelector);

    power = std::make_unique<gui::PowerToggle> (processor.getState(), pid::bypass);
    power->setPalette (&palette);
    content.addAndMakeVisible (*power);

    presetBar = std::make_unique<PresetBar> (processor);
    presetBar->setPalette (&palette);
    content.addAndMakeVisible (*presetBar);

    layOutContent();
    pushPalette (true);

    // Before the first timer tick, so an editor that is opened - or snapshotted offline,
    // where no timer ever fires - already shows the state of the thing rather than a flat
    // line waiting for a frame.
    timerCallback();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kLogicalWidth / (double) kLogicalHeight);
    setResizeLimits (kLogicalWidth * 3 / 4, kLogicalHeight * 3 / 4,
                     kLogicalWidth * 2,     kLogicalHeight * 2);
    setSize (kLogicalWidth, kLogicalHeight);

    startTimerHz (30);
}

CollapseEditor::~CollapseEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void CollapseEditor::layOutContent()
{
    content.setSize (kLogicalWidth, kLogicalHeight);
    backdrop.setBounds (content.getLocalBounds());

    const auto left  = kMargin;
    const auto width = (float) kLogicalWidth - 2.0f * kMargin;

    // ---- header ------------------------------------------------------------
    {
        const auto powerSize = kHeaderH;
        power->setBounds (juce::Rectangle<float> (left + width - powerSize, kHeaderY,
                                                  powerSize, powerSize).toNearestInt());

        constexpr float presetWidth = 210.0f;
        presetBar->setBounds (juce::Rectangle<float> (left + width - powerSize - kGap
                                                          - presetWidth,
                                                      kHeaderY + 4.0f, presetWidth,
                                                      kHeaderH - 8.0f).toNearestInt());
    }

    wave.setBounds (juce::Rectangle<float> (left, kWaveY, width, kWaveH).toNearestInt());

    // ---- the two selectors -------------------------------------------------
    // State gets the wider box: four options with longer names, and it is the decision
    // that changes the most.
    const auto stateWidth = 500.0f;
    const auto cabinetWidth = width - stateWidth - kGap;

    std::vector<gui::Section> sections;
    sections.push_back ({ { left, kSelectorY, stateWidth, kSelectorH }, "STATE" });
    sections.push_back ({ { left + stateWidth + kGap, kSelectorY, cabinetWidth, kSelectorH },
                          "CABINET" });

    const auto pillsY = kSelectorY + kSectionTitleH;
    const auto pillsH = 48.0f;

    stateSelector->setBounds (juce::Rectangle<float> (left + 16.0f, pillsY,
                                                      stateWidth - 32.0f, pillsH)
                                  .toNearestInt());
    cabinetSelector->setBounds (juce::Rectangle<float> (left + stateWidth + kGap + 16.0f,
                                                        pillsY, cabinetWidth - 32.0f, pillsH)
                                    .toNearestInt());

    // ---- the knob row ------------------------------------------------------
    // Three pairs and the output trim. The order is the order of the signal path, left to
    // right, which is the only ordering anybody has to be told once.
    const auto outputWidth = 140.0f;
    const auto pairWidth = (width - outputWidth - 3.0f * kGap) / 3.0f;

    struct Group { const char* title; int first, count; };
    static const Group groups[] =
    {
        { "SUPERPOSITION", 2, 2 },   // Split, Weight
        { "COLLAPSE",      0, 2 },   // Drive, Blend
        { "OBSERVATION",   4, 2 },   // Tone, Grit
        { "OUTPUT",        6, 1 },   // Level
    };

    auto x = left;

    for (const auto& group : groups)
    {
        const auto w = group.count > 1 ? pairWidth : outputWidth;
        sections.push_back ({ { x, kRowY, w, kRowH }, group.title });

        const auto span = (float) group.count * kKnobWidth
                            + (float) (group.count - 1) * 16.0f;
        auto knobX = x + (w - span) * 0.5f;
        const auto knobY = kRowY + kSectionTitleH + 6.0f;

        for (int i = 0; i < group.count; ++i)
        {
            knobs[group.first + i]->setBounds (
                juce::Rectangle<float> (knobX, knobY, kKnobWidth, kKnobHeight).toNearestInt());
            knobX += kKnobWidth + 16.0f;
        }

        x += w + kGap;
    }

    backdrop.setSections (std::move (sections));
    backdrop.setTitle ("THE COLLAPSE OF THE WAVE FUNCTION");
    backdrop.setFooter (versionText(),
                        "CABINET IMPULSES: JESTER'S BRUTAL PACK 1.0, CC0");
}

void CollapseEditor::pushPalette (bool force)
{
    const auto stateIndex = (int) processor.getState()
                                     .getRawParameterValue (pid::state)->load();

    palette = skin::paletteFor (smoothedHeat, stateIndex);

    // The backdrop is a cached image, so it is only rebuilt when the hue actually changes
    // - which is on a click, not on a frame. Heat alone must never invalidate it.
    if (force || stateIndex != pushedState)
    {
        pushedState = stateIndex;
        panelLookAndFeel.setAccent (palette.accent);
        backdrop.setPalette (palette);
        stateSelector->repaint();
        cabinetSelector->repaint();
        presetBar->repaint();
        power->repaint();

        for (auto& k : knobs)
            k->repaint();

        pushedHeat = smoothedHeat;
        return;
    }

    // Heat only shifts brightness, so the widgets that show it are repainted when it has
    // moved enough to see - in practice, while somebody is dragging Drive.
    if (force || std::abs (smoothedHeat - pushedHeat) > 0.03f)
    {
        pushedHeat = smoothedHeat;

        for (auto& k : knobs)
            k->repaint();

        stateSelector->repaint();
        power->repaint();
    }
}

void CollapseEditor::timerCallback()
{
    auto& state = processor.getState();

    const auto drive = state.getRawParameterValue (pid::drive)->load() * 0.01f;
    const auto blend = state.getRawParameterValue (pid::blend)->load() * 0.01f;
    const auto active = power->isActive();

    smoothedHeat += 0.20f * (drive - smoothedHeat);
    pushPalette (false);

    // Bypassed, the picture goes still and coherent rather than freezing mid-collapse:
    // it is showing what the plug-in is doing, and it is not doing anything.
    wave.setPalette (palette);
    wave.setEnergy (active ? processor.getLevel() : 0.0f,
                    active ? processor.getCollapse() : 0.0f,
                    active ? blend : 0.0f,
                    active ? processor.getCoherence() : 1.0f);
}

void CollapseEditor::paint (juce::Graphics& g)
{
    // Only ever seen behind a resize, and for one frame: the backdrop is opaque and covers
    // every pixel of the content.
    g.fillAll (skin::colour::voidDeep);
}

void CollapseEditor::resized()
{
    const auto scale = (double) getWidth() / (double) kLogicalWidth;
    content.setTransform (juce::AffineTransform::scale ((float) scale));
    content.setBounds (0, 0, kLogicalWidth, kLogicalHeight);
}

} // namespace collapse
