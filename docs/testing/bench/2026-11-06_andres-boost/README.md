# Bench: Andres Vazquez Sieber's boost controller, 2026-11-06

Scope captures (Tektronix, 400 ms/div, 4 s) of the boost controller in `core-micro-verter-boost` commit `43e249e` on the uSolarVerter 0.9.0 board. Channels: Ch1 DC bus voltage (100 V/div), Ch2/Ch3 gate signals (2 V/div), Ch4 input current (200 mA/div).

| File | Test | What it shows |
|---|---|---|
| `no_load_055.png` | Start-up without load | The bus rises from 0 to about 410 V in about 1.3 s, input current about 0.3–0.4 A (the voltage loop's output is clamped at 0.25 A of current reference), then the converter enters burst (pulse skipping): sparse gate pulses |
| `with_load_057.png` | Load above the current limit, on purpose | From about 410 V in burst, the load is applied: the bus falls to about 252 V and stays there; the input current settles near the limit. The current limit is respected (intended result) |
| `without_load_056.png` | Load removed | From about 252 V, the bus climbs back to about 410 V in about 0.8 s and returns to burst |

Controller: input current PI (Kp 0.002712, Ti 341.3 µs, 100 µs) plus a 100 Hz PR; bus voltage PI every 1 ms (Kp 0.433·Vbus/Vin, Ti 4.57 ms) whose output, the current reference, is clamped at ±0.25 A; light load handled by HRTIM burst mode driven by the current error (burst duty raised when the current is above its reference at minimum duty, lowered when below). The analysis and comparison with Core's boost object are in the session notes of 2026-11-06 and in test L1-BST-09 (procedure §6).
