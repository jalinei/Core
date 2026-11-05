# Studies: exploratory runs, overnight

A **study** is a list of scenario runs that answer questions, not a campaign that gives verdicts. Its runs go in parallel, longest first, and one review page is rewritten after each finished run. Anything worth a verdict later becomes a catalogue criterion, with its limits decided then ([procedure §7.4](../../../docs/testing/test-procedure.md#74-studies)).

## Launch and follow

```bash
# From Core: detached, survives closing VS Code, keeps the machine awake (stay on mains power)
tests/l1/study.sh tests/l1/studies/night-2026-11-06.json            # all cores
tests/l1/study.sh tests/l1/studies/night-2026-11-06.json --jobs 6   # leave two cores free

journalctl --user -fu l1-study      # progress: one line per finished case
systemctl --user stop l1-study      # stop; finished cases and their pages are kept
```

In the morning, open `tests/out/study_<name>_<date>_<time>/report.html`. The summary table at the top lists every case: done, running (with its start time and estimate), queued or failed (with the last line of its log). Each finished case then has its four parts: conditions, model snapshot, results, figures (procedure §8.5).

## Study file

```json
{"name": "…", "title": "…", "purpose": "…",
 "cases": [{"folder": "pv-day", "base": "pv_mppt.json",
            "set": {"title": "…", "purpose": "…", "duration_s": 9.5, "commands": […], "light": […]},
            "figures": [{"title": "…", "signal": "VDCBus", "unit": "V", "reference": "v_bus_ref", "panels": […]}]}]}
```

- `base` is a scenario in `tests/l1/scenarios/`; `set` replaces its keys. Every case needs a `title` and a `purpose` stating its initial state (precharged or discharged bus), loads and commands.
- Exploratory keys (study runs only): `open_loop_duty` (both legs held at a duty, firmware not obeyed), `grid` (steps of `v_rms`/`f_hz`, phase-continuous), `light` with `pv_model: lambertw`, `pv_initial_bus_V`.
- `figures` use the review-plot format (`tests/l1/review_plots.json`) without `scenario`; signals come from `control.csv` (per control tick) or `waveforms.npz` (electrical).
- Long runs: set `store_period_s` to 1e-4 s or more (a 9.5 s run at 1 µs would store 9.5 million points per signal).

## Cost on Luiz's laptop (i7-1185G7), per simulated second, one run alone

| Plant | Step | Cost |
|---|---|---|
| Ideal 40 V source, switching | 50 ns | ~32 min |
| Lambert W PV panel, switching | 50 ns | ~50 min |
| Diode PV panel, switching | 50 ns | ~3.8 h (avoid for long runs) |
| No switching (IDLE, grid only) | 1 µs | ~1.5 min |

With all cores busy, each run is slower (shared memory bandwidth); count about +25 %. `study.py` orders runs by these estimates, longest first.

## Studies

| File | Content |
|---|---|
| `night-2026-11-06.json` | Compressed PV day (turn-on → MPPT → cloud → sundown → IDLE) on the Lambert W panel; the #3 dead zone on real sequences (full startup, STARTUP → BUSREG, no load, 300 ms, 1 s burst, open-loop sweep 0.36–0.46); grid sync at 49 / 51 Hz and 210 / 250 V, and faults at 195 V, 265 V, 47 Hz, 53 Hz |
