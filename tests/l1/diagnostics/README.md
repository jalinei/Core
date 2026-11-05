# L1 diagnostics

Scripts that explain a failing criterion from a saved run. They print `INFO` only, never change a verdict and are not part of the campaign. Each one names the finding it serves.

| Script | Finding | What it shows |
|---|---|---|
| `busreg_balance.py` | [#3](https://github.com/luizvilla/Core/issues/3) | Source power vs bus voltage, firmware-sampled vs physically averaged input currents, and the current-reference/duty history of a `dcdc_busreg` run |

```bash
# From Core, on any campaign's regulation folder (records are local, see procedure §7)
.venv/bin/python tests/l1/diagnostics/busreg_balance.py tests/out/<campaign>/dcdc_busreg
.venv/bin/python tests/l1/diagnostics/busreg_balance.py tests/out/<campaign>/dcdc_busreg 0 0.05   # other window, s
```
