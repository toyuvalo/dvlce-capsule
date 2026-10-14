// C++ core vs the Python prototype (references from make_refs.py), the model-file rules, and
// real-time properties.   dvlce_capsule_core_tests <refs.bin>
#include "dvlce/capsule.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace dvlce::capsule;

namespace {
int failures = 0;
#define CHECK(cond)                                               \
    do {                                                          \
        if (!(cond)) {                                            \
            std::printf("  FAIL line %d: %s\n", __LINE__, #cond); \
            ++failures;                                           \
        }                                                         \
    } while (0)

template <typename T>
void readPod(std::ifstream& f, T& v) { f.read(reinterpret_cast<char*>(&v), sizeof(T)); }

Model modelFromSpec(const std::string& spec) {
    if (spec.empty()) return Model();
    const auto plus = spec.find('+');
    if (plus == std::string::npos) return builtin(spec);
    return combine(builtin(spec.substr(0, plus)), builtin(spec.substr(plus + 1)));
}

// Runs the engine over the whole input in random block sizes.
std::vector<std::vector<double>> run(Engine& e, const std::vector<double>& F, const std::vector<double>& B, int nOut,
                                     std::mt19937& rng, int maxBlock = 1024) {
    const int n = int(F.size());
    std::vector<std::vector<double>> y(static_cast<std::size_t>(nOut), std::vector<double>(static_cast<std::size_t>(n)));
    std::uniform_int_distribution<int> blk(1, maxBlock);
    for (int pos = 0; pos < n;) {
        const int len = std::min(blk(rng), n - pos);
        e.process(F.data() + pos, B.data() + pos, y[0].data() + pos, nOut > 1 ? y[1].data() + pos : nullptr, len);
        pos += len;
    }
    return y;
}

void testAgainstPrototype(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) {
        std::printf("  FAIL: cannot open %s\n", path);
        ++failures;
        return;
    }
    std::int32_t count = 0;
    readPod(f, count);
    CHECK(count > 0);
    std::mt19937 rng(5);
    for (int c = 0; c < count; ++c) {
        std::int32_t len = 0;
        readPod(f, len);
        std::string spec(static_cast<std::size_t>(len), '\0');
        f.read(spec.data(), len);
        double pv[10];
        for (double& d : pv) readPod(f, d);
        std::int32_t nOut = 0, n = 0;
        readPod(f, nOut);
        readPod(f, n);
        std::vector<double> F(static_cast<std::size_t>(n)), B(static_cast<std::size_t>(n)), want(static_cast<std::size_t>(nOut) * static_cast<std::size_t>(n));
        f.read(reinterpret_cast<char*>(F.data()), std::streamsize(F.size() * sizeof(double)));
        f.read(reinterpret_cast<char*>(B.data()), std::streamsize(B.size() * sizeof(double)));
        f.read(reinterpret_cast<char*>(want.data()), std::streamsize(want.size() * sizeof(double)));
        Params p;
        p.pattern = pv[0];
        p.patternRear = pv[1];
        p.stereo180 = pv[2] > 0.5;
        p.flip = pv[3] > 0.5;
        p.sourceDistanceM = pv[4];
        p.virtualDistanceM = pv[5];
        p.proximityCapDb = pv[6];
        p.offAxisAmount = pv[7];
        p.nonlinearity = pv[8] > 0.5;
        p.outputGainDb = pv[9];
        Engine e;
        e.setParams(p);
        e.prepare(48000.0, modelFromSpec(spec));
        const int L = e.latencySamples();
        const auto got = run(e, F, B, nOut, rng);
        if (const char* dump = std::getenv("CAPSULE_DUMP")) {
            std::ofstream o(std::string(dump) + "/case" + std::to_string(c) + ".bin", std::ios::binary);
            o.write(reinterpret_cast<const char*>(got[0].data()), std::streamsize(got[0].size() * sizeof(double)));
            o.write(reinterpret_cast<const char*>(want.data()), std::streamsize(std::size_t(n) * sizeof(double)));
        }
        double err = 0.0, peak = 0.0;
        for (int ch = 0; ch < nOut; ++ch)
            for (int i = 0; i + L < n; ++i) {
                const double w = want[static_cast<std::size_t>(ch) * static_cast<std::size_t>(n) + static_cast<std::size_t>(i)];
                err = std::max(err, std::fabs(got[static_cast<std::size_t>(ch)][static_cast<std::size_t>(i + L)] - w));
                peak = std::max(peak, std::fabs(w));
            }
        std::printf("  case %d %-32s out %d latency %4d  max error %.3g (peak %.3g)\n", c, spec.empty() ? "(unity)" : spec.c_str(),
                    nOut, L, err, peak);
        CHECK(peak > 1e-3);
        CHECK(err < 1e-9 * std::max(1.0, peak));
    }
}

const char* kBase = R"({"format":"dvlce-capsule-model","version":1,"provenance":{"kind":"self-designed","distributable":true},"voicing":{%s}%s})";

std::string doc(const std::string& voicing, const std::string& top = "") {
    std::string s = kBase;
    s.replace(s.find("%s"), 2, voicing);
    s.replace(s.find("%s"), 2, top);
    return s;
}

bool rejects(const std::string& json, const std::string& msg) {
    try {
        Model::fromJson(json);
    } catch (const ModelError& e) {
        if (std::string(e.what()).find(msg) != std::string::npos) return true;
        std::printf("    wrong message: %s (wanted '%s')\n", e.what(), msg.c_str());
        return false;
    }
    std::printf("    accepted, wanted a rejection with '%s'\n", msg.c_str());
    return false;
}

void testModelFiles() {
    for (const auto& name : builtinNames()) {
        const Model m = builtin(name);
        CHECK(m.distributable);
        CHECK(m.provenance == Provenance::SelfDesigned);
    }
    CHECK(builtinNames().size() == 4);
    CHECK(Model::fromJson(doc("")).patternEq.empty());
    const std::string pe = R"("pattern_eq":{"0.5":{"freq_hz":[100,200],"gain_db":[0,0]}})";
    CHECK(Model::fromJson(doc(pe)).patternEq.size() == 1);
    // the same 13 rejections as the prototype's test_bad_model_files_are_rejected
    std::string d = doc("");
    CHECK(rejects(std::string(d).replace(d.find("dvlce-capsule-model"), 19, "other"), "not a"));
    CHECK(rejects(std::string(d).replace(d.find("\"version\":1"), 11, "\"version\":2"), "version"));
    CHECK(rejects(std::string(d).replace(d.find("self-designed"), 13, "vendor"), "provenance"));
    CHECK(rejects(std::string(d).replace(d.find("self-designed"), 13, "fitted-to-third-party-plugin"), "distributable"));
    auto tab = [](const char* key, const char* f, const char* g) {
        return std::string(R"("pattern_eq":{")") + key + R"(":{"freq_hz":)" + f + R"(,"gain_db":)" + g + "}}";
    };
    CHECK(rejects(doc(tab("1.5", "[100,200]", "[0,0]")), "outside"));
    CHECK(rejects(doc(tab("x", "[100,200]", "[0,0]")), "number"));
    CHECK(rejects(doc(tab("0.5", "[200,100]", "[0,0]")), "ascending"));
    CHECK(rejects(doc(tab("0.5", "[100,200]", "[0]")), "equal-length"));
    CHECK(rejects(doc(tab("0.5", "[100,200]", "[0,80]")), "40 dB"));
    CHECK(rejects(doc(tab("0.5", "[100,200]", "[0,NaN]")), "non-finite"));
    CHECK(rejects(doc(R"("off_axis":{"freq_hz":[100,200],"angle_deg":[10,90],"gain_db":[[0,0],[0,0]]})"), "off_axis"));
    CHECK(rejects(doc(R"("nonlinearity":{"drive_db":99})"), "nonlinearity"));
    CHECK(rejects(doc(R"("output_gain_db":60)"), "output_gain"));
    // malformed JSON is a ModelError too, never a crash
    CHECK(rejects("{\"format\":", "JSON"));
    CHECK(rejects("[1,2", "JSON"));
    CHECK(rejects("", "JSON"));
    // a private model stays private when combined
    std::string priv = std::string(d).replace(d.find("self-designed"), 13, "fitted-to-third-party-plugin");
    priv.replace(priv.find("true"), 4, "false");
    CHECK(!combine(builtin("capsule-example"), Model::fromJson(priv)).distributable);
    CHECK(combine(builtin("capsule-example"), Model()).distributable);
    // off-axis 0 deg row becomes the 0 dB reference
    const Model oa = Model::fromJson(doc(R"("off_axis":{"freq_hz":[100,200],"angle_deg":[0,90],"gain_db":[[2,2],[-4,-4]]})"));
    CHECK(oa.offAxis && oa.offAxis->gainDb[0][0] == 0.0 && oa.offAxis->gainDb[1][1] == -6.0);
}

std::vector<double> noise(int n, unsigned seed, double amp) {
    std::mt19937 r(seed);
    std::normal_distribution<double> g(0.0, amp);
    std::vector<double> v(static_cast<std::size_t>(n));
    for (double& x : v) x = g(r);
    return v;
}

void testRealTime() {
    const int n = 48000;
    const auto F = noise(n, 1, 0.2), B = noise(n, 2, 0.2);
    Params p;
    p.pattern = 0.35;
    p.patternRear = 0.8;
    p.stereo180 = true;
    p.offAxisAmount = 0.6;
    p.sourceDistanceM = 0.1;
    p.virtualDistanceM = 0.3;
    const Model heavy = combine(builtin("capsule-example"), builtin("generic-warm"));
    // block-size invariance with constant parameters: bit-exact
    Engine a, b;
    a.setParams(p);
    b.setParams(p);
    a.prepare(48000.0, heavy);
    b.prepare(48000.0, heavy);
    std::mt19937 r1(1), r2(2);
    const auto ya = run(a, F, B, 2, r1, 1), yb = run(b, F, B, 2, r2, 2048);
    CHECK(ya == yb);
    // silence in -> silence out (also with an asymmetric nonlinearity)
    Engine s;
    s.setParams(p);
    s.prepare(48000.0, heavy);
    std::vector<double> z(4096, 0.0), o0(4096, 1.0), o1(4096, 1.0);
    s.process(z.data(), z.data(), o0.data(), o1.data(), 4096);
    CHECK(*std::max_element(o0.begin(), o0.end()) == 0.0 && *std::min_element(o1.begin(), o1.end()) == 0.0);
    // parameter changes every block: finite, bounded, no jumps larger than the signal allows
    Engine m;
    m.setParams(p);
    m.prepare(48000.0, heavy);
    std::vector<double> y0(static_cast<std::size_t>(n)), y1(static_cast<std::size_t>(n));
    double peak = 0.0;
    bool finite = true;
    for (int pos = 0, k = 0; pos < n; pos += 64, ++k) {
        Params q = p;
        q.pattern = 0.5 + 0.5 * std::sin(k * 0.05);
        q.patternRear = 0.5 + 0.5 * std::cos(k * 0.03);
        q.offAxisAmount = (k / 50) % 2 ? 1.0 : 0.0;
        q.outputGainDb = (k / 70) % 2 ? -6.0 : 3.0;
        q.virtualDistanceM = (k / 90) % 2 ? 0.05 : 1.0;
        q.stereo180 = (k / 200) % 2 == 0;
        m.setParams(q);
        m.process(F.data() + pos, B.data() + pos, y0.data() + pos, y1.data() + pos, 64);
    }
    for (int i = 0; i < n; ++i) {
        finite = finite && std::isfinite(y0[static_cast<std::size_t>(i)]) && std::isfinite(y1[static_cast<std::size_t>(i)]);
        peak = std::max(peak, std::fabs(y0[static_cast<std::size_t>(i)]));
    }
    CHECK(finite);
    CHECK(peak < 20.0);
    // latency
    Engine l0, l1;
    l0.prepare(48000.0, builtin("capsule-example"));
    l1.prepare(48000.0, builtin("generic-bright"));
    CHECK(l0.latencySamples() == 0);
    CHECK(l1.latencySamples() == kStftSize - 2);
    // other sample rates prepare and run
    for (double fs : {44100.0, 96000.0, 192000.0}) {
        Engine e;
        e.setParams(p);
        e.prepare(fs, heavy);
        std::mt19937 rr(3);
        const auto y = run(e, F, B, 2, rr);
        bool ok = true;
        for (const auto& ch : y)
            for (double v : ch) ok = ok && std::isfinite(v) && std::fabs(v) < 20.0;
        CHECK(ok);
    }
    // speed table: seconds of audio per second of CPU, per configuration (mono unless noted)
    auto speed = [&](const char* what, const Model& model, Params q) {
        Engine e;
        e.setParams(q);
        e.prepare(48000.0, model);
        std::vector<double> b0(512), b1(512);
        const auto s0 = std::chrono::steady_clock::now();
        for (int pos = 0; pos + 512 <= 3 * n; pos += 512)
            e.process(F.data() + pos % (n - 512), B.data() + pos % (n - 512), b0.data(), b1.data(), 512);
        const double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - s0).count();
        std::printf("  speed %-44s %6.0fx real time\n", what, 3.0 / d);
    };
    Params mono;
    speed("unity", Model(), mono);
    speed("capsule tables (3 convolutions)", builtin("capsule-example"), mono);
    Model eqOnly = builtin("generic-warm");
    eqOnly.offAxis.reset();
    speed("pattern EQ only", eqOnly, mono);
    speed("pattern EQ + off-axis", builtin("generic-warm"), mono);
    // speed: seconds of audio per second of CPU, heaviest model, stereo 180
    Engine sp;
    sp.setParams(p);
    sp.prepare(48000.0, heavy);
    std::vector<double> q0(512), q1(512);
    const auto t0 = std::chrono::steady_clock::now();
    const int seconds = 5;
    for (int pos = 0; pos + 512 <= seconds * n; pos += 512) sp.process(F.data() + pos % (n - 512), B.data() + pos % (n - 512), q0.data(), q1.data(), 512);
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  speed: %.0fx real time (stereo 180, capsule + warm voicing + off-axis, 48 kHz)\n", seconds / dt);
    CHECK(seconds / dt > 10.0);
}
}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("model files\n");
    testModelFiles();
    std::printf("real-time properties\n");
    testRealTime();
    if (argc > 1) {
        std::printf("against the prototype (%s)\n", argv[1]);
        testAgainstPrototype(argv[1]);
    } else {
        std::printf("  (no refs file given: prototype comparison skipped)\n");
    }
    std::printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
