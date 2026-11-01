# Control and Telemetry Inventory

The live engine registry is authoritative and is available through `GET /api/v1/tags` and `GET /api/v1/controls`. The line server exposes the same identifiers through its snapshot and command paths.

## Control groups

The live registry now includes controls for CRD/HCU, CST makeup and drain, electrical breakers and source selection, EDG startup/trip, RCIC/RHR/LPCI, ADS inhibit, polisher selection, MCC autocontrol, condensate/feedwater pumps, preheaters, rod control and movement, recirculation loops, turbine run-up/synchronization/valves, condenser circulation/CAR/SJAE, cooling tower valves, and deaerator controls.

Rate-switch controls use `/` as neutral and are subject to the 600 ms dead-man timer. Selector and percent controls are validated against their registered positions or range before reaching the process model.

## Telemetry groups

The published snapshot includes nominal and process values for:

- Turbine oil, hydraulic, vibration, expansion, thrust, radial, seal, generator, steam, RPM, valve, and synchroscope data.
- Electrical transformer loads, battery, EDG RPM, site usage, generator load, net export, bus state, and panel power.
- CST levels and flow, CRD pressure/flow, reactor pressure/level, feedwater flow/temperature, pump flow/temperature/valves, deaerator state, hotwell state, condenser state, cooling tower state, and polisher levels.
- Xenon, iodine, fuel remaining, rods, APRM/RTP/IRM/SRM values, recirculation flow, suppression pool values, and ECCS flow.

Every tag carries a value, unit, and quality field in snapshots. Values backed by reduced-order state are marked `GOOD`; detailed sensor lag, noise, drift, and failure semantics remain planned work.

Instructor sensor faults use `mf.sensor.<tag-id>.<mode>` identifiers. The
implemented modes are `dead`, which publishes `value: "null"` with `quality:
"BAD"`, and `zero`, `low`, and `high`, which publish a deterministic
substituted value with `quality: "SUBSTITUTED"`. Clearing the malfunction
restores the process measurement without changing raw process state.

## Annunciators

The snapshot contains the normalized annunciator inventory for the electrical,
CRD, CST, ECCS, ADS, water-treatment, MCC, RPS, auxiliary, main reactor,
radiation, recirculation, turbine, condenser, cooling-tower, and deaerator
panels. Registered annunciators remain `NORMAL` until their corresponding rule
is implemented; implemented rules emit trigger/clear events and maintain
acknowledge/silence state.

## Fidelity boundary

Adding an identifier to the live registry does not claim that every physical subsystem is complete. Controls are accepted through the same validation path, while detailed pump curves, valve stroke timing, thermal hydraulics, sensor models, and automatic controls remain to be implemented per subsystem.
