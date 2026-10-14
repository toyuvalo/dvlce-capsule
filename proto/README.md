# Dvlce Capsule - Python prototype

Model of `../SPEC.md`: two coincident capsule signals in, virtual polar pattern out. No third-party data.

```
pip install -r requirements.txt
python -m pytest -q          # 134 tests, ~3 s
python mutate.py             # breaks the engine in 22 places, one at a time; every one must turn the suite red
python make_models.py        # rewrites models/*.json (generic example curves)
```

- `dvlce_capsule.py` - engine, model loader and validator (`Model`, `CapsuleEngine`, `Params`).
- `capsule_sim.py` - simulated recordings: plane or spherical wave, angle, distance, imperfect capsules.
- `test_dvlce_capsule.py` - polar accuracy, null depth, proximity vs the textbook formula, model files, stereo 180.
- Offline (whole array) prototype. Latency and cost notes: SPEC §10.
