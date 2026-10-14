"""Prototype reference outputs for the C++ test.   python make_refs.py <out.bin>
File: int32 count, then per case:
  int32 len + model spec bytes ("" unity, "a" built-in, "a+b" = combine(a, b)),
  10 float64 params (pattern, pattern_rear, stereo180, flip, source_m, virtual_m, cap_db, off_axis, nonlin, out_db),
  int32 n_out, int32 n, float64 front[n], float64 rear[n], float64 expected[n_out * n]."""
import pathlib
import struct
import sys

import numpy as np

PROTO = pathlib.Path(__file__).resolve().parents[2] / "proto"
sys.path.insert(0, str(PROTO))
import capsule_sim as sim  # noqa: E402
import dvlce_capsule as dc  # noqa: E402

FS = 48000
N = FS // 2
rng = np.random.default_rng(7)


def model(spec):
    if not spec:
        return dc.Model()
    parts = [dc.Model.load(PROTO / "models" / f"{s}.json") for s in spec.split("+")]
    return parts[0] if len(parts) == 1 else dc.combine(parts[0], parts[1])


def source():
    t = np.arange(N) / FS
    return 0.2 * rng.standard_normal(N) + 0.3 * np.sin(2 * np.pi * 220.0 * t) * (t > 0.1)


P = dc.Params
cases = [  # (model spec, params, angle, distance, hardware used to simulate the capsules)
    ("", P(pattern=0.5), 30.0, None, None),
    ("capsule-example", P(pattern=0.25), 60.0, None, "capsule-example"),
    ("capsule-example+generic-warm", P(pattern=0.4, source_distance_m=0.1, virtual_distance_m=0.3,
                                       proximity_cap_db=30.0, output_gain_db=-3.0), 20.0, 0.1, "capsule-example"),
    ("generic-bright", P(pattern=0.7, off_axis_amount=0.7), 120.0, None, None),
    ("generic-bright", P(pattern=0.5, pattern_rear=0.0, stereo180=True, flip=True, off_axis_amount=1.0), 45.0, None, None),
    ("generic-neutral", P(pattern=1.0), 90.0, None, None),
    ("generic-warm", P(pattern=0.0, nonlinearity=False, virtual_distance_m=0.05), 0.0, None, None),
    ("", P(pattern=0.3, pattern_rear=0.8, stereo180=True, flip=True, source_distance_m=0.5), 150.0, 0.5, None),
    ("capsule-example+generic-bright", P(pattern=0.6, pattern_rear=0.25, stereo180=True, off_axis_amount=0.5,
                                         source_distance_m=0.2, virtual_distance_m=0.6), 75.0, 0.2, "capsule-example"),
    ("generic-warm", P(pattern=0.85, off_axis_amount=0.3, output_gain_db=4.0), 10.0, None, None),
]

with open(sys.argv[1], "wb") as fh:
    fh.write(struct.pack("<i", len(cases)))
    for spec, p, ang, r, hw in cases:
        F, B = sim.capsules(source(), FS, ang, r, model(hw) if hw else None)
        y = dc.CapsuleEngine(model(spec), FS).process(F, B, p)
        y = np.atleast_2d(y)
        b = spec.encode()
        fh.write(struct.pack("<i", len(b)) + b)
        fh.write(struct.pack("<10d", p.pattern, p.pattern_rear, float(p.stereo180), float(p.flip),
                             p.source_distance_m or 0.0, p.virtual_distance_m or 0.0, p.proximity_cap_db,
                             p.off_axis_amount, float(p.nonlinearity), p.output_gain_db))
        fh.write(struct.pack("<ii", y.shape[0], N))
        for a in (F, B, y):
            fh.write(np.ascontiguousarray(a, dtype="<f8").tobytes())
print(f"wrote {len(cases)} cases to {sys.argv[1]}")
