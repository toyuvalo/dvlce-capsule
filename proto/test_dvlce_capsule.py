"""Tests on simulated two-capsule recordings (capsule_sim.py): plane and spherical waves at known
angles and distances, tones of known level.  No vendor data is used anywhere."""
import json
import math
import pathlib

import numpy as np
import pytest

import dvlce_capsule as ds
import capsule_sim as sim

FS = 48000
HERE = pathlib.Path(__file__).resolve().parent
FREQS = [124, 500, 1000, 4000, 8000, 12000]          # even Hz: integer cycles in the lock-in tail
ANGLES = list(range(0, 360, 30))
PATTERNS = {"figure8": 0.0, "hypercardioid": 0.25, "cardioid": 0.5, "subcardioid": 0.7, "omni": 1.0}


def hw():
    return ds.Model.load(HERE / "models" / "capsule-example.json")


def g(engine, params, f, ang, r=None, hwm=None, ch=0):
    return sim.gain(engine, params, f, ang, r, hwm, FS, ch)


# ---------------------------------------------------------------- pattern synthesis
@pytest.mark.parametrize("name", PATTERNS)
@pytest.mark.parametrize("f", FREQS)
def test_polar_matches_ideal_with_ideal_capsules(name, f):
    p = PATTERNS[name]
    e = ds.CapsuleEngine()
    for a in ANGLES:
        got = g(e, ds.Params(pattern=p), f, a)
        assert got.real == pytest.approx(ds.ideal_gain(p, a), abs=0.003), (name, f, a)
        assert abs(got.imag) < 0.003


@pytest.mark.parametrize("f", FREQS)
def test_polar_corrected_for_nonideal_capsules(f):
    """Imperfect capsules (tilted, mismatched, gradient droop) + the matching capsule tables = ideal polar."""
    e = ds.CapsuleEngine(hw())
    for p in (0.0, 0.5, 1.0):
        for a in ANGLES:
            got = g(e, ds.Params(pattern=p), f, a, hwm=hw())
            assert got.real == pytest.approx(ds.ideal_gain(p, a), abs=0.01), (p, f, a)


def test_nonideal_capsules_without_correction_are_wrong():
    """The same recordings through an engine that has no capsule tables: error far above the tolerance."""
    e = ds.CapsuleEngine()
    errs = [abs(g(e, ds.Params(pattern=0.5), f, a, hwm=hw()).real - ds.ideal_gain(0.5, a))
            for f in (12000,) for a in ANGLES]
    assert max(errs) > 0.1


def test_pattern_is_continuous_and_monotone_in_rear_pickup():
    e = ds.CapsuleEngine()
    # figure-8 rear = -1 (inverted), cardioid 0, omni +1: the signed value rises 1 step per 0.1
    signed = [g(e, ds.Params(pattern=p), 1000, 180).real for p in np.linspace(0, 1, 11)]
    assert np.all(np.diff(signed) > 0)
    assert signed[0] == pytest.approx(-1, abs=0.003) and signed[-1] == pytest.approx(1, abs=0.003)


# ---------------------------------------------------------------- off-axis rejection
@pytest.mark.parametrize("p,null", [(0.0, 90.0), (0.5, 180.0), (0.25, math.degrees(math.acos(-1 / 3))),
                                    (1 / 3, 120.0)])
@pytest.mark.parametrize("f", [250, 2000, 10000])
def test_null_depth(p, null, f):
    e = ds.CapsuleEngine(hw())
    lvl = abs(g(e, ds.Params(pattern=p), f, null, hwm=hw()))
    assert 20 * math.log10(max(lvl, 1e-9)) < -40.0


def test_null_is_lost_when_capsule_mismatch_is_not_corrected():
    e = ds.CapsuleEngine()
    lvl = abs(g(e, ds.Params(pattern=0.5), 12000, 180, hwm=hw()))
    assert 20 * math.log10(lvl) > -25.0


