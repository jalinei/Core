# L1 diagnostics

Scripts that explain a failing criterion from a saved run. They print `INFO` only, never change a verdict and are not part of the campaign. Each one names the finding it serves.

| Script | Finding | What it shows |
|---|---|---|
| `duty_sweep.py` | [#3](https://github.com/luizvilla/Core/issues/3), H3 | Open loop: power the DC/DC delivers at 400 V for fixed duties (both legs), with the BUSREG load. Writes `duty_sweep.json` and `duty_sweep.png`. About 5 min for 10 duties (4 in parallel) |
| `busreg_balance.py` | [#3](https://github.com/luizvilla/Core/issues/3) | Source power vs bus voltage, firmware-sampled vs physically averaged input currents, and the current-reference/duty history of a `dcdc_busreg` run |

```bash
# From Core, on any campaign's regulation folder (records are local, see procedure §7)
.venv/bin/python tests/l1/diagnostics/busreg_balance.py tests/out/<campaign>/dcdc_busreg
.venv/bin/python tests/l1/diagnostics/busreg_balance.py tests/out/<campaign>/dcdc_busreg 0 0.05   # other window, s
.venv/bin/python tests/l1/diagnostics/duty_sweep.py --out tests/out/duty-sweep
.venv/bin/python tests/l1/diagnostics/duty_sweep.py --out tests/out/duty-sweep --plot-only --mark "leg 1 at 100 ms" 0.221
```
