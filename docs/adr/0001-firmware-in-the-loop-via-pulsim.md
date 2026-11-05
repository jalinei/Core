---
status: accepted
---

# L1 tests run the real firmware inside Pulsim through a `controlStep` seam

L1 test cases need the controllers (boost, grid observer, grid-following inverter, and the application logic that sequences them) closed-loop against a model of the uSolarVerter. We decided to extract the application's per-tick control logic from `loop_critical_task` into a hardware-free `controlStep()` function, compile it unchanged with the controller sources into a shared library, and run it as a Pulsim C-Block at the 100 µs control-task period, so the same application/controller source is compiled for the host tests and the Spin board. This is source identity; it does not imply identical host/ARM machine code or hardware timing.

## Considered options

- **Re-implement the controllers in the simulator** (Python, PLECS C-script, Simulink blocks): rejected because the re-implementation drifts from the firmware and the tests would then verify the copy, not the product.
- **PLECS or Simscape** (models already exist in `OwnModels`): rejected because they need licences and cannot be reproduced from this repository alone.
- **Pulsim with a C-Block wrapping `controlStep`**: chosen. It is open source (MIT), installs from PyPI, compiles C/C++ through a stable ABI, and the model and its parameters can live in this repository.

## Consequences

- `main.cpp` keeps sensor reading and PWM writing; anyone editing the critical task must keep control logic inside `controlStep`, or it escapes L0 and L1 testing.
- ADC scaling, sensor calibration, PWM timing and dead time are outside the seam and are not covered by L1.
- Simulator behaviour affects results, so the Pulsim version is pinned and stamped in every test record.
