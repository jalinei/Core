# uSolarVerter microinverter firmware

Firmware for the OwnTech Spin board driving the uSolarVerter shield: a dual-leg boost converter feeding a DC bus, and a single-phase grid-following inverter supervised by a grid observer.

## Testing

**Test procedure**:
The document that defines every test case, the order in which they run, and how their results are recorded.
_Avoid_: test plan, test spec

**Test case**:
One identified check with a fixed setup, stimulus and numeric pass criteria.
_Avoid_: test, scenario (when meaning the whole check)

**Test campaign**:
One run of a set of test cases against a single firmware commit.
_Avoid_: test session, test run (when meaning the whole set)

**Test record**:
The filed evidence of a test campaign: setup, versions, per-criterion verdicts and raw data.
_Avoid_: report, log, result file

**Test level**:
How much of the system a test case exercises: L0 tests one object with synthetic inputs, L1 tests the controllers closed-loop against the plant model.
_Avoid_: stage, phase

**Plant model**:
The simulated uSolarVerter power stage and grid that the controllers act on during L1 test cases.
_Avoid_: simulation, model (unqualified), digital twin

**Averaged plant model**:
A plant model in which each switching leg is replaced by its duty-cycle-weighted average, used for long test cases.
_Avoid_: average model, small-signal model

**Switched plant model**:
A plant model with real switches driven by PWM, used for short test cases where ripple or switching events matter.
_Avoid_: detailed model, full model

**Firmware block**:
The application's control code, compiled unchanged and run by the simulator at the control-task rate during L1 test cases.
_Avoid_: controller model, C block (when meaning our code)

**Control step**:
One execution of the application's control logic for one control-task period: measurements and operator commands in, PWM duties and enables out.
_Avoid_: critical task (when meaning the logic rather than the scheduled task), ISR
