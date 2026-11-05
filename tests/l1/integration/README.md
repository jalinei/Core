# L1 integration checks

Run `bash tests/run_all.sh --l1` from Core. `../run.sh` builds the native firmware block, compiles `test_wrapper.cpp` with strict warnings and UBSan, then runs the Python RC and switched-model checks in Core's pinned environment. Every criterion emits the existing `@@` protocol. Exceptions/crashes/build errors make the campaign fail; formal catalogue prerequisites are reported separately as BLOCKED.

- `test_wrapper.cpp`: L1-INT-01; direct unchanged `controlStep` versus ABI outputs, cold phase/VAC age, independent state, wrong ABI/timing rejection.
- `test_rc.py`: L1-INT-02; actual `pulsim.add_c_block(lib=...)` execution, 31 calls in 3 ms including t=0, changing RC feedback, identical fresh runs.
- `test_model.py`: L1-INT-03/04; accepted switched model, 13001 native calls in 1.3 s including t=0, nominal sync with bridge/relay off, physical current binding, tick-aligned short STARTUP and actual 10 V/s reference, VAC delivery dropout at age 200 while electrical VAC continues.

The nominal 50 Hz integration result does not close known finding #1 or replace gated L1-APP-06/07. The short startup is not the 40 s zero-to-400 V application test. No physical current/regulation limit is invented. Test-call order and time are explicit: sources deliver commands for the next integration interval endpoint; each C-Block tick reads the resulting state, calls native code, then the hardware adapter applies stop/update/hold actions.

Known implementation constraints are documented in `../README.md`: predeclare C-Block output nodes for Pulsim 2.0 current-index stability, and keep intentional NaN observer telemetry out of diagnostic-only electrical stamps while retaining it in the actual output log.

- `test_pv.py`: L1-INT-05; native single-diode source with matched passive load, unchanged Core MPPT with a freely responding PV voltage, and an initial 2 ms 50/25 ns refinement check. Short-run power ratio is INFO, not settled MPPT acceptance. The original full model and its twelve integration criteria remain in the campaign.
