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

#include <juce_audio_processors/juce_audio_processors.h>

namespace collapse
{

/** Every parameter ID in one place, grouped the way the panel is: what belongs to the
    coherent band, what belongs to the collapsed one, and the three that belong to both.

    `cabinet` keeps its old ID even though it now only owns the collapsed path's speaker.
    It is the one every session saved before the split carries, and renaming it would
    silently reset that speaker to its default in every one of them. */
namespace pid
{
    // The band below Split: never driven, its own EQ, its own speaker.
    inline constexpr auto lowSub  = "lowsub";
    inline constexpr auto lowBody = "lowbody";
    inline constexpr auto lowTrim = "lowtrim";
    inline constexpr auto lowCab  = "lowcab";

    // The band above it, and what happens to it.
    inline constexpr auto drive   = "drive";
    inline constexpr auto blend   = "blend";
    inline constexpr auto tone    = "tone";
    inline constexpr auto grit    = "grit";
    inline constexpr auto state   = "state";
    inline constexpr auto cabinet = "cabinet";

    // Shared: where the two bands meet, and what happens after they are back together.
    inline constexpr auto split   = "split";
    inline constexpr auto weight  = "weight";
    inline constexpr auto level   = "level";
    inline constexpr auto bypass  = "bypass";
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

juce::StringArray stateNames();
juce::StringArray cabinetNames();

/** Raw pointers resolved once at construction, so reading a parameter on the audio thread
    is one relaxed atomic load rather than a string lookup in a hash map. */
struct ParameterHandles
{
    void attach (juce::AudioProcessorValueTreeState& state);

    std::atomic<float>* lowSub  = nullptr;
    std::atomic<float>* lowBody = nullptr;
    std::atomic<float>* lowTrim = nullptr;
    std::atomic<float>* lowCab  = nullptr;

    std::atomic<float>* drive   = nullptr;
    std::atomic<float>* blend   = nullptr;
    std::atomic<float>* tone    = nullptr;
    std::atomic<float>* grit    = nullptr;
    std::atomic<float>* state   = nullptr;
    std::atomic<float>* cabinet = nullptr;

    std::atomic<float>* split   = nullptr;
    std::atomic<float>* weight  = nullptr;
    std::atomic<float>* level   = nullptr;
    std::atomic<float>* bypass  = nullptr;

    juce::AudioParameterBool* bypassParameter = nullptr;
};

/** The crossover's range, shared by the parameter and by anything that has to map a
    normalised position back onto it. */
inline constexpr float kSplitMinHz = 20.0f;
inline constexpr float kSplitMaxHz = 800.0f;

} // namespace collapse
