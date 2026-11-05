# PV model options for the PULSIM tests

Assessed on 2026-11-05 against PULSIM 2.0.0, the refined `full_model.pulsim`, and Core's current application. **Use a native single-diode equivalent circuit first, with a tabulated current–voltage model as an alternative if nonlinear switching performance is inadequate.** Both can leave the firmware unchanged. The source bench, converter integration and GUI variant are now implemented; sustained dynamic validation remains pending. See the [PV run guide](pv-model-run.md) for the measured results.

## What the current test needs

The present input is an ideal 40 V supply with `Rsource = 0.1 ohm`. Its voltage and resistance do not define a panel's short-circuit current, open-circuit voltage, or power maximum. Replacing it must let input voltage respond electrically to the converter's current demand.

[Core's MPPT](../../src/boost_converter/boost.cpp) perturbs the total current reference by 0.1 A at each 1 ms cold-loop call, comparing `VLow * (ILow1 + ILow2)`. Its present total reference range is 0–2 A, with converter 1 assigned up to 1 A and converter 2 taking the excess. A first positive tracking fixture should have its maximum-power current within that range. A second fixture should deliberately put the maximum outside that range and assess constrained operation rather than report controller failure for unavailable power.

Both DCDC stages currently share one source and one voltage measurement. Start with one shared panel feeding both stages. Two independently shaded panels would require reviewing the electrical topology and the firmware's single-voltage power calculation.

There must also be a sustained energy sink. The application keeps the inverter and relay off, while `Rbus_bleed = 200 kohm` consumes only 0.8 W at 400 V. Tens of watts of PV input will charge the bus until burst protection intervenes. Add an explicit simulation load for DCDC/MPPT assessment; distinguish source tracking from bus-limited curtailment. For the approximately 57 W synthetic source below, a load around 2.8 kohm consumes 57 W at 400 V, but the selected load still needs checking against conversion losses, equilibrium bus voltage, and burst thresholds. A programmable DC load offers better control of separate operating cases.

## Available approaches

| Approach | Strength | Limitation | Use here |
|---|---|---|---|
| Native single-diode equivalent circuit | Electrical coupling is solved with the plant; visible current source, diode and resistors | Adds nonlinear solves; module fitting and switched performance require validation | Recommended first implementation |
| Tabulated current–voltage curves | Supports measured curves, predictable data and deliberately unusual shapes | Needs a solver-compatible nonlinear table element or explicit PWL segment logic | Alternative runtime representation and measured-panel support |
| Analytic behavioral current source | Compact `I = f(V, irradiance, temperature)` interface | Explicit delayed feedback can destabilize or distort the source; needs implicit coupling or convergence checks | Use if native circuit proves too expensive and a suitable stamp is available |
| Measured curve family | Can represent a particular panel or PV emulator directly | Requires calibrated curves, environmental metadata and interpolation rules | Best later calibration input to a table model |
| Ideal voltage source plus resistor | Already runs cheaply | No panel current plateau or realistic voltage knee; power optimum is set by artificial source resistance | Keep for converter bring-up, not panel MPPT validation |

The physical reference for the first option is the standard single-diode model with photocurrent, diode saturation current, series resistance, shunt resistance and ideality factor. It defines a complete I–V curve. [Sandia PVPMC model description](https://pvpmc.sandia.gov/modeling-guide/2-dc-module-iv/single-diode-equivalent-circuit-models/).

For the module equation, use:

```text
I = IL - I0 * (exp((V + I*Rs)/a) - 1) - (V + I*Rs)/Rsh
a = n * Ns * k * Tcell / q
```

`a` is an effective module voltage parameter. Keep cell and module parameters distinct. For changing irradiance and cell temperature, use a documented parameter transformation such as De Soto; ambient temperature is not cell temperature. [pvlib single-diode workflow](https://pvlib-python.readthedocs.io/en/stable/user_guide/modeling_topics/singlediode.html), [De Soto parameter calculation](https://pvlib-python.readthedocs.io/en/stable/reference/generated/pvlib.pvsystem.calcparams_desoto.html).

## Native PULSIM feasibility

The installed `CircuitBuilder` exposes `add_current_source`, `add_shockley_diode` and ordinary resistors. Its existing `add_diode` is a binary PWL approximation and should not replace the exponential PV junction. The fixed-step `pwl` simulation path documents automatic nonlinear refresh; the variable-step `trbdf2` path currently excludes nonlinear devices. These are observations from the installed 2.0.0 API, not claims about future versions.

The [standalone source bench](../../tests/l1/plant/assess_pv_source.py) uses a photocurrent source parallel to an exponential diode and shunt resistor, followed by a series resistor. It compares five clamped terminal voltages with an independent scalar root solve. Run from Core:

```sh
.venv/bin/python tests/l1/plant/assess_pv_source.py
```

Synthetic parameters: `IL = 1.6 A`, `I0 = 2.7070366766422116e-10 A`, `a = 2 V`, `Rs = 0.3 ohm`, `Rsh = 1000 ohm`. They are a software fixture, not a fitted commercial panel. The native diode has an additional `G_min = 1e-12 S` for numerical regularization.

| Terminal voltage | Native source current |
|---|---|
| 0 V | 1.59952014 A |
| 20 V | 1.57951859 A |
| 35 V | 1.55093025 A |
| 40 V | 1.39761240 A |
| 45 V | -0.03630063 A |

Maximum absolute disagreement was below `4e-11 A`. The reference curve has `Voc = 44.9430 V`, `Vmp = 38.4636 V`, `Imp = 1.48497 A`, and `Pmp = 57.1173 W`. Negative delivered current at 45 V is expected: that voltage is above open circuit and the clamp supplies current into the model. Do not silently clamp this behavior without specifying a blocking diode or another physical reason.

This verifies source-equation feasibility at steady clamped points. It does not establish switching convergence, panel transient accuracy, irradiance delivery, or successful closed-loop MPPT.

## Parameters and fidelity

Three parameter avenues are practical:

1. **Synthetic fixture now.** Use the disclosed curve above to exercise reachable and repeatable tracking. No panel selection is needed to begin.
2. **Datasheet fit.** Obtain the panel identity, `Voc`, `Isc`, `Vmp`, `Imp`, temperature coefficients, cell count and reference conditions. `pvlib.ivtools.sdm.fit_desoto` estimates model parameters from these inputs. Its documentation warns that fits are not constrained against negative resistances; validate signs and residuals and convert percentage temperature coefficients into the required absolute units. Pin the chosen pvlib version and record all fitted parameters. [Official fitting API](https://pvlib-python.readthedocs.io/en/stable/reference/generated/pvlib.ivtools.sdm.fit_desoto.html).
3. **Measured calibration.** Fit the same model or retain full measured curves at several irradiances and cell temperatures. Reserve curves for independent validation, rather than evaluating only the data used for fitting.

Use pvlib for parameter fitting, curve generation and an independent reference. It need not run a Python root solve every 50 ns switching step. A generated table can be used with the switching converter if the electrical adapter is validated.

The simple model is quasi-static. Preserve the board input capacitors; investigate panel and cable capacitance separately if those dynamics matter. Do not invent a capacitance merely to conceal numerical instability. Partial shading with bypass diodes produces multiple power maxima and requires substring/bypass modeling or measured curves. It is a later stress case: the present local perturb-and-observe controller should not be assumed to find the global maximum.

## Proposed implementation and acceptance

1. Add a derived PV plant variant with a named, wired PV source section. Preserve the accepted full-model snapshot. Keep both converter power connections explicit; only scope, control and data signals use Goto/From. Extend the strict translator for current sources and a clearly parameterized exponential diode; these PV elements are now supported by the translator.
2. Replace the ideal supply rather than placing the PV source in parallel with it. Decide whether the existing 0.1 ohm resistor represents external wiring or part of the fitted module series resistance, avoiding double counting. Update source-dependent initialization, including capacitors currently initialized to 40 V; establish open-circuit and precharged startup fixtures explicitly.
3. Validate static I–V sweeps, open circuit, near short circuit, MPP and operation above `Voc`. Propose a source-table/interpolation target of less than 0.5% MPP error against an independent reference; record an agreed criterion before assigning a formal verdict.
4. Run the source with passive loads, then the unchanged native Core controller. Compare 50 ns and 25 ns switched steps and record nonlinear iterations, runtime, energy balance and MPP estimates. If native performance is insufficient, benchmark a tabulated model using the same reference curve. The active test plan retains the switching converter for long runs as well.
5. Add constant irradiance, irradiance steps/ramps and cell-temperature cases. Temperature should normally change more slowly than irradiance; abrupt temperature changes can be labeled numerical stress tests. Deliver environmental events independently of firmware measurements and preserve exact 100 us control calls.
6. Observe actual PV power, sensor-derived power, total current reference, both physical currents, bus voltage, burst state and environmental inputs. Additional source-side probes are simulator diagnostics, not new board sensors fed to firmware. Input-capacitor current means source current and summed converter currents need not match instantaneously; compare power over appropriate windows.
7. Calculate the reference MPP independently of the controller. Report steady tracking ratio and energy tracking ratio `integral(Ppv dt) / integral(Pmpp dt)` over declared windows, with settling and ripple. Report bus-limited or current-limited intervals separately. Do not hide input-capacitor energy changes in transient power accounting. Characterize the 0.1 A / 1 ms perturbation before fixing a tracking-efficiency threshold.
8. Make the PV parameters, environmental schedule, external load, reference power and tracking plots inspectable through the same GUI backend. Record source model identity and profile in each manifest. Only then judge the formal L1-APP-05 case; a PV source alone does not satisfy it.

The native source, matched passive load check, GUI variant and short unchanged-Core MPPT run are now implemented; see the [PV run guide](pv-model-run.md) for current results. Sustained tracking assessment and environmental sweeps remain next. A real-panel datasheet improves calibration but does not block those software fixtures.
