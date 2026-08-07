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

#include "Parameters.h"

namespace collapse
{

juce::StringArray stateNames()
{
    return { "Drift", "Phase", "Decay", "Collapse" };
}

juce::StringArray cabinetNames()
{
    return { "Off", "Cone", "Steel" };
}

namespace
{

juce::String percentText (float value, int)
{
    return juce::String (value, value < 100.0f ? 1 : 0) + " %";
}

std::unique_ptr<juce::AudioParameterFloat> percentParam (const char* id,
                                                         const juce::String& name,
                                                         float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name,
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, defaultValue,
        juce::AudioParameterFloatAttributes().withLabel ("%")
                                             .withStringFromValueFunction (percentText));
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (percentParam (pid::drive,  "Drive",  30.0f));
    layout.add (percentParam (pid::blend,  "Blend",  55.0f));
    layout.add (percentParam (pid::weight, "Weight", 55.0f));
    layout.add (percentParam (pid::tone,   "Tone",   50.0f));
    layout.add (percentParam (pid::grit,   "Grit",   45.0f));

    // Skewed so the middle of the knob lands near 125 Hz. Linear travel would spend two
    // thirds of it above 300 Hz, which is a setting nobody wants, and squeeze the 60 to
    // 150 Hz region that decides whether a bass keeps its bottom into a few degrees.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::split, 1 }, "Split",
        juce::NormalisableRange<float> { kSplitMinHz, kSplitMaxHz, 1.0f, 0.35f }, 110.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value)) + " Hz";
            })));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::level, 1 }, "Level",
        // Symmetric, and that is the reason for it rather than a coincidence: the dial
        // draws its value arc out from the centre of the range, so the centre of the range
        // has to be unity gain or the picture lies about what the knob is doing.
        juce::NormalisableRange<float> { -18.0f, 18.0f, 0.1f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float value, int)
            {
                // The sign is part of the reading on a trim: "+3.0 dB" and "3.0 dB" are
                // the same number, but only one of them says which way it went.
                return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
            })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::state, 1 }, "State", stateNames(), 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::cabinet, 1 }, "Cabinet", cabinetNames(), 1));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { pid::bypass, 1 }, "Bypass", false));

    return layout;
}

void ParameterHandles::attach (juce::AudioProcessorValueTreeState& tree)
{
    drive   = tree.getRawParameterValue (pid::drive);
    blend   = tree.getRawParameterValue (pid::blend);
    split   = tree.getRawParameterValue (pid::split);
    weight  = tree.getRawParameterValue (pid::weight);
    tone    = tree.getRawParameterValue (pid::tone);
    grit    = tree.getRawParameterValue (pid::grit);
    level   = tree.getRawParameterValue (pid::level);
    state   = tree.getRawParameterValue (pid::state);
    cabinet = tree.getRawParameterValue (pid::cabinet);
    bypass  = tree.getRawParameterValue (pid::bypass);

    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (tree.getParameter (pid::bypass));

    jassert (drive != nullptr && blend != nullptr && split != nullptr && weight != nullptr
             && tone != nullptr && grit != nullptr && level != nullptr
             && state != nullptr && cabinet != nullptr && bypassParameter != nullptr);
}

} // namespace collapse
