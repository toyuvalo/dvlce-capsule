# Dvlce Capsule

Dual-capsule microphone engine. Front and rear capsule in, virtual polar pattern out:
omni, cardioid, figure-8 and everything between. Also capsule correction, proximity effect
(remove and add), off-axis colour and a front/back stereo mode.

Status: v0.1.0, Python prototype with 134 tests (`proto/`). No plugin build yet.
The full design is in [`SPEC.md`](SPEC.md). The model-file format is in SPEC §8.

## Run the prototype

```
cd proto
pip install -r requirements.txt
python -m pytest -q          # 134 tests
python mutate.py             # 22 deliberate bugs; each one must make the suite fail
python make_models.py        # rewrites models/*.json (generic example curves)
```

## Licence

**Free for non-commercial use. Commercial use needs a paid licence.**

The code, the spec and the example models are under the
[PolyForm Noncommercial License 1.0.0](LICENSE).

- You may use, change and share it for any non-commercial purpose:
  personal projects, hobby, study, research, education, charities and public bodies.
- To sell it, ship it in a paid product, or use it in commercial work,
  you need a commercial licence from the author first. See [`COMMERCIAL.md`](COMMERCIAL.md).

This is source-available software, not OSI "open source". The `LICENSE` text governs; this section is a summary.

Required Notice: Copyright 2026 Yuval Olsha (https://github.com/toyuvalo)
