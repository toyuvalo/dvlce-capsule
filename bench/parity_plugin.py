"""End-to-end parity: render the BUILT plugin (VST3 or AU) with pedalboard and compare it with the
Python prototype on simulated two-capsule recordings.   python bench/parity_plugin.py <Dvlce Capsule.vst3>
Needs: pip install pedalboard numpy scipy.  Pass = max |plugin - prototype| < 2e-5 (float32 audio I/O)."""
import pathlib
import sys
import time

import numpy as np
import pedalboard

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "proto"))
import capsule_sim as sim  # noqa: E402
import dvlce_capsule as dc  # noqa: E402

FS = 48000
N = FS
LATENCY = 1022          # samples, when the voicing has an off-axis table
TOL = 2e-5
plug = pedalboard.load_plugin(sys.argv[1])
print("plugin:", plug.name, "| parameters:", ", ".join(plug.parameters))
HW = {"None (ideal capsules)": None, "Example capsule pair": "capsule-example"}
VOICING = {"None (flat)": None, "Neutral": "generic-neutral", "Bright": "generic-bright", "Warm": "generic-warm"}


def model(hw, voicing):
    m = lambda s: dc.Model.load(ROOT / "proto" / "models" / f"{s}.json") if s else dc.Model()
    return dc.combine(m(HW[hw]), m(VOICING[voicing]))


cases = [  # plugin settings, prototype params, source angle and distance, capsules simulated with the example pair?
    (dict(hardware="None (ideal capsules)", voicing="None (flat)", pattern=0.5), dc.Params(pattern=0.5), 30.0, None, False),
    (dict(hardware="Example capsule pair", voicing="Warm", pattern=0.4, source_distance=10.0, virtual_distance=30.0,
          proximity_cap_db=30.0, output_db=-3.0),
     dc.Params(pattern=0.4, source_distance_m=0.1, virtual_distance_m=0.3, proximity_cap_db=30.0, output_gain_db=-3.0), 20.0, 0.1, True),
    (dict(hardware="None (ideal capsules)", voicing="Bright", mode="Stereo 180", pattern=0.5, pattern_rear=0.0, flip=True, off_axis=100.0),
     dc.Params(pattern=0.5, pattern_rear=0.0, stereo180=True, flip=True, off_axis_amount=1.0), 45.0, None, False),
    (dict(hardware="None (ideal capsules)", voicing="Bright", pattern=0.7, off_axis=70.0), dc.Params(pattern=0.7, off_axis_amount=0.7), 120.0, None, False),
    (dict(hardware="Example capsule pair", voicing="Neutral", mode="Stereo 180", pattern=0.3, pattern_rear=0.8, drive=False),
     dc.Params(pattern=0.3, pattern_rear=0.8, stereo180=True, nonlinearity=False), 150.0, None, True),
]
DEFAULTS = dict(mode="Mono", pattern=0.5, pattern_rear=0.5, flip=False, source_distance=0.0, virtual_distance=0.0,
                proximity_cap_db=40.0, off_axis=0.0, drive=True, output_db=0.0)
# float parameters: (start, end, skew) of the plugin's NormalisableRange; set as normalised raw values
RANGES = dict(pattern=(0, 1, 1), pattern_rear=(0, 1, 1), source_distance=(0, 200, 0.4), virtual_distance=(0, 200, 0.4),
              proximity_cap_db=(10, 40, 1), off_axis=(0, 100, 1), output_db=(-24, 24, 1))


def set_param(name, v):
    # VST3 appends the unit to some names (proximity_cap_db); AU does not (proximity_cap)
    key = name if name in plug.parameters else name.removesuffix("_db")
    if name in RANGES:
        a, b, skew = RANGES[name]
        plug.parameters[key].raw_value = ((v - a) / (b - a)) ** skew
    else:
        setattr(plug, key, v)

rng = np.random.default_rng(3)
fails = 0
for k, (settings, p, ang, r, hw_sim) in enumerate(cases):
    for name, v in {**DEFAULTS, **settings}.items():
        set_param(name, v)
    time.sleep(1.5)     # the model swap is built on the plugin's background thread
    t = np.arange(N) / FS
    x = 0.2 * rng.standard_normal(N) + 0.3 * np.sin(2 * np.pi * 330.0 * t)
    F, B = sim.capsules(x, FS, ang, r, dc.Model.load(ROOT / "proto" / "models" / "capsule-example.json") if hw_sim else None)
    m = model(settings["hardware"], settings["voicing"])
    want = np.atleast_2d(dc.CapsuleEngine(m, FS).process(F, B, p))
    got = plug(np.stack([F, B]).astype(np.float32), FS, buffer_size=512, reset=True).astype(float)
    if not p.stereo180:
        assert np.array_equal(got[0], got[1]), "mono mode must copy the output to both channels"
        got = got[:1]
    lat = LATENCY if (m.off_axis is not None) else 0
    errs = {}
    for lag in sorted({0, lat}):   # pedalboard may or may not compensate the reported latency
        a, b = got[:, lag:], want[:, :N - lag]
        errs[lag] = float(np.max(np.abs(a[:, 2048:N - 4096] - b[:, 2048:N - 4096])))
    lag = min(errs, key=errs.get)
    ok = errs[lag] < TOL
    fails += not ok
    print(f"case {k}: {settings['hardware']} + {settings['voicing']}, {settings.get('mode', 'Mono')}: "
          f"max error {errs[lag]:.2e} at lag {lag} (peak {np.max(np.abs(want)):.2f})  {'PASS' if ok else 'FAIL'}")
print("ALL PASS" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
