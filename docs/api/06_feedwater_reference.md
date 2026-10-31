# Feedwater and Water Inventory Reference

The current engine includes a reduced-order water inventory model for the two CSTs, condenser hotwell, deaerator, condensate pumps, and feedwater pumps.

## Controls

- `cst.cst1_makeup`, `cst.cst1_drain`, `cst.cst2_makeup`, and `cst.cst2_drain` change the corresponding CST level when Bus A is energized.
- `mcc.cond_pump1` or `mcc.cond_pump2` establishes condensate availability when the safety bus is energized.
- `mcc.fw_pump1` or `mcc.fw_pump2` produces nominal feedwater flow when condensate is available.

Makeup and drain are mutually exclusive in the reduced-order balance. Each active CST makeup or drain command changes level at 0.10 percentage points per simulated second. Levels are bounded to 0..100 percent.

## Published tags

- `cst.cst1_level_pct`
- `cst.cst2_level_pct`
- `cond.hotwell_level_pct`
- `dea.level_pct`
- `fw.flow_kgs`

With condensate and feedwater pumps running, feedwater flow is 1500 kg/s. A feedwater pump malfunction forces flow to zero and participates in the existing reactor-level decline and RCIC response chain.

This is a correlation-based educational model. Individual pump curves, preheater heat transfer, polisher chemistry, deaerator pressure dynamics, and detailed three-element control remain future subsystem work.
