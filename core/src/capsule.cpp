// Dvlce Capsule - real-time C++ core.  Port of proto/dvlce_capsule.py (same maths, block based).
// Copyright 2026 Yuval Olsha.  PolyForm Noncommercial 1.0.0 (see LICENSE).
#include "dvlce/capsule.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <locale.h>
#elif defined(__APPLE__)
#include <xlocale.h>
#else
#include <locale.h>
#endif

namespace dvlce::capsule {

const char* const kFormat = "dvlce-capsule-model";

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kLn10 = 2.302585092994045684;

// ------------------------------------------------------------------ locale-independent numbers
double parseDouble(const char* s, char** end) {
#if defined(_WIN32)
    static const _locale_t c = _create_locale(LC_NUMERIC, "C");
    return _strtod_l(s, end, c);
#else
    static const locale_t c = newlocale(LC_NUMERIC_MASK, "C", locale_t(nullptr));
    return strtod_l(s, end, c);
#endif
}

// ------------------------------------------------------------------ minimal JSON reader
struct JVal {
    enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;

    const JVal* get(const std::string& k) const {   // last duplicate wins, as in Python
        const JVal* r = nullptr;
        for (const auto& kv : o)
            if (kv.first == k) r = &kv.second;
        return r;
    }
    bool truthy() const {
        switch (t) {
            case T::Null: return false;
            case T::Bool: return b;
            case T::Num: return n != 0.0;
            case T::Str: return !s.empty();
            case T::Arr: return !a.empty();
            case T::Obj: return !o.empty();
        }
        return false;
    }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& text) : p(text.c_str()), end(text.c_str() + text.size()) {}
    JVal parseDocument() {
        JVal v = value(0);
        ws();
        if (p != end) fail("trailing characters");
        return v;
    }

private:
    const char* p;
    const char* end;

