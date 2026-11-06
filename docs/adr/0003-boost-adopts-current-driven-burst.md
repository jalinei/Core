---
status: accepted
date: 2026-11-06
---

# The boost object adopts Andres Vazquez Sieber's control: current-driven burst, no reset at burst, his PI gains

Andres Vazquez Sieber's boost controller (`core-micro-verter-boost`, commit `43e249e`) was tested on the board on 2026-11-06 ([bench evidence](../testing/bench/2026-11-06_andres-boost/README.md)) and is the one to ship. Luiz Villa decided to amend Core's `BoostConverter` to take its control approach, keeping Core's structure: the modes and the leg split. The decision was taken after finding #3, where Core's boost restarts its current loop from duty 0.05 at every mode change and every burst pause, and needs over 100 ms to reach the duty that delivers power.

## What changes

| | Before (Core today) | After |
|---|---|---|
| Light load | Voltage hysteresis: pause both legs at ≥ 405 V, resume at ≤ 395 V | Current-driven pulse skipping (Andres): when the current loop sits at minimum duty with the current above its reference, the burst duty (skipped PWM periods out of a burst period) goes up one step per tick; when the current is below its reference, it goes down. The boost object computes the burst command; the application applies it with the HRTIM burst mode |
| Controller state at burst | All PIDs reset on every pause | Kept across burst ↔ continuous transitions |
| PI gains | Voltage Kp 0.05 A/V, Ti 100 ms; current Kp 0.000215, Ti 75.2 µs | Andres's: current Kp 0.002712, Ti 341.3 µs; voltage Kp = 0.433·Vbus/Vin (scheduled with the measured Vin, clamped 24–48 V), Ti 4.57 ms |
| STARTUP | Bus reference ramp at 200 V/s, current ceiling 1 A, leg 1 only | BUSREG toward 400 V with a small current clamp (value to confirm, Andres's bench used 0.25 A); switching to BUSREG resets nothing |
| BUSREG / MPPT current | Total reference bounded 0–2 A, leg 1 up to 1 A | Bounded at the rated input current (value to confirm, about 11 A for 400 W at 36 V); leg 1 first, leg 2 takes the excess |
| Burst and legs | Both legs paused | Burst only while leg 2 is off (light load runs on leg 1) |
| Over-voltage / over-current | Burst pause at 405 V | Andres's thresholds: full burst above 440 V; stop above 460 V or 8 A per leg |

Kept: the modes IDLE / STARTUP / BUSREG / MPPT as the application's interface, and the leg split. Not adopted: the 100 Hz PR on the current loop (in Andres's code it is always on, which looks unintended; to revisit for grid-connected operation).

## Considered options

- **Keep Core's boost and retune it** (L1-BST-09 compares tunings): rejected as the shipping path; Andres's controller is already validated on the bench, and the restart behaviour of #3 is structural, not a tuning problem.
- **Ship Andres's `main.cpp` as is**: rejected; it is a bench program (serial menu, one leg at a time, gains changed only in IDLE, a modified control library with `setKp`), and it would lose the tests and the application structure built around `BoostConverter`.
- **Burst by software pulse skipping** (enable/disable the legs per 100 µs tick): rejected; 20 PWM periods of granularity, where the HRTIM burst mode skips single periods.

## Consequences

- Production change under the procedure's rules: L0 tests written first and failing (new burst, no reset, gains, protection), then the change with a worklog entry, then L1, then a hardware check against the bench captures above.
- The contract becomes **app-v2**: a burst command (skipped periods, burst period) joins the outputs, and the L1 PWM adapter models the HRTIM burst mode. The ADC sampling instant fix from #3 (L1-MOD-08.b) goes into the same version.
- Tests tied to the old behaviour are retired or rewritten: L0-BST-05 (ramp), L0-BST-06 (1 A ceiling), L0-BST-07 (voltage burst), L1-APP-02 and L1-APP-04 and their L1-BST counterparts. ADR 0002's argument for the 200 V/s ramp (a 2 s start-up keeps L1 runs affordable) is replaced by the current clamp, which sets the start-up time instead.
- The current-clamp values (STARTUP, rated) are to be confirmed with Andres before the change is merged.