# ---------------------------------------------------------------- proximity effect
@pytest.mark.parametrize("r", [0.03, 0.1, 0.5])
@pytest.mark.parametrize("f", [100, 200, 500, 1000, 4000])
def test_simulation_is_the_textbook_gradient_formula(r, f):
    """Sim anchor: figure-8 (F - B) of a spherical wave = sqrt(1 + 1/(kr)^2) times the plane-wave figure-8."""
    x = sim.tone(f, FS)
    F, B = sim.capsules(x, FS, 0.0, r)
    got = abs(sim.lockin(F - B, f, FS) / sim.lockin(x, f, FS))
    k = 2 * math.pi * f / ds.C_AIR
    assert got == pytest.approx(math.sqrt(1 + 1 / (k * r) ** 2), rel=1e-4)


@pytest.mark.parametrize("r", [0.03, 0.1, 0.5])
@pytest.mark.parametrize("f", [100, 250, 1000, 4000, 8000])
def test_proximity_removal_restores_far_field_response(r, f):
    for p in (0.0, 0.5):
        e = ds.CapsuleEngine()
        raw = g(e, ds.Params(pattern=p), f, 0.0, r=r)
        fixed = g(e, ds.Params(pattern=p, source_distance_m=r), f, 0.0, r=r)
        assert abs(fixed) == pytest.approx(1.0, abs=0.012)
        if f <= 250 and r <= 0.1 and p == 0.0:
            assert 20 * math.log10(abs(raw)) > 6.0          # the proximity boost that was removed


def test_removal_with_the_wrong_distance_leaves_a_residue():
    e = ds.CapsuleEngine()
    fixed = abs(g(e, ds.Params(pattern=0.0, source_distance_m=0.3), 124, 0.0, r=0.03))
    assert fixed > 2.0


@pytest.mark.parametrize("r", [0.05, 0.2, 1.0])
@pytest.mark.parametrize("f", [124, 250, 500, 1000, 4000])
def test_added_proximity_matches_textbook(r, f):
    """Far-field source, virtual distance r: figure-8 gain = |1 + 1/(jkr)|; cardioid = |1/2 + 1/2 G|."""
    k = 2 * math.pi * f / ds.C_AIR
    G = 1 + 1 / (1j * k * r)
    e = ds.CapsuleEngine()
    for p, want in ((0.0, abs(G)), (0.5, abs(0.5 + 0.5 * G))):
        got = abs(g(e, ds.Params(pattern=p, virtual_distance_m=r), f, 0.0))
        assert 20 * math.log10(got / want) == pytest.approx(0.0, abs=0.35), (p, f, r)


def test_added_proximity_is_capped():
    e = ds.CapsuleEngine()
    lvl = abs(g(e, ds.Params(pattern=0.0, virtual_distance_m=0.01, proximity_cap_db=20.0), 20, 0.0))
    assert 20 * math.log10(lvl) < 21.0


def test_proximity_acts_on_the_gradient_part_only():
    """Omni has no gradient term: no proximity effect at any distance."""
    e = ds.CapsuleEngine()
    a = abs(g(e, ds.Params(pattern=1.0, virtual_distance_m=0.03), 124, 0.0))
    b = abs(g(e, ds.Params(pattern=1.0, source_distance_m=0.03), 124, 0.0, r=0.03))
    assert a == pytest.approx(1.0, abs=0.003) and b == pytest.approx(1.0, abs=0.003)


# ---------------------------------------------------------------- model file
def test_example_models_load_and_are_distributable():
    for f in sorted((HERE / "models").glob("*.json")):
        m = ds.Model.load(f)
        assert m.distributable and m.provenance == "self-designed", f


def test_round_trip_through_json_keeps_the_engine_output():
    m = ds.Model.load(HERE / "models" / "generic-warm.json")
    m2 = ds.Model.from_dict(json.loads(json.dumps(m.to_dict())))
    x = sim.tone(1000, FS, 0.4)
    F, B = sim.capsules(x, FS, 30.0)
    a = ds.CapsuleEngine(m).process(F, B, ds.Params(pattern=0.4))
    b = ds.CapsuleEngine(m2).process(F, B, ds.Params(pattern=0.4))
    assert np.max(np.abs(a - b)) < 1e-9


def base_dict():
    return {"format": ds.FORMAT, "version": 1, "provenance": {"kind": "self-designed", "distributable": True},
            "voicing": {}}


