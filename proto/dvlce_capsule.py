"""Dvlce Capsule - prototype of the open engine (see ../SPEC.md).

Two coincident capsules in, virtual polar pattern out.  Signal model:
    F = front cardioid, B = rear cardioid.   omni O = F + B,   figure-8 D = F - B.
    out = p * O + (1 - p) * D          p = 1 omni, 0.5 cardioid, 0 figure-8
Everything here is public acoustics (first-order pressure-gradient arrays) and our own code.
No vendor data, names or plugin IDs.  Offline (whole-array) prototype; the C++ port is block based.
"""
from __future__ import annotations

import json
import math
from dataclasses import dataclass, field

import numpy as np
from scipy import signal

C_AIR = 343.0           # speed of sound, m/s
MAX_EQ_DB = 24.0        # cap on any correction boost
FORMAT = "dvlce-capsule-model"
VERSION = 1
PROVENANCE_KINDS = ("self-designed", "measured-own-microphone", "fitted-to-third-party-plugin")


class ModelError(ValueError):
    """A model file is malformed."""


# ------------------------------------------------------------------ tables and min-phase EQ
@dataclass(frozen=True)
class Table:
    """Gain in dB at ascending frequencies (Hz).  Interpolated on log frequency, flat outside."""
    freq_hz: np.ndarray
    gain_db: np.ndarray

    @staticmethod
    def parse(d, what):
        try:
            f = np.asarray(d["freq_hz"], dtype=float)
            g = np.asarray(d["gain_db"], dtype=float)
        except (KeyError, TypeError, ValueError) as e:
            raise ModelError(f"{what}: needs numeric freq_hz and gain_db ({e!r})") from None
        if f.ndim != 1 or g.shape != f.shape or len(f) < 2:
            raise ModelError(f"{what}: freq_hz and gain_db must be equal-length lists of at least 2")
        if not (np.all(np.isfinite(f)) and np.all(np.isfinite(g))):
            raise ModelError(f"{what}: non-finite value")
        if f[0] <= 0 or np.any(np.diff(f) <= 0):
            raise ModelError(f"{what}: freq_hz must be positive and strictly ascending")
        if np.max(np.abs(g)) > 40.0:
            raise ModelError(f"{what}: |gain_db| above 40 dB")
        return Table(f, g)

    def at(self, f):
        f = np.maximum(np.asarray(f, dtype=float), 1e-3)          # np.interp holds the end values
        return np.interp(np.log(f), np.log(self.freq_hz), self.gain_db)

    def to_json(self):
        return {"freq_hz": self.freq_hz.tolist(), "gain_db": self.gain_db.tolist()}


