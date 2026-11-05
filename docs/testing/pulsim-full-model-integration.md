---
status: proposal-first-milestone-implemented
reviewed_on: 2026-11-05
procedure_version_reviewed: "0.7 (draft)"
core_commit_reviewed: 28f7f0f5dae0764d06cee04942d9341ffae1fd4d
plant_model: ../../../Model/full_model.pulsim
plant_sha256_reviewed: 58839236cbd50d8564e36793e33aaa6ee3ab58935eed73dbf724a069da5529de
---

# Using the Core test procedure with full_model.pulsim

> **Superseded in part (procedure 1.3, 2026-11-05).** Two recommendations below no longer apply: there will be no averaged plant ([ADR 0002](../adr/0002-switched-plant-only.md)), and the production startup slew rate is now 200 V/s ([worklog](worklog.md)). The rest of this review stands. The text is kept as written so its reasoning can be followed.

The first executable integration milestone is now available in [tests/l1](../../tests/l1/README.md). The review observations below describe the pre-implementation checkout; the L1 README and generated campaign records describe the implemented scope and remaining prerequisites.

## 1. Recommendation

Use **Core's existing `controlInit()` / `controlStep()` implementation as the firmware block**, and use the refined `Model/full_model.pulsim` as the starting **switched plant model**. Build the missing L1 infrastructure around these two artifacts. Do not use the separate application sequencer in `Simulation/firmware/src/application.cpp` as the application under test.

Core already implements the architectural correction discussed earlier: the target critical task calls the same hardware-free control step that the L0 tests call. The next work is to connect that seam to the plant model, rather than extract or rewrite the application again.

Maintain two explicit test paths:

1. **L1-APP:** execute the unchanged application control step. Its current inverter behavior is synchronization only; bridge PWM must remain disabled and the grid relay stays open.
2. **L1-GFL:** execute the existing inverter library through a clearly labeled test-only harness for connection, RUN, current-reference steps, stopping and relay faults. Those are library capabilities that the present application does not use.

This document proposes implementation work. It does not implement L1, change the test procedure, alter Core production code, or claim that a campaign was run against Core.

## 2. Basis of the proposal

The review covered:

- [Test procedure v0.7](test-procedure.md), including its catalogue, verdict rules, findings register and commit plan.
- [ADR 0001](../adr/0001-firmware-in-the-loop-via-pulsim.md) and [production-change worklog](worklog.md).
- [Tests overview](../../tests/README.md), [shared test infrastructure](../../tests/common/README.md), and the L0 README files for [boost](../../tests/l0/boost/README.md), [grid observer](../../tests/l0/grid_observer/README.md), [inverter](../../tests/l0/inverter_following/README.md) and [application](../../tests/l0/app/README.md).
- [Glossary](../../GLOSSARY.md), [control_step.h](../../src/app/control_step.h), [control_step.cpp](../../src/app/control_step.cpp), the call in [main.cpp](../../src/main.cpp), and the target source list in [zephyr/CMakeLists.txt](../../zephyr/CMakeLists.txt).
- The refined [full_model.pulsim](../../../Model/full_model.pulsim), the existing experimental translator/actuator code under [Simulation](../../../Simulation/README.md), and the [schematic guidelines](../../../SIMULATION_SCHEMATIC_GUIDELINES.md).

### Observed state of this checkout

| Item | Observed state |
|---|---|
| Core branch / commit | `test/microinverter-validation` / commit stamped above |
| Shared application seam | Implemented; target CMake includes `src/app/control_step.cpp` and `main.cpp` calls `controlStep` |
| L0 source and record infrastructure | Present |
| `Core/tests/l1/` | Absent; procedure commits 7 onward describe planned work |
| Core-fetched `control_library` and `single_phase_inverter` under `owntech/lib/USB/` | Absent in this copied checkout |
| Inverter library selection | `src/app.ini` fetches `jalinei/single_phase_inverter`; L0 supports `SINGLE_PHASE_INVERTER_DIR` |
| Switched plant model | Present outside Core; 95 components and 139 wires at review |
| Plant sources | 40 V DC input; `Vgrid` = 230 V RMS / 50 Hz, represented by 325.269119 V peak |
| Stored integration settings | 50 ns step, 3.2 s stop time |
| Averaged plant model / PV curve | Not provided by the present file |
| Locally recorded finding #1 | Open in the procedure; gate for the affected synchronization/frequency test cases |

No dependency installation, firmware build, campaign execution or upstream issue-status check was performed for this document. The absence of fetched libraries must be resolved explicitly before builds; the older `firmware/` copy elsewhere in this workspace must not be substituted silently.

