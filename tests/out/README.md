# Committed simulation results

This snapshot preserves all results accumulated during the PULSIM integration, PV assessment and switched DCDC work, including preliminary/development runs. `snapshot-manifest.json` records the byte size and SHA-256 of every pre-existing result file. Build products and Python environments are excluded; source, dependency pins and run manifests support rebuilding.

The canonical latest switched application campaign is [2026-11-05_28f7f0f-dirty_181727_1IftGG](2026-11-05_28f7f0f-dirty_181727_1IftGG/record.md): **24 PASS, 1 new FAIL, 5 INFO, 9 BLOCKED**. The failed criterion is L1-APP-03.a (regulation in the declared window). The [results report](../../docs/testing/switched-dcdc-results.md) explains its limits and the independent 25 ns comparison. This is L1 only; the older five known L0 frequency failures remain documented in the combined campaigns.

The `2026-11-05_28f7f0f-dirty_165933_YDB1dI` folder preserves the combined integration baseline (92 PASS, five known FAIL, 11 INFO, 12 BLOCKED). `pv-mppt-bringup` contains the 20 ms PV/MPPT characterization. `switched-dcdc-busreg-25ns` contains the regulation refinement. Development and GUI folders are retained as historical evidence; their names do not supersede the canonical campaign.

Run manifests intentionally retain the commit/dirty identity at execution time. They were not rewritten to name the subsequent archival commit. Integration source hashes identify the exact tested code. The archive captures results as they existed; failed and incomplete cases are preserved.

`tests/out/` remains ignored for future generated files. This snapshot was explicitly included in Git at the user's request. Existing tracked result files can still be overwritten by a GUI run or an explicit reused `--out` path, so choose a fresh output folder to retain committed evidence.
