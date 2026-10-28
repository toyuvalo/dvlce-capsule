// Dvlce Capsule - JUCE processor.  Model changes are built on a background thread and swapped in,
// so the audio thread never designs filters or allocates.
// Copyright 2026 Yuval Olsha.  PolyForm Noncommercial 1.0.0 (see LICENSE).
#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace cap = dvlce::capsule;

namespace {
enum Param { kPattern, kPatternRear, kMode, kFlip, kSourceCm, kVirtualCm, kProxCap, kOffAxis, kDrive, kOutput, kHardware, kVoicing, kNumParams };
const char* const kIds[kNumParams] = {"pattern", "pattern_rear", "mode", "flip", "source_cm", "virtual_cm",
                                      "prox_cap", "off_axis", "drive", "output", "hardware", "voicing"};
const char* const kVoicingBuiltins[] = {"", "generic-neutral", "generic-bright", "generic-warm"};

juce::String patternText(float v, int) {
    struct Named { float p; const char* name; };
    static const Named names[] = {{0.0f, "figure-8"}, {0.25f, "hypercardioid"}, {0.5f, "cardioid"}, {0.7f, "subcardioid"}, {1.0f, "omni"}};
    for (const auto& n : names)
        if (std::abs(v - n.p) < 0.005f) return juce::String(v, 2) + " " + n.name;
    return juce::String(v, 2);
}

juce::String distanceText(float v, int) { return v <= 0.0f ? juce::String("Off") : juce::String(v, 1) + " cm"; }

juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pat = AudioParameterFloatAttributes().withStringFromValueFunction(patternText);
    auto dist = AudioParameterFloatAttributes().withStringFromValueFunction(distanceText);
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{kIds[kPattern], 1}, "Pattern", NormalisableRange<float>(0.f, 1.f, 0.01f), 0.5f, pat));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{kIds[kPatternRear], 1}, "Pattern Rear", NormalisableRange<float>(0.f, 1.f, 0.01f), 0.5f, pat));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{kIds[kMode], 1}, "Mode", StringArray{"Mono", "Stereo 180"}, 0));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{kIds[kFlip], 1}, "Flip", false));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{kIds[kSourceCm], 1}, "Source Distance",
                                                NormalisableRange<float>(0.f, 200.f, 0.1f, 0.4f), 0.f, dist));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{kIds[kVirtualCm], 1}, "Virtual Distance",
                                                NormalisableRange<float>(0.f, 200.f, 0.1f, 0.4f), 0.f, dist));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{kIds[kProxCap], 1}, "Proximity Cap", NormalisableRange<float>(10.f, 40.f, 0.1f), 40.f,
                                                AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{kIds[kOffAxis], 1}, "Off-axis", NormalisableRange<float>(0.f, 100.f, 0.1f), 0.f,
                                                AudioParameterFloatAttributes().withLabel("%")));
    l.add(std::make_unique<AudioParameterBool>(ParameterID{kIds[kDrive], 1}, "Drive", true));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID{kIds[kOutput], 1}, "Output", NormalisableRange<float>(-24.f, 24.f, 0.1f), 0.f,
                                                AudioParameterFloatAttributes().withLabel("dB")));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{kIds[kHardware], 1}, "Hardware", DvlceCapsuleProcessor::hardwareChoices(), 0));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{kIds[kVoicing], 1}, "Voicing", DvlceCapsuleProcessor::voicingChoices(), 0));
    return l;
}
}  // namespace

// ------------------------------------------------------------------ background builder
class DvlceCapsuleProcessor::Builder : public juce::Thread {
public:
    explicit Builder(DvlceCapsuleProcessor& o) : juce::Thread("Dvlce Capsule model builder"), owner(o) { startThread(); }
    ~Builder() override {
        signalThreadShouldExit();
        wake.signal();
        stopThread(5000);
    }
    void request(const Selection& s, double fs) {
        {
            const juce::ScopedLock l(lock);
            pending = s;
            pendingFs = fs;
            hasPending = true;
        }
        wake.signal();
    }
    // Wakes on request() or every 50 ms, and rebuilds whenever the selected models differ from the
    // installed ones.  Needs no message loop, so offline hosts get model changes too.
    void run() override {
        while (!threadShouldExit()) {
            wake.wait(50);
            {
                const Selection now = owner.currentSelection();
                const juce::ScopedLock l(lock);
                if (!hasPending && now != owner.installedSelection() && now != lastBuilt) {
                    pending = now;
                    pendingFs = owner.sampleRate.load();
                    hasPending = true;
                }
            }
            for (;;) {
                Selection s;
                double fs;
                {
                    const juce::ScopedLock l(lock);
                    if (!hasPending) break;
                    s = pending;
                    fs = pendingFs;
                    hasPending = false;
                }
                if (threadShouldExit()) return;
                juce::String error;
                auto e = owner.build(s, fs, error);
                lastBuilt = s;
                if (std::abs(fs - owner.sampleRate.load()) < 1e-6) owner.install(std::move(e), s, error);   // else prepareToPlay changed the rate
            }
        }
    }

private:
    DvlceCapsuleProcessor& owner;
    juce::WaitableEvent wake;
    juce::CriticalSection lock;
    Selection pending, lastBuilt{-1, -1, {}, {}};
    double pendingFs = 48000.0;
    bool hasPending = false;
};

