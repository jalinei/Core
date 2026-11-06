# Test work log

A dated log of every change made to **production code** (anything under `src/` or `owntech/`) while building the test procedure. Test-only changes are not logged here; they are described by their commits.

The procedure tests existing features. It must not change behaviour. Each entry therefore states the change, why the tests needed it, and the evidence that behaviour is preserved.

## Entry format

```markdown
## YYYY-MM-DD — <short title>

| Field | Value |
|---|---|
| Commit | `<sha7>` |
| Files | `path/a`, `path/b` |
| Reason | Why the test procedure needed this change |
| Behaviour change | None / description |
| Evidence | Tests, campaign record, or hardware check that shows behaviour is preserved |
| Hardware check | Not done / done on <date> with <board/shield version>: <result> |

<free-form notes: anything a reviewer must know, e.g. changed order of operations inside a tick>
```

## Entries

## 2026-11-05 — Extract the control step from the critical task

| Field | Value |
|---|---|
| Commit | the `refactor(app): extract controlStep …` commit that adds this entry (parent `566d9b3`) |
| Files | `src/app/control_step.h` (new), `src/app/control_step.cpp` (new), `src/main.cpp`, `zephyr/CMakeLists.txt` |
| Reason | L0-APP and L1 must run the application's real control logic and tuning without the shield API ([ADR 0001](../adr/0001-firmware-in-the-loop-via-pulsim.md)) |
| Behaviour change | None intended. Two timing differences inside one tick, listed below |
| Evidence | Firmware builds (`pio run -e USB`) with the same warning set as a full rebuild of the parent; flash +256 B (146 916 → 147 172), RAM +7 B. `control_step.cpp` compiles on the host with `-Wall -Wextra -Werror`. L0 campaign unchanged: 69 pass, 1 fail (L0-GFL-03.b, pre-existing), 7 info. L0-APP tests on the seam follow in the next commit |
| Hardware check | Not done. Recommended before L1: flash, run `i → p → b → m → c → i` from the serial menu and compare telemetry with the parent commit |

**What moved, unchanged.** From `loop_critical_task()`: boost measurement and reference updates, mode-change handling, the cold-loop divider, the hot loop, inverter bus update and sync/disconnect request, `calculateDuty`, grid observer update with the stale-VAC flag, grid-quality gating and the grid angle. From `setup_routine()`: controller `init` calls, startup ramp rate, startup current ceiling, bus reference and burst bounds, same order and values. From the globals: timing constants, PID tuning, grid constants and the observer configuration, now `app::` constants in `control_step.h`. The three controllers and the per-tick state (`cold_loop_tick`, `previous_mode`, `grid_voltage_age_ticks`) are now members of `ControlState`.

**What stays in `main.cpp`.** Sensor reads with last-value hold, the serial menu (which only sets `mode`, `inverter_sync_requested`, `voltage_reference`), telemetry, the scope, and every `shield.power.*` call.

**Order of hardware calls in a tick.** Unchanged: on a mode change `stop(LEG1_LOW)`, `stop(LEG2_LOW)`; then, if the mode is not IDLE, `setDutyCycle` ×2 and `start`/`stop` per leg; then `stop(LEG1_HIGH)`, `stop(LEG2_HIGH)`.

**Timing differences inside a tick:**

1. On a mode change, the two boost legs are now stopped **after** the control computation instead of before it, so for the duration of that computation (tens of µs) they keep the previous period's duty. They are stopped before any new duty is written, as before.
2. The VAC staleness counter is now updated at the start of `controlStep` instead of during the sensor read. Nothing reads it in between, so its value at the observer update is the same.

The leg enables are read before `scope.acquire()` instead of after; the scope does not touch the boost controller, so the values are the same. Operator commands are captured once per tick at the call to `controlStep`; the communication task cannot preempt the critical task, so this matches the previous reads.

**Signature.** The procedure sketched `controlStep(Measurements, OperatorCommands)`; the implementation also takes the `ControlState` explicitly, so tests and the L1 firmware block can hold independent instances instead of sharing hidden globals.

## 2026-11-05 — Temporarily build against the library fix for finding #1

