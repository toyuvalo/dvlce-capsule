# Dvlce Capsule - spec v0.1

Goal: an engine that turns the two coincident capsule signals of a dual-capsule microphone into a
virtual polar pattern, plus a documented model-file format. Everything in §2 to §6 is public first-order
acoustics. All code here is our own.

Status (v0.2.0): Python prototype in `proto/` (134 tests, 22 of 22 mutants killed). Real-time C++ core in `core/`
(equal to the prototype within 2e-14 on 10 reference cases). JUCE plugin in `plugin/` (VST3, AU, Standalone),
pluginval strictness 10, the built plugin equal to the prototype within 1e-7 (float audio) (§13).

## 1. Scope
In the open engine: pattern synthesis, capsule correction, proximity effect (remove and add), off-axis
colour, front/rear (180) stereo use, generic voicings, model-file loader.
Not in this repository: any model fitted to a third-party product (§7).

## 2. Signal model
Two back-to-back cardioid capsules on one axis, so close together that they see the same sound field.
- `F` = front capsule, `B` = rear capsule. A plane wave from angle `theta` (0 = on the front axis) gives
  `F = s * (1 + cos theta) / 2` and `B = s * (1 - cos theta) / 2`.
- Omni `O = F + B = s`. Figure-8 `D = F - B = s * cos theta`.
- Output `y = p * O + (1 - p) * D`. Pattern `p`: 1 omni, 0.7 subcardioid, 0.5 cardioid, 0.25 hypercardioid, 0 figure-8.
  Response `p + (1 - p) cos theta`: on-axis gain is 1 for every `p`. Nulls at `acos(-p / (1 - p))` (cardioid 180 deg, figure-8 90 deg, hypercardioid 109.5 deg).
- Flip swaps `F` and `B` (mic turned round). Equivalent to `D -> -D`.
Prototype result: polar error under 0.003 (linear, on-axis = 1) at 124 Hz to 12 kHz, every 30 deg, five patterns.

## 3. Capsule non-idealities and correction
Real capsules differ from the ideal. The model file can describe three effects (all min-phase, in dB):
1. `front_db(f)`, `rear_db(f)`: each capsule's own response (tilt, presence peak, sensitivity mismatch, roll-off).
2. `gradient_db(f)`: the gradient term's gain error `g(f)` against the pressure term (high-frequency
   directivity loss: the cardioids narrow into omni and the null fills in).
Forward model used by the simulator: `out_k = C_k(f) * s * (1/2 +/- 1/2 * g(f) * G(f, r) * cos theta)`.
Correction in the engine: `F' = F / C_F`, `B' = B / C_B`, `O = F' + B'`, `D = (F' - B') / g`.
Corrections are min-phase FIRs designed from the tables (cepstral method), boost capped at 24 dB.
Prototype result: with imperfect capsules and the matching tables the polar error stays under 0.01 and
nulls stay under -40 dB. Without the tables the same recordings give errors above 0.1 and a null above -25 dB.
Where the tables come from: a user measures their own microphone (§8, `provenance.kind = measured-own-microphone`).

## 4. Proximity effect
A spherical wave from distance `r` raises the gradient term: `D -> D * G`, `G(f) = 1 + 1/(j k r)`,
`k = 2 pi f / c`, `c = 343 m/s`. Magnitude `sqrt(1 + 1/(kr)^2)` (textbook). The omni part is unchanged.
- Remove (source distance `r_s`): `D * s tau_s / (1 + s tau_s)`, `tau_s = r_s / c`. First-order high-pass.
- Add (virtual distance `r_v`): `D * (1 + 1/(s tau_v + eps))`, `eps = 10^(-cap/20)`. Cap default 40 dB so DC does not run away.
- Both together are two first-order sections in series (bilinear transform). Both act on `D` only,
  so the effect scales with `(1 - p)`: none for omni, full for figure-8.
Prototype result: simulator equals the textbook formula to 1e-4; removal restores a flat response to
within 0.012 (linear) from 100 Hz to 8 kHz for r = 3, 10, 50 cm; added effect matches the textbook
`|G|` (figure-8) and `|1/2 + G/2|` (cardioid) within 0.35 dB, 124 Hz to 4 kHz, r = 5 cm to 1 m.
Not tested: corner frequencies near Nyquist (the bilinear transform warps them). The C++ uses the same transform; at normal distances the corners are below 100 Hz.

## 5. Off-axis colour
Only a dual-capsule pair at one axis can tell the angle of one source: for a plane wave `D = cos(theta) * O`
in every frequency bin. The engine reads `theta` per STFT bin as `acos(Re(D conj(O)) / |O|^2)` and applies
the model's `off_axis` table (dB over frequency and angle, 0 deg row = 0 dB) scaled by the *Off-axis* control.
This gives a voicing its off-axis colour for one dominant source per bin. With several sources in one bin
the angle is an average. The angle is ambiguous around the axis (it has no sign): colour depends on `theta` only.
Rear virtual mics (stereo 180) use their own axis (`-D`).
Prototype result: tables of -6 dB at 90 deg and -12 dB at 180 deg (8 kHz) reproduced within 0.6 dB; amount 0.5 gives half the dB.
Cost: STFT latency (§10). Optional per model.

## 6. Stereo 180 mode
Two virtual mics from one capsule pair, 180 deg apart: L faces front, R faces back.
`L = p_L * O + (1 - p_L) * D`, `R = p_R * O - (1 - p_R) * D`. Patterns are set per side.
With two cardioids `L = F'` and `R = B'`: a corrected back-to-back pair. With figure-8s it is a front/back
Blumlein-style pair when later mixed or matrixed. Proximity, voicing, off-axis and drive run per channel.
`L + R = (p_L + p_R) O + (p_R - p_L) D`: for equal patterns the sum is omni.
Flip swaps the two sides. Left/right positions *across* the axis cannot come from one pair; that needs a second pair (four inputs).

## 7. Model provenance
| Part | Release | Reason |
|---|---|---|
| Sections 2 to 6, the loader, the generic voicings in `proto/models/` | open | public acoustics, our code, our curves |
| A model fitted to measurements of a third-party product | never in this repository | it is someone else's data and trademarks |
| A model measured from your own real microphone | your choice | your data; keep third-party names out of a shared file |
Rules for this repository:
- No third-party product name, no classic-microphone name, no third-party graphics or plugin ID, anywhere (code, files, docs, UI, presets).
- Model files carry `provenance.kind` and `provenance.distributable`. The loader rejects
  `fitted-to-third-party-plugin` with `distributable: true`. `combine()` of a private and an open file is private.
- Release tooling must refuse any model with `distributable: false` (check on every file in `models/` before a tag).
- Private models live outside this repository, never in `models/`.

## 8. Model-file format (v1, JSON, our schema)
```
{ "format": "dvlce-capsule-model", "version": 1,
  "name": "...", "author": "...", "licence": "PolyForm-Noncommercial-1.0.0",
  "provenance": { "kind": "self-designed | measured-own-microphone | fitted-to-third-party-plugin",
                  "distributable": true },
  "capsule": {                                  // optional: describes the hardware, inverted by the engine
    "front_db":    { "freq_hz": [..], "gain_db": [..] },
    "rear_db":     { "freq_hz": [..], "gain_db": [..] },
    "gradient_db": { "freq_hz": [..], "gain_db": [..] } },
  "voicing": {                                  // optional: the character
    "pattern_eq": { "0.0": TABLE, "0.5": TABLE, "1.0": TABLE },   // EQ per pattern anchor, dB-linear blend between
    "off_axis":   { "freq_hz": [..], "angle_deg": [0, ..], "gain_db": [[..], ..] },   // [angle][freq]
    "nonlinearity": { "drive_db": 6, "asymmetry": 0.05 },
    "output_gain_db": 0 } }
```
Rules (enforced by `Model.from_dict`, 13 rejection tests):
- `freq_hz` positive and strictly ascending; tables of equal length, at least 2 points, finite, `|gain_db| <= 40`.
  Interpolation is linear in dB over log frequency; end values hold outside the range.
- `pattern_eq` keys are numbers in 0..1. A voicing may have any number of anchors; one anchor is flat in `p`.
- `off_axis`: `angle_deg` ascending from 0 to at most 180; the 0 deg row is the 0 dB reference (subtracted on load).
- `nonlinearity`: `y = (tanh(a (x + b)) - tanh(a b)) / (a (1 - tanh^2(a b)))`, `a = 10^(drive_db/20)`, `b = asymmetry`.
  Unity small-signal gain; odd harmonics from the tanh, even harmonics from `b`; DC removed (5 Hz one-pole). The C++ runs it at the session rate, like the prototype (no oversampling yet, §12).
- `capsule` and `voicing` can be in separate files. The plugin has two slots: *Hardware* and *Voicing*.
- Unknown keys are ignored (room for later versions); a higher `version` is an error.
Example files: `proto/models/` (`capsule-example`, `generic-neutral`, `generic-bright`, `generic-warm`). Made by `proto/make_models.py`. Same licence as the code (§11).

