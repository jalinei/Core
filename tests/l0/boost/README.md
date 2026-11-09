# L0-BST: boost converter controller

Open-loop tests of `BoostConverter` ([src/boost_converter/](../../../src/boost_converter/)). Catalogue: [procedure §6.3](../../../docs/testing/test-procedure.md#63-boost).

## Run

```bash
bash tests/l0/boost/run.sh       # this object only
tests/run_all.sh --l0            # with the record
```

## What is compiled

| Source | Flags |
|---|---|
| `test_boost.cpp` | strict (`-Werror`) |
| `src/boost_converter/boost.cpp` | strict |
| `owntech/lib/USB/control_library/src/pid.cpp` | library (no `-Werror`) |
| `owntech/lib/USB/control_library/src/{filters,pr,fir,trigo}.cpp` | library (no `-Werror`) |

## How the test drives the controller

Each section sets measurements with `setILow1/2`, `setVLow`, `setVBus`, references with `setILowRef`/`setVBusRef`, then calls `coldLoop()` (voltage loop or MPPT, `voltage.Ts` = 1 ms) and `hotLoop()` (current loop, `current.Ts` = 100 µs) and checks the outputs. Nothing is simulated: a value such as 52 V on the bus is simply set.

Test tuning, chosen for exact arithmetic rather than the hardware: voltage PID `Kp` 0.1, `Ti` 1, bounds 0–12 A; current PID `Kp` 0.1, `Ti` 1, bounds 0–0.95; leg 1 current limit 5 A. Sections L0-BST-05 to 08 use the application's structure instead (ADR 0003): voltage PID `Kp` 0.4333·400/36, `Ti` 4.57 ms, bounds 0–11 A; current PID `Kp` 0.002712, `Ti` 341.3 µs, duty 0.05–0.60; leg 1 limit 5.5 A; STARTUP clamp 0.25 A; burst period 200; full burst 440 V, trip 460 V / 8 A.

> **Order matters.** The sections share one controller and each starts from the state the previous one left. Do not reorder them, and keep every call that has side effects (`hotLoop`, `coldLoop`, `set*`) in place, including those inside a check expression.

## Criteria

| Test case | Criteria | Checks |
|---|---|---|
| L0-BST-01 | .a, .c | Uninitialized controller is inert; NaN measurement ignored; invalid leg 1 limit or duty bound rejects init |
| L0-BST-02 | .a – .c | Leg 1 up to its limit, leg 2 takes the excess; PI duties; leg 2 integral cleared before reuse |
| L0-BST-03 | .a, .b | Saturated voltage loop gives 12 A split 5/7 A, duties stay in [0, 0.95] for 10 000 ticks; IDLE clears everything |
| L0-BST-04 | .a – .d | MPPT direction from power, ±0.1 A step, leg 1 priority, turnaround at bounds, independence from voltage loop |
| L0-BST-05 | .a – .c | STARTUP current clamp on leg 1; STARTUP → BUSREG resets nothing (mirror PID); IDLE and entry from IDLE reset all, burst starts fully skipped |
| L0-BST-06 | .a – .e | Current-driven burst: leaving full skipping, entering at minimum duty, following the current error within [0, period], PID state kept (mirror PID), none while leg 2 runs, zero period rejected |
| L0-BST-07 | .a – .d | Full burst above 440 V with PID held; trip above 460 V or 8 A, latched until IDLE; inconsistent settings rejected |
| L0-BST-08 | .a – .c | Voltage Kp scheduled with VBusRef / VLow (clamped), STARTUP/BUSREG only, without reset (mirror PID); inconsistent schedules rejected |
| L0-BST-10 | .a – .d | 100 Hz notch attenuation, DC preservation, bypass and raw-voltage protection; resonator selectivity, correction on both legs, combined duty bounds and reset; PI/resonator state held through burst; library buffer lifecycle on reconfiguration |

L0-BST-10 uses independent controllers and the new nominal resonant tuning
(`Kr = 0.88`, phase `0.09425` rad, hot period 100 µs). These are signal-response
and lifecycle tests; they do not establish closed-loop plant stability.

Per-mode criteria carry a `-startup` or `-busreg` suffix.

## History

Migrated in commit `ba9b62d` from the original `assert()` test (`tests/boost/`). No assertion was removed and the call order is unchanged.