## 3. Firmware identity and configuration

Compile the following sources directly from Core and its resolved dependencies:

- `src/app/control_step.cpp`;
- `src/boost_converter/boost.cpp`;
- `src/grid_observer/gridObserver.cpp`;
- the selected `single_phase_inverter` checkout's following controller and SOGI;
- the same required PID, transform and trigonometry sources selected by the Core build.

Reuse `tests/common/shim/` for necessary host compatibility. Keep state in a fresh `ControlState` per test case. Use the existing library override consistently for both L0 and L1, and stamp exact dependency commits and dirty state in the test record. The library is currently declared on an unpinned branch; reproducible campaigns need an immutable resolved revision, with any pinning-policy change documented.

This establishes **source-identical application/control logic** across target, L0 and L1. It does not produce identical host and ARM machine code, reproduce CMSIS/host numerical behavior exactly, or test the complete flashed image. ADR 0001's “byte-for-byte” wording should be understood or clarified at the source boundary.

### Keep the production and accelerated profiles separate

Core's `app::startup_ramp_rate` was **10 V/s** at review time: a 0 → 400 V reference took about 40 s. The 200 V/s profile of the earlier experimental application was kept out of the baseline, and the rule was that it could enter only as a deliberate production change documented in the worklog and procedure.

That change was made on 2026-11-05, at Jean Alinei's request: `app::startup_ramp_rate` is now **200 V/s** in production, so target, L0 and L1 share one profile and the full startup lasts about 2 s. See the [worklog](worklog.md) entry. L1 checks read the rate from `src/app/control_step.h` (`tests/l1/app_constants.py`) instead of repeating it.

Likewise, do not inherit the experimental simulation's 5–95% inverter duty policy as an undocumented target setting. Duty clamping, timer counts, modulation and dead time belong to the plant/hardware adapter and need their own source references.

## 4. Model ↔ firmware block contract

Revise procedure contract v0 before enabling L1. The model already supplies the six application measurements:

| Plant probe | `Measurements` field | Unit / polarity |
|---|---|---|
| `DC1_Vin` | `v_low` | V, shared converter input relative to its reference |
| `DC1_Iin` | `i_low_1` | A, positive from input source into DC/DC channel 1 |
| `DC2_Iin` | `i_low_2` | A, positive from input source into DC/DC channel 2 |
| `DC_bus` | `v_dc_bus` | V, shared bus capacitor voltage |
| `Grid_voltage` | `v_ac` | V, grid line minus neutral at the actual probe terminals |
| `Grid_current` | `i_ac` | A, positive from inverter/filter toward the grid at the actual probe location |
| Sample-delivery fixture | `v_ac_updated` | Boolean: a new VAC sample was delivered this tick |

Bind the probes through their component identities and saved wire connectivity. Do not depend on temporary numeric node aliases or add internal inductor/transformer states as control measurements. Verify current direction with an independent native solver check.

The procedure's description of IAC as “bridge current” needs clarification: the refined probe is at the grid-side line. Those locations differ by filter capacitor currents. Preserve the physical measurement location in the contract.

### Commands and outputs

The scenario timeline supplies only the existing `OperatorCommands`: `mode`, `sync_requested` and `v_bus_ref`. It emulates operator requests; it does not recreate the application's cold-loop logic or automatically introduce STARTUP → BUSREG transitions that the application does not implement.

The application adapter must consume **all relevant `ControlOutputs` semantics**, not just duties:

1. If `boost_mode_changed`, stop both boost legs before applying new outputs.
2. If `boost_pwm_update`, apply the two duties and start/stop each leg according to its enable flag.
3. If no PWM update is requested, preserve the adapter's previously applied state, including the stop above. Do not invent an update that `main.cpp` would not make.
4. Keep both inverter legs disabled for current L1-APP tests. Log `bridge_duty`, but do not apply it merely because the controller calculated it.
5. Hold the relay open in the application path. Core's current `ControlOutputs` has no relay command; an optional contract field does not add that product feature.

L1 represents these actions at a logical tick boundary. It must not insert a full extra 100 us off interval for a stop/restart that occurs within one target tick. Actual computation latency and timer-register timing remain outside the seam, including the ordering differences already described in the worklog.

### VAC sample-loss semantics

For L1-APP-08, keep the electrical grid running, hold the last delivered `v_ac`, and set `v_ac_updated = false` for the selected ticks. Resume fresh samples explicitly. This exercises Core's 200-tick stale counter.

Setting the grid source to zero is a voltage disturbance, not a lost sample. The experimental adapter's global `measurements_valid` fault path is also not a substitute for Core's `v_ac_updated` behavior.

