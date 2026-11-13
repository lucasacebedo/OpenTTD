# Economy Lab model card

Version economy-lab/1.0. The rows below are the same register `lab::FormatRegister()` prints.

The measured game GDP is transport value added minus route subsidies. Cargo pieces are not added into GDP. Policy points convert at 1000 points = calibrated real GDP. Potential output follows E-027 and is not replaced when GDP leaves that range; the panel reports a calibration error instead.

F2 is the integer laboratory without idiosyncratic shocks. F0 adds a shock of -1, 0 or 1. The distance between them is the published multi-fidelity error. Provider logs redact secrets. Decisions with the same context replay exactly.

Scenarios: `docs/model/scenarios/`. Golden baseline: `docs/economylab/baseline.txt`.
