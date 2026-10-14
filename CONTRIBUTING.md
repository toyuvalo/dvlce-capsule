# Contributing

Issues and pull requests are welcome.

The author offers this project under two licences: PolyForm Noncommercial 1.0.0 for everyone, and
separate commercial licences on request. To keep that possible, every contribution must allow it.

By opening a pull request you agree that:
1. The contribution is your own work, or you have the right to submit it.
2. You license your contribution to Yuval Olsha under the PolyForm Noncommercial License 1.0.0, and you also
   grant Yuval Olsha a perpetual, worldwide, non-exclusive, royalty-free, irrevocable licence to use, change,
   sublicense and distribute it under any terms, commercial licences included.
3. Your contribution contains no third-party code, measured data, product names, graphics or plugin IDs
   (see SPEC §7).

Sign each commit with `git commit -s` to confirm this.

Before you open a pull request, run `python -m pytest -q` and `python mutate.py` in `proto/`. Both must pass.
