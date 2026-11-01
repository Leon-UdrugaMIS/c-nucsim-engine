# RBWR Unit 2 Engine

This repository contains a deterministic C++20 simulation kernel and a line-oriented diagnostic server for the RBWR Unit 2 control-room simulator.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Targets:

- `rbwr_core`: deterministic engine library.
- `rbwr_cli`: cold-start smoke scenario.
- `rbwr_server`: newline-delimited JSON diagnostic transport on stdin/stdout.
- `rbwr_tests`: core contract regression tests.
- `rbwr_docgen`: generates live registry tables under `docs/GENERATED`.

Generate registry documentation with:

```sh
./build/rbwr_docgen docs/GENERATED
```

## Diagnostic server

Run `./build/rbwr_server`, then send commands such as:

```text
step 0.05
toggle elec.startup_tr_breaker true
toggle elec.bus_a_tr true
toggle elec.lvac_dc_tr true
toggle crd.pump1 true
step 0.5
save_slot 1 saves
slots saves
load_slot 1 saves
quit
```

The server emits authoritative snapshots; clients must treat tag values and annunciator events as engine-owned state.

## Game save slots

The engine exposes exactly five numbered slots through `save_slot(slot, directory)`,
`load_slot(slot, directory)`, and `save_slots(directory)`. Slot numbers are `1..5`;
invalid numbers fail without changing engine state. Files are named
`rbwr_save_slot_1.save` through `rbwr_save_slot_5.save` in the selected directory.
Each slot stores the deterministic process state, controls, malfunctions, tick,
simulation time, speed, and pause state.

## Current fidelity

The kernel implements deterministic fixed-step state evolution, role and control validation, bus interlocks, actuator slew/dead-man handling, two-button scram timing, basic reactor pressure/power and condenser behavior, alarm transitions, malfunction flags, and save/load of the core state. The detailed plant models in the master prompt remain approximations until their dedicated subsystem phases are implemented. This is an educational/game simulation, not a licensed safety-analysis code.

## Contract sources

The machine-readable starting registries are in `config/`. Transport envelope rules are in `docs/api/schemas/envelope.schema.json`. The implementation deliberately reports unsupported controls rather than silently pretending to simulate a subsystem that is not present.