"""Mutation check: break the engine in one place at a time; the test suite must go red each time.
    python mutate.py          (runs the suite once per mutant, ~3 s each)
A surviving mutant means a test is missing or too loose."""
import pathlib
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE / "dvlce_capsule.py"
MUTANTS = [
    ("gradient correction skipped", "        if hg is not None:\n            D = apply_fir(hg, D)", "        if False:\n            D = apply_fir(hg, D)"),
    ("rear capsule correction skipped", "        if hb is not None:\n            B = apply_fir(hb, B)", "        if False:\n            B = apply_fir(hb, B)"),
    ("front capsule correction skipped", "        if hf is not None:\n            F = apply_fir(hf, F)", "        if False:\n            F = apply_fir(hf, F)"),
    ("omni sum mis-weighted", "O, D = F + B, F - B", "O, D = F + 0.97 * B, F - B"),
    ("pattern mix swapped", "y = pat * O + (1 - pat) * sgn * D", "y = (1 - pat) * O + pat * sgn * D"),
    ("removal distance doubled", "tau = p.source_distance_m / C_AIR", "tau = 2 * p.source_distance_m / C_AIR"),
    ("proximity add inverted", "signal.bilinear([tau, 1.0 + eps], [tau, eps], self.fs)", "signal.bilinear([tau, eps], [tau, 1.0 + eps], self.fs)"),
    ("proximity removal off", "if p.source_distance_m:", "if False:"),
    ("proximity cap ignored", "eps = 10 ** (-p.proximity_cap_db / 20)", "eps = 1e-6"),
    ("rear channel sign", "outs.append((p.pattern_rear, -1.0))", "outs.append((p.pattern_rear, +1.0))"),
    ("flip ignored", "        if p.flip:\n            front, rear = rear, front", "        if False:\n            front, rear = rear, front"),
    ("pattern_eq blend reversed", "w = (p - ks[i]) / (ks[i + 1] - ks[i])", "w = 1 - (p - ks[i]) / (ks[i + 1] - ks[i])"),
    ("min-phase fold wrong", "fold[1:n // 2] = 2 * cep[1:n // 2]", "fold[1:n // 2] = cep[1:n // 2]"),
    ("off-axis angle folded", "theta = np.degrees(np.arccos(np.clip(c, -1.0, 1.0)))", "theta = np.degrees(np.arccos(np.clip(np.abs(c), 0.0, 1.0)))"),
    ("off-axis amount ignored", "gain = 10 ** (amount * g / 20)", "gain = 10 ** (g / 20)"),
    ("rear off-axis not mirrored", "y = self._off_axis(y, O, sgn * D, p.off_axis_amount)", "y = self._off_axis(y, O, D, p.off_axis_amount)"),
    ("nonlinearity slope unnormalised", "/ slope\n", "/ a\n"),
    ("nonlinearity DC not blocked", "if b > 0:", "if False:"),
    ("nonlinearity ignores switch", "if p.nonlinearity:", "if True:"),
    ("third-party model may be distributable", 'if kind == "fitted-to-third-party-plugin" and dist:', "if False:"),
    ("combine ignores private flag", "distributable=hardware.distributable and voicing.distributable", "distributable=hardware.distributable"),
    ("table ordering not checked", "np.any(np.diff(f) <= 0)", "False"),
]


def main():
    orig = SRC.read_text(encoding="utf-8")
    survivors = []
    for name, old, new in MUTANTS:
        if orig.count(old) != 1:
            print(f"BAD MUTANT (pattern found {orig.count(old)}x): {name}")
            survivors.append(name)
            continue
        with tempfile.TemporaryDirectory() as td:
            shutil.copytree(HERE, td, dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", ".pytest_cache"))
            pathlib.Path(td, "dvlce_capsule.py").write_text(orig.replace(old, new), encoding="utf-8")
            r = subprocess.run([sys.executable, "-m", "pytest", "-q", "-x", "-p", "no:cacheprovider"], cwd=td,
                               capture_output=True, text=True)
        killed = r.returncode != 0
        first = next((ln for ln in r.stdout.splitlines() if ln.startswith("FAILED")), "")
        print(f"{'killed  ' if killed else 'SURVIVED'} {name}   {first[:90]}")
        if not killed:
            survivors.append(name)
    print(f"{len(MUTANTS) - len(survivors)}/{len(MUTANTS)} killed")
    return 1 if survivors else 0


if __name__ == "__main__":
    sys.exit(main())