def min_phase_response(db_on_grid):
    """Complex min-phase frequency response on an rfft grid from a magnitude in dB."""
    n = 2 * (len(db_on_grid) - 1)
    logmag = db_on_grid * (math.log(10) / 20)
    cep = np.fft.irfft(logmag, n)
    fold = np.zeros(n)
    fold[0] = cep[0]
    fold[1:n // 2] = 2 * cep[1:n // 2]
    fold[n // 2] = cep[n // 2]
    return np.exp(np.fft.rfft(fold))


def min_phase_fir(db_fn, fs, taps=1024, nfft=16384):
    """Min-phase FIR whose magnitude is db_fn(f) in dB (clipped to +-MAX_EQ_DB)."""
    f = np.fft.rfftfreq(nfft, 1 / fs)
    db = np.clip(db_fn(f), -MAX_EQ_DB, MAX_EQ_DB)
    h = np.fft.irfft(min_phase_response(db), nfft)[:taps].copy()
    k = taps // 4
    h[-k:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(1, k + 1) / k)
    return h


def apply_fir(h, x):
    return signal.fftconvolve(x, h)[:len(x)]


# ------------------------------------------------------------------ model file
@dataclass
class Model:
    name: str = "unity"
    author: str = ""
    licence: str = ""
    provenance: str = "self-designed"
    distributable: bool = True
    front: Table | None = None                 # capsule response, dB (inverted by the engine)
    rear: Table | None = None
    gradient: Table | None = None              # gradient-term gain error g(f), dB (inverted)
    pattern_eq: dict = field(default_factory=dict)   # {pattern: Table}
    off_axis: tuple | None = None              # (freq_hz[nf], angle_deg[na], gain_db[na, nf])
    nonlinearity: dict | None = None           # {"drive_db", "asymmetry"}
    output_gain_db: float = 0.0

    @staticmethod
    def from_dict(d):
        if not isinstance(d, dict) or d.get("format") != FORMAT:
            raise ModelError(f"not a {FORMAT} file")
        if d.get("version") != VERSION:
            raise ModelError(f"unsupported version {d.get('version')!r} (this engine reads {VERSION})")
        prov = d.get("provenance", {})
        kind = prov.get("kind")
        if kind not in PROVENANCE_KINDS:
            raise ModelError(f"provenance.kind must be one of {PROVENANCE_KINDS}")
        dist = bool(prov.get("distributable", False))
        if kind == "fitted-to-third-party-plugin" and dist:
            raise ModelError("a model fitted to a third-party plugin must not be marked distributable")
        m = Model(name=str(d.get("name", "")), author=str(d.get("author", "")), licence=str(d.get("licence", "")),
                  provenance=kind, distributable=dist)
        cap = d.get("capsule", {})
        for key, attr in (("front_db", "front"), ("rear_db", "rear"), ("gradient_db", "gradient")):
            if key in cap:
                setattr(m, attr, Table.parse(cap[key], f"capsule.{key}"))
        voc = d.get("voicing", {})
        for key, tab in voc.get("pattern_eq", {}).items():
            try:
                p = float(key)
            except ValueError:
                raise ModelError(f"voicing.pattern_eq key {key!r} is not a number") from None
            if not 0.0 <= p <= 1.0:
                raise ModelError(f"voicing.pattern_eq key {key!r} outside 0..1")
            m.pattern_eq[p] = Table.parse(tab, f"voicing.pattern_eq[{key}]")
        if "off_axis" in voc:
            o = voc["off_axis"]
            try:
                fq = np.asarray(o["freq_hz"], float)
                an = np.asarray(o["angle_deg"], float)
                gd = np.asarray(o["gain_db"], float)
            except (KeyError, TypeError, ValueError) as e:
                raise ModelError(f"voicing.off_axis: {e!r}") from None
            if gd.shape != (len(an), len(fq)) or len(an) < 2 or len(fq) < 2:
                raise ModelError("voicing.off_axis.gain_db must be [len(angle_deg)][len(freq_hz)]")
            if np.any(np.diff(fq) <= 0) or np.any(np.diff(an) <= 0) or an[0] != 0.0 or an[-1] > 180.0:
                raise ModelError("voicing.off_axis: ascending freq_hz and angle_deg, angle_deg from 0 up to <=180")
            if not (np.all(np.isfinite(gd)) and np.max(np.abs(gd)) <= 40.0):
                raise ModelError("voicing.off_axis.gain_db: non-finite or above 40 dB")
            m.off_axis = (fq, an, gd - gd[0])        # on-axis row is the 0 dB reference
        if "nonlinearity" in voc:
            n = voc["nonlinearity"]
            drive, asym = float(n.get("drive_db", 0.0)), float(n.get("asymmetry", 0.0))
            if not (-20.0 <= drive <= 40.0 and 0.0 <= asym <= 0.5):
                raise ModelError("voicing.nonlinearity: drive_db in -20..40, asymmetry in 0..0.5")
            m.nonlinearity = {"drive_db": drive, "asymmetry": asym}
        m.output_gain_db = float(voc.get("output_gain_db", 0.0))
        if not -24.0 <= m.output_gain_db <= 24.0:
            raise ModelError("voicing.output_gain_db outside +-24")
        return m

    @staticmethod
    def load(path):
        with open(path, encoding="utf-8") as f:
            return Model.from_dict(json.load(f))

    def to_dict(self):
        d = {"format": FORMAT, "version": VERSION, "name": self.name, "author": self.author, "licence": self.licence,
             "provenance": {"kind": self.provenance, "distributable": self.distributable}, "capsule": {}, "voicing": {}}
        for key, attr in (("front_db", "front"), ("rear_db", "rear"), ("gradient_db", "gradient")):
            if getattr(self, attr) is not None:
                d["capsule"][key] = getattr(self, attr).to_json()
        if self.pattern_eq:
            d["voicing"]["pattern_eq"] = {repr(p): t.to_json() for p, t in sorted(self.pattern_eq.items())}
        if self.off_axis is not None:
            fq, an, gd = self.off_axis
            d["voicing"]["off_axis"] = {"freq_hz": fq.tolist(), "angle_deg": an.tolist(), "gain_db": gd.tolist()}
        if self.nonlinearity:
            d["voicing"]["nonlinearity"] = dict(self.nonlinearity)
        d["voicing"]["output_gain_db"] = self.output_gain_db
        return d

    def save(self, path):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(self.to_dict(), f, indent=1)

    def pattern_curve_db(self, f, p):
        """Voicing EQ at pattern p: dB-linear blend of the two nearest anchors; flat beyond the end anchors."""
        if not self.pattern_eq:
            return np.zeros_like(f, dtype=float)
        ks = sorted(self.pattern_eq)
        if p <= ks[0]:
            return self.pattern_eq[ks[0]].at(f)
        if p >= ks[-1]:
            return self.pattern_eq[ks[-1]].at(f)
        i = int(np.searchsorted(ks, p)) - 1
        w = (p - ks[i]) / (ks[i + 1] - ks[i])
        return (1 - w) * self.pattern_eq[ks[i]].at(f) + w * self.pattern_eq[ks[i + 1]].at(f)


def combine(hardware: Model, voicing: Model) -> Model:
    """Capsule tables from `hardware`, voicing from `voicing`.  Distributable only if both are."""
    return Model(name=f"{hardware.name} + {voicing.name}", author=voicing.author, licence=voicing.licence,
                 provenance=voicing.provenance, distributable=hardware.distributable and voicing.distributable,
                 front=hardware.front, rear=hardware.rear, gradient=hardware.gradient,
                 pattern_eq=voicing.pattern_eq, off_axis=voicing.off_axis, nonlinearity=voicing.nonlinearity,
                 output_gain_db=voicing.output_gain_db)


# ------------------------------------------------------------------ engine
@dataclass
class Params:
    pattern: float = 0.5                 # 0 figure-8 .. 0.5 cardioid .. 1 omni (left / only virtual mic)
    pattern_rear: float = 0.5            # stereo180: pattern of the rear-facing (right) virtual mic
    stereo180: bool = False              # False: one mono output.  True: L = front-facing, R = rear-facing
    flip: bool = False                   # swap front and rear (mic turned around)
    source_distance_m: float | None = None    # remove the proximity effect of a source at this distance
    virtual_distance_m: float | None = None   # add the proximity effect of this distance
    proximity_cap_db: float = 40.0       # limit of the added bass boost
    off_axis_amount: float = 0.0         # 0..1, depth of the model's off-axis colour
    nonlinearity: bool = True            # apply the model's nonlinearity if it has one
    output_gain_db: float = 0.0


class CapsuleEngine:
    NPERSEG = 1024                       # off-axis STFT size (latency NPERSEG * 3/4 in real time)

    def __init__(self, model: Model | None = None, fs: float = 48000.0):
        self.m = model or Model()
        self.fs = float(fs)
        self._fir_cache = {}

    # -- filters
    def _cap_fir(self, which):
        if which not in self._fir_cache:
            t = {"front": self.m.front, "rear": self.m.rear, "gradient": self.m.gradient}[which]
            self._fir_cache[which] = None if t is None else min_phase_fir(lambda f: -t.at(f), self.fs)
        return self._fir_cache[which]

    def _prox(self, x, p):
        y = x
        if p.source_distance_m:                         # remove: s*tau / (1 + s*tau)
            tau = p.source_distance_m / C_AIR
            b, a = signal.bilinear([tau, 0.0], [tau, 1.0], self.fs)
            y = signal.lfilter(b, a, y)
        if p.virtual_distance_m:                        # add: 1 + 1/(s*tau + eps)
            tau = p.virtual_distance_m / C_AIR
            eps = 10 ** (-p.proximity_cap_db / 20)
            b, a = signal.bilinear([tau, 1.0 + eps], [tau, eps], self.fs)
            y = signal.lfilter(b, a, y)
        return y

    def _nonlin(self, x):
        n = self.m.nonlinearity
        if not n:
            return x
        a, b = 10 ** (n["drive_db"] / 20), n["asymmetry"]
        slope = a * (1 - math.tanh(a * b) ** 2)
        y = (np.tanh(a * (x + b)) - math.tanh(a * b)) / slope
        if b > 0:                                       # asymmetry makes DC: block it (one pole, 5 Hz)
            bb, aa = signal.butter(1, 5.0, "highpass", fs=self.fs)
            y = signal.lfilter(bb, aa, y)
        return y

    def _off_axis(self, y, O, D, amount):
        fq, an, gd = self.m.off_axis
        kw = dict(fs=self.fs, nperseg=self.NPERSEG, noverlap=self.NPERSEG * 3 // 4, window="hann")
        f, _, Y = signal.stft(y, **kw)
        _, _, SO = signal.stft(O, **kw)
        _, _, SD = signal.stft(D, **kw)
        # plane wave: D = cos(theta) * O in every bin, so the angle is read from the ratio
        c = np.real(SD * np.conj(SO)) / np.maximum(np.abs(SO) ** 2, 1e-20)
        theta = np.degrees(np.arccos(np.clip(c, -1.0, 1.0)))
        lf = np.log(np.clip(f, fq[0], fq[-1]))
        rows = np.stack([np.interp(lf, np.log(fq), g) for g in gd])            # [angle, bin]
        g = np.empty_like(theta)
        for k in range(len(f)):
            g[k] = np.interp(theta[k], an, rows[:, k])      # beyond the last angle the last row holds
        gain = 10 ** (amount * g / 20)
        _, out = signal.istft(Y * gain, **kw)
        return out[:len(y)]

    # -- main
    def process(self, front, rear, p: Params = Params()):
        front, rear = np.asarray(front, float), np.asarray(rear, float)
        if p.flip:
            front, rear = rear, front
        F, B = front, rear
        hf, hb = self._cap_fir("front"), self._cap_fir("rear")
        if hf is not None:
            F = apply_fir(hf, F)
        if hb is not None:
            B = apply_fir(hb, B)
        O, D = F + B, F - B
        hg = self._cap_fir("gradient")
        if hg is not None:
            D = apply_fir(hg, D)
        D = self._prox(D, p)
        outs = [(p.pattern, +1.0)]
        if p.stereo180:
            outs.append((p.pattern_rear, -1.0))
        res = []
        for pat, sgn in outs:
            y = pat * O + (1 - pat) * sgn * D
            if self.m.off_axis is not None and p.off_axis_amount > 0:
                y = self._off_axis(y, O, sgn * D, p.off_axis_amount)
            if self.m.pattern_eq:
                h = min_phase_fir(lambda f, pat=pat: self.m.pattern_curve_db(f, pat), self.fs)
                y = apply_fir(h, y)
            if p.nonlinearity:
                y = self._nonlin(y)
            res.append(y * 10 ** ((self.m.output_gain_db + p.output_gain_db) / 20))
        return np.stack(res) if p.stereo180 else res[0]


def ideal_gain(pattern, angle_deg):
    return pattern + (1 - pattern) * np.cos(np.radians(angle_deg))