| Field | Value |
|---|---|
| Commit | the `build(app): temporarily fetch …` commit that adds this entry |
| Files | `src/app.ini` |
| Reason | Evaluate the proposed fix for finding [#1](https://github.com/luizvilla/Core/issues/1) (PLL never synchronizes away from 50.00 Hz) in the firmware and on hardware before it is merged upstream |
| Behaviour change | **Yes, intended and temporary.** `lib_deps` fetches `luizvilla/single_phase_inverter` branch `fix/sogi-frequency-adaptive` (`4378319`, PR jalinei/single_phase_inverter#1) instead of `jalinei/single_phase_inverter` `main` (`0f22378`). Both SOGIs run at the PLL estimate clamped to ±5 % of ω_ref instead of the fixed ω_ref |
| Evidence | Firmware builds (`pio run -e USB`). L0 campaign on the fetched copy: 85 pass, 0 fail, 11 info; the record lists #1 as ready to close. Measured effects: Vq ripple at 51 Hz 3.27 V → 0.039 V; cold-start synchronization at 47.6–52.4 Hz now passes; cold-start lock at 50 Hz 0.19 s → 0.29 s; app grid-qualified-to-synchronized 1.12 s → 1.14 s |
| Hardware check | Not done |

**Revert condition.** When the PR is merged into jalinei `main` (or replaced by another remedy), restore `single_phase_inverter = https://github.com/jalinei/single_phase_inverter`, delete `owntech/lib/USB/single_phase_inverter` so PlatformIO re-fetches it, run `tests/run_all.sh --l0`, close #1 with that campaign ID and mark it `Closed` in the findings register. If the PR is rejected, revert the same way and reopen the triage on #1.

**Re-fetching.** PlatformIO does not always refresh a git dependency when only the URL or branch changes; deleting the library folder forces it.

## 2026-11-05 — Raise the startup slew rate to 200 V/s

| Field | Value |
|---|---|
| Commit | the `feat(app): raise the startup slew rate to 200 V/s` commit that adds this entry |
| Files | `src/app/control_step.h` (`app::startup_ramp_rate` 10.0F → 200.0F) |
| Reason | Requested by Jean Alinei, agreed by Luiz Villa. L1 uses the switched plant only ([ADR 0002](../adr/0002-switched-plant-only.md)); at 10 V/s the full 0 → 400 V startup is a 40 s run, hours of simulation at 50 ns. At 200 V/s it takes about 2 s |
| Behaviour change | **Yes.** STARTUP raises the bus reference 20× faster: 0.2 V per 1 ms cold-loop call instead of 0.01 V; 0 → 400 V in about 2 s instead of 40 s. The startup current ceiling (1 A on leg 1) and every other constant are unchanged. Charging 120 µF at 200 V/s needs 24 mA at the bus, about 0.25 A from 40 V before losses, within the ceiling |
| Evidence | Firmware builds (`pio run -e USB`). L0 campaign `2026-11-05_7b3857d-dirty_200722_RXHcr3`: 85 pass, 1 fail (known, #2), 11 info; L0-APP-01 reads the new value through `controlInit`. L1 campaign `2026-11-05_7b3857d-dirty_200921_gZABaa`: 24 pass, 1 fail (L1-APP-03.a, #3, unrelated to the ramp), 5 info, 9 blocked, the same counts as before the change. L1-INT-03.d and L1-APP-02.a measure the new rate (0.2 V after the first cold call; ramp error < 5 µV over 50 ms); L1-APP-02.b peak leg-1 current 0.605 A ≤ 1 A |
| Hardware check | Not done. Before relying on it: on the board, run STARTUP from a discharged bus and check that ILow1 stays under the 1 A ceiling and the bus reaches 400 V without overshoot |

The `BoostConverter` class default stays 10 V/s (L0-BST-05 tests the class); only the application's value changes. L1 checks now read the rate from `control_step.h` (`tests/l1/app_constants.py`) instead of repeating it, so a later change is picked up by the tests. `tests/l1/scenarios/dcdc_startup_full.json` now runs 2.5 s instead of 40 s; it is still run on demand, so L1-APP-02.d stays `BLOCKED` in the default campaign.

## 2026-11-06 — Build against jalinei/single_phase_inverter main again (#1 merged)

| Field | Value |
|---|---|
| Commit | the `build(app): fetch single_phase_inverter from jalinei main again …` commit that adds this entry |
| Files | `src/app.ini`, `tests/l1/dependencies.lock.json` |
| Reason | Revert condition of the 2026-11-05 entry met: Jean Alinei merged the fix (jalinei/single_phase_inverter#1) into `main` as `bcb7e71` |
| Behaviour change | None compared with the fix branch: `bcb7e71` has the same tree as `4378319` (squash merge, empty diff). `lib_deps` fetches `jalinei/single_phase_inverter` again; the test lock pins `bcb7e71` |
| Evidence | L0 campaign on `bcb7e71`: 85 pass, 1 fail (known #2), 11 info; every #1 criterion passes (record lists #1 as ready to close). Campaign ID in the findings register (§11) |
| Hardware check | Not needed for this revert (same source as the 2026-11-05 build) |

The local checkout `owntech/lib/USB/single_phase_inverter` was moved to `bcb7e71` with its origin set to jalinei, instead of being deleted and re-fetched: same commit, same result.

