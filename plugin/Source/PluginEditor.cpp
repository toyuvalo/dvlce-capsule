// Dvlce Capsule - editor.
// Copyright 2026 Yuval Olsha.  PolyForm Noncommercial 1.0.0 (see LICENSE).
#include "PluginEditor.h"

namespace {
const juce::Colour kBg{0xff16181d}, kPanel{0xff20232a}, kText{0xffe6e6e6}, kDim{0xff9aa0a6}, kAccent{0xffd9a441};
}

DvlceCapsuleEditor::DvlceCapsuleEditor(DvlceCapsuleProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    look.setColourScheme(juce::LookAndFeel_V4::getMidnightColourScheme());
    look.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    look.setColour(juce::Slider::thumbColourId, kAccent);
    look.setColour(juce::ToggleButton::tickColourId, kAccent);
    setLookAndFeel(&look);

    setupChoice(hardware, "hardware", "Hardware");
    setupChoice(voicing, "voicing", "Voicing");
    setupChoice(mode, "mode", "Mode");
    setupToggle(flip, "flip", "Flip (mic turned round)");
    setupToggle(drive, "drive", "Drive");
    setupKnob(pattern, "pattern", "Pattern");
    setupKnob(patternRear, "pattern_rear", "Pattern rear");
    setupKnob(sourceDist, "source_cm", "Source distance");
    setupKnob(virtualDist, "virtual_cm", "Virtual distance");
    setupKnob(proxCap, "prox_cap", "Proximity cap");
    setupKnob(offAxis, "off_axis", "Off-axis");
    setupKnob(output, "output", "Output");

    for (auto* b : {&loadHardware, &loadVoicing}) addAndMakeVisible(b);
    loadHardware.onClick = [this] { chooseFile(0); };
    loadVoicing.onClick = [this] { chooseFile(1); };
    loadHardware.setTooltip("Load a capsule model file (.json): your microphone's measured capsule tables");
    loadVoicing.setTooltip("Load a voicing model file (.json)");

    for (auto* l : {&status, &fileNames}) {
        l->setColour(juce::Label::textColourId, kDim);
        l->setFont(juce::FontOptions(13.0f));
        addAndMakeVisible(l);
    }
    setResizable(true, true);
    setResizeLimits(720, 400, 1600, 900);
    setSize(820, 440);
    startTimerHz(5);
    timerCallback();
}

DvlceCapsuleEditor::~DvlceCapsuleEditor() { setLookAndFeel(nullptr); }

void DvlceCapsuleEditor::setupKnob(Knob& k, const char* id, const char* text) {
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 110, 20);
    k.label.setText(text, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(k.slider);
    addAndMakeVisible(k.label);
    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.params, id, k.slider);
    const juce::String unit = proc.params.getParameter(id)->getLabel();   // dB, %
    if (unit.isNotEmpty()) k.slider.setTextValueSuffix(" " + unit);
}

void DvlceCapsuleEditor::setupChoice(Choice& c, const char* id, const char* text) {
    c.box.addItemList(proc.params.getParameter(id)->getAllValueStrings(), 1);
    c.label.setText(text, juce::dontSendNotification);
    c.label.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(c.box);
    addAndMakeVisible(c.label);
    c.attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.params, id, c.box);
}

void DvlceCapsuleEditor::setupToggle(Toggle& t, const char* id, const char* text) {
    t.button.setButtonText(text);
    t.button.setColour(juce::ToggleButton::textColourId, kText);
    addAndMakeVisible(t.button);
    t.attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.params, id, t.button);
}

void DvlceCapsuleEditor::chooseFile(int slot) {
    chooser = std::make_unique<juce::FileChooser>(slot == 0 ? "Load a hardware (capsule) model" : "Load a voicing model",
                                                  juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    juce::Component::SafePointer<DvlceCapsuleEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe, slot](const juce::FileChooser& fc) {
                             if (safe == nullptr) return;
                             const juce::File f = fc.getResult();
                             if (f == juce::File()) return;
                             const juce::String err = safe->proc.loadUserModel(slot, f);
                             safe->loadError = err.isEmpty() ? juce::String() : f.getFileName() + " refused: " + err;
                             safe->timerCallback();
                         });
}

void DvlceCapsuleEditor::timerCallback() {
    status.setText(loadError.isNotEmpty() ? loadError : proc.statusText(), juce::dontSendNotification);
    status.setColour(juce::Label::textColourId, loadError.isNotEmpty() ? juce::Colours::orangered : kDim);
    const juce::String h = proc.userFileName(0), v = proc.userFileName(1);
    fileNames.setText("User files:  hardware " + (h.isEmpty() ? juce::String("-") : h) + "   voicing " + (v.isEmpty() ? juce::String("-") : v),
                      juce::dontSendNotification);
    const bool stereo = mode.box.getSelectedItemIndex() == 1;
    patternRear.slider.setEnabled(stereo);
    patternRear.label.setAlpha(stereo ? 1.0f : 0.4f);
}

void DvlceCapsuleEditor::paint(juce::Graphics& g) {
    g.fillAll(kBg);
    auto r = getLocalBounds().reduced(12);
    auto head = r.removeFromTop(40);
    g.setColour(kText);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("Dvlce Capsule", head.removeFromLeft(240), juce::Justification::centredLeft);
    g.setColour(kDim);
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("v" DVLCE_CAPSULE_VERSION "   dual-capsule virtual microphone   in 1 = front capsule, in 2 = rear capsule",
               head, juce::Justification::centredLeft);
    g.setColour(kPanel);
    g.fillRoundedRectangle(getLocalBounds().reduced(12).withTrimmedTop(48).withHeight(112).toFloat(), 6.0f);
    g.fillRoundedRectangle(getLocalBounds().reduced(12).withTrimmedTop(168).withTrimmedBottom(52).toFloat(), 6.0f);
}

void DvlceCapsuleEditor::resized() {
    auto r = getLocalBounds().reduced(20);
    r.removeFromTop(44);
    auto top = r.removeFromTop(100);
    auto row1 = top.removeFromTop(30);
    auto slotRow = [](juce::Rectangle<int>& row, Choice& c, juce::TextButton* b, int w) {
        auto area = row.removeFromLeft(w);
        c.label.setBounds(area.removeFromLeft(72));
        if (b) b->setBounds(area.removeFromRight(96).reduced(2));
        c.box.setBounds(area.reduced(2));
        row.removeFromLeft(12);
    };
    slotRow(row1, hardware, &loadHardware, (row1.getWidth() - 12) / 2);
    slotRow(row1, voicing, &loadVoicing, row1.getWidth());
    top.removeFromTop(8);
    auto row2 = top.removeFromTop(30);
    slotRow(row2, mode, nullptr, 260);
    flip.button.setBounds(row2.removeFromLeft(220));
    drive.button.setBounds(row2.removeFromLeft(120));
    top.removeFromTop(6);
    fileNames.setBounds(top.removeFromTop(22));

    auto bottom = r.removeFromBottom(40);
    status.setBounds(bottom.withTrimmedTop(16));
    r.removeFromTop(16);
    Knob* knobs[] = {&pattern, &patternRear, &sourceDist, &virtualDist, &proxCap, &offAxis, &output};
    const int w = r.getWidth() / 7;
    for (auto* k : knobs) {
        auto a = r.removeFromLeft(w).reduced(4);
        k->label.setBounds(a.removeFromTop(22));
        k->slider.setBounds(a);
    }
}
