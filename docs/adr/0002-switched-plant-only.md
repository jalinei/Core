---
status: accepted
date: 2026-11-05
---

# L1 uses the switched plant model only, with no averaged variant

The procedure originally planned two plant models: an averaged one for long cases (the 40 s startup ramp, MPPT, observer timing) and a switched one for short windows. We decided, at Jean Alinei's request and with Luiz Villa's agreement, to keep only the switched model he built in Pulsim (`tests/l1/plant/full_model.pulsim`). Long cases are made affordable by shortening the production startup instead: `app::startup_ramp_rate` went from 10 V/s to 200 V/s (see the [worklog](../testing/worklog.md)), so the full 0 → 400 V startup lasts about 2 s instead of 40 s.

## Considered options

- **Averaged plus switched models** (the original plan): rejected. A second model must be written, given its own parameters and checked against the first. Every disagreement between them becomes a question about the model, not about the firmware.
- **Switched model only, with the 10 V/s ramp**: rejected. A 40 s run at a 50 ns step is about 800 million electrical steps, several hours per run, so the full startup would stay `BLOCKED` in practice.
- **Switched model only, with a 200 V/s production ramp**: chosen. One model, one set of parameters. The full startup takes about 2.5 s of simulated time: about 30 minutes on Jean's machine, about 1.3 hours on Luiz's laptop (cost measured 2026-11-05, after the stamp cache in `runtime.py`).

## Consequences

- Every L1 verdict comes from the same switched circuit, at a step small enough to resolve the 200 kHz PWM (50 ns by default).
- Long runs remain expensive. They run on demand (for example `tests/l1/scenarios/dcdc_startup_full.json`) and are not part of the default campaign; a criterion that has not been run is reported `BLOCKED`, never assumed.
- The 200 V/s ramp is a production change, not a simulation setting: it also applies on the board and needs a hardware check (worklog).
- Cases that need many seconds of grid time (MPPT settling, repeated burst cycles) still cost minutes to hours each. If that becomes the bottleneck, revisit this decision rather than shortening other production timings without a record.
- The variable-step engines of Pulsim 2.0.0 (`trbdf2`, `dsed`) refuse C-Blocks, so every L1 run is fixed step. Revisit this decision when Pulsim runs C-Blocks with a variable step: it would shorten long runs without a second model.
- The synthetic PV panel with a diode in the circuit is about 5× slower per step than the ideal source (the solver iterates on its diode equation). Long PV runs use the Lambert W panel instead: same single-diode equation, explicit, about 4.5× faster, checked against the diode panel by L1-INT-07 in every campaign ([PV guide](../testing/pv-model-run.md#lambert-w-panel-for-long-runs)).
