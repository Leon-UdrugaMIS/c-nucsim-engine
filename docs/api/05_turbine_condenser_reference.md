# Turbine and Condenser Reference

## Run-up

`turb.auto_runup` enables first-order shaft acceleration toward `turb.runup_target_rpm`, which accepts `STOP`, `900`, `1800`, `2700`, or `3600`. The current model uses a deterministic 15%/s response toward the selected target. `turb.rpm` is published in RPM.

## Synchronization

`turb.gen_breaker_sync` closes the modeled generator breaker only when turbine speed is between 3500 and 3700 RPM and `turb.exciter_avr_breaker` is closed. Otherwise the command returns `ERR_SEQUENCE_PERMISSIVE` or `ERR_INTERLOCK` and does not change generator state.

`turb.sync` is an object-valued tag containing `phase_angle_deg`, `slip_hz`, `freq_diff_hz`, `voltage_diff_pct`, `sync_permissive`, and `breaker_closed`. Generator load is zero until synchronization succeeds. Once synchronized, gross generator load follows the reduced-order reactor power calibration.

## Condenser

`cond.auto_control` changes condenser pressure toward 60 mbar using a first-order response. With automatic control disabled, pressure returns toward atmospheric pressure. This is a reduced-order correlation and does not yet model individual circulation pumps, SJAEs, CARs, cooling-tower heat rejection, or offgas recovery.
