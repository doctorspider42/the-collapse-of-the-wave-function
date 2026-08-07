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

#include "Parameters.h"
#include "dsp/CollapseEngine.h"
#include "dsp/DelayLine.h"

namespace collapse
{

class CollapseProcessor final : public juce::AudioProcessor
{
public:
    CollapseProcessor();
    ~CollapseProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioParameterBool* getBypassParameter() const override
    {
        return handles.bypassParameter;
    }

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }

    /** Values the editor polls at frame rate. Written by the audio thread as plain relaxed
        stores - no locks, no queues, and nothing the audio thread has to wait for. */
    float getCollapse()  const noexcept { return collapseForUi .load (std::memory_order_relaxed); }
    float getLevel()     const noexcept { return levelForUi    .load (std::memory_order_relaxed); }
    float getSag()       const noexcept { return sagForUi      .load (std::memory_order_relaxed); }
    float getCoherence() const noexcept { return coherenceForUi.load (std::memory_order_relaxed); }

private:
    /** 0.7 ms at 48 kHz: far below anything audible as a step, and 32x cheaper than
        rebuilding filter coefficients per sample. */
    static constexpr int kControlBlock = 32;

    juce::AudioProcessorValueTreeState apvts;
    ParameterHandles handles;

    dsp::CollapseEngine engine;
    dsp::IntegerDelay   dryDelay;

    juce::AudioBuffer<float> dryBuffer;

    juce::SmoothedValue<float> smDrive, smBlend, smSplit, smWeight, smTone, smGrit;
    juce::SmoothedValue<float> smLowSub, smLowBody, smLowTrim;
    juce::SmoothedValue<float> smLevel, smBypass;

    int currentProgram = 0;

    std::atomic<float> collapseForUi  { 0.0f };
    std::atomic<float> levelForUi     { 0.0f };
    std::atomic<float> sagForUi       { 0.0f };
    std::atomic<float> coherenceForUi { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollapseProcessor)
};

} // namespace collapse
