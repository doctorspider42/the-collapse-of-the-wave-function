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

#include "Presets.h"
#include "Parameters.h"

namespace collapse
{

/*  Every preset sets every parameter, so recalling one is never a partial state sitting
    on top of whatever happened to be there before.

    The number that varies most between them is Split, and that is the point: a fuzz that
    would be unusable full-range is perfectly playable once the bottom two octaves have
    been taken out of its way.

    The second thing that varies is whether the two speakers agree. Both set the same is
    one amp with one cabinet; the coherent band on Off while the collapsed one has a
    speaker is a DI and an amp on the same instrument, which is the rig half these presets
    are named after.                                                                   */

const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets =
    {
        // The one most sessions want, and now literally what it says: the low band goes
        // to the desk untouched, a little Phase growl sits on top of it, and only the
        // growl sees a speaker.
        { "DI and Grind", {
            { pid::lowSub, 50 }, { pid::lowBody, 56 }, { pid::lowTrim, 0 },
            { pid::lowCab, 0 },
            { pid::drive, 34 }, { pid::blend, 52 }, { pid::tone, 52 },
            { pid::grit, 48 }, { pid::state, 1 }, { pid::cabinet, 1 },
            { pid::split, 110 }, { pid::weight, 56 }, { pid::level, 0 },
            { pid::bypass, 0 } } },

        // Barely there. Drift at low drive is a valve preamp: it rounds peaks, adds a
        // second harmonic, and does not otherwise announce itself. One amp, one cabinet,
        // both bands through it.
        { "Valve Warmth", {
            { pid::lowSub, 55 }, { pid::lowBody, 50 }, { pid::lowTrim, 0 },
            { pid::lowCab, 1 },
            { pid::drive, 22 }, { pid::blend, 38 }, { pid::tone, 44 },
            { pid::grit, 30 }, { pid::state, 0 }, { pid::cabinet, 1 },
            { pid::split, 60 }, { pid::weight, 60 }, { pid::level, 0 },
            { pid::bypass, 0 } } },

        // Flatwounds and a thumb. No speaker anywhere - the point is weight and a hint of
        // compression, not a cabinet.
        { "Thumb and Flats", {
            { pid::lowSub, 64 }, { pid::lowBody, 58 }, { pid::lowTrim, 1 },
            { pid::lowCab, 0 },
            { pid::drive, 18 }, { pid::blend, 30 }, { pid::tone, 26 },
            { pid::grit, 12 }, { pid::state, 0 }, { pid::cabinet, 0 },
            { pid::split, 45 }, { pid::weight, 78 }, { pid::level, 0 },
            { pid::bypass, 0 } } },

        // Fast lines with a pick. Decay is mid-forward and tight, Grit is up where the
        // pick lives, and the bottom stays on the desk so the speaker never smears it.
        { "Pick Attack", {
            { pid::lowSub, 46 }, { pid::lowBody, 54 }, { pid::lowTrim, 0 },
            { pid::lowCab, 0 },
            { pid::drive, 58 }, { pid::blend, 62 }, { pid::tone, 64 },
            { pid::grit, 72 }, { pid::state, 2 }, { pid::cabinet, 2 },
            { pid::split, 165 }, { pid::weight, 50 }, { pid::level, -1 },
            { pid::bypass, 0 } } },

        // The reason the plug-in has a Split control. Full Collapse fuzz above 190 Hz
        // through Cone, an untouched DI below it, and the two together still read as one
        // instrument.
        { "Observed", {
            { pid::lowSub, 58 }, { pid::lowBody, 56 }, { pid::lowTrim, 1 },
            { pid::lowCab, 0 },
            { pid::drive, 74 }, { pid::blend, 76 }, { pid::tone, 48 },
            { pid::grit, 55 }, { pid::state, 3 }, { pid::cabinet, 1 },
            { pid::split, 190 }, { pid::weight, 66 }, { pid::level, -2 },
            { pid::bypass, 0 } } },

        // Slow, heavy, and deliberately enormous. Two cabinets, both Steel: this one is a
        // rig in a room rather than a desk, and the low end is supposed to move air.
        { "Event Horizon", {
            { pid::lowSub, 70 }, { pid::lowBody, 60 }, { pid::lowTrim, 2 },
            { pid::lowCab, 2 },
            { pid::drive, 88 }, { pid::blend, 68 }, { pid::tone, 34 },
            { pid::grit, 40 }, { pid::state, 3 }, { pid::cabinet, 2 },
            { pid::split, 92 }, { pid::weight, 84 }, { pid::level, -3 },
            { pid::bypass, 0 } } },

        // Everything through the drive path, nothing held back - the setting the split
        // exists to make unnecessary, kept because sometimes it is the sound. With Split
        // at the bottom there is no coherent band left, so its own controls do nothing.
        { "Total Collapse", {
            { pid::lowSub, 50 }, { pid::lowBody, 50 }, { pid::lowTrim, 0 },
            { pid::lowCab, 1 },
            { pid::drive, 92 }, { pid::blend, 100 }, { pid::tone, 58 },
            { pid::grit, 62 }, { pid::state, 2 }, { pid::cabinet, 1 },
            { pid::split, 20 }, { pid::weight, 62 }, { pid::level, -4 },
            { pid::bypass, 0 } } },
    };

    return presets;
}

void applyPreset (juce::AudioProcessorValueTreeState& state, int index)
{
    const auto& presets = getFactoryPresets();

    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    for (const auto& v : presets[(std::size_t) index].values)
    {
        if (auto* p = state.getParameter (v.id))
            p->setValueNotifyingHost (p->convertTo0to1 (v.value));
    }
}

} // namespace collapse
