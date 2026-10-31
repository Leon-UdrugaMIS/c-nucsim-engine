# Reactor and ECCS Reference

## Rods

The engine publishes `rod.01.pos_pct` through `rod.24.pos_pct`. Zero is fully inserted and 100 is fully withdrawn. `rmcp.group_movement` accepts `INSERT`, `/`, or `WITHDRAW`; the client must resend a held position before the 600 ms dead-man timeout. Current movement is a reduced-order common-bank model; group membership and individual HCU variation are scheduled for the detailed rod-drive phase.

Rod movement requires reactor mode other than `SD`, scram inactive, and drive-water pressure at least 5500 kPa. Scram insertion is 25 percentage points per simulated second and is driven by the two-button scram command or a high-pressure trip.

## Reactor level and pressure

`reactor.level_nr_m` is relative to normal level. A feedwater pump or valve malfunction causes a deterministic level decline. `reactor.pressure_kpa` follows reduced-order thermal power; `nuc.rtp_pct` is driven by APR control toward `rmcp.apr_setpoint`.

## RCIC

RCIC starts when `eccs.rcic_turbopump` is enabled or level reaches the low-low threshold of -1.9 m. It requires emergency-bus power, reactor pressure above 1000 kPa, and no `mf.eccs.rcic_fail_start`. Active output is published as `eccs.rcic_flow_kgs` and `eccs.rcic_turbine_rpm`.

## RHR/LPCI

RHR pump controls require the safety bus. A pump produces reduced-order flow when its pump command is true and its injection valve is `OPEN`. LPCI flow is blocked above 1500 kPa. RHR/SDC flow is blocked above 1000 kPa. The current model does not yet include detailed heat-exchanger temperatures, suppression-pool energy, or valve stroke timing.

All pressure, level, and flow values in this file are correlation-based or gameplay-tuned approximations.