Log controller telemetry separately: startup and per-leg current references, burst state, cold-loop phase, VAC age, inverter mode/fault, synchronization, angle, frequency, observer qualification/trip, and SOGI Valpha/Vbeta/Vd/Vq where available. These are diagnostic outputs, not additional physical sensors.

## 5. Execute the firmware block as an actual Pulsim C-Block

Follow ADR 0001 with a thin C/C++ wrapper exporting the installed Pulsim 2.0.0 C-Block ABI:

- Initialization allocates a context containing `ControlState` and last-delivered samples, then calls `controlInit` once.
- Each scheduled block execution decodes measurements/commands, calls `controlStep` **exactly once**, and encodes the returned outputs and telemetry.
- Termination releases that context. Each test case starts with independent state.
- The wrapper contains no duplicated boost loop, PLL, observer, mode sequencer or altered tuning.

Put a visible firmware-block component in the GUI schematic with named measurement/command inputs and output ports. Connect its outputs to the actuator adapter; existing gate inputs remain named Goto/From channels. Preserve explicit electrical wiring everywhere else.

A diagram symbol alone is not execution evidence. Prove the wrapper is actually loaded and invoked: expose an invocation count, verify one call per 100 us, test reset/repeated-run independence, and close the block against the small RC stand-in planned in procedure commit 7 before connecting the full plant model. A missing ABI symbol, unsupported component or unwired required channel must fail setup rather than select a demonstration backend.

Python may load the model, schedule scenarios, convert measurements, drive the physical actuator representation and write the test record. It must not become a second application implementation. The scripts can remain convenient entry points while the actual C-Block owns application execution.

## 6. Tick scheduling and plant fidelity

Use a deterministic, documented order:

1. Advance the plant model to control tick `t[k]` using the previously applied commands.
2. Deliver the six physical samples, the VAC freshness flag and that tick's operator commands.
3. Execute the firmware block once.
4. Apply its output actions and hold commands until the next tick.
5. Resolve PWM switching between ticks in the switched plant model, or apply average actuator behavior in the averaged plant model.

Use `app::control_task_period` as the timing source; let `controlStep` own the cold-loop divider and forced cold-loop update on a mode change. Measure actual call timestamps to catch off-by-one or double-call errors.

The actuator adapter must be checked independently against the applicable board configuration. Specify carrier shape/phase, controlled transistor convention, complementary outputs, enable behavior, timer quantization, clamps and dead time. All-off must leave body diodes active. The two relay poles must follow one command with an explicitly modeled acknowledgment delay in the library-only path.

These choices affect the electrical response, but L1 does not validate the real ADC, calibration, ISR deadlines, GPIO, relay driver or HRTIM implementation. A later target-executed test is needed for those boundaries.

## 7. Use two plant-model variants

### Switched variant: import the refined schematic

Import a reviewed, versioned snapshot of `full_model.pulsim` into `tests/l1/plant/`, preserving the model author's original and stamping the imported SHA-256. Reconcile later revisions deliberately; do not maintain two independently edited “latest” files. Do not regenerate the refined schematic from the earlier draft generator.

The current model contains both DC/DC stages, the shared 120 uF bus capacitor and 200 kohm load, the full bridge, filter and relay contacts, the six probes and the ideal grid source. Native translation must retain:

- transformer winding polarity, inductance matrix, leakage and winding resistance;
- both coupled-inductor chokes, including mutual coupling rather than two independent inductors;
- current probes as ideal series measurements and voltage probes at their actual terminals;
- separate PE/neutral/DC-reference relationships and declared initial energy-storage states;
- the user's physical wiring and signal-only Goto/From separation.

Use it for short switching windows, burst behavior and actuator/measurement-interface checks. The current 50 ns step is a starting configuration, not established numerical convergence. Check relevant observables under refinement before treating their physical magnitudes as firmware verdicts. Long-run decimated storage must not be used to measure switching ripple.

### Averaged variant: a required new deliverable

The present `.pulsim` file is switched; increasing its timestep does not make it an averaged plant model. Develop a separate averaged representation with the same electrical parameters and sensor interface for the approximately 40 s baseline startup and other long cases.

Create the procedure's shared parameter file with each value's source, unit and uncertainty. At initial import, reconcile it against the reviewed schematic. Thereafter, make it the common parameter authority consumed by both variants, with a check that the GUI schematic's values agree. Preserve the GUI layout when materializing updated parameters.