// ------------------------------------------------------------------ processor
const juce::StringArray& DvlceCapsuleProcessor::hardwareChoices() {
    static const juce::StringArray c{"None (ideal capsules)", "Example capsule pair", "User file"};
    return c;
}

const juce::StringArray& DvlceCapsuleProcessor::voicingChoices() {
    static const juce::StringArray c{"None (flat)", "Neutral", "Bright", "Warm", "User file"};
    return c;
}

DvlceCapsuleProcessor::DvlceCapsuleProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Capsules (front, rear)", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "params", layout()) {
    for (int i = 0; i < kNumParams; ++i) raw[i] = params.getRawParameterValue(kIds[i]);
    builder = std::make_unique<Builder>(*this);
    startTimerHz(10);
}

DvlceCapsuleProcessor::~DvlceCapsuleProcessor() {
    stopTimer();
    builder.reset();
}

bool DvlceCapsuleProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

DvlceCapsuleProcessor::Selection DvlceCapsuleProcessor::currentSelection() const {
    Selection s;
    s.hardware = juce::jlimit(0, hardwareChoices().size() - 1, int(raw[kHardware]->load() + 0.5f));
    s.voicing = juce::jlimit(0, voicingChoices().size() - 1, int(raw[kVoicing]->load() + 0.5f));
    const juce::ScopedLock l(selLock);
    if (s.hardware == hardwareChoices().size() - 1) s.hardwareJson = userJson[0];
    if (s.voicing == voicingChoices().size() - 1) s.voicingJson = userJson[1];
    return s;
}

std::unique_ptr<cap::Engine> DvlceCapsuleProcessor::build(const Selection& sel, double fs, juce::String& error) const {
    cap::Model hw, voc;
    auto user = [&](const juce::String& json, const char* what) {
        if (json.isEmpty()) {
            error << what << ": no file loaded. ";
            return cap::Model();
        }
        try {
            return cap::Model::fromJson(json.toStdString());
        } catch (const std::exception& e) {
            error << what << ": " << e.what() << ". ";
            return cap::Model();
        }
    };
    if (sel.hardware == 1) hw = cap::builtin("capsule-example");
    else if (sel.hardware == 2) hw = user(sel.hardwareJson, "Hardware file");
    if (sel.voicing >= 1 && sel.voicing <= 3) voc = cap::builtin(kVoicingBuiltins[sel.voicing]);
    else if (sel.voicing == 4) voc = user(sel.voicingJson, "Voicing file");
    cap::Model m = cap::combine(hw, voc);
    m.name = (sel.hardware == 2 ? hw.name : hardwareChoices()[sel.hardware].toStdString()) + " + " +
             (sel.voicing == 4 ? voc.name : voicingChoices()[sel.voicing].toStdString());
    auto e = std::make_unique<cap::Engine>();
    e->setParams(currentParams());
    e->prepare(fs, m);
    return e;
}

void DvlceCapsuleProcessor::install(std::unique_ptr<cap::Engine> e, const Selection& sel, const juce::String& error) {
    const int lat = e->latencySamples();
    const juce::String name = juce::String(e->model().name);
    {
        const juce::SpinLock::ScopedLockType l(engineLock);
        std::swap(engine, e);   // e now holds the old engine
    }
    e.reset();                  // freed here, never on the audio thread
    {
        const juce::ScopedLock l(selLock);
        installed = sel;
        lastError = error.trim();
        modelName = name;
    }
    engineLatency = lat;
    latencyDirty = true;
}

void DvlceCapsuleProcessor::prepareToPlay(double sr, int maxBlock) {
    sampleRate = sr;
    scratch.setSize(4, juce::jmax(64, maxBlock), false, true, false);
    const Selection sel = currentSelection();
    juce::String error;
    install(build(sel, sr, error), sel, error);
    setLatencySamples(engineLatency.load());
    latencyDirty = false;
}

void DvlceCapsuleProcessor::timerCallback() {
    if (latencyDirty.exchange(false)) setLatencySamples(engineLatency.load());
}

DvlceCapsuleProcessor::Selection DvlceCapsuleProcessor::installedSelection() const {
    const juce::ScopedLock l(selLock);
    return installed;
}