@pytest.mark.parametrize("edit,msg", [
    (lambda d: d.update(format="other"), "not a"),
    (lambda d: d.update(version=2), "version"),
    (lambda d: d["provenance"].update(kind="vendor"), "provenance"),
    (lambda d: d.update(provenance={"kind": "fitted-to-third-party-plugin", "distributable": True}), "distributable"),
    (lambda d: d["voicing"].update(pattern_eq={"1.5": {"freq_hz": [100, 200], "gain_db": [0, 0]}}), "outside"),
    (lambda d: d["voicing"].update(pattern_eq={"x": {"freq_hz": [100, 200], "gain_db": [0, 0]}}), "number"),
    (lambda d: d["voicing"].update(pattern_eq={"0.5": {"freq_hz": [200, 100], "gain_db": [0, 0]}}), "ascending"),
    (lambda d: d["voicing"].update(pattern_eq={"0.5": {"freq_hz": [100, 200], "gain_db": [0]}}), "equal-length"),
    (lambda d: d["voicing"].update(pattern_eq={"0.5": {"freq_hz": [100, 200], "gain_db": [0, 80]}}), "40 dB"),
    (lambda d: d["voicing"].update(pattern_eq={"0.5": {"freq_hz": [100, 200], "gain_db": [0, float("nan")]}}), "non-finite"),
    (lambda d: d["voicing"].update(off_axis={"freq_hz": [100, 200], "angle_deg": [10, 90], "gain_db": [[0, 0], [0, 0]]}), "off_axis"),
    (lambda d: d["voicing"].update(nonlinearity={"drive_db": 99}), "nonlinearity"),
    (lambda d: d["voicing"].update(output_gain_db=60), "output_gain"),
])
def test_bad_model_files_are_rejected(edit, msg):
    d = base_dict()
    edit(d)
    with pytest.raises(ds.ModelError, match=msg):
        ds.Model.from_dict(d)


def test_private_model_cannot_be_combined_into_a_distributable_one():
    priv = ds.Model.from_dict({**base_dict(), "provenance": {"kind": "fitted-to-third-party-plugin",
                                                            "distributable": False}})
    assert not ds.combine(hw(), priv).distributable
    assert ds.combine(hw(), ds.Model()).distributable


@pytest.mark.parametrize("p", [0.0, 0.25, 0.5, 0.8, 1.0])
def test_pattern_eq_interpolates_between_anchors(p):
    t = lambda db: {"freq_hz": [100, 10000], "gain_db": [db, db]}
    d = base_dict()
    d["voicing"]["pattern_eq"] = {"0.0": t(-4.0), "0.5": t(0.0), "1.0": t(6.0)}
    e = ds.CapsuleEngine(ds.Model.from_dict(d))
    want = np.interp(p, [0, 0.5, 1], [-4, 0, 6])
    got = 20 * math.log10(abs(g(e, ds.Params(pattern=p), 1000, 0.0)))
    assert got == pytest.approx(want, abs=0.1)


def test_voicing_curve_shape_is_applied():
    d = base_dict()
    d["voicing"]["pattern_eq"] = {"0.5": {"freq_hz": [100, 1000, 4000, 8000], "gain_db": [0, 0, 6, 6]}}
    e = ds.CapsuleEngine(ds.Model.from_dict(d))
    for f, want in ((250, 0.0), (8000, 6.0), (12000, 6.0)):
        got = 20 * math.log10(abs(g(e, ds.Params(pattern=0.5), f, 0.0)))
        assert got == pytest.approx(want, abs=0.25), f


def test_off_axis_table_colours_only_the_off_axis_sound():
    d = base_dict()
    d["voicing"]["off_axis"] = {"freq_hz": [200, 2000, 8000], "angle_deg": [0, 90, 180],
                                "gain_db": [[0, 0, 0], [0, 0, -6], [0, 0, -12]]}
    e = ds.CapsuleEngine(ds.Model.from_dict(d))
    P = ds.Params(pattern=1.0, off_axis_amount=1.0)
    for ang, want in ((0.0, 0.0), (90.0, -6.0), (180.0, -12.0)):
        got = 20 * math.log10(abs(g(e, P, 8000, ang)))
        assert got == pytest.approx(want, abs=0.6), ang
    low = 20 * math.log10(abs(g(e, P, 500, 90.0)))
    assert abs(low) < 0.3
    half = 20 * math.log10(abs(g(e, ds.Params(pattern=1.0, off_axis_amount=0.5), 8000, 90.0)))
    assert half == pytest.approx(-3.0, abs=0.4)
    off = 20 * math.log10(abs(g(e, ds.Params(pattern=1.0, off_axis_amount=0.0), 8000, 90.0)))
    assert abs(off) < 0.01


