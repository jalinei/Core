# L0-GFL: grid-following inverter controller (library)

Open-loop tests of `singlePhaseInverterFollowing` from [jalinei/single_phase_inverter](https://github.com/jalinei/single_phase_inverter), branch `main`, not pinned. Catalogue: [procedure §6.3](../../../docs/testing/test-procedure.md#63-l0-gfl--grid-following-inverter-library).

## Run

```bash
bash tests/l0/inverter_following/run.sh
tests/run_all.sh --l0
```

The script tests the copy PlatformIO fetched for the USB environment (`owntech/lib/USB/single_phase_inverter`) and prints its commit first; the record stamps it too. If the folder is missing, run a PlatformIO build once.

## What is compiled

| Source | Flags |
|---|---|
| `test_inverter_following.cpp` | strict (`-Werror`) |
| `single_phase_inverter/src/singlePhaseInverterFollowing.cpp`, `sogi.cpp` | library (no `-Werror`) |
| `control_library/src/pid.cpp`, `transform.cpp`, `trigo.cpp` | library |

## How the test drives the controller

- **Grid:** a synthetic `Grid` struct, `v = Vpk·cos(phase)`, `i = Ipk·cos(phase)`, advanced by one period after each `calculateDuty()` call. It starts at 1 rad so the PLL has to move. Nothing the controller outputs feeds back into it.
- **Relay:** feedback is set directly with `setGridConnected()`.
- **Settings:** the application's: 400 V bus, 230 V RMS (325.3 V peak), 50 Hz, 100 µs.
- **Off-nominal frequency:** the PLL estimate carries a 100 Hz ripple away from 50 Hz, so frequency is judged on a 20 ms moving mean (two ripple periods).

Limits come from the library's own documentation and constants: synchronization needs `Vd` within ±10 % of the peak, `|Vq|` < 0.1 V and `|ω − ω_ref|` < 5 % for 1000 consecutive calls; connection and disconnection time out after 1 s; duty = α / (2·V_bus) + 0.5, unclamped. Durations the library does not state are `INFO`. The 5 s and 3 s simulation horizons only bound the test.

## Criteria

| Test case | Criteria | Checks |
|---|---|---|
| L0-GFL-01 | .a – .d | Valid init state; eight invalid parameter sets give `-EINVAL` and `INVALID_CONFIGURATION`; recovery by init; `calculateDuty` before init |
| L0-GFL-02 | .a, .b; INFO .c – .e | Synchronization on a clean 50 Hz grid; sync-only never requests PWM or relay; lock time, phase and frequency error |
| L0-GFL-03 | .a, .b; INFO .c – .f | Frequency tracking after a 50 → 51 Hz step; re-synchronization at 51 Hz; tracking time, unsynchronized time, error, Vq ripple |
| L0-GFL-04 | .a – .e | Full lifecycle with STOPPING held while current flows; connection timeout; disconnection timeout; relay feedback lost in RUN; phase jump while awaiting the relay |
| L0-GFL-05 | .a – .c | Bad quality blocks sync and restarts the dwell; bad quality trips CONNECT, CONNECTED, RUN at once; `clearFault` conditions |
| L0-GFL-06 | .a – .d | Non-finite measurement; non-positive bus; invalid current or frequency reference; first cause kept and requests ignored while latched |
| L0-GFL-07 | .a, .b | Duty exactly 0.5 in OFF and SYNC; duty formula at 400, 300 and 100 V, exceeding 1 when unclamped |
| L0-GFL-08 | .a – .e; INFO .f, .g | Cold-start synchronization at 47.6, 49, 50, 51 and 52.4 Hz from four grid angles, settled estimate within ±5 %; worst lock time and transient |

## Known findings

| Criterion | Status | Issue |
|---|---|---|
| L0-GFL-03.b | `FAIL`: synchronization only within about 50 ± 0.03 Hz; at 51 Hz the Vq ripple is 3.3 V against a 0.1 V tolerance. Cause is in the library (SOGI evaluated at the fixed ω_ref). | [luizvilla/Core#1](https://github.com/luizvilla/Core/issues/1) |
| L0-GFL-08.a, .b, .d, .e | `FAIL`: no cold-start synchronization away from 50 Hz, same cause. Added because a first fix attempt (SOGI at the PLL estimate) passed 03.b but diverged at 49 Hz from a cold start. | [luizvilla/Core#1](https://github.com/luizvilla/Core/issues/1) |

Known findings are listed in the procedure's [findings register](../../../docs/testing/test-procedure.md#11-findings-register); test records mark these failures as *known*.

## Testing a library fix before merge

```bash
SINGLE_PHASE_INVERTER_DIR=~/code/single_phase_inverter tests/run_all.sh --l0
```

Builds L0-GFL and L0-APP against that checkout instead of the PlatformIO copy; the record stamps its commit and lists any finding whose criteria now all pass.

## History

Added in commit `bec6342`. L0-GFL-08 added after #1 was filed.
