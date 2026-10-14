// Dvlce Capsule - JUCE processor around dvlce::capsule::Engine.
// Copyright 2026 Yuval Olsha.  PolyForm Noncommercial 1.0.0 (see LICENSE).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

#include "dvlce/capsule.h"

class DvlceCapsuleProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    // Choice lists of the two model slots.  The last entry is "User file".
    static const juce::StringArray& hardwareChoices();
    static const juce::StringArray& voicingChoices();

    DvlceCapsuleProcessor();
    ~DvlceCapsuleProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Dvlce Capsule"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    // Message thread.  Loads a user model file into a slot (0 hardware, 1 voicing) and selects it.
    // Returns an empty string on success, else the reason it was refused.
    juce::String loadUserModel(int slot, const juce::File& file);
    juce::String statusText() const;      // message thread
    juce::String userFileName(int slot) const;

    juce::AudioProcessorValueTreeState params;

private:
    struct Selection {
        int hardware = 0, voicing = 0;
        juce::String hardwareJson, voicingJson;
        bool operator==(const Selection& o) const {
            return hardware == o.hardware && voicing == o.voicing && hardwareJson == o.hardwareJson && voicingJson == o.voicingJson;
        }
        bool operator!=(const Selection& o) const { return !(*this == o); }
    };
    class Builder;

    void timerCallback() override;
    Selection currentSelection() const;
    Selection installedSelection() const;
    // Builds a prepared engine for a selection (heavy; any thread but the audio thread).
    std::unique_ptr<dvlce::capsule::Engine> build(const Selection& sel, double fs, juce::String& error) const;
    void install(std::unique_ptr<dvlce::capsule::Engine> e, const Selection& sel, const juce::String& error);
    dvlce::capsule::Params currentParams() const;

    std::atomic<float>* raw[12] = {};
    juce::SpinLock engineLock;
    std::unique_ptr<dvlce::capsule::Engine> engine;      // swapped under engineLock
    std::unique_ptr<Builder> builder;
    std::atomic<double> sampleRate{48000.0};
    std::atomic<bool> latencyDirty{false};
    std::atomic<int> engineLatency{0};

    mutable juce::CriticalSection selLock;   // guards everything below
    Selection installed;                     // what the running engine was built from
    juce::String userJson[2], userName[2];
    juce::String lastError, modelName;

    juce::AudioBuffer<double> scratch;       // front, rear, out0, out1

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DvlceCapsuleProcessor)
};
