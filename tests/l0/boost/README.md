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

## How the test drives the controller

Each section sets measurements with `setILow1/2`, `setVLow`, `setVBus`, references with `setILowRef`/`setVBusRef`, then calls `coldLoop()` (voltage loop or MPPT, `voltage.Ts` = 1 ms) and `hotLoop()` (current loop, `current.Ts` = 100 µs) and checks the outputs. Nothing is simulated: a value such as 52 V on the bus is simply set.

Test tuning, chosen for exact arithmetic rather than the hardware: voltage PID `Kp` 0.1, `Ti` 1, bounds 0–12 A; current PID `Kp` 0.1, `Ti` 1, bounds 0–0.95; leg 1 current limit 5 A; burst bounds 50/52 V.

> **Order matters.** The sections share one controller and each starts from the state the previous one left. Do not reorder them, and keep every call that has side effects (`hotLoop`, `coldLoop`, `set*`) in place, including those inside a check expression.

## Criteria

| Test case | Criteria | Checks |
|---|---|---|
| L0-BST-01 | .a, .b-startup, .b-busreg, .c | Uninitialized controller is inert; invalid burst bounds rejected; NaN measurement ignored; invalid leg 1 limit or duty bound rejects init |
| L0-BST-02 | .a – .c | Leg 1 up to its limit, leg 2 takes the excess; PI duties; leg 2 integral cleared before reuse |
| L0-BST-03 | .a, .b | Saturated voltage loop gives 12 A split 5/7 A, duties stay in [0, 0.95] for 10 000 ticks; IDLE clears everything |
| L0-BST-04 | .a – .d | MPPT direction from power, ±0.1 A step, leg 1 priority, turnaround at bounds, independence from voltage loop |
| L0-BST-05 | .a – .c | STARTUP ramp seeding, rate, no overshoot, rejection of invalid rates, reseed on reset and re-entry |
| L0-BST-06 | .a – .c | STARTUP current ceiling, converter 2 never on, ceiling ≤ leg 1 limit, ceiling below the PID lower bound |
| L0-BST-07 | .a – .d per mode, .e | Burst pause at ≥ upper, resume at ≤ lower, ramp frozen, no bypass, persists across init, MPPT/IDLE exempt, no windup |

Per-mode criteria carry a `-startup` or `-busreg` suffix.

## History

Migrated in commit `ba9b62d` from the original `assert()` test (`tests/boost/`). No assertion was removed and the call order is unchanged.
