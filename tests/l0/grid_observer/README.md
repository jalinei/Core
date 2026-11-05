# L0-GOB: grid observer

Open-loop tests of `GridObserver` ([src/grid_observer/](../../../src/grid_observer/)). Catalogue: [procedure §6.2](../../../docs/testing/test-procedure.md#62-l0-gob--grid-observer).

## Run

```bash
bash tests/l0/grid_observer/run.sh
tests/run_all.sh --l0
```

## What is compiled

| Source | Flags |
|---|---|
| `test_grid_observer.cpp` | strict (`-Werror`) |
| `src/grid_observer/gridObserver.cpp` | strict |

## How the test drives the observer

`tick(observer, rms, hz, phase)` feeds one balanced α/β sample (`√2·rms·cos`, `√2·rms·sin`) and the angular frequency `2π·hz`, exactly what the application passes from the PLL.

The window is the application's (207–253 V, 47.5–52.5 Hz), but the timing uses binary-exact periods so checks land on the exact tick:

| Setting | Test | Application |
|---|---|---|
| Sample period | 1/128 s | 100 µs |
| Voltage trip delay | 3 ticks | 0.1 s |
| Frequency trip delay | 4 ticks | 0.1 s |
| Qualification | 8 ticks | 1 s |

The timing logic is the same; the application's real values are exercised in L0-APP and L1-APP.

## Criteria

| Test case | Criteria | Checks |
|---|---|---|
| L0-GOB-01 | .a, .b | Inoperable before init; six invalid configurations rejected |
| L0-GOB-02 | .a, .b | No nuisance trip with the grid absent at startup; dwell restarts on a dropout |
| L0-GOB-03 | .a | RMS within 0.001 V and frequency within 0.001 Hz over a full cycle |
| L0-GOB-04 | .a – .g | UV, OV, UF, OF trip exactly at their delay, shorter events do not; opposite excursions do not add up; negative frequency is UF and the first cause is kept; zero delays act on the first sample |
| L0-GOB-05 | .a – .d | Each trip latches; clearing needs a full healthy interval and open-relay feedback |
| L0-GOB-06 | .a – .d | NaN α, infinite β, NaN frequency or a false validity flag trip at once with `InvalidMeasurement` |

## History

Migrated in commit `ba9b62d` from the original `assert()` test (`tests/grid_observer/`). No assertion was removed. L0-GOB-06 was already covered by the original test.