cap::Params DvlceCapsuleProcessor::currentParams() const {
    cap::Params p;
    p.pattern = raw[kPattern]->load();
    p.patternRear = raw[kPatternRear]->load();
    p.stereo180 = raw[kMode]->load() > 0.5f;
    p.flip = raw[kFlip]->load() > 0.5f;
    p.sourceDistanceM = raw[kSourceCm]->load() / 100.0;
    p.virtualDistanceM = raw[kVirtualCm]->load() / 100.0;
    p.proximityCapDb = raw[kProxCap]->load();
    p.offAxisAmount = raw[kOffAxis]->load() / 100.0;
    p.nonlinearity = raw[kDrive]->load() > 0.5f;
    p.outputGainDb = raw[kOutput]->load();
    return p;
}

void DvlceCapsuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    uiActivity.store(n > 0 ? buffer.getMagnitude(0, n) : 0.0f, std::memory_order_relaxed);
    const int inCh = getTotalNumInputChannels(), outCh = buffer.getNumChannels();
    const juce::SpinLock::ScopedTryLockType l(engineLock);
    if (!l.isLocked() || engine == nullptr || inCh < 2 || scratch.getNumSamples() == 0) {
        buffer.clear();
        return;
    }
    cap::Params p = currentParams();
    const bool stereo = p.stereo180 && outCh > 1;
    p.stereo180 = stereo;
    engine->setParams(p);
    double* fr = scratch.getWritePointer(0);
    double* re = scratch.getWritePointer(1);
    double* o0 = scratch.getWritePointer(2);
    double* o1 = scratch.getWritePointer(3);
    const float* inF = buffer.getReadPointer(0);
    const float* inR = buffer.getReadPointer(1);
    for (int pos = 0; pos < n;) {
        const int len = juce::jmin(n - pos, scratch.getNumSamples());
        for (int i = 0; i < len; ++i) {
            fr[i] = inF[pos + i];
            re[i] = inR[pos + i];
        }
        engine->process(fr, re, o0, stereo ? o1 : nullptr, len);
        float* outL = buffer.getWritePointer(0);
        for (int i = 0; i < len; ++i) outL[pos + i] = float(o0[i]);
        if (outCh > 1) {
            float* outR = buffer.getWritePointer(1);
            const double* src = stereo ? o1 : o0;
            for (int i = 0; i < len; ++i) outR[pos + i] = float(src[i]);
        }
        pos += len;
    }
    for (int c = 2; c < outCh; ++c) buffer.clear(c, 0, n);
}

juce::String DvlceCapsuleProcessor::loadUserModel(int slot, const juce::File& file) {
    if (file.getSize() > 4 * 1024 * 1024) return "file is larger than 4 MB";
    const juce::String text = file.loadFileAsString();
    try {
        cap::Model::fromJson(text.toStdString());
    } catch (const std::exception& e) {
        return e.what();
    }
    {
        const juce::ScopedLock l(selLock);
        userJson[slot] = text;
        userName[slot] = file.getFileName();
    }
    auto* param = params.getParameter(kIds[slot == 0 ? kHardware : kVoicing]);
    const int last = (slot == 0 ? hardwareChoices() : voicingChoices()).size() - 1;
    param->setValueNotifyingHost(param->convertTo0to1(float(last)));
    builder->request(currentSelection(), sampleRate.load());   // also rebuilds when "User file" was already selected
    return {};
}

juce::String DvlceCapsuleProcessor::userFileName(int slot) const {
    const juce::ScopedLock l(selLock);
    return userName[slot];
}

juce::String DvlceCapsuleProcessor::statusText() const {
    const juce::ScopedLock l(selLock);
    juce::String s = modelName + "   |   latency " + juce::String(engineLatency.load()) + " samples";
    if (lastError.isNotEmpty()) s << "   |   " << lastError;
    return s;
}

void DvlceCapsuleProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::XmlElement root("DvlceCapsule");
    root.setAttribute("version", DVLCE_CAPSULE_VERSION);
    if (auto xml = params.copyState().createXml()) root.addChildElement(xml.release());
    auto* user = root.createNewChildElement("UserModels");
    {
        const juce::ScopedLock l(selLock);
        user->setAttribute("hardwareName", userName[0]);
        user->setAttribute("voicingName", userName[1]);
        user->createNewChildElement("Hardware")->addTextElement(userJson[0]);
        user->createNewChildElement("Voicing")->addTextElement(userJson[1]);
    }
    copyXmlToBinary(root, dest);
}

void DvlceCapsuleProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName("DvlceCapsule")) return;
    if (auto* user = xml->getChildByName("UserModels")) {
        const juce::ScopedLock l(selLock);
        userName[0] = user->getStringAttribute("hardwareName");
        userName[1] = user->getStringAttribute("voicingName");
        if (auto* h = user->getChildByName("Hardware")) userJson[0] = h->getAllSubText();
        if (auto* v = user->getChildByName("Voicing")) userJson[1] = v->getAllSubText();
    }
    if (auto* p = xml->getChildByName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*p));
}

juce::AudioProcessorEditor* DvlceCapsuleProcessor::createEditor() { return new DvlceCapsuleEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DvlceCapsuleProcessor(); }
