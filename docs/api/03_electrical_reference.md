# Electrical Reference

## Sources and buses

The reduced-order electrical solver evaluates offsite availability, breaker state, bus energization, load demand, and emergency transfer on every simulation step.

- `elec.startup_tr_breaker` connects the offsite startup transformer.
- `elec.bus_a_tr` energizes Bus A when the startup source is available.
- `elec.bus_b_tr` energizes Bus B from the same offsite source in the current kernel.
- `elec.lvac_dc_tr` transfers Bus A to the 480 VAC safety bus.
- `elec.safety_bus_to_dg` permits a running EDG to supply the safety bus.
- `elec.ac_dc_1_interlock` enables battery charging when AC power is available.

The `mf.elec.loop` malfunction removes the offsite source. If `elec.edg_remote_startup` and `elec.breaker_72dcg` are enabled, EDG-1 starts automatically unless `mf.elec.edg1_fail_start` is active. The EDG ramps from 100 RPM to 1800 RPM at 180 RPM/s, then becomes active when the safety-bus-to-DG connection is closed.

## Battery

`elec.battery_pct` starts at 100 percent. It charges at 0.01 percent/s when the safety bus and AC/DC interlock are available. With offsite AC unavailable, the emergency bus consumes 0.0069 percent/s while charge remains. `elec.panel_power_ok` is false when the emergency bus and battery are both unavailable.

## Power balance

`elec.site_usage_mw` is the sum of the enabled reduced-order loads. `elec.generator_load_mw` is derived from reactor thermal power using the current 9.11 MW/% calibration. `elec.sent_to_grid_mw` equals generator load minus site usage while offsite power is available.

These are correlation-based educational models, not grid protection or transient-analysis models.