Compare averaged and switched results at selected operating conditions using declared observation methods. Record discrepancies as model-validation evidence, not automatic firmware failures. State transfer into a short switched window must include consistent capacitor voltages, inductor/winding currents and controller state (integrators, PLL/SOGI, stale age, cold-loop phase). If full state transfer is unavailable, label the window as a separate initialized fixture instead of claiming it continues the long startup.

### Parameters and capabilities still missing

| Item | Consequence |
|---|---|
| Ideal 40 V source, no PV I–V curve | L1-APP-05 cannot establish maximum-power tracking; keep it `BLOCKED` until a sourced PV model is supplied |
| Averaged plant model absent | Long averaged-model cases remain `BLOCKED`; a short switched bring-up does not fulfill them |
| Choke coupling currently assumed 0.999; part inductance convention/DCR/saturation not validated | Record assumptions; cases whose verdict relies on unavailable parameters remain `BLOCKED` |
| Simplified transistor/diode parameters | Document their source or `TBD`; do not claim device-spike/loss accuracy |
| Relay present in the plant, absent from the application's control interface | Supports a prospective L1-GFL fixture after adapter validation; does not enable application connection |

## 8. Map the existing catalogue to the model

| Case | How I would use the plant model | Remaining condition |
|---|---|---|
| L1-APP-01 | Healthy `Vgrid`, IDLE commands, relay open; log qualification and PLL | Define qualification timing relative to measured healthy estimates, not blindly from source energization |
| L1-APP-02 | Discharged bus, actual STARTUP command and unchanged 10 V/s Core profile | Averaged variant; resolve reference-limit versus physical-current wording below |
| L1-APP-03 | Explicit BUSREG command, 400 V reference and declared load fixture | Define the observation/settling window without changing the published limits silently |
| L1-APP-04 | Switched light-load window; record pause/resume versus measured bus | Verified state initialization and sufficient timestep/storage resolution |
| L1-APP-05 | Replace the ideal source with the sourced PV fixture | `BLOCKED` until a PV curve exists |
| L1-APP-06 | Set `sync_requested`; keep inverter PWM disabled and relay open | Respect the procedure's finding #1 gate |
| L1-APP-07 | Phase-continuous grid voltage/frequency disturbances through `Vgrid` | Separate estimator latency from observer timer; respect finding #1 dependencies |
| L1-APP-08 | Suppress sample delivery while the grid itself remains healthy | Hold last VAC and use `v_ac_updated`, checking the exact 200th tick |
| L1-APP-09 | Scenario-controlled mode changes; compare output action ordering with Core | No automatic test-only application transitions; peak current remains `INFO` |
| L1-GFL-01 | Separate library harness drives bridge and modeled relay; supplies contact feedback | Validate bridge/relay adapter; keep results labeled library-only |
| L1-GFL-02 | Apply grid fault during the library harness's RUN | Check library fault/request behavior separately from mechanical contact delay |
| L1-GFL-03 | Withhold closure acknowledgment despite a close request | Check the library's 1 s connection timeout; do not alter the application to expose it |

Use a continuous phase accumulator for frequency steps. Replacing `sin(2π f t)` with a different `f` at a nonzero time would otherwise add an unintended phase jump and change the test case.

## 9. Clarify procedure criteria before assigning verdicts

Keep `PASS`, `FAIL`, `INFO` and `BLOCKED` as defined in the procedure. Do not import the earlier experiment's ±8 V or ±20 mA checks as official Core limits.

The following deserve explicit procedure/changelog review:

1. **Startup current:** the code bounds the current **reference** at 1 A; that is not a guaranteed instantaneous physical-current ceiling. L1-APP-02 currently says `ILow1 ≤ 1 A`. Identify whether that criterion means the reference or a separately specified physical response. Record measured current peaks, and do not quietly replace a failing physical-current criterion with a reference check.
2. **Bus regulation:** 395/405 V are burst thresholds. State the load, transient exclusion and measurement window for the catalogue's regulation criterion. A threshold does not independently establish a settling-time guarantee.
3. **Observer qualification/trips:** SOGI/PLL settling occurs before their estimates qualify as healthy or cross a supervision threshold. Check the observer's specified dwell/delay from the relevant estimated-condition transition. Record external-grid-event-to-response latency separately unless the procedure explicitly makes it a bounded requirement.
4. **Finding #1:** the local procedure records the off-nominal synchronization defect as open. Keep its gate for the affected cases and the first formal full campaign. A successful 50 Hz run or an experimental library override alone does not close it; all listed regression criteria, including cold starts, must pass against the identified revision.
5. **Relay scope:** reconcile the physical relay shown in the supplied schematic with the procedure's statement that the board definition has no relay. Preserve the distinction between a physical model component, a driver mapping and a feature the application actually commands.