    [[noreturn]] void fail(const char* what) { throw ModelError(std::string("JSON: ") + what); }
    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool lit(const char* w) {
        const std::size_t n = std::strlen(w);
        if (static_cast<std::size_t>(end - p) >= n && std::memcmp(p, w, n) == 0) {
            p += n;
            return true;
        }
        return false;
    }
    JVal value(int depth) {
        if (depth > 64) fail("nested too deep");
        ws();
        if (p >= end) fail("unexpected end");
        JVal v;
        const char c = *p;
        if (c == '{') {
            v.t = JVal::T::Obj;
            ++p;
            ws();
            if (p < end && *p == '}') {
                ++p;
                return v;
            }
            for (;;) {
                ws();
                if (p >= end || *p != '"') fail("expected a key");
                std::string k = str();
                ws();
                if (p >= end || *p != ':') fail("expected ':'");
                ++p;
                v.o.emplace_back(std::move(k), value(depth + 1));
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; return v; }
                fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.t = JVal::T::Arr;
            ++p;
            ws();
            if (p < end && *p == ']') {
                ++p;
                return v;
            }
            for (;;) {
                v.a.push_back(value(depth + 1));
                ws();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; return v; }
                fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.t = JVal::T::Str;
            v.s = str();
            return v;
        }
        if (lit("true")) { v.t = JVal::T::Bool; v.b = true; return v; }
        if (lit("false")) { v.t = JVal::T::Bool; return v; }
        if (lit("null")) return v;
        // Python's json module also writes NaN, Infinity and -Infinity
        if (lit("NaN")) { v.t = JVal::T::Num; v.n = std::nan(""); return v; }
        if (lit("Infinity")) { v.t = JVal::T::Num; v.n = HUGE_VAL; return v; }
        if (lit("-Infinity")) { v.t = JVal::T::Num; v.n = -HUGE_VAL; return v; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* q = p;
            if (*q == '-') ++q;
            while (q < end && ((*q >= '0' && *q <= '9') || *q == '.' || *q == 'e' || *q == 'E' || *q == '+' || *q == '-')) ++q;
            const std::string num(p, q);
            char* e = nullptr;
            v.n = parseDouble(num.c_str(), &e);
            if (e != num.c_str() + num.size()) fail("bad number");
            v.t = JVal::T::Num;
            p = q;
            return v;
        }
        fail("unexpected character");
    }
    std::string str() {
        ++p;   // opening quote
        std::string r;
        while (p < end && *p != '"') {
            if (*p == '\\') {
                if (++p >= end) break;
                switch (*p) {
                    case '"': r += '"'; break;
                    case '\\': r += '\\'; break;
                    case '/': r += '/'; break;
                    case 'b': r += '\b'; break;
                    case 'f': r += '\f'; break;
                    case 'n': r += '\n'; break;
                    case 'r': r += '\r'; break;
                    case 't': r += '\t'; break;
                    case 'u': {
                        if (end - p < 5) fail("bad \\u escape");
                        unsigned cp = 0;
                        for (int i = 1; i <= 4; ++i) {
                            const char h = p[i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= unsigned(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= unsigned(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= unsigned(h - 'A' + 10);
                            else fail("bad \\u escape");
                        }
                        p += 4;
                        if (cp < 0x80) r += char(cp);
                        else if (cp < 0x800) { r += char(0xC0 | (cp >> 6)); r += char(0x80 | (cp & 0x3F)); }
                        else { r += char(0xE0 | (cp >> 12)); r += char(0x80 | ((cp >> 6) & 0x3F)); r += char(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: fail("bad escape");
                }
                ++p;
            } else {
                r += *p++;
            }
        }
        if (p >= end) fail("unterminated string");
        ++p;
        return r;
    }
};

// ------------------------------------------------------------------ model parsing helpers
std::vector<double> numbers(const JVal* v, const std::string& what) {
    if (!v || v->t != JVal::T::Arr) throw ModelError(what + ": needs numeric freq_hz and gain_db");
    std::vector<double> r;
    r.reserve(v->a.size());
    for (const auto& x : v->a) {
        if (x.t != JVal::T::Num) throw ModelError(what + ": needs numeric freq_hz and gain_db");
        r.push_back(x.n);
    }
    return r;
}

bool allFinite(const std::vector<double>& v) {
    return std::all_of(v.begin(), v.end(), [](double x) { return std::isfinite(x); });
}

double maxAbs(const std::vector<double>& v) {
    double m = 0.0;
    for (double x : v) m = std::max(m, std::fabs(x));
    return m;
}

Table parseTable(const JVal* d, const std::string& what) {
    if (!d || d->t != JVal::T::Obj) throw ModelError(what + ": needs numeric freq_hz and gain_db");
    Table t;
    t.freqHz = numbers(d->get("freq_hz"), what);
    t.gainDb = numbers(d->get("gain_db"), what);
    if (t.gainDb.size() != t.freqHz.size() || t.freqHz.size() < 2)
        throw ModelError(what + ": freq_hz and gain_db must be equal-length lists of at least 2");
    if (!allFinite(t.freqHz) || !allFinite(t.gainDb)) throw ModelError(what + ": non-finite value");
    if (t.freqHz[0] <= 0.0) throw ModelError(what + ": freq_hz must be positive and strictly ascending");
    for (std::size_t i = 1; i < t.freqHz.size(); ++i)
        if (t.freqHz[i] <= t.freqHz[i - 1]) throw ModelError(what + ": freq_hz must be positive and strictly ascending");
    if (maxAbs(t.gainDb) > 40.0) throw ModelError(what + ": |gain_db| above 40 dB");
    return t;
}

double numberOr(const JVal* v, double dflt, const std::string& what) {
    if (!v) return dflt;
    if (v->t == JVal::T::Num) return v->n;
    if (v->t == JVal::T::Bool) return v->b ? 1.0 : 0.0;
    if (v->t == JVal::T::Str) {
        char* e = nullptr;
        const double x = parseDouble(v->s.c_str(), &e);
        if (e != v->s.c_str() && *e == '\0') return x;
    }
    throw ModelError(what + ": not a number");
}

std::string stringOr(const JVal* v) {
    if (!v || v->t == JVal::T::Null) return "";
    if (v->t == JVal::T::Str) return v->s;
    return "";
}

const JVal kEmptyObject = [] {
    JVal v;
    v.t = JVal::T::Obj;
    return v;
}();

const JVal& objectOr(const JVal* v, const std::string& what) {
    if (!v) return kEmptyObject;
    if (v->t != JVal::T::Obj) throw ModelError(what + ": must be an object");
    return *v;
}

// np.interp: linear, end values hold outside, xs ascending
double interp(double x, const std::vector<double>& xs, const std::vector<double>& ys) {
    if (x <= xs.front()) return ys.front();
    if (x >= xs.back()) return ys.back();
    const auto it = std::upper_bound(xs.begin(), xs.end(), x);
    const std::size_t i = std::size_t(it - xs.begin()) - 1;
    const double w = (x - xs[i]) / (xs[i + 1] - xs[i]);
    return ys[i] + w * (ys[i + 1] - ys[i]);
}

// ------------------------------------------------------------------ FFT (radix 2)
class Fft {
public:
    explicit Fft(int n) : n(n) {
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            if (i < r) swaps.emplace_back(i, r);
        }
        // twiddles stage by stage, contiguous: stage len uses w_j = exp(-2 pi i j / len), j < len / 2
        for (int len = 2; len <= n; len <<= 1)
            for (int j = 0; j < len / 2; ++j) {
                const double ang = -2.0 * kPi * double(j) / double(len);
                twr.push_back(std::cos(ang));
                twi.push_back(std::sin(ang));
            }
    }
    // in place; inverse includes the 1/n factor (numpy convention)
    void run(std::complex<double>* xc, bool inverse) const {
        double* x = reinterpret_cast<double*>(xc);   // std::complex<double> is layout-compatible with double[2]
        for (const auto& sw : swaps) {
            std::swap(x[2 * sw.first], x[2 * sw.second]);
            std::swap(x[2 * sw.first + 1], x[2 * sw.second + 1]);
        }
        const double sgn = inverse ? -1.0 : 1.0;
        std::size_t off = 0;
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len / 2;
            const double* wr = twr.data() + off;
            const double* wi = twi.data() + off;
            for (int i = 0; i < n; i += len) {
                double* a = x + 2 * i;
                double* b = x + 2 * (i + half);
                for (int j = 0; j < half; ++j) {
                    const double cr = wr[j], ci = sgn * wi[j];
                    const double vr = b[2 * j] * cr - b[2 * j + 1] * ci;
                    const double vi = b[2 * j] * ci + b[2 * j + 1] * cr;
                    const double ur = a[2 * j], ui = a[2 * j + 1];
                    a[2 * j] = ur + vr;
                    a[2 * j + 1] = ui + vi;
                    b[2 * j] = ur - vr;
                    b[2 * j + 1] = ui - vi;
                }
            }
            off += std::size_t(half);
        }
        if (inverse) {
            const double inv = 1.0 / double(n);
            for (int i = 0; i < 2 * n; ++i) x[i] *= inv;
        }
    }
    int size() const { return n; }

private:
    int n;
    std::vector<std::pair<int, int>> swaps;
    std::vector<double> twr, twi;
};

// ------------------------------------------------------------------ real-time building blocks
// Zero-latency partitioned convolution.  Taps [0, P) run direct; taps [P, taps) run as uniformly
// partitioned overlap-save blocks of P, computed one block ahead (they only need past input).
constexpr int kPart = 64;

struct ConvCoeffs {
    std::vector<double> head;                                  // h[0..P)
    std::vector<std::vector<std::complex<double>>> parts;      // FFT_2P([h_q, 0]), bins 0..P, q = 1..K
};

ConvCoeffs makeCoeffs(const std::vector<double>& h, const Fft& fft) {
    ConvCoeffs c;
    const int taps = int(h.size());
    c.head.assign(h.begin(), h.begin() + std::min(taps, kPart));
    c.head.resize(kPart, 0.0);
    std::vector<std::complex<double>> buf(2 * kPart);
    for (int q = 1; q * kPart < taps; ++q) {
        std::fill(buf.begin(), buf.end(), 0.0);
        for (int m = 0; m < kPart && q * kPart + m < taps; ++m) buf[static_cast<std::size_t>(m)] = h[static_cast<std::size_t>(q * kPart + m)];
        fft.run(buf.data(), false);
        c.parts.emplace_back(buf.begin(), buf.begin() + kPart + 1);
    }
    return c;
}

struct PartConv {
    const Fft* fft = nullptr;            // size 2P
    int K = 0;                           // tail partitions
    double hist[2 * kPart] = {};         // head history, stored twice (newest first)
    int hpos = 0;
    double prev[kPart] = {}, cur[kPart] = {};
    int fill = 0;
    std::vector<std::vector<std::complex<double>>> fdl;   // input spectra; fdl[newest] = X_n, fdl[newest - i] = X_{n-i}
    int newest = 0;
    double tailA[kPart] = {}, tailB[kPart] = {};
    const ConvCoeffs* a = nullptr;       // active
    const ConvCoeffs* b = nullptr;       // crossfade target for the current block
    const ConvCoeffs* want = nullptr;    // requested
    std::vector<std::complex<double>> work;

    void init(const Fft& f, int partitions) {
        fft = &f;
        K = partitions;
        fdl.assign(static_cast<std::size_t>(std::max(K, 1)), std::vector<std::complex<double>>(kPart + 1));
        work.assign(2 * kPart, 0.0);
        reset();
    }
    void reset() {
        std::fill(std::begin(hist), std::end(hist), 0.0);
        std::fill(std::begin(prev), std::end(prev), 0.0);
        std::fill(std::begin(cur), std::end(cur), 0.0);
        std::fill(std::begin(tailA), std::end(tailA), 0.0);
        std::fill(std::begin(tailB), std::end(tailB), 0.0);
        for (auto& x : fdl) std::fill(x.begin(), x.end(), 0.0);
        hpos = fill = newest = 0;
        b = nullptr;
        if (want) a = want;
    }
    void request(const ConvCoeffs* c) {
        want = c;
        if (!a) a = c;
    }
    double head(const ConvCoeffs& c) const {
        const double* w = hist + hpos;
        const double* h = c.head.data();
        double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        for (int k = 0; k < kPart; k += 4) {
            s0 += h[k] * w[k];
            s1 += h[k + 1] * w[k + 1];
            s2 += h[k + 2] * w[k + 2];
            s3 += h[k + 3] * w[k + 3];
        }
        return (s0 + s1) + (s2 + s3);
    }
    void tail(const ConvCoeffs& c, double* out) {
        std::fill(work.begin(), work.end(), 0.0);
        for (int q = 1; q <= K; ++q) {
            const auto& X = fdl[static_cast<std::size_t>(((newest - (q - 1)) % K + K) % K)];
            const auto& H = c.parts[static_cast<std::size_t>(q - 1)];
            const double* xr = reinterpret_cast<const double*>(X.data());
            const double* hr = reinterpret_cast<const double*>(H.data());
            double* wr = reinterpret_cast<double*>(work.data());
            for (int k = 0; k <= kPart; ++k) {
                wr[2 * k] += xr[2 * k] * hr[2 * k] - xr[2 * k + 1] * hr[2 * k + 1];
                wr[2 * k + 1] += xr[2 * k] * hr[2 * k + 1] + xr[2 * k + 1] * hr[2 * k];
            }
        }
        work[0] = work[0].real();
        work[kPart] = work[kPart].real();
        for (int k = 1; k < kPart; ++k) work[static_cast<std::size_t>(2 * kPart - k)] = std::conj(work[static_cast<std::size_t>(k)]);
        fft->run(work.data(), true);
        for (int m = 0; m < kPart; ++m) out[m] = work[static_cast<std::size_t>(kPart + m)].real();
    }
    double tick(double x) {
        hpos = (hpos == 0 ? kPart : hpos) - 1;
        hist[hpos] = x;
        hist[hpos + kPart] = x;
        double y = head(*a) + tailA[fill];
        if (b) {
            const double w = double(fill + 1) / kPart;
            y = (1.0 - w) * y + w * (head(*b) + tailB[fill]);
        }
        cur[fill++] = x;
        if (fill == kPart) {
            fill = 0;
            if (b) {
                a = b;
                b = nullptr;
            }
            if (want != a) b = want;
            if (K > 0) {
                for (int m = 0; m < kPart; ++m) {
                    work[static_cast<std::size_t>(m)] = prev[m];
                    work[static_cast<std::size_t>(kPart + m)] = cur[m];
                }
                fft->run(work.data(), false);
                newest = (newest + 1) % K;
                std::copy(work.begin(), work.begin() + kPart + 1, fdl[static_cast<std::size_t>(newest)].begin());
                tail(*a, tailA);
                if (b) tail(*b, tailB);
            }
            std::copy(std::begin(cur), std::end(cur), std::begin(prev));
        }
        return y;
    }
};

// First-order IIR section, transposed direct form II (as scipy.signal.lfilter)
struct OnePole {
    double b0 = 1, b1 = 0, a1 = 0, z = 0;
    void set(double nb0, double nb1, double na0, double na1) {
        b0 = nb0 / na0;
        b1 = nb1 / na0;
        a1 = na1 / na0;
    }
    double tick(double x) {
        const double y = b0 * x + z;
        z = b1 * x - a1 * y;
        return y;
    }
};

std::vector<double> hannPeriodic(int n) {
    std::vector<double> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) w[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / n);
    return w;
}

}  // namespace

// ------------------------------------------------------------------ Table / Model
double Table::at(double f) const {
    f = std::max(f, 1e-3);
    const double lf = std::log(f);
    // np.interp over log frequency
    const std::size_t n = freqHz.size();
    if (lf <= std::log(freqHz[0])) return gainDb[0];
    if (lf >= std::log(freqHz[n - 1])) return gainDb[n - 1];
    std::size_t i = 0;
    while (i + 2 < n && lf >= std::log(freqHz[i + 1])) ++i;
    const double x0 = std::log(freqHz[i]), x1 = std::log(freqHz[i + 1]);
    const double w = (lf - x0) / (x1 - x0);
    return gainDb[i] + w * (gainDb[i + 1] - gainDb[i]);
}

Model Model::fromJson(const std::string& text) {
    const JVal d = JsonReader(text).parseDocument();
    const JVal* fmt = d.t == JVal::T::Obj ? d.get("format") : nullptr;
    if (!fmt || fmt->t != JVal::T::Str || fmt->s != kFormat) throw ModelError(std::string("not a ") + kFormat + " file");
    const JVal* ver = d.get("version");
    if (!ver || ver->t != JVal::T::Num || ver->n != double(kVersion))
        throw ModelError("unsupported version (this engine reads 1)");
    const JVal& prov = objectOr(d.get("provenance"), "provenance");
    const JVal* kind = prov.get("kind");
    Model m;
    const std::string k = kind && kind->t == JVal::T::Str ? kind->s : "";
    if (k == "self-designed") m.provenance = Provenance::SelfDesigned;
    else if (k == "measured-own-microphone") m.provenance = Provenance::MeasuredOwnMicrophone;
    else if (k == "fitted-to-third-party-plugin") m.provenance = Provenance::FittedToThirdPartyPlugin;
    else throw ModelError("provenance.kind must be one of self-designed, measured-own-microphone, fitted-to-third-party-plugin");
    const JVal* dist = prov.get("distributable");
    m.distributable = dist && dist->truthy();
    if (m.provenance == Provenance::FittedToThirdPartyPlugin && m.distributable)
        throw ModelError("a model fitted to a third-party plugin must not be marked distributable");
    m.name = stringOr(d.get("name"));
    m.author = stringOr(d.get("author"));
    m.licence = stringOr(d.get("licence"));

    const JVal& cap = objectOr(d.get("capsule"), "capsule");
    if (const JVal* t = cap.get("front_db")) m.front = parseTable(t, "capsule.front_db");
    if (const JVal* t = cap.get("rear_db")) m.rear = parseTable(t, "capsule.rear_db");
    if (const JVal* t = cap.get("gradient_db")) m.gradient = parseTable(t, "capsule.gradient_db");

    const JVal& voc = objectOr(d.get("voicing"), "voicing");
    const JVal& pe = objectOr(voc.get("pattern_eq"), "voicing.pattern_eq");
    for (const auto& kv : pe.o) {
        std::string key = kv.first;
        key.erase(0, key.find_first_not_of(" \t\n\r"));
        key.erase(key.find_last_not_of(" \t\n\r") + 1);
        char* e = nullptr;
        const double p = parseDouble(key.c_str(), &e);
        if (key.empty() || *e != '\0') throw ModelError("voicing.pattern_eq key '" + kv.first + "' is not a number");
        if (!(p >= 0.0 && p <= 1.0)) throw ModelError("voicing.pattern_eq key '" + kv.first + "' outside 0..1");
        Table t = parseTable(&kv.second, "voicing.pattern_eq[" + kv.first + "]");
        auto it = std::find_if(m.patternEq.begin(), m.patternEq.end(), [p](const auto& x) { return x.first == p; });
        if (it != m.patternEq.end()) it->second = std::move(t);
        else m.patternEq.emplace_back(p, std::move(t));
    }
    std::sort(m.patternEq.begin(), m.patternEq.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    if (const JVal* o = voc.get("off_axis")) {
        if (o->t != JVal::T::Obj) throw ModelError("voicing.off_axis: must be an object");
        OffAxis oa;
        oa.freqHz = numbers(o->get("freq_hz"), "voicing.off_axis");
        oa.angleDeg = numbers(o->get("angle_deg"), "voicing.off_axis");
        const JVal* g = o->get("gain_db");
        if (!g || g->t != JVal::T::Arr) throw ModelError("voicing.off_axis.gain_db must be [len(angle_deg)][len(freq_hz)]");
        for (const auto& row : g->a) oa.gainDb.push_back(numbers(&row, "voicing.off_axis.gain_db"));
        bool shapeOk = oa.gainDb.size() == oa.angleDeg.size() && oa.angleDeg.size() >= 2 && oa.freqHz.size() >= 2;
        for (const auto& row : oa.gainDb) shapeOk = shapeOk && row.size() == oa.freqHz.size();
        if (!shapeOk) throw ModelError("voicing.off_axis.gain_db must be [len(angle_deg)][len(freq_hz)]");
        bool asc = oa.angleDeg[0] == 0.0 && oa.angleDeg.back() <= 180.0;
        for (std::size_t i = 1; i < oa.freqHz.size(); ++i) asc = asc && oa.freqHz[i] > oa.freqHz[i - 1];
        for (std::size_t i = 1; i < oa.angleDeg.size(); ++i) asc = asc && oa.angleDeg[i] > oa.angleDeg[i - 1];
        if (!asc) throw ModelError("voicing.off_axis: ascending freq_hz and angle_deg, angle_deg from 0 up to <=180");
        for (const auto& row : oa.gainDb)
            if (!allFinite(row) || maxAbs(row) > 40.0) throw ModelError("voicing.off_axis.gain_db: non-finite or above 40 dB");
        if (!allFinite(oa.freqHz) || oa.freqHz[0] <= 0.0) throw ModelError("voicing.off_axis: freq_hz must be positive");
        const std::vector<double> row0 = oa.gainDb[0];   // the on-axis row is the 0 dB reference
        for (auto& row : oa.gainDb)
            for (std::size_t j = 0; j < row.size(); ++j) row[j] -= row0[j];
        m.offAxis = std::move(oa);
    }
    if (const JVal* nl = voc.get("nonlinearity")) {
        const JVal& n = objectOr(nl, "voicing.nonlinearity");
        const double drive = numberOr(n.get("drive_db"), 0.0, "voicing.nonlinearity"),
                     asym = numberOr(n.get("asymmetry"), 0.0, "voicing.nonlinearity");
        if (!(drive >= -20.0 && drive <= 40.0 && asym >= 0.0 && asym <= 0.5))
            throw ModelError("voicing.nonlinearity: drive_db in -20..40, asymmetry in 0..0.5");
        m.hasNonlinearity = true;
        m.driveDb = drive;
        m.asymmetry = asym;
    }
    m.outputGainDb = numberOr(voc.get("output_gain_db"), 0.0, "voicing.output_gain_db");
    if (!(m.outputGainDb >= -24.0 && m.outputGainDb <= 24.0)) throw ModelError("voicing.output_gain_db outside +-24");
    return m;
}

double Model::patternCurveDb(double f, double p) const {
    if (patternEq.empty()) return 0.0;
    if (p <= patternEq.front().first) return patternEq.front().second.at(f);
    if (p >= patternEq.back().first) return patternEq.back().second.at(f);
    std::size_t i = 0;   // np.searchsorted(ks, p) - 1  (side='left')
    while (i + 1 < patternEq.size() && patternEq[i + 1].first < p) ++i;
    const double k0 = patternEq[i].first, k1 = patternEq[i + 1].first;
    const double w = (p - k0) / (k1 - k0);
    return (1 - w) * patternEq[i].second.at(f) + w * patternEq[i + 1].second.at(f);
}

Model combine(const Model& hw, const Model& v) {
    Model m = v;
    m.name = hw.name + " + " + v.name;
    m.distributable = hw.distributable && v.distributable;
    m.front = hw.front;
    m.rear = hw.rear;
    m.gradient = hw.gradient;
    return m;
}

// ------------------------------------------------------------------ filter design
std::vector<double> minPhaseFirFromGrid(std::vector<double> db, int taps, int nfft) {
    const int n = nfft;
    if (int(db.size()) != n / 2 + 1) throw std::invalid_argument("minPhaseFirFromGrid: grid size");
    static thread_local std::unique_ptr<Fft> fftCache;
    if (!fftCache || fftCache->size() != n) fftCache = std::make_unique<Fft>(n);
    const Fft& fft = *fftCache;
    std::vector<std::complex<double>> x(static_cast<std::size_t>(n));
    for (int k = 0; k <= n / 2; ++k) {
        const double lm = std::clamp(db[static_cast<std::size_t>(k)], -kMaxEqDb, kMaxEqDb) * (kLn10 / 20.0);
        x[static_cast<std::size_t>(k)] = lm;
        if (k > 0 && k < n / 2) x[static_cast<std::size_t>(n - k)] = lm;
    }
    fft.run(x.data(), true);   // real cepstrum
    std::vector<std::complex<double>> fold(static_cast<std::size_t>(n), 0.0);
    fold[0] = x[0].real();
    for (int i = 1; i < n / 2; ++i) fold[static_cast<std::size_t>(i)] = 2.0 * x[static_cast<std::size_t>(i)].real();
    fold[static_cast<std::size_t>(n / 2)] = x[static_cast<std::size_t>(n / 2)].real();
    fft.run(fold.data(), false);
    std::vector<std::complex<double>> spec(static_cast<std::size_t>(n));
    for (int k = 0; k <= n / 2; ++k) {
        std::complex<double> h = std::exp(fold[static_cast<std::size_t>(k)]);
        if (k == 0 || k == n / 2) h = h.real();
        spec[static_cast<std::size_t>(k)] = h;
        if (k > 0 && k < n / 2) spec[static_cast<std::size_t>(n - k)] = std::conj(h);
    }
    fft.run(spec.data(), true);
    std::vector<double> h(static_cast<std::size_t>(taps));
    for (int i = 0; i < taps; ++i) h[static_cast<std::size_t>(i)] = spec[static_cast<std::size_t>(i)].real();
    const int kk = taps / 4;
    for (int i = 1; i <= kk; ++i) h[static_cast<std::size_t>(taps - kk + i - 1)] *= 0.5 + 0.5 * std::cos(kPi * i / kk);
    return h;
}

// ------------------------------------------------------------------ engine
namespace {

// Real-time STFT colour stage.  Frames as scipy.signal.stft (Hann, N 1024, hop 256, zero
// boundary padding), output as scipy.signal.istft, delayed by kStftSize - 2 samples.
struct OffAxisStage {
    const std::vector<double>* win = nullptr;
    const std::vector<std::vector<double>>* rows = nullptr;   // [angle][bin]
    const std::vector<double>* angles = nullptr;
    const Fft* fft = nullptr;
    double winSum = 1.0;
    double norm[kStftHop] = {};
    std::vector<double> ry, ro, rd, ola;
    std::vector<std::complex<double>> fy, fo, fd;
    long long count = 0;

    void init(const std::vector<double>& w, const std::vector<std::vector<double>>& r,
              const std::vector<double>& a, const Fft& f) {
        win = &w;
        rows = &r;
        angles = &a;
        fft = &f;
        winSum = 0.0;
        for (double x : w) winSum += x;
        for (int ph = 0; ph < kStftHop; ++ph) {
            double s = 0.0;
            for (int j = ph; j < kStftSize; j += kStftHop) s += w[static_cast<std::size_t>(j)] * w[static_cast<std::size_t>(j)];
            norm[ph] = s;
        }
        ry.assign(kStftSize, 0.0);
        ro.assign(kStftSize, 0.0);
        rd.assign(kStftSize, 0.0);
        ola.assign(kStftSize, 0.0);
        fy.assign(kStftSize, 0.0);
        fo.assign(kStftSize, 0.0);
        fd.assign(kStftSize, 0.0);
        count = 0;
    }
    void reset() {
        std::fill(ry.begin(), ry.end(), 0.0);
        std::fill(ro.begin(), ro.end(), 0.0);
        std::fill(rd.begin(), rd.end(), 0.0);
        std::fill(ola.begin(), ola.end(), 0.0);
        count = 0;
    }
    static std::size_t slot(long long i) { return std::size_t(((i % kStftSize) + kStftSize) % kStftSize); }

    void frame(long long start, double amount) {
        for (int m = 0; m < kStftSize; ++m) {
            const std::size_t s = slot(start + m);
            const double w = (*win)[static_cast<std::size_t>(m)];
            fy[static_cast<std::size_t>(m)] = ry[s] * w;
            fo[static_cast<std::size_t>(m)] = ro[s] * w;
            fd[static_cast<std::size_t>(m)] = rd[s] * w;
        }
        fft->run(fy.data(), false);
        fft->run(fo.data(), false);
        fft->run(fd.data(), false);
        const double sc = 1.0 / winSum;   // scipy 'spectrum' scaling: only matters for the 1e-20 floor
        const auto& an = *angles;
        for (int k = 0; k <= kStftSize / 2; ++k) {
            const std::complex<double> so = fo[static_cast<std::size_t>(k)] * sc, sd = fd[static_cast<std::size_t>(k)] * sc;
            const double c = (sd * std::conj(so)).real() / std::max(std::norm(so), 1e-20);
            const double theta = std::acos(std::clamp(c, -1.0, 1.0)) * (180.0 / kPi);
            double g;
            if (theta <= an.front()) g = (*rows)[0][static_cast<std::size_t>(k)];
            else if (theta >= an.back()) g = (*rows)[an.size() - 1][static_cast<std::size_t>(k)];
            else {
                std::size_t i = 0;
                while (theta >= an[i + 1]) ++i;
                const double w = (theta - an[i]) / (an[i + 1] - an[i]);
                const double g0 = (*rows)[i][static_cast<std::size_t>(k)], g1 = (*rows)[i + 1][static_cast<std::size_t>(k)];
                g = g0 + w * (g1 - g0);
            }
            const double gain = std::pow(10.0, amount * g / 20.0);
            fy[static_cast<std::size_t>(k)] *= gain;
            if (k > 0 && k < kStftSize / 2) fy[static_cast<std::size_t>(kStftSize - k)] *= gain;
        }
        fy[0] = fy[0].real();
        fy[static_cast<std::size_t>(kStftSize / 2)] = fy[static_cast<std::size_t>(kStftSize / 2)].real();
        fft->run(fy.data(), true);
        // indices below 0 are the zero padding: scipy trims them, and their slots belong to later samples
        for (int m = int(std::max<long long>(0, -start)); m < kStftSize; ++m)
            ola[slot(start + m)] += fy[static_cast<std::size_t>(m)].real() * (*win)[static_cast<std::size_t>(m)];
    }

    double process(double y, double o, double d, double amount) {
        const long long i = count++;
        const std::size_t s = slot(i);
        ry[s] = y;
        ro[s] = o;
        rd[s] = d;
        if (i >= kStftSize / 2 - 1 && (i - (kStftSize / 2 - 1)) % kStftHop == 0)
            frame(i - kStftSize + 1, amount);
        const long long out = i - (kStftSize - 2);
        if (out < 0) return 0.0;
        const std::size_t os = slot(out);
        double nv = norm[static_cast<std::size_t>(out % kStftHop)];
        if (out < kStftSize / 2) {   // stream start: frames begin at -N/2, so fewer overlap here (as scipy)
            nv = 0.0;
            for (long long m = out % kStftHop; m <= out + kStftSize / 2 && m < kStftSize; m += kStftHop)
                nv += (*win)[static_cast<std::size_t>(m)] * (*win)[static_cast<std::size_t>(m)];
        }
        const double v = ola[os] / nv;
        ola[os] = 0.0;
        return v;
    }
};

struct Output {
    OffAxisStage offAxis;
    PartConv eq;           // pattern EQ; coefficients come from the pattern grid
    OnePole dc;
    double pattern = 0.5;  // smoothed
    double gain = 1.0;     // smoothed linear output gain
    void reset() {
        offAxis.reset();
        eq.reset();
        dc.z = 0.0;
    }
};

int patternIndex(double p) { return int(std::lround(std::clamp(p, 0.0, 1.0) * kPatternSteps)); }

}  // namespace

struct Engine::Impl {
    double fs = 48000.0;
    Model m;
    bool prepared = false;
    Params par, target;
    PartConv front, rear, grad;
    ConvCoeffs cFront, cRear, cGrad;
    std::unique_ptr<Fft> convFft;
    bool hasFront = false, hasRear = false, hasGrad = false;
    OnePole proxRemove, proxAdd;
    double proxSrc = -1, proxVirt = -1, proxCap = -1;
    std::vector<ConvCoeffs> eqTable;            // [kPatternSteps + 1]
    std::vector<double> win;
    std::vector<std::vector<double>> rows;      // off-axis [angle][bin]
    std::unique_ptr<Fft> stftFft;
    Output out[2];
    double nlA = 1, nlB = 0, nlSlope = 1, nlTanhB = 0;
    bool stereoWas = false;

    void updateProximity() {
        const double fs2 = 2.0 * fs;
        if (target.sourceDistanceM != proxSrc) {
            proxSrc = target.sourceDistanceM;
            if (proxSrc > 0) {
                const double tau = proxSrc / kCAir;
                proxRemove.set(tau * fs2, -tau * fs2, tau * fs2 + 1.0, 1.0 - tau * fs2);
            }
        }
        if (target.virtualDistanceM != proxVirt || target.proximityCapDb != proxCap) {
            proxVirt = target.virtualDistanceM;
            proxCap = target.proximityCapDb;
            if (proxVirt > 0) {
                const double tau = proxVirt / kCAir, eps = std::pow(10.0, -proxCap / 20.0);
                proxAdd.set(tau * fs2 + 1.0 + eps, 1.0 + eps - tau * fs2, tau * fs2 + eps, eps - tau * fs2);
            }
        }
    }

    double nonlin(Output& o, double x) const {
        double y = (std::tanh(nlA * (x + nlB)) - nlTanhB) / nlSlope;
        if (nlB > 0) y = o.dc.tick(y);
        return y;
    }
};

Engine::Engine() : impl(std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare(double fs, const Model& model) {
    Impl& s = *impl;
    s.fs = fs;
    s.m = model;
    s.hasFront = bool(model.front);
    s.hasRear = bool(model.rear);
    s.hasGrad = bool(model.gradient);
    s.convFft = std::make_unique<Fft>(2 * kPart);
    const int K = (kFirTaps - 1) / kPart;
    auto setup = [&](PartConv& c, ConvCoeffs& cc, const Table& t) {
        cc = makeCoeffs(minPhaseFir([&](double f) { return -t.at(f); }, fs), *s.convFft);
        c.a = c.b = c.want = nullptr;
        c.init(*s.convFft, K);
        c.request(&cc);
    };
    if (s.hasFront) setup(s.front, s.cFront, *model.front);
    if (s.hasRear) setup(s.rear, s.cRear, *model.rear);
    if (s.hasGrad) setup(s.grad, s.cGrad, *model.gradient);
    s.eqTable.clear();
    if (!model.patternEq.empty()) {
        s.eqTable.reserve(kPatternSteps + 1);
        for (int i = 0; i <= kPatternSteps; ++i) {
            const double p = double(i) / double(kPatternSteps);
            s.eqTable.push_back(makeCoeffs(minPhaseFir([&](double f) { return model.patternCurveDb(f, p); }, fs), *s.convFft));
        }
    }
    if (model.offAxis) {
        s.win = hannPeriodic(kStftSize);
        const auto& oa = *s.m.offAxis;   // the engine's own copy: the stage keeps pointers into it
        std::vector<double> lfq(oa.freqHz.size());
        for (std::size_t i = 0; i < lfq.size(); ++i) lfq[i] = std::log(oa.freqHz[i]);
        s.rows.assign(oa.angleDeg.size(), std::vector<double>(kStftSize / 2 + 1));
        for (int k = 0; k <= kStftSize / 2; ++k) {
            const double f = double(k) * fs / kStftSize;
            const double lf = std::log(std::clamp(f, oa.freqHz.front(), oa.freqHz.back()));
            for (std::size_t a = 0; a < oa.angleDeg.size(); ++a) s.rows[a][static_cast<std::size_t>(k)] = interp(lf, lfq, oa.gainDb[a]);
        }
        s.stftFft = std::make_unique<Fft>(kStftSize);
        for (auto& o : s.out) o.offAxis.init(s.win, s.rows, oa.angleDeg, *s.stftFft);
    }
    for (int j = 0; j < 2; ++j) {
        Output& o = s.out[j];
        o.eq.a = o.eq.b = o.eq.want = nullptr;
        o.eq.init(*s.convFft, K);
        if (!s.eqTable.empty())
            o.eq.request(&s.eqTable[static_cast<std::size_t>(patternIndex(j == 0 ? s.target.pattern : s.target.patternRear))]);
    }
    if (model.hasNonlinearity) {
        s.nlA = std::pow(10.0, model.driveDb / 20.0);
        s.nlB = model.asymmetry;
        s.nlTanhB = std::tanh(s.nlA * s.nlB);
        s.nlSlope = s.nlA * (1.0 - s.nlTanhB * s.nlTanhB);
        const double k = std::tan(kPi * 5.0 / fs);   // butter(1, 5 Hz, highpass)
        for (auto& o : s.out) o.dc.set(1.0, -1.0, 1.0 + k, k - 1.0);
    }
    s.prepared = true;
    s.proxSrc = s.proxVirt = s.proxCap = -1;
    setParams(s.target);
    s.par = s.target;
    s.out[0].pattern = s.target.pattern;
    s.out[1].pattern = s.target.patternRear;
    for (auto& o : s.out) o.gain = std::pow(10.0, (model.outputGainDb + s.target.outputGainDb) / 20.0);
    reset();
}

void Engine::reset() {
    Impl& s = *impl;
    s.front.reset();
    s.rear.reset();
    s.grad.reset();
    s.proxRemove.z = s.proxAdd.z = 0.0;
    for (auto& o : s.out) {
        o.reset();
    }
}

int Engine::latencySamples() const { return impl->m.offAxis ? kStftSize - 2 : 0; }
const Model& Engine::model() const { return impl->m; }

void Engine::setParams(const Params& p) {
    impl->target = p;
    impl->target.pattern = std::clamp(p.pattern, 0.0, 1.0);
    impl->target.patternRear = std::clamp(p.patternRear, 0.0, 1.0);
    impl->target.offAxisAmount = std::clamp(p.offAxisAmount, 0.0, 1.0);
    impl->target.proximityCapDb = std::clamp(p.proximityCapDb, 0.0, 80.0);
    if (impl->prepared) impl->updateProximity();
}

void Engine::process(const double* front, const double* rear, double* out0, double* out1, int n) {
    Impl& s = *impl;
    if (n <= 0) return;
    const Params& t = s.target;
    const bool stereo = t.stereo180 && out1 != nullptr;
    if (stereo && !s.stereoWas) {
        s.out[1].eq.a = s.out[1].eq.want;
        s.out[1].reset();
        s.out[1].pattern = t.patternRear;
    }
    s.stereoWas = stereo;
    const int nOut = stereo ? 2 : 1;
    const double gainTarget = std::pow(10.0, (s.m.outputGainDb + t.outputGainDb) / 20.0);
    const bool eq = !s.eqTable.empty();
    const bool nl = s.m.hasNonlinearity && t.nonlinearity;
    double patStep[2], gainStep[2];
    for (int j = 0; j < nOut; ++j) {
        Output& o = s.out[j];
        const double pt = j == 0 ? t.pattern : t.patternRear;
        patStep[j] = (pt - o.pattern) / n;
        gainStep[j] = (gainTarget - o.gain) / n;
        if (eq) o.eq.request(&s.eqTable[static_cast<std::size_t>(patternIndex(pt))]);
    }
    for (int i = 0; i < n; ++i) {
        double fr = front[i], re = rear[i];
        if (t.flip) std::swap(fr, re);
        const double F = s.hasFront ? s.front.tick(fr) : fr;
        const double B = s.hasRear ? s.rear.tick(re) : re;
        const double O = F + B;
        double D = F - B;
        if (s.hasGrad) D = s.grad.tick(D);
        if (t.sourceDistanceM > 0) D = s.proxRemove.tick(D);
        if (t.virtualDistanceM > 0) D = s.proxAdd.tick(D);
        for (int j = 0; j < nOut; ++j) {
            Output& o = s.out[j];
            const double sgn = j == 0 ? 1.0 : -1.0;
            const double pat = (i == n - 1) ? (j == 0 ? t.pattern : t.patternRear) : o.pattern + patStep[j];
            o.pattern = pat;
            double y = pat * O + (1.0 - pat) * sgn * D;
            if (s.m.offAxis) y = o.offAxis.process(y, O, sgn * D, t.offAxisAmount);
            if (eq) y = o.eq.tick(y);
            if (nl) y = s.nonlin(o, y);
            o.gain = (i == n - 1) ? gainTarget : o.gain + gainStep[j];
            (j == 0 ? out0 : out1)[i] = y * o.gain;
        }
    }
}

// ------------------------------------------------------------------ built-in models
#include "builtin_models.inc"

std::vector<std::string> builtinNames() {
    std::vector<std::string> r;
    for (const auto& b : kBuiltins) r.emplace_back(b.name);
    return r;
}

Model builtin(const std::string& name) {
    for (const auto& b : kBuiltins)
        if (name == b.name) return Model::fromJson(b.json);
    throw ModelError("no built-in model named '" + name + "'");
}

}  // namespace dvlce::capsule
