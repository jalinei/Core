# Switched DCDC application tests

The active test plan uses the supplied switching circuit throughout. There is no averaged-model prerequisite. The application and controller code remain unchanged; a derived model adds visible simulation loads and declared initial capacitor voltages.

## Declared cases

| Scenario | Initial bus | Simulation load | Run and observation window |
|---|---|---|---|
| `dcdc_startup.json` | 0 V | 20 kohm | First 50 ms; reference at `app::startup_ramp_rate` (200 V/s), physical leg-1 current <= 1 A, leg 2 disabled |
| `dcdc_busreg.json` | 400 V | 20 kohm, then parallel 10 kohm switched on at 50 ms | Run 100 ms; assess 395–405 V over 80–100 ms |
| `dcdc_burst.json` | 406 V | 10 kohm | Run 150 ms; initial upper-bound pause and electrical discharge to 395 V; later upper crossing assessed separately |
| `dcdc_startup_full.json` | 0 V | 20 kohm | 2.5 s full-startup scenario (2 s ramp at 200 V/s, then hold); not part of the default short campaign |

All cases retain the ideal 40 V input, original source resistance, transformer model, switches, diodes, input/output filters and existing 200 kohm bleed. The inverter remains disabled, as in the real application. The separately named simulation load consumes the DCDC output power; it does not represent grid injection or an added board component. The load values are reproducible software fixtures within the controller's nominal input-power envelope, not measured appliance loads.

The 395–405 V band is the existing plan criterion. The 50 ms load event and 80–100 ms observation window are declared engineering test conditions, fixed before execution. A failure in this window establishes failure of that scenario, not a universal stability or hardware verdict. Report the trace and model assumptions before attributing it to firmware tuning.

## Initialization and numerical evidence

Startup retains the accepted zero-bus initial condition. For precharged cases, the bus capacitor starts at the declared bus voltage, the secondary split capacitors each start at half that voltage, and the primary upper split capacitor starts at `max(Vbus * transformer_ratio - Vin, 0)`. The lower split capacitor starts at the input voltage. Inductor/winding currents remain zero; controller integrators start from `controlInit`, not from an invented steady controller state.

Floating secondary winding/rectifier nodes start at their secondary midpoint voltage so an unenergized transformer does not acquire a fictitious initial differential voltage from zero-filled algebraic nodes. Run manifests record capacitor voltage consistency and initial transformer voltage mismatch. These checks establish consistency of the declared initialization; a precharged fixture is still a transient, not a proven steady operating point.

PWM is still modeled at 200 kHz with the versioned actuator adapter. Active runs use 50 ns electrical steps. Physical input-current extrema are observed at every electrical callback before waveform decimation; the callback count is checked against the number of integration steps. `ILow1`/`ILow2` in saved traces are the firmware's held sampled inputs, while `IP(DC1_Iin)`/`IP(DC2_Iin)` expose stored electrical probe samples. The manifest extrema retain peaks between stored samples.

The regulation case is also characterized at 25 ns to assess timestep sensitivity. Compare the same declared observation window, including both the bus envelope and transient differences. The result is numerical evidence for this fixture; physical device values and the target PWM/ADC adapter still require their own verification.

## Run and inspect

From Core:

```sh
# Campaign: integration checks plus selected formal application cases
bash tests/run_all.sh --l1

# Open a declared fixture in the GUI, then click Run
./tests/l1/open_gui.sh --scenario tests/l1/scenarios/dcdc_busreg.json

# Standalone run, followed by direct saved-trace inspection
.venv/bin/python tests/l1/runner.py --scenario tests/l1/scenarios/dcdc_busreg.json --out tests/out/my-busreg
./tests/l1/open_gui.sh --results tests/out/my-busreg
```

Saved DCDC results reconstruct their fixture and settings in the GUI. The default plot shows bus voltage, plus the startup reference for STARTUP. Use the Waveform Viewer to select current references, physical probes, duties, enables and `burst_paused`. The native Core C-Block, separate load and load-step switch are inspectable in the derived schematic. Original power connections remain explicit wires; signal Goto/From usage is unchanged.

For a campaign folder, use its `dcdc_startup`, `dcdc_busreg` or `dcdc_burst` subfolder as `--results`. Each contains raw waveforms, sampled controller inputs/outputs and a manifest. The campaign record preserves FAIL verdicts and identifies unexecuted criteria.

## Coverage limits and next steps

- L1-APP-01 checks IDLE and the one-second observer qualification dwell, measured from continuously healthy estimates rather than from source energization. Lock time remains INFO.
- L1-APP-02 checks the first 50 ms. Its full-trajectory completion criterion stays BLOCKED until a complete run is assessed. At 200 V/s the ramp takes about 2 s; `dcdc_startup_full.json` runs 2.5 s, about 50 million electrical steps at 50 ns: about 30 minutes on Jean's machine, about 1.3 hours on Luiz's laptop. It is run on demand, not in the default campaign. No averaged model is substituted.
- L1-APP-03 assesses the explicitly declared regulation window. Keep failure evidence and investigate current control, sampling, actuator mapping and fixture transients before changing gains.
- L1-APP-04 checks initial pause/disable and release. If the bus never returns to 405 V during the run, subsequent rising-edge and repeated-cycle coverage remains BLOCKED; an initial pause alone is not a completed cycling test.
- L1-APP-08 assesses actual lost VAC delivery and the 200-period stale threshold using the switching model.
- Sustained MPPT, the remaining mode-transition/grid-disturbance scenarios and inverter-library relay/RUN cases remain separate work. The existing frequency finding gates the affected formal verdicts, not unrelated DCDC tests.

See the [switched results report](switched-dcdc-results.md) and its campaign record for measured values and failures. A new FAIL is evidence to investigate; it does not justify silently moving the observation window, relaxing a limit, or retuning production code.
