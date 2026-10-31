# Built-in Scenarios

The line-oriented server accepts `scenario <name>`. Scenario loading resets simulation tick and time, clears malfunctions, applies deterministic initial conditions, and publishes a `scenario_status` response.

## `cold_shutdown`

- Reactor mode: `SD`
- Reactor pressure: approximately 101.3 kPa
- Reactor power: 0%
- Turbine speed: 3 RPM
- Offsite breakers: open
- Generator: unsynchronized

Use this scenario for startup procedure tests.

## `full_power`

- Reactor mode: `RUN`
- Reactor pressure: 7100 kPa
- Reactor power: 100%
- Turbine speed: 3600 RPM
- Generator: synchronized
- Bus A and safety-bus lineup: energized

Use this scenario for steady-state telemetry and load tests.

## `scram_recovery`

- Reactor mode: `SD`
- Reactor power: 0%
- Reactor pressure: approximately 101.3 kPa
- `rps.scram_active`: true

Use this scenario to exercise rod insertion, alarm state, and recovery controls.

These are reduced-order deterministic presets. They are not replacements for the full cold-start, LOOP, loss-of-vacuum, or 24-hour soak procedures described in the master specification.
