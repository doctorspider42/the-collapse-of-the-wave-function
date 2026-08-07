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
    constexpr float kWaveY       = 58.0f,  kWaveH     = 164.0f;

    // The two paths, side by side. The collapsed column is wider because it has four knobs
    // and two selectors to the coherent one's three and one - which is the panel saying
    // what the signal path says: one of these is a channel, the other is an amplifier.
    constexpr float kColumnY     = 234.0f, kColumnH   = 312.0f;
    constexpr float kCoherentW   = 336.0f;
    constexpr float kRowAY       = 272.0f;   // State, and the band readout opposite it
    constexpr float kRowBY       = 344.0f;   // one speaker each, at the same height
    constexpr float kKnobRowY    = 414.0f;
    constexpr float kPillsH      = 48.0f;
    constexpr float kSelectorCaptionH = 14.0f;

    constexpr float kBottomY     = 558.0f, kBottomH   = 168.0f;
    constexpr float kSplitW      = 296.0f;

    constexpr float kSectionTitleH = 30.0f;
    constexpr float kKnobWidth  = 88.0f;
    constexpr float kKnobHeight = 104.0f;
    constexpr float kKnobDial   = 72.0f;
    constexpr float kGap        = 12.0f;
    constexpr float kKnobGap    = 16.0f;

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
//  Band readout
// ---------------------------------------------------------------------------------

void CollapseEditor::BandReadout::setSplitHz (float hz)
{
    if (std::abs (hz - splitHz) < 0.5f)
        return;

    splitHz = hz;
    repaint();
}

