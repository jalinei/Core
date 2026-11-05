# L1 firmware-block integration

This is the first executable milestone of the [integration proposal](../../docs/testing/pulsim-full-model-integration.md) and [procedure](../../docs/testing/test-procedure.md). **It runs the actual Core application, not the earlier workspace Simulation application.**

The native Pulsim C-Block exports `pulsim_cblock_step`, calls unchanged `controlInit` / `controlStep`, and is attached through `pulsim.add_c_block(lib=...)`. The target CMake and L0 tests compile those same Core application/controller sources. Core currently synchronizes only: inverter PWM is disabled and the relay stays open.

## Set up and run

From Core:

```sh
tests/l1/setup.sh                  # pinned host dependencies + Core/.venv
bash tests/run_all.sh --l0         # known library finding remains visible
bash tests/run_all.sh --l1         # native integration checks + explicit BLOCKED formal cases
bash tests/run_all.sh              # one combined test record
tests/l1/open_gui.sh               # actual C-Block diagram and same runner
```

The setup fetches only `control_library` and `single_phase_inverter` at the commits in `dependencies.lock.json`. It refuses to reset an existing checkout at another revision. `SINGLE_PHASE_INVERTER_DIR=/path/to/fix` selects a library checkout consistently for L0/L1. The build manifest stamps the actual checkout/revision/dirty state; never fall back to the older `firmware/` tree outside Core. The target's unpinned dependency declaration remains unchanged: verify dependency identity before comparing a target build.

Pulsim 2.0.0 and PulsimGUI 1.1.1 are pinned in `plant/requirements.txt`. Compiler, source hashes and resolved library hashes are recorded. No sudo, flashing, debugger or powered hardware is involved.

## What is implemented

| ID | Evidence |
|---|---|
| L1-INT-01.a | ABI wrapper matches direct application calls exactly, including mode changes and held VAC |
| L1-INT-01.b | Independent contexts start with independent state |
| L1-INT-01.c | Wrong ABI dimensions and skipped control ticks report errors |
| L1-INT-02.a | Pulsim actually invokes native code every 100 us |
| L1-INT-02.b | C-Block output drives an RC circuit and reads its changing voltage back |
| L1-INT-02.c | Repeated fresh model/block runs produce identical results |
| L1-INT-03.a | Refined switched model and real application run at the declared cadence |
| L1-INT-03.b | Nominal 50 Hz qualification/synchronization without PWM/relay requests |
| L1-INT-03.c | Current-probe offsets remain correct when C-Block outputs add nodes |
| L1-INT-03.d | STARTUP command lands at 1 ms, cold update occurs on that tick, actual 10 V/s ramp retained |
| L1-INT-04.a | VAC delivery loss trips InvalidMeasurement at age 200 (20 ms after last fresh sample) |
| L1-INT-04.b | Physical grid continues during the delivery loss |

These checks establish integration. They do not replace the formal L1-APP/GFL cases. `blocked.py` reports every unmet formal prerequisite. The user selected the switching model only. Switched startup, regulation and initial burst fixtures are implemented; full-duration/repeated-cycle coverage and the library-only connection harness remain pending. A synthetic native PV variant is implemented; sustained MPPT acceptance is still pending. The procedure's finding #1 gate is unchanged. A campaign can pass its implemented integration checks while listing formal cases as BLOCKED; read both counts and scope.

