// Dvlce Capsule - real-time C++ core (no JUCE).  Port of proto/dvlce_capsule.py; see SPEC.md.
// Two coincident capsule signals in (front F, rear B), virtual polar pattern out.
//   O = F + B (omni),  D = F - B (figure-8),  y = p * O + (1 - p) * D.
// Copyright 2026 Yuval Olsha.  PolyForm Noncommercial 1.0.0 (see LICENSE).
#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dvlce::capsule {

constexpr double kCAir = 343.0;       // speed of sound, m/s
constexpr double kMaxEqDb = 24.0;     // cap on any correction boost
constexpr int kFirTaps = 1024;
constexpr int kDesignFft = 16384;
constexpr int kStftSize = 1024;       // off-axis colour: STFT size and hop
constexpr int kStftHop = 256;
constexpr int kPatternSteps = 100;    // pattern EQ is designed on a 0.01 grid of the pattern
extern const char* const kFormat;     // "dvlce-capsule-model"
constexpr int kVersion = 1;

// Gain in dB at ascending frequencies (Hz).  Linear in dB over log frequency; end values hold outside.
struct Table {
    std::vector<double> freqHz, gainDb;
    double at(double f) const;
};

struct OffAxis {
    std::vector<double> freqHz, angleDeg;
    std::vector<std::vector<double>> gainDb;   // [angle][freq]; the 0 deg row is 0 dB after loading
};

enum class Provenance { SelfDesigned, MeasuredOwnMicrophone, FittedToThirdPartyPlugin };

struct Model {
    std::string name = "unity", author, licence;
    Provenance provenance = Provenance::SelfDesigned;
    bool distributable = true;
    std::optional<Table> front, rear, gradient;          // capsule tables, inverted by the engine
    std::vector<std::pair<double, Table>> patternEq;     // {pattern, table}, sorted by pattern
    std::optional<OffAxis> offAxis;
    bool hasNonlinearity = false;
    double driveDb = 0.0, asymmetry = 0.0;
    double outputGainDb = 0.0;

    // Parses and validates a model file (SPEC section 8).  Throws ModelError.
    static Model fromJson(const std::string& text);
    double patternCurveDb(double f, double p) const;
};

struct ModelError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Capsule tables from `hardware`, the voicing from `voicing`.  Distributable only if both are.
Model combine(const Model& hardware, const Model& voicing);

// Built-in example models (from proto/models/, compiled in).
std::vector<std::string> builtinNames();
Model builtin(const std::string& name);   // throws ModelError for an unknown name

// Min-phase FIR whose magnitude is db(f) in dB, clipped to +-kMaxEqDb (cepstral method).
template <typename F>
std::vector<double> minPhaseFir(F&& db, double fs, int taps = kFirTaps, int nfft = kDesignFft);
std::vector<double> minPhaseFirFromGrid(std::vector<double> dbOnGrid, int taps, int nfft);

struct Params {
    double pattern = 0.5;          // 0 figure-8 .. 0.5 cardioid .. 1 omni (left / only virtual mic)
    double patternRear = 0.5;      // stereo180: the rear-facing (right) virtual mic
    bool stereo180 = false;
    bool flip = false;             // swap front and rear
    double sourceDistanceM = 0.0;  // > 0: remove the proximity effect of a source at this distance
    double virtualDistanceM = 0.0; // > 0: add the proximity effect of this distance
    double proximityCapDb = 40.0;
    double offAxisAmount = 0.0;    // 0..1
    bool nonlinearity = true;
    double outputGainDb = 0.0;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Heavy (designs every filter).  Not real-time safe; call before processing.
    void prepare(double fs, const Model& model);
    void reset();
    // Latency in samples: kStftSize - 2 when the model has an off-axis table, else 0.
    int latencySamples() const;
    const Model& model() const;

    void setParams(const Params& p);   // real-time safe
    // front, rear: n input samples.  out0: the only / left output.  out1: right output, used only
    // in stereo180 mode (may be null otherwise).  In-place use (out0 == front) is allowed.
    void process(const double* front, const double* rear, double* out0, double* out1, int n);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// ---------------------------------------------------------------- template definition
template <typename F>
std::vector<double> minPhaseFir(F&& db, double fs, int taps, int nfft) {
    std::vector<double> grid(std::size_t(nfft / 2 + 1));
    for (std::size_t k = 0; k < grid.size(); ++k) grid[k] = db(double(k) * fs / double(nfft));
    return minPhaseFirFromGrid(std::move(grid), taps, nfft);
}

}  // namespace dvlce::capsule
