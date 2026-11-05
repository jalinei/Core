# Integration scenario timelines

These fixtures supply existing operator commands and sample-delivery flags. They do not reimplement Core's application logic or connect its inverter.

- `idle_healthy.json`: 1.3 s, mode IDLE, sync request true, 400 V reference, fresh grid samples. All switches disabled; 1 us electrical step for nominal integration only.
- `startup_short.json`: ten IDLE control periods then STARTUP exactly at 1 ms; 10 ms switched window at 50 ns. The startup rate is the production `app::startup_ramp_rate`.
- `vac_dropout.json`: fresh VAC until 10 ms, then no new delivery. Electrical grid remains healthy; the wrapper holds the last delivered sample. InvalidMeasurement is expected at 29.9 ms, exactly 20 ms after the final fresh sample at 9.9 ms.

Times are simulated seconds and command events must align to the 100 us control period. A command arriving on a control tick is delivered on that tick, not a tick later. Mode values follow the existing boost enum. `v_bus_ref` is volts. `sync_request` and `vac_updated` are Boolean. Unknown/missing prerequisites are not silently replaced with another controller/profile.

These are L1-INT integration fixtures, not the formal L1-APP catalogue cases. A scenario duration is a run horizon, not an invented performance requirement.
