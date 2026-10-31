# Implementation Status

This file is the honest boundary of the current repository implementation.

## Implemented

- C++20 `rbwr_core` library with deterministic stepping.
- Role checks, control-type checks, selector/range validation, and common error codes.
- Startup transformer, Bus A, Bus B, safety-bus interlock behavior.
- Basic energized-load accounting and generator/net-export tags.
- CRD drive-water response to safety-bus power and CRD pump commands.
- Reactor pressure, thermal-power, turbine-speed, and condenser-pressure reduced-order state.
- Rate-switch integration with a 600 ms dead-man release event.
- Two-button scram acceptance within a two-second window.
- Alarm trigger/clear events, acknowledge/silence state, malfunction flags.
- Version-independent text save/load of the core state.
- CLI and line-oriented JSON diagnostic server.
- Regression tests for the core command and state contracts.

## Not yet high-fidelity

The full master prompt still calls for detailed IAPWS properties, six-group kinetics, fuel assembly thermal hydraulics, EDG sequencing, RHR/RCIC/ADS, turbine component state machines, complete sensor fault injection, WebSocket/REST networking, all registry-generated documentation, and the complete malfunction/scenario catalog. Those are not silently represented as complete here; unsupported behavior must be implemented in later subsystem work before claiming acceptance against the full specification.