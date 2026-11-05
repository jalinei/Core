# Running the PV model with Core MPPT

The derived PV model runs Core's unchanged MPPT code against a native single-diode source feeding both DCDC stages. The accepted `Model/full_model.pulsim` and its imported snapshot are preserved. The new source is a disclosed synthetic software fixture, not a model fitted to a particular commercial panel.

## Open the schematic and results

From the workspace root:

```sh
# Generate the PV schematic and run it through the GUI
./Core/tests/l1/open_gui.sh --pv

# Open the existing 20 ms characterization with its traces already loaded
./Core/tests/l1/open_gui.sh --pv --results Core/tests/out/pv-mppt-bringup
```

The derived editable file is `Core/tests/l1/plant/derived/core_pv_model.pulsim`. Use this launcher to register the Core backend. The GUI shows a wired current source, exponential junction, shunt resistance and module series resistance at the input, plus a separate resistor across the DC bus. All power connections use wires. The existing firmware C-Block and its signal labels remain visible.

The source parameters are in [pv-profile.json](../../tests/l1/plant/pv-profile.json). In the GUI, the photocurrent source, diode saturation current/ideality, series and shunt resistors, test load and capacitor initial voltages are editable circuit elements. The module diode uses an effective ideality `a / Vthermal`; this is a module representation, not an individual cell's physical ideality. Change the profile to keep edits across fresh launcher runs. This milestone uses a fixed environmental condition; irradiance/temperature timelines are not implemented yet.

The scenario defaults are mode **3 = MPPT**, 20 ms stop time and **50 ns timestep**. The DC bus starts precharged to 380 V, the PV input starts at open circuit, and branch currents start at zero. This starts the electrical MPPT experiment directly; it does not pretend that Core performs a STARTUP-to-MPPT transition or that controller integrators begin at a settled operating point. Inverter PWM and relay remain off.

Click **Run**, then open **View → Panels → Waveform Viewer**. Saved results open with `pv_power` and `pv_mpp_power` selected. Select these additional signals separately as needed:

| Signals | Interpretation |
|---|---|
| `pv_voltage`, `pv_current` | Actual panel terminal voltage and current, measured by the simulator |
| `pv_power`, `pv_mpp_power` | Extracted source power and independently calculated available maximum |
| `pv_sensor_power` | Power computed from the voltage and summed converter currents supplied to firmware |
| `i_ref1`, `i_ref2`, `ILow1`, `ILow2` | MPPT's split current references and measured currents |
| `duty_leg1`, `duty_leg2`, `en_leg1`, `en_leg2` | Requested control duties and enables; applied PWM retains the versioned hardware adapter limits |
| `VDCBus`, `burst_paused` | Bus response and whether power extraction has been curtailed |

Source power and sensor-derived power can differ during transients because the input capacitors store energy. The extra PV diagnostics do not create new firmware sensor inputs. GUI runs save into `Core/tests/out/l1-gui-pv/`; CLI runs save into the explicitly requested directory. `--results` opens prior data rather than rerunning it. It checks PV parameter identity; the saved manifest remains the authority for the circuit and firmware used by that run.

## Circuit and source reference

The photocurrent is 1.6 A, shunt resistance 1000 ohm, saturation current `2.7070366766422116e-10 A`, and effective module thermal/ideality voltage 2 V. The original ideal-source resistor is replaced by the module's 0.3 ohm series resistance; there is no additional hidden 0.1 ohm. The input capacitors and converter circuit remain present.

The reference single-diode equation gives:

| Quantity | Value |
|---|---|
| Open-circuit voltage | 44.9430 V |
| MPP voltage | 38.4636 V |
| MPP current | 1.48497 A |
| Available MPP power | 57.1173 W |

A passive load equal to `Vmp/Imp` produces 38.46359250 V in the native electrical solver, matching the independent reference within 1e-5 V. The source feasibility bench also checks five clamped voltages. These are source checks, separate from controller performance.

The bus test load is 2800 ohm, in parallel with the existing 200 kohm board bleed. Its nominal demand is about 57 W at 400 V and falls with bus voltage. It permits sustained power flow in principle, without enabling the inverter, but a precharged capacitor can also supply it during a short run. A falling bus therefore cannot be interpreted as successful PV delivery.

## First switched results

The 20 ms characterization, using 50 ns steps, completed with finite states and 201 native Core calls, including time zero. It took approximately 57 s on this host. The bridge and relay remained off, and no native block contract error occurred.

| Observation | Result |
|---|---|
| PV voltage | Approximately 44.74–45.11 V, close to open circuit |
| Average PV power in the last 5 ms | Approximately 0.88 W, versus 57.12 W available |
| DC bus | 380 V initially, 358.23 V finally |
| Leg 1 current reference | 0–0.4 A across the run |
| Leg 2 current reference | Zero throughout |
| Burst state | Not paused |

**This is not successful MPPT tracking evidence.** The source is connected and responds electrically, but little available power is extracted during the observed interval. The bus capacitor supplies most of the load. Twenty milliseconds is also too short to declare a settled tracking failure; retain the trace as a characterization and investigate before choosing acceptance windows or changing gains.

A separate 2 ms refinement check compared 50 ns and 25 ns steps on the same sampled time grid. Maximum differences were approximately 0.0348 V in panel voltage and 0.00279 V in bus voltage. This supports the short integration check only; it does not establish convergence over the full 20 ms or at a settled MPP.

## Next assessment steps

1. Exercise the DCDC current loop with a known fixed current demand in a clearly labeled test harness, checking measured-current polarity, voltage headroom, duty mapping and settling. Keep that distinct from the unchanged application path.
2. Compare actual current demand/response with Core's 0.1 A perturbation every 1 ms. Establish whether the current loop settles between perturbations before blaming the PV model or retuning MPPT.
3. Review acquisition fidelity. The current software adapter supplies instantaneous simulated measurements; establish the physical board's ADC trigger phase and filtering/averaging before claiming equivalent power sampling. `pv_sensor_power` and source power help identify capacitor transients and switching ripple; neither alone proves a firmware defect.
4. Extend the run and refine timestep over the declared observation window. Compare load energy with changes in capacitor/inductor energy and delivered PV energy. Retain the switching model for longer campaigns, as selected in the active test plan; report their runtime and coverage explicitly.
5. Add irradiance and cell-temperature profiles, then define settling and tracking acceptance conditions. Include bus/current constrained cases separately from a reachable MPP. A real-panel datasheet can calibrate the synthetic source later.

Formal L1-APP-05 remains pending sustained tracking assessment, rather than a missing PV source. The source/passive-load, Core coupling and initial timestep checks are recorded as L1-INT-05, with short-run extracted-power ratio reported as INFO. No production controller tuning, mode sequencing, or sensor count changed.

## Reproduce the runs

From Core:

```sh
.venv/bin/python tests/l1/plant/assess_pv_source.py
.venv/bin/python tests/l1/integration/test_pv.py
.venv/bin/python tests/l1/runner.py --scenario tests/l1/scenarios/pv_mppt.json --out tests/out/pv-mppt-bringup
bash tests/run_all.sh --l1
```

The raw result folders contain `waveforms.npz`, `control.csv` and `manifest.json`, including actual PV circuit parameters, independent MPP, bus load, source-profile hash, base-model identity, derived circuit hash and firmware source/build identity. The campaign additionally records integration-source hashes and the environment. Do not overwrite a result folder you want to retain: choose a new `--out` name for another characterization.