void CollapseEditor::BandReadout::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    auto figure = area.removeFromTop (area.getHeight() * 0.60f);

    const auto range = "20 " + skin::u8 ("\xe2\x80\x93") + " "
                         + juce::String (juce::roundToInt (splitHz)) + " Hz";

    g.setFont (skin::displayFont (17.0f));
    g.setColour (skin::colour::cold.withAlpha (0.86f));
    g.drawText (range, figure, juce::Justification::centred, false);

    skin::drawTracked (g, "NEVER DRIVEN, WHATEVER DRIVE IS DOING", skin::labelFont (8.5f),
                       area, juce::Justification::centred,
                       skin::colour::textFaint.withAlpha (0.95f), 0.9f);
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

    struct KnobSpec { const char* id; const char* caption; const char* tip; bool bipolar;
                      bool coherent; };

    static const KnobSpec specs[kNumKnobs] =
    {
        { pid::lowSub,  "Sub",
          "The coherent band's bottom: a shelf at 45 Hz, under the low E. Nothing on this "
          "path is in front of a clipper, so it stays where you put it.", true, true },
        { pid::lowBody, "Body",
          "A bell just under the crossover, where the weight of the note is. It follows "
          "Split, so it is always inside the band it is shaping.", true, true },
        { pid::lowTrim, "Trim",
          "How much clean bass there is, -12 to +12 dB. Blend only ever moves the band "
          "above Split, so this is the control that balances the two paths.", true, true },
        { pid::drive,   "Drive",
          "How hard the gain stages are hit. Only the band above Split ever reaches them.",
          false, false },
        { pid::blend,   "Blend",
          "Coherent to collapsed, inside the band above Split. The clean path is "
          "delay-matched, so this is a crossfade rather than a comb filter.", false, false },
        { pid::tone,    "Tone",
          "A tilt from dark to bright across the collapsed path.", true, false },
        { pid::grit,    "Grit",
          "Pick attack and fret noise, around 2.3 kHz, on the collapsed path.", false, false },
        { pid::split,   "Split",
          "The crossover, and the boundary between the two halves of this panel. "
          "Everything below it stays clean no matter what Blend and Drive are doing.",
          false, false },
        { pid::weight,  "Weight",
          "The output low end, after both speakers: subsonic corner and a shelf at 92 Hz.",
          true, false },
        { pid::level,   "Level",
          "Output trim, -18 to +18 dB.", true, false },
    };

    for (int i = 0; i < kNumKnobs; ++i)
    {
        knobs[i] = std::make_unique<gui::CollapseKnob> (processor.getState(), specs[i].id,
                                                        specs[i].caption, specs[i].tip,
                                                        specs[i].bipolar);
        knobs[i]->setKnobDiameter (kKnobDial);
        knobs[i]->setPalette (specs[i].coherent ? &coldPalette : &palette);
        content.addAndMakeVisible (*knobs[i]);
    }

    content.addAndMakeVisible (bandReadout);

    stateSelector = std::make_unique<gui::StateSelector> (
        processor.getState(), pid::state, stateNames(),
        "The voicing of the drive path: four gain structures, from a valve preamp to a fuzz.");

    juce::StringArray stateCaptions;

    for (int i = 0; i < (int) dsp::State::numStates; ++i)
        stateCaptions.add (juce::String (dsp::stateSpec (i).blurb).toUpperCase());

    stateSelector->setCaptions (stateCaptions);
    stateSelector->setPalette (&palette);
    content.addAndMakeVisible (*stateSelector);

    const juce::StringArray cabinetCaptions { "no speaker - straight out",
                                              "warmer cone, lower port, gives way earlier",
                                              "stiffer cone, higher port, stays tight" };

    cabinetSelector = std::make_unique<gui::StateSelector> (
        processor.getState(), pid::cabinet, cabinetNames(),
        "The speaker the collapsed band goes through. Off leaves the drive path as a "
        "pedal into the desk.");
    cabinetSelector->setCaptions (cabinetCaptions);
    cabinetSelector->setPalette (&palette);
    content.addAndMakeVisible (*cabinetSelector);

    lowCabSelector = std::make_unique<gui::StateSelector> (
        processor.getState(), pid::lowCab, cabinetNames(),
        "The speaker the coherent band goes through. Off is a straight DI under the amp, "
        "which is the rig half the presets are named after.");
    lowCabSelector->setCaptions (cabinetCaptions);
    lowCabSelector->setPalette (&coldPalette);
    content.addAndMakeVisible (*lowCabSelector);

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

    // ---- the two paths -----------------------------------------------------
    // One box each, and the frames are the two colours the rest of the panel already uses
    // for them: cold for the band that is never driven, the State's accent for the one
    // that is. Everything inside a frame belongs to that path and to nothing else.
    const auto collapsedX = left + kCoherentW + kGap;
    const auto collapsedW = width - kCoherentW - kGap;

    std::vector<gui::Section> sections;
    sections.push_back ({ { left, kColumnY, kCoherentW, kColumnH }, "COHERENT",
                          skin::colour::cold });
    sections.push_back ({ { collapsedX, kColumnY, collapsedW, kColumnH }, "COLLAPSED" });

    const auto pillsAndCaption = kPillsH + kSelectorCaptionH;

    bandReadout.setBounds (juce::Rectangle<float> (left + 16.0f, kRowAY,
                                                   kCoherentW - 32.0f, pillsAndCaption)
                               .toNearestInt());

    stateSelector->setBounds (juce::Rectangle<float> (collapsedX + 16.0f, kRowAY,
                                                      collapsedW - 32.0f, pillsAndCaption)
                                  .toNearestInt());

    // The two speakers sit at the same height on purpose: it is the same decision twice,
    // and the panel should not make it look like two different kinds of thing.
    lowCabSelector->setBounds (juce::Rectangle<float> (left + 16.0f, kRowBY,
                                                       kCoherentW - 32.0f, pillsAndCaption)
                                   .toNearestInt());
    cabinetSelector->setBounds (juce::Rectangle<float> (collapsedX + 16.0f, kRowBY,
                                                        collapsedW - 32.0f, pillsAndCaption)
                                    .toNearestInt());

    // ---- every knob, in the box it belongs to -------------------------------
    // Left to right is the order of the signal path within each box, and the boxes
    // themselves are in the order the signal reaches them.
    struct Group { juce::Rectangle<float> bounds; const char* title; int first, count; };

    const Group groups[] =
    {
        { { left, kKnobRowY, kCoherentW, kKnobHeight },       nullptr, lowSub, 3 },
        { { collapsedX, kKnobRowY, collapsedW, kKnobHeight }, nullptr, drive,  4 },
        { { left, kBottomY, kSplitW, kBottomH },         "CROSSOVER", split,  1 },
        { { left + kSplitW + kGap, kBottomY, width - kSplitW - kGap, kBottomH },
          "OUTPUT", weight, 2 },
    };

    for (const auto& group : groups)
    {
        if (group.title != nullptr)
            sections.push_back ({ group.bounds, group.title });

        const auto span = (float) group.count * kKnobWidth
                            + (float) (group.count - 1) * kKnobGap;
        auto knobX = group.bounds.getCentreX() - span * 0.5f;

        // A knob row inside its own section starts under the title; the two path columns
        // have already spent that space on selectors.
        const auto knobY = group.title != nullptr
                             ? group.bounds.getY() + kSectionTitleH + 6.0f
                             : group.bounds.getY();

        for (int i = 0; i < group.count; ++i)
        {
            knobs[group.first + i]->setBounds (
                juce::Rectangle<float> (knobX, knobY, kKnobWidth, kKnobHeight).toNearestInt());
            knobX += kKnobWidth + kKnobGap;
        }
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
        lowCabSelector->repaint();
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

    const auto driveAmount = state.getRawParameterValue (pid::drive)->load() * 0.01f;
    const auto blendAmount = state.getRawParameterValue (pid::blend)->load() * 0.01f;
    const auto active = power->isActive();

    smoothedHeat += 0.20f * (driveAmount - smoothedHeat);
    pushPalette (false);

    // The readout is the coherent column's title bar, in effect, so it follows the
    // crossover rather than waiting for a repaint of the panel.
    bandReadout.setSplitHz (state.getRawParameterValue (pid::split)->load());

    // Bypassed, the picture goes still and coherent rather than freezing mid-collapse:
    // it is showing what the plug-in is doing, and it is not doing anything.
    wave.setPalette (palette);
    wave.setEnergy (active ? processor.getLevel() : 0.0f,
                    active ? processor.getCollapse() : 0.0f,
                    active ? blendAmount : 0.0f,
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