Record unresolved prerequisites as `BLOCKED` where applicable. Changing a criterion or production behavior belongs in its own documented change; a test integration commit must not tune controllers or weaken limits to manufacture a pass.

## 10. Implementation sequence and proposed files

Proceed along the existing commit plan, with these concrete deliverables:

| Step | Deliverable | Exit evidence |
|---|---|---|
| 1 | Resolve the exact Core dependencies; run the existing L0 campaign | Test record with actual commits and known/new failures; no fallback to the older workspace firmware |
| 2 | `tests/l1/firmware_block/`: ABI wrapper, build script, README and source manifest | Same seam/source selection as target and L0; independent state; one call per tick |
| 3 | RC stand-in integration check | Actual C-Block loading, reset, timing and output-hold behavior verified |
| 4 | `tests/l1/plant/`: imported switched model, shared sourced parameters, translator and model README | Every component supported, probe polarity checked, mutual coupling retained, GUI inspection works |
| 5 | `tests/l1/`: scenario runner, contract definition, actuator adapter and criterion reporting | No duplicated application logic; sample-drop and mode-change semantics verified |
| 6 | Averaged plant model and capability manifest | Suitable long startup fixture; comparison against selected switched windows |
| 7 | Unblock cases selectively according to model capabilities and procedure gates | Criterion-level test record; blocked and known failures preserved |
| 8 | Separate L1-GFL harness for library connection features | Library-only identification, validated relay/PWM fixture and fault-injection evidence |

Suggested layout, **not present yet**:

```text
tests/l1/
  README.md
  run.sh
  contract.yaml
  firmware_block/
    README.md
    wrapper.cpp
    build.sh
  plant/
    README.md
    requirements.txt           # pulsim==2.0.0
    params.yaml
    full_model.pulsim          # reviewed switched-model import
    switched.py
    averaged.py
  scenarios/
    ...                       # timelines identified by existing case IDs
  gfl_harness/
    README.md
    ...                       # explicit library-only wrapper
  runner.py
  actuators.py
  evaluate.py
```

Reuse the electrical translation, six-probe mapping, plotting and telemetry ideas from `Simulation/` only after review. Do not reuse its independent application state machine, immediate global-validity shutdown semantics, profile overrides or its results as Core campaign evidence.

Follow procedure §7.3 for README files, licenses, Doxygen, `@test` tags and script documentation. Any later production change needs a worklog entry. Keep the same Pulsim version in the C-Block, CLI and GUI paths.

## 11. Test records and GUI evidence

Integrate L1 with `tests/run_all.sh` and `tests/common/make_record.py`, emitting the existing per-criterion `@@` format. Build/ABI errors, crashes and missing result lines must not become passing campaigns. Preserve the existing rule that known failures still fail a campaign; model absence is reported explicitly as `BLOCKED`.

Stamp the Core commit/dirty state, actual dependency revisions, wrapper/contract revision, named application profile, plant variant and hashes, parameter provenance, Pulsim/compiler/Python versions, integration settings, sample phase, initial-state description and scenario timeline. Include test-only harness identity for every L1-GFL result. Distinguish repeated campaigns/profiles and retain their separate raw data.

Retain measured signals and applied duties/enables, output-action flags, relay request/contact state when applicable, freshness flags and timestamps, firmware telemetry, source/model manifests and plots. Use an explicit capability list so a PV-, averaged-model- or relay-dependent case cannot run against an unsuitable fixture unnoticed.

The GUI should open the same versioned plant and execute the same firmware block as the command-line campaign. Saved waveform inspection must show which campaign/profile produced the data. Valpha/Vbeta, Vq, frequency, qualification and synchronization remain useful telemetry, but a pleasing waveform is not a replacement for the catalogue's verdicts.

## 12. Proposed first review milestone

The first milestone is a **small, reproducible L1-APP synchronization-only bring-up** against the switched plant: current Core source, 230 V RMS / 50 Hz grid, relay open, inverter PWM off, real six-sensor mapping and actual C-Block execution. Treat it as integration evidence while the formal procedure gates remain open.

Then add the averaged plant for unchanged 10 V/s startup, resolve the criterion clarifications and library finding, and proceed to the formal application campaign. Run connection and current-step scenarios through the separate library harness until an approved production application actually implements those features.

This uses the refined power-stage drawing while keeping the question under test precise: **does the current Core firmware block behave as documented when its outputs drive the plant model?** Hardware timing and the actual flashed target remain a later test boundary, as the procedure specifies.