def test_nonlinearity_is_unity_for_small_signals_and_compresses_large_ones():
    d = base_dict()
    d["voicing"]["nonlinearity"] = {"drive_db": 12.0, "asymmetry": 0.0}
    e = ds.CapsuleEngine(ds.Model.from_dict(d))
    x0 = sim.tone(1000, FS, amp=0.002)
    small = abs(sim.lockin(e.process(x0, np.zeros_like(x0), ds.Params(pattern=1.0)), 1000, FS)
                / sim.lockin(x0, 1000, FS))
    assert small == pytest.approx(1.0, abs=0.01)
    x = sim.tone(1000, FS, amp=0.8)
    y = e.process(x, np.zeros_like(x), ds.Params(pattern=1.0))
    assert np.max(np.abs(y)) < 0.8 * 0.9
    off = e.process(x, np.zeros_like(x), ds.Params(pattern=1.0, nonlinearity=False))
    assert np.max(np.abs(off - x)) < 1e-9


def _h(y, n, f=1000):
    return abs(sim.lockin(y, n * f, FS))


def test_symmetric_nonlinearity_makes_odd_harmonics_and_asymmetry_makes_even_ones():
    x = sim.tone(1000, FS, amp=0.5)
    z = np.zeros_like(x)
    sym = ds.CapsuleEngine(ds.Model.from_dict({**base_dict(), "voicing": {"nonlinearity": {"drive_db": 12}}}))
    asy = ds.CapsuleEngine(ds.Model.from_dict({**base_dict(),
                                              "voicing": {"nonlinearity": {"drive_db": 12, "asymmetry": 0.2}}}))
    ys, ya = sym.process(x, z, ds.Params(pattern=1.0)), asy.process(x, z, ds.Params(pattern=1.0))
    assert _h(ys, 3) > 1e-2 and _h(ys, 2) < 1e-4
    assert _h(ya, 2) > 1e-2
    assert abs(np.mean(ya[FS // 2:])) < 2e-3                          # the bias is removed: no DC


def test_min_phase_eq_has_negligible_latency():
    d = base_dict()
    d["voicing"]["pattern_eq"] = {"0.5": {"freq_hz": [100, 10000], "gain_db": [0, 3]}}
    e = ds.CapsuleEngine(ds.Model.from_dict(d))
    imp = np.zeros(4800)
    imp[1000] = 1.0
    y = e.process(imp, np.zeros_like(imp), ds.Params(pattern=0.5))
    assert abs(int(np.argmax(np.abs(y))) - 1000) <= 1
    assert abs(np.argmax(np.abs(ds.CapsuleEngine().process(imp, np.zeros_like(imp), ds.Params(pattern=1.0))))
               - 1000) == 0


def test_correction_filters_follow_the_table_to_a_tenth_of_a_db():
    t = hw().front
    h = ds.min_phase_fir(lambda f: t.at(f), FS)
    H = np.fft.rfft(h, 16384)
    f = np.fft.rfftfreq(16384, 1 / FS)
    for fq in (60, 200, 1000, 5000, 9000, 14000):
        i = int(np.argmin(abs(f - fq)))
        assert 20 * math.log10(abs(H[i])) == pytest.approx(float(t.at(f[i])), abs=0.1), fq


# ---------------------------------------------------------------- stereo 180 mode
def test_stereo180_with_cardioids_is_the_corrected_back_to_back_pair():
    e = ds.CapsuleEngine(hw())
    P = ds.Params(pattern=0.5, pattern_rear=0.5, stereo180=True)
    for a, wl, wr in ((0.0, 1.0, 0.0), (180.0, 0.0, 1.0), (90.0, 0.5, 0.5), (60.0, 0.75, 0.25)):
        L = g(e, P, 2000, a, hwm=hw(), ch=0)
        R = g(e, P, 2000, a, hwm=hw(), ch=1)
        assert L.real == pytest.approx(wl, abs=0.01) and R.real == pytest.approx(wr, abs=0.01), a


def test_stereo180_channels_have_independent_patterns():
    e = ds.CapsuleEngine()
    P = ds.Params(pattern=1.0, pattern_rear=0.0, stereo180=True)
    for a in ANGLES:
        L = g(e, P, 1000, a, ch=0).real
        R = g(e, P, 1000, a, ch=1).real
        assert L == pytest.approx(1.0, abs=0.003)
        assert R == pytest.approx(-math.cos(math.radians(a)), abs=0.003)     # rear-facing figure-8


def test_stereo180_sum_of_equal_patterns_is_the_omni_part():
    e = ds.CapsuleEngine()
    x = sim.tone(1000, FS)
    F, B = sim.capsules(x, FS, 40.0)
    y = e.process(F, B, ds.Params(pattern=0.3, pattern_rear=0.3, stereo180=True))
    assert np.max(np.abs(y[0] + y[1] - 0.6 * (F + B))) < 1e-9


def test_flip_swaps_the_two_channels():
    e = ds.CapsuleEngine(hw())
    x = sim.tone(2000, FS)
    F, B = sim.capsules(x, FS, 50.0, 0.2, hw())
    P = dict(pattern=0.3, pattern_rear=0.7, stereo180=True, source_distance_m=0.2)
    a = e.process(F, B, ds.Params(**P))
    b = e.process(B, F, ds.Params(flip=True, **P))
    assert np.max(np.abs(a - b)) < 1e-9


def test_stereo180_proximity_removal_applies_to_both_channels():
    e = ds.CapsuleEngine()
    P = ds.Params(pattern=0.0, pattern_rear=0.0, stereo180=True, source_distance_m=0.05)
    for a in (0.0, 180.0):
        for ch in (0, 1):
            assert abs(g(e, P, 124, a, r=0.05, ch=ch)) == pytest.approx(1.0, abs=0.012)


def test_off_axis_colour_follows_each_stereo180_channels_own_axis():
    d = base_dict()
    d["voicing"]["off_axis"] = {"freq_hz": [200, 2000, 8000], "angle_deg": [0, 90, 180],
                                "gain_db": [[0, 0, 0], [0, 0, -6], [0, 0, -12]]}
    e = ds.CapsuleEngine(ds.Model.from_dict(d))
    P = ds.Params(pattern=1.0, pattern_rear=1.0, stereo180=True, off_axis_amount=1.0)
    for ang, wl, wr in ((0.0, 0.0, -12.0), (180.0, -12.0, 0.0)):
        L = 20 * math.log10(abs(g(e, P, 8000, ang, ch=0)))
        R = 20 * math.log10(abs(g(e, P, 8000, ang, ch=1)))
        assert L == pytest.approx(wl, abs=0.6) and R == pytest.approx(wr, abs=0.6), ang


def test_tables_hold_their_end_values_outside_the_measured_range():
    t = ds.Table(np.array([1000.0, 4000.0]), np.array([3.0, -2.0]))
    assert t.at(np.array([0.0, 10.0, 999.0]))[0] == pytest.approx(3.0)
    assert t.at(np.array([20000.0]))[0] == pytest.approx(-2.0)
    assert t.at(np.array([2000.0]))[0] == pytest.approx(0.5)


def test_asymmetric_nonlinearity_still_has_unity_small_signal_gain():
    e = ds.CapsuleEngine(ds.Model.from_dict({**base_dict(), "voicing": {"nonlinearity": {"drive_db": 12, "asymmetry": 0.2}}}))
    x0 = sim.tone(1000, FS, amp=0.002)
    y = e.process(x0, np.zeros_like(x0), ds.Params(pattern=1.0))
    assert abs(sim.lockin(y, 1000, FS) / sim.lockin(x0, 1000, FS)) == pytest.approx(1.0, abs=0.01)
