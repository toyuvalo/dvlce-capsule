// Dvlce Capsule - editor: model slots with file loading, pattern and proximity controls.
// Copyright 2026 Yuval Olsha.  PolyForm Noncommercial 1.0.0 (see LICENSE).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "ui/QuantumHeader.h"

class DvlceCapsuleEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit DvlceCapsuleEditor(DvlceCapsuleProcessor&);
    ~DvlceCapsuleEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow};
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    struct Choice {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attach;
    };
    struct Toggle {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach;
    };

    void timerCallback() override;
    void setupKnob(Knob&, const char* id, const char* text);
    void setupChoice(Choice&, const char* id, const char* text);
    void setupToggle(Toggle&, const char* id, const char* text);
    void chooseFile(int slot);

    DvlceCapsuleProcessor& proc;
    dvlce::ui::DvlceLookAndFeel look{dvlce::ui::ThemeId::Quantum};
    dvlce::ui::QuantumHeader header;
    Knob pattern, patternRear, sourceDist, virtualDist, proxCap, offAxis, output;
    Choice hardware, voicing, mode;
    Toggle flip, drive;
    juce::TextButton loadHardware{"Load file..."}, loadVoicing{"Load file..."};
    juce::Label status, fileNames;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String loadError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DvlceCapsuleEditor)
};
