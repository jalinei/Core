# Switched DCDC application results

The switching-only L1 campaign on 2026-11-05 reports **24 PASS, 1 FAIL, 5 INFO and 9 BLOCKED**. The new failure is bus regulation in the declared post-load-step window. Core production sources, tuning and the accepted power schematic remain unchanged. No averaged plant was introduced.

> **Raw results are not versioned.** The `tests/out/` paths below name the campaign folders of the original run (campaign `2026-11-05_28f7f0f-dirty_181727_1IftGG`). They were kept on the machines that ran them and are not in git ([procedure §7](test-procedure.md#7-running-a-campaign)). Regenerate them with `bash tests/run_all.sh --l1` from the same commit, and the 25 ns run with `runner.py` on a copy of `tests/l1/scenarios/dcdc_busreg.json` whose `dt_s` is `2.5e-8`.

The campaign record (`tests/out/2026-11-05_28f7f0f-dirty_181727_1IftGG/record.md`) contains every criterion, its verdict and the raw-data links. This was an L1-only campaign; the previously recorded L0 frequency finding was neither retested nor closed here.

## Test conditions and objectives

All three cases use the switched model, an ideal 40 V source (100 mΩ), the board's 200 kΩ bleed, the grid present at 230 V / 50 Hz with the inverter and relay off, and a 50 ns step. Each case's review page also shows a picture of the model as run and every change from the accepted model (procedure §8.5).

| Case | DC bus at t = 0 | Loads | Commands | Objective (criterion) |
|---|---|---|---|---|
| `dcdc_startup` | **discharged, 0 V** | 20 kΩ from t = 0 | STARTUP from t = 0 | Reference ramps at the production rate; leg-1 current ≤ 1 A; leg 2 off, over the first 50 ms (L1-APP-02) |
| `dcdc_busreg` | **precharged, 400 V** (split capacitors 200 V; primary capacitor 26.7 V) | 20 kΩ from t = 0, plus 10 kΩ at 50 ms | BUSREG from t = 0, controllers from `controlInit` | Bus within 395–405 V over 80–100 ms (L1-APP-03) |
| `dcdc_burst` | **precharged, 406 V** | 10 kΩ from t = 0 | BUSREG from t = 0 | Starts paused with legs off; releases at 395 V; later rise to 405 V (L1-APP-04) |

The precharged cases start the converter on a full bus with its controllers reset: the converter has not charged that bus itself. See [finding #3](https://github.com/luizvilla/Core/issues/3) for why that matters.

## Results by scenario

These results were measured with the earlier 10 V/s startup slew rate. Production now uses 200 V/s ([worklog](worklog.md)); only the STARTUP row depends on it.

| Scenario | Result | Scope |
|---|---|---|
| Healthy IDLE | PASS: legs disabled; observer qualification dwell 1.0001 s | Dwell begins at continuously eligible estimates; synchronization occurs at 1.1309 s from source start |
| VAC delivery loss | PASS: InvalidMeasurement at age 200 | Last fresh delivery 9.9 ms; stale trip 29.9 ms; electrical grid remains present |
| STARTUP | PASS over first 50 ms: 10 V/s reference, physical current peak 0.60470 A, leg 2 disabled | Full approximately 40 s zero-to-400 V trajectory remains unexecuted |
| BUSREG | **FAIL:** 367.673–376.945 V during 80–100 ms | Required band 395–405 V; load step at 50 ms |
| Burst release | PASS: starts paused at 406 V, both legs disabled, releases at 394.9836 V at 31.7 ms | No subsequent rising 405 V crossing in 150 ms; repeated-cycle coverage remains BLOCKED |

The loads and initial energies are defined in the [fixture guide](switched-dcdc-tests.md). Regulation starts precharged at 400 V with a 20 kohm load. A parallel 10 kohm load switches on at 50 ms; the original 200 kohm bleed remains connected. Those loads are the energy sink while the inverter is disabled. This is DCDC assessment, not grid-injection assessment.

## Regulation failure and numerical refinement

At 50 ns, the sensed bus is 367.673–376.945 V in the declared window. At 25 ns, it is 367.634–376.953 V. Maximum sensed-bus disagreement over the run is 0.05270 V. The stored electrical bus probe gives nearly identical envelopes, with maximum disagreement 0.05269 V. Halving the timestep therefore does not remove the observed failure.

The comparison data (`dcdc_busreg/timestep-comparison.json` in that campaign) and the 25 ns run (`tests/out/switched-dcdc-busreg-25ns/manifest.json`) preserve this evidence. The refinement is specific to this case; it does not validate every model parameter or establish general numerical convergence.

The bus starts at 400 V and falls to about 367.7 V by 100 ms. No overshoot above 400 V is observed. Both current references reach their 1 A shares, while commanded duty at the end is approximately 0.221 for leg 1 and 0.112 for leg 2. Per-electrical-step current extrema include peaks near 17.9 A and negative excursions near -13.8 A. These excursions are part of the supplied circuit/fixture response and need investigation; reference-current limits do not themselves prove a bound on physical current.

This result means the present controller/actuator/model combination does not meet the declared 80–100 ms regulation criterion. It does not prove that regulation never recovers later or identify a hardware defect. The observation window and voltage limits were not moved to turn the test green.

## Fixture and execution checks

All three DCDC cases completed with finite electrical states. Initial capacitor-voltage errors and initial primary/secondary winding-voltage mismatches are zero for the declared fixture initialization. The model observes 1,000,002 electrical callbacks for startup, 2,000,002 for regulation and 3,000,002 for burst, including the explicit initial observation and simulator callbacks. Current peaks are collected before sample decimation. Actual Core calls remain scheduled every 100 us through the native Pulsim C-Block.

On this host, the 50 ns startup, regulation and burst runs took approximately 37 s, 73 s and 106 s respectively. The independent 25 ns regulation characterization took approximately 156 s. These costs explain why the complete 40 s startup is separately identified rather than silently replaced by a short or averaged run.

## Inspect in the GUI

From Core, open the canonical failed regulation run:

```sh
./tests/l1/open_gui.sh --results tests/out/2026-11-05_28f7f0f-dirty_181727_1IftGG/dcdc_busreg
```

The launcher reconstructs the scenario's wired fixture and opens its saved bus-voltage trace. Select `v_bus_ref` to overlay the 400 V target. Inspect `i_ref1`, `i_ref2`, `ILow1`, `ILow2`, physical `IP(DC1_Iin)`/`IP(DC2_Iin)` probes, duty and enable signals on suitable separate scales. Replace `dcdc_busreg` with `dcdc_startup` or `dcdc_burst` to inspect those runs. The GUI's actual Run path was also exercised on the regulation fixture for a short check.

## Follow-up order

1. Investigate the regulation shortfall using the saved traces: compare current demand with physical current, verify the assumed PWM-to-switch mapping and establish sampling/filtering fidelity. Review the declared precharged transient before attributing it to controller gains.
2. Characterize a longer regulation window without deleting the original failed verdict. Separate slow recovery from an unstable or incorrectly driven power stage.
3. Extend the burst fixture until a real rising 405 V crossing and repeated cycles can be assessed. The current first release is not a complete cycling test.
4. Complete the full startup trajectory or explicitly retain its unexecuted status. Continue sustained PV MPPT, mode transitions, grid faults and the later inverter-library connection cases in the plan.

No production gains, limits or controller states were altered to improve these results. The regulation failure remains new and local for investigation; no external issue was opened.
