"""Simulated two-capsule recordings: a plane or spherical wave from a known angle and distance.

Capsule k (front k=F, rear k=B), source at angle theta from the front axis, distance r:
    out_k = C_k(f) * X(f) * (1/2 + s_k * 1/2 * g(f) * G(f,r) * cos(theta))        s_F = +1, s_B = -1
    G(f,r) = 1 + 1/(j k r),  k = 2 pi f / c        (textbook near-field gradient of a spherical wave)
C_k = capsule frequency response, g = gradient-term gain error (both min-phase, from a Model's tables).
r = None means a plane wave (G = 1).  Done on the whole array in the frequency domain, so keep
integer-cycle test tones or long noise.  The DC bin of the source is zeroed (G is infinite there).
"""
from __future__ import annotations

import numpy as np

from dvlce_capsule import C_AIR, Model, min_phase_response


def near_field(f, r):
    """G(f, r); 1 for a plane wave."""
    if r is None:
        return np.ones_like(f, dtype=complex)
    k = 2 * np.pi * np.maximum(f, 1e-9) / C_AIR
    return 1 + 1 / (1j * k * r)


def capsules(x, fs, angle_deg, r=None, hw: Model | None = None):
    """(front, rear) for source signal x.  hw supplies the capsule tables (None = ideal capsules)."""
    x = np.asarray(x, float)
    n = len(x)
    X = np.fft.rfft(x)
    X[0] = 0.0
    f = np.fft.rfftfreq(n, 1 / fs)
    G = near_field(f, r)
    g = np.ones_like(G)
    cf = np.ones_like(G)
    cb = np.ones_like(G)
    if hw is not None:
        if hw.gradient is not None:
            g = min_phase_response(hw.gradient.at(f))
        if hw.front is not None:
            cf = min_phase_response(hw.front.at(f))
        if hw.rear is not None:
            cb = min_phase_response(hw.rear.at(f))
    grad = 0.5 * g * G * np.cos(np.radians(angle_deg))
    front = np.fft.irfft(cf * X * (0.5 + grad), n)
    rear = np.fft.irfft(cb * X * (0.5 - grad), n)
    return front, rear


def tone(f, fs, seconds=1.0, amp=0.1):
    n = int(fs * seconds)
    return amp * np.sin(2 * np.pi * f * np.arange(n) / fs)


def lockin(y, f, fs, skip=0.5):
    """Complex amplitude of y at f over the integer-cycle tail after `skip` seconds."""
    a = int(skip * fs)
    t = np.arange(len(y) - a) / fs
    return 2 * np.mean(y[a:] * np.exp(-2j * np.pi * f * t))


def gain(engine, params, f, angle_deg, r=None, hw=None, fs=48000, ch=0):
    """Complex gain of the engine for a tone at f from angle_deg / r, relative to the source tone."""
    x = tone(f, fs)
    F, B = capsules(x, fs, angle_deg, r, hw)
    y = engine.process(F, B, params)
    y = y[ch] if y.ndim == 2 else y
    return lockin(y, f, fs) / lockin(x, f, fs)
