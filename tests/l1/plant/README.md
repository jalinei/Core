# Accepted switched plant model

`full_model.pulsim` is the reviewed import of workspace `Model/full_model.pulsim`, SHA-256 `58839236cbd50d8564e36793e33aaa6ee3ab58935eed73dbf724a069da5529de`. It retains the user's physical layout, six real probes and 230 V RMS / 50 Hz grid. The original is not modified by these tests.

`params.yaml` is JSON syntax valid as YAML 1.2. It records all physical component parameters and their sources/confidence and is checked against the accepted schematic by `import_model.py`. Unsupported/mismatched components fail setup. Both averaged/PV capabilities are explicitly false. For a new reviewed model import, replace the snapshot deliberately, inspect changes and run `python3 tests/l1/plant/import_model.py --write`; do not silently regenerate the author's model or update hashes to conceal an unexplained drift.

`model.py` stamps the coupled windings and actual probe terminals into native Pulsim. Current probes are zero-volt series sources; differential voltage measurements use unity VCVS measurement nodes. Only those six measured signals feed the firmware block. Source/probe polarity and current-index stability are checked separately. Native source frequency is 50 Hz, amplitude is peak (325.269119 V), not 230 V peak.

The output-filter reference image is in `reference/output_filter.png`. Input/DC/DC values were supplied by the original current-fed half-bridge model. Choke coupling 0.999 is assumed, device/contact parameters are placeholders, and physical DCR/saturation/calibration remain TBD. Tests depending on unavailable physical parameters must not be declared validated.

`create_gui.py` appends signal-only native firmware-block, scenario-source and timer-adapter representations to a disposable GUI view under `derived/`. It never rewrites the accepted snapshot. The main controller block has ten inputs and 32 command/telemetry outputs. It is executed via Pulsim's native C-Block API in the same runner used for tests. Power paths stay explicitly wired; only signal/control routing uses Goto/From.

The user selected switching only; an averaged variant is not a prerequisite or planned deliverable. Long 10 V/s startup, sustained PV MPPT and repeated burst cycling still need coverage. Standalone commands are in `../README.md`.

PV source options, native feasibility results and the proposed implementation path are documented in the [PV model assessment](../../../docs/testing/pv-model-assessment.md). `assess_pv_source.py` is a standalone synthetic source bench; it does not enable PV capability in the accepted plant or change formal campaign verdicts. Run it with Core's `.venv/bin/python`.

`pv.py` derives a PV variant, using `pv-profile.json`, and supplies an independent MPP reference. `Rsource` becomes the module series resistance, the existing ideal source is replaced, and a distinct `PV_bus_test_load` is wired across the bus. The accepted snapshot's PV capability stays false; the derived variant's run manifest reports true. The translator supports its native current source and exponential junction. See the [PV run guide](../../../docs/testing/pv-model-run.md).

`fixtures.py` adds explicit bus test loads and declared capacitor precharge for switched application scenarios. Startup begins at 0 V; BUSREG begins at 400 V with a load step at 50 ms; the burst-release fixture begins at 406 V. Floating transformer secondary/rectifier voltages are initialized consistently without modifying stored capacitor/inductor energies. Per-step current extrema and initial-condition checks are stamped into run manifests. See [switched DCDC tests](../../../docs/testing/switched-dcdc-tests.md).