At the reviewed dependency revision, the L0 baseline has 80 PASS, five known FAIL (#1) and 11 INFO. Those failures still fail combined campaigns. Do not close #1 from the nominal integration check.

## Code map

- `firmware_block/`: documented ABI wrapper, strict/UBSan shared-library build, source manifest.
- `contract.json`: app-v1 input/output indices and explicit actuator assumptions.
- `runtime.py`: native C-Block attachment and target-style stop/update/hold precedence.
- `plant/`: imported, hash-checked switched model, parameter/provenance manifest, native translation, derived GUI view.
- `scenarios/`: operator command/sample-delivery timelines only.
- `runner.py`: plant time stepping, source delivery, raw data and provenance.
- `integration/`: direct-reference, RC and switched-model integration checks.
- `gui.py`: same runner/native block, with an editable GUI project.

Production source under `src/` and target setup remain unchanged. The simulation-only 200 V/s sequence and 5–95% inverter policy from the earlier workspace experiment are not imported.

## Timing, sampling and scope

The C-Block owns the cold-loop divider, mode changes and controller states. Python supplies measurements/commands and models hardware only. The wrapper holds the last delivered VAC when `vac_updated=0`, as `main.cpp`'s sensor reader does. It does not introduce the earlier global-validity shutdown behavior.

The software adapter stops both legs on `boost_mode_changed`, applies duties/enables only on `boost_pwm_update`, otherwise holds the last applied state. Stop/restart actions occur at the same logical tick, not a fabricated extra 100 us blank interval. Hardware execution latency/ADC/timer-register effects are outside L1.

For the switched boost fixture, explicit adapter assumptions are center-aligned 200 kHz, 100 ns rising-edge dead time, 6800 virtual timer counts and 5–90% applied duty from the earlier recorded PWM configuration. The board overlay supplies frequency/deadtime; the duty/count assumptions need separate target verification. They are not evidence that real PWM timing is tested. Both inverter legs and relay contacts stay off.

The disabled-switch nominal-grid and delivery-dropout bring-up use a 1 us integration step because no PWM is enabled. The short switching case uses 50 ns. These are switching runs. The original integration checks alone do not establish regulation or ripple limits; the application fixtures below assess their separately declared criteria.

**Pulsim 2.0 details:** output nodes are predeclared before attaching the C-Block so current-input indices cannot shift. Invalid VAC intentionally makes some observer telemetry NaN. Raw native outputs retain those NaNs in the log; diagnostic-only controlled-source stamps are zeroed for those nonfinite telemetry values so they cannot corrupt the electrical matrix. Nonfinite actuator commands fail the test. No sensor/controller value is sanitized inside the application.

## Records and GUI

`tests/run_all.sh` now creates a unique campaign folder instead of deleting an earlier same-day record. Each L1 campaign includes `record.md`, `l1-manifest.json`, `firmware-manifest.json`, `contract.json` and a folder for each scenario with `control.csv`, `waveforms.npz` and `manifest.json`.

The GUI derives a view from the accepted imported model; it does not overwrite `Model/full_model.pulsim`. The visible `Core_controlStep` block uses the same native library as the CLI. Timer symbols show the plant adapter's duty/complement/deadtime roles; their enable channel is explicit metadata. The adapter is hardware modeling, not another application sequencer. Four constants supply the GUI's operator commands. A STARTUP run must use a timestep no greater than 50 ns; nominal IDLE can use 1 us. Block contract or timer-adapter edits that disagree with the versioned runtime contract fail setup.

```sh
# Standalone raw integration run (from Core)
.venv/bin/python tests/l1/runner.py --scenario tests/l1/scenarios/idle_healthy.json --out tests/out/manual-idle
# GUI setup and actual Run-path checks
tests/l1/open_gui.sh --check
tests/l1/open_gui.sh --check-run
```

This is host execution of shared source. It does not execute the flashed ARM image or validate physical acquisition, scheduling, GPIO or HRTIM drivers.

## PV source and MPPT variant

The [PV guide](../../docs/testing/pv-model-run.md) describes a derived single-diode source with a separate 2.8 kohm bus test load. It preserves the accepted full model and runs unchanged Core in MPPT mode. This is a synthetic 57.1 W panel, not a calibrated commercial panel.

```sh
# From Core: run the default 20 ms switched MPPT fixture
.venv/bin/python tests/l1/runner.py --scenario tests/l1/scenarios/pv_mppt.json --out tests/out/pv-mppt-bringup
# Inspect that saved run immediately in the GUI
tests/l1/open_gui.sh --pv --results tests/out/pv-mppt-bringup
# Or open a fresh PV schematic and click Run
tests/l1/open_gui.sh --pv
```

`L1-INT-05.a–c` check a passive MPP load, actual MPPT coupling and 50/25 ns agreement over 2 ms. `05.d` reports short-run power as INFO. These do not assert settled tracking efficiency. The initial 20 ms characterization extracts little power despite an available MPP; see the guide for observations and follow-up work. GUI traces include `pv_voltage`, `pv_current`, `pv_power`, `pv_mpp_power` and `pv_sensor_power` alongside Core telemetry. Source-side traces are simulator diagnostics; no new measurement is fed into Core.

## Switching application tests

The active plan uses the switching model only. `application/test_switched.py` adds formal APP-01/08 and switched DCDC startup, regulation and burst criteria to the same campaign. Loads and initial capacitor voltages are explicit derived fixtures; production code and the accepted power schematic stay unchanged. See [switched DCDC guide](../../docs/testing/switched-dcdc-tests.md) for windows, scope and GUI commands.

```sh
# From Core, inspect or run the declared switching regulation fixture
./tests/l1/open_gui.sh --scenario tests/l1/scenarios/dcdc_busreg.json
# A saved DCDC result reconstructs its fixture automatically
./tests/l1/open_gui.sh --results tests/out/switched-dcdc-busreg
```