## 9. Controls
| Control | Range | Note |
|---|---|---|
| Pattern | 0 figure-8 .. 0.5 cardioid .. 1 omni, steps of 0.01 | the display names figure-8, hypercardioid, cardioid, subcardioid, omni |
| Pattern rear | same | Stereo 180 only |
| Mode | Mono / Stereo 180 | Mono copies the one virtual mic to both outputs |
| Flip | off / on | swaps the two inputs (mic turned round) |
| Source distance | Off (0), up to 200 cm | removes the proximity effect of a source at this distance |
| Virtual distance | Off (0), up to 200 cm | adds the proximity effect of this distance |
| Proximity cap | 10 .. 40 dB | limit of the added bass boost |
| Off-axis | 0 .. 100 % | needs a voicing with an `off_axis` table |
| Drive | off / on | needs a voicing with a `nonlinearity` |
| Output | -24 .. +24 dB | |
| Hardware | None, Example capsule pair, User file | capsule tables of a model file |
| Voicing | None, Neutral, Bright, Warm, User file | voicing part of a model file |
Inputs: ch1 = front capsule, ch2 = rear capsule (stereo input bus). Outputs: mono or stereo.
The Hardware slot uses only the `capsule` part of its file, the Voicing slot only the `voicing` part.
A user file is checked by the same rules as §8 and saved inside the session. A refused file shows the reason.
Pattern EQ: the C++ designs it on the 0.01 pattern grid at model load and crossfades over 64 samples on a change.

## 10. Latency and cost
| Stage | Latency | Note |
|---|---|---|
| Capsule correction, pattern mix, proximity, voicing EQ | 0 | min-phase FIRs of 1024 taps, zero-latency partitioned convolution (64 direct + 15 x 64 by FFT) |
| Off-axis colour | 1022 samples at any rate (STFT 1024, hop 256) | only when the voicing has an off_axis table; then always on (also at 0 %), so the latency is constant per model |
| Nonlinearity | 0 | at the session rate |
The 1022 is exact: an STFT sample is complete once the newest frame that covers it with a non-zero window weight is done.
The plugin reports it to the host. CPU, one core of the studio PC (Windows, MSVC, 48 kHz), as speed over real time:
unity 3600x, capsule tables 71x, pattern EQ 171x, pattern EQ + off-axis 47x, heaviest stereo case 16x.
The off-axis STFT is the largest cost (4 FFTs of 1024 per 256 samples per output).

## 11. Licence
PolyForm Noncommercial 1.0.0 (`LICENSE`), for the code, this spec and the example models.
- Free for any non-commercial use: personal, hobby, research, education, charities, public bodies. You may change it and share it.
- Commercial use (selling it, shipping it inside a paid product, using it for paid work) needs a separate licence from the author. See `COMMERCIAL.md`.
- This is source-available, not OSI "open source": an OSI licence cannot forbid commercial use.
- The plugin links JUCE 8, which has its own licence (AGPLv3 or a JUCE licence; README "Building the plugin").
- Contributions: see `CONTRIBUTING.md` (they must allow the author to keep offering commercial licences).
This is a summary; the `LICENSE` text governs.

## 12. Not done, or unknown
- Real capsule data: none yet. The capsule tables in `models/capsule-example.json` are invented.
- Oversampling of the nonlinearity (it aliases a little at high drive).
- Cheaper off-axis STFT (real FFTs, shared O and D spectra between the two outputs).
- Installers and code signing (macOS builds are not notarised).
- Multiple sources per bin for off-axis colour (§5) and moving sources.
- Prototype tests run at 48 kHz only; the C++ also runs at 44.1, 96 and 192 kHz (finite, bounded output) but is not compared there.

## 13. Verification
| Check | Where | Result (v0.2.0) |
|---|---|---|
| Prototype suite + mutation | `proto/`: `pytest`, `mutate.py` | 134 pass, 22 of 22 mutants killed |
| C++ core vs prototype | `core/tests/` (`build-win.bat`, `build-mac.sh` run it) | 10 cases, max error 1.75e-14 |
| C++ model-file rules | same | the 13 prototype rejections + malformed JSON |
| Real-time properties | same | block-size invariant (bit-exact), silence in = silence out, parameter changes bounded |
| Plugin validation | pluginval `--strictness-level 10` | pass (Windows VST3) |
| Built plugin vs prototype | `bench/parity_plugin.py <plugin>` | 5 cases, max error 8.9e-8 (float32 I/O) |
