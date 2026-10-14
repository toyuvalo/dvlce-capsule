"""Writes the example model files in models/.  Every curve here is designed by us, not measured
from any product.  Run:  python make_models.py"""
import pathlib

import numpy as np

from dvlce_capsule import Model, Table

OUT = pathlib.Path(__file__).resolve().parent / "models"
FQ = np.array([20, 50, 100, 200, 500, 1000, 2000, 4000, 6000, 8000, 10000, 12500, 16000, 20000], float)


def tab(db):
    return Table(FQ, np.array(db, float))


def bump(f0, db, q=1.0):
    x = np.log2(FQ / f0)
    return db * np.exp(-(x / (0.5 / q)) ** 2)


def hardware_example():
    """A deliberately imperfect capsule pair, to exercise the correction path."""
    front = bump(9000, 3.0) - 1.0 * (FQ < 60) + 0.5
    rear = bump(14000, -4.0) - 1.0 + (FQ < 60) * -2.0
    grad = -3.0 * np.clip(np.log2(FQ / 2000.0), 0, None) / 3.0
    return Model(name="example-imperfect-capsule-pair", author="Dvlce", licence="PolyForm-Noncommercial-1.0.0",
                 front=tab(front), rear=tab(rear), gradient=tab(grad))


def voicing(name, omni, card, fig8, off=0.0, drive=None, out=0.0):
    pe = {0.0: tab(fig8), 0.5: tab(card), 1.0: tab(omni)}
    ang = np.array([0, 45, 90, 135, 180], float)
    hf = np.clip(np.log2(FQ / 2000.0), 0, None)
    oa = -np.outer(ang / 180.0, hf) * off - np.outer(ang / 180.0, np.ones_like(FQ)) * 0.0
    m = Model(name=name, author="Dvlce", licence="PolyForm-Noncommercial-1.0.0", pattern_eq=pe,
              off_axis=(FQ, ang, oa), output_gain_db=out)
    if drive is not None:
        m.nonlinearity = {"drive_db": drive[0], "asymmetry": drive[1]}
    return m


def main():
    OUT.mkdir(exist_ok=True)
    z = np.zeros_like(FQ)
    hardware_example().save(OUT / "capsule-example.json")
    voicing("generic-neutral", z, z, z).save(OUT / "generic-neutral.json")
    voicing("generic-bright", bump(10000, 3.0), bump(10000, 4.0), bump(10000, 3.0) + bump(100, 1.0), off=1.5).save(OUT / "generic-bright.json")
    voicing("generic-warm", -bump(10000, 2.5) + bump(150, 2.0), -bump(9000, 2.0) + bump(200, 2.0),
            -bump(8000, 2.0) + bump(250, 1.5), off=2.0, drive=(6.0, 0.05), out=-1.0).save(OUT / "generic-warm.json")


if __name__ == "__main__":
    main()
