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

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace collapse
{

CollapseProcessor::CollapseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "COLLAPSE", createParameterLayout())
{
    handles.attach (apvts);
}

bool CollapseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void CollapseProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const auto numChannels = juce::jmax (1, juce::jmin (getTotalNumInputChannels(),
                                                        getTotalNumOutputChannels()));

    engine.prepare (sampleRate, numChannels, kControlBlock);

    // Latency depends only on the sample rate, so it is settled here on the message thread
    // and never changes while audio is running - no AsyncUpdater needed.
    const auto latency = engine.getLatencySamples();
    setLatencySamples (latency);

    dryDelay.prepare (numChannels, latency);

    // Sized once, here, where allocating is allowed. A host that hands over a larger block
    // than it promised is handled by splitting the block in processBlock rather than by
    // resizing this - resizing would be an allocation on the audio thread.
    dryBuffer.setSize (numChannels, juce::jmax (1024, maximumExpectedSamplesPerBlock),
                       false, true, true);

    // Voicing controls ramp over 50 ms, slow enough that no single block contains a step
    // and fast enough to feel immediate. Gains and the crossover get 20 ms: the crossover
    // because a slow sweep through it is a thing people do on purpose, and a 50 ms ramp
    // makes that sweep lag the knob.
    for (auto* s : { &smDrive, &smWeight, &smTone, &smGrit })
        s->reset (sampleRate, 0.05);

    for (auto* s : { &smBlend, &smSplit, &smLevel, &smBypass })
        s->reset (sampleRate, 0.02);

    reset();
}

/** Hosts call this on transport stop and before starting playback. Without it, filter and
    envelope state from the last thing that played leaks into the next one - which shows
    up as a burst of decaying rubbish over what should be silence. */
void CollapseProcessor::reset()
{
    smDrive .setCurrentAndTargetValue (handles.drive->load()  * 0.01f);
    smBlend .setCurrentAndTargetValue (handles.blend->load()  * 0.01f);
    smSplit .setCurrentAndTargetValue (handles.split->load());
    smWeight.setCurrentAndTargetValue (handles.weight->load() * 0.01f);
    smTone  .setCurrentAndTargetValue (handles.tone->load()   * 0.01f);
    smGrit  .setCurrentAndTargetValue (handles.grit->load()   * 0.01f);
    smLevel .setCurrentAndTargetValue (juce::Decibels::decibelsToGain (handles.level->load()));
    smBypass.setCurrentAndTargetValue (handles.bypass->load() > 0.5f ? 1.0f : 0.0f);

    dryDelay.reset();
    engine.reset();
    dryBuffer.clear();

    collapseForUi .store (0.0f, std::memory_order_relaxed);
    levelForUi    .store (0.0f, std::memory_order_relaxed);
    sagForUi      .store (0.0f, std::memory_order_relaxed);
    coherenceForUi.store (1.0f, std::memory_order_relaxed);
}

void CollapseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples  = buffer.getNumSamples();
    const auto numOutputs  = getTotalNumOutputChannels();
    const auto numChannels = juce::jmin (getTotalNumInputChannels(), numOutputs);

    for (int ch = numChannels; ch < numOutputs; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels <= 0 || numSamples <= 0)
        return;

    // Reading the target is the half of parameter handling that is easy to forget; the
    // smoother is advanced below, per control block for the voicing and per sample for the
    // gains. Miss this and every knob is silently inert while the plug-in still looks like
    // it works.
    smDrive .setTargetValue (handles.drive->load()  * 0.01f);
    smBlend .setTargetValue (handles.blend->load()  * 0.01f);
    smSplit .setTargetValue (handles.split->load());
    smWeight.setTargetValue (handles.weight->load() * 0.01f);
    smTone  .setTargetValue (handles.tone->load()   * 0.01f);
    smGrit  .setTargetValue (handles.grit->load()   * 0.01f);
    smLevel .setTargetValue (juce::Decibels::decibelsToGain (handles.level->load()));
    smBypass.setTargetValue (handles.bypass->load() > 0.5f ? 1.0f : 0.0f);

    const auto stateIndex   = (int) handles.state->load();
    const auto cabinetIndex = (int) handles.cabinet->load();

    // Split into segments the dry buffer can already hold. A host that hands over a bigger
    // block than it promised then costs an extra loop iteration instead of an allocation
    // on the audio thread.
    const auto capacity = juce::jmax (1, juce::jmin (dryBuffer.getNumSamples(), numSamples));

    for (int offset = 0; offset < numSamples; offset += capacity)
    {
        const auto segment = juce::jmin (capacity, numSamples - offset);

        // The bypassed path is delayed by exactly the latency the oversampler reports, so
        // bypassing does not shift the audio against every other track in the session.
        {
            const float* inputs[2] = { buffer.getReadPointer (0, offset),
                                       numChannels > 1 ? buffer.getReadPointer (1, offset)
                                                       : nullptr };
            float* delayed[2]      = { dryBuffer.getWritePointer (0),
                                       numChannels > 1 ? dryBuffer.getWritePointer (1)
                                                       : nullptr };
            dryDelay.process (inputs, delayed, segment);
        }

        // The wet path is always computed, even while bypassed, so that unbypassing does
        // not click on stale filter state and so the crossfade has something to fade to.
        for (int start = 0; start < segment; start += kControlBlock)
        {
            const auto len = juce::jmin (kControlBlock, segment - start);

            dsp::CollapseEngine::Controls controls;
            controls.drive   = smDrive .skip (len);
            controls.blend   = smBlend .skip (len);
            controls.splitHz = smSplit .skip (len);
            controls.weight  = smWeight.skip (len);
            controls.tone    = smTone  .skip (len);
            controls.grit    = smGrit  .skip (len);
            controls.state   = stateIndex;
            controls.cabinet = cabinetIndex;

            engine.setControls (controls);

            float* io[2] = { buffer.getWritePointer (0, offset + start),
                             numChannels > 1 ? buffer.getWritePointer (1, offset + start)
                                             : nullptr };
            engine.process (io, len);
        }

        for (int n = 0; n < segment; ++n)
        {
            const auto bypassAmount = smBypass.getNextValue();
            const auto level        = smLevel.getNextValue();

            const auto wetGain = (1.0f - bypassAmount) * level;
            const auto dryGain = bypassAmount;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* wet = buffer.getWritePointer (ch);
                wet[offset + n] = wet[offset + n] * wetGain
                                      + dryBuffer.getSample (ch, n) * dryGain;
            }
        }
    }

    collapseForUi .store (engine.getCollapse(),  std::memory_order_relaxed);
    levelForUi    .store (engine.getLevel(),     std::memory_order_relaxed);
    sagForUi      .store (engine.getSag(),       std::memory_order_relaxed);
    coherenceForUi.store (engine.getCoherence(), std::memory_order_relaxed);
}

int CollapseProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

void CollapseProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;
    applyPreset (apvts, index);
}

const juce::String CollapseProcessor::getProgramName (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return {};

    return getFactoryPresets()[(std::size_t) index].name;
}

juce::AudioProcessorEditor* CollapseProcessor::createEditor()
{
    return new CollapseEditor (*this);
}

void CollapseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CollapseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace collapse

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new collapse::CollapseProcessor();
}
