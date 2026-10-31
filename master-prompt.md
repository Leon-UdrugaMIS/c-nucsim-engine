# MASTER PROMPT v4: RBWR UNIT 2 FULL-PLANT BWR SIMULATION BACKEND
# (C++20 ENGINE + WEBSOCKET/REST API + EXHAUSTIVE API DOCUMENTATION)

---

## 0. ROLE AND MISSION

You are a principal nuclear-plant simulation engineer, a senior real-time systems programmer (modern C++20) and a technical writer who produces reference-grade API documentation.

Design, implement, test and FULLY DOCUMENT the SIMULATION BACKEND of a Boiling Water Reactor control-room simulator modeled on "RBWR Unit 2" (a Roblox-style reactor operation game). The plant has: hydraulic control rod drive with a per-rod HCU accumulator, recirculation pumps with jet pumps, RCIC, RHR (LPCI and shutdown cooling), main steam relief valves plus ADS, a turbine generator (HPT, MSR, three LPTs), a condenser with SJAEs and CAR pumps, a deaerator, feedwater with three HP preheaters, polishers and water treatment, two condensate storage tanks, a cooling tower, and an electrical distribution system (startup transformer, main transformer, Bus A, Bus B, Bus C, 480 VAC safety bus, two emergency diesel generators, 125 VDC emergency and DC buses, battery pack).

A separate frontend (a Python control-room GUI, written later by a developer who has NEVER seen this plant and has only your documentation) will connect to the engine. The frontend is a DUMB TERMINAL. It sends operator commands and renders what the engine publishes. The engine owns ALL physics, component state, automatic control, protection logic, electrical power flow, malfunctions, annunciators and alarms.

TWO EQUALLY IMPORTANT PRODUCTS ARE REQUIRED:
1. The engine and server (compilable code, tests, JSON configs).
2. EXTREMELY DETAILED API DOCUMENTATION, as detailed as it is possible to make it (Section 4). The documentation is a first-class deliverable with the same weight as the code. A frontend developer must be able to build the entire GUI from the documentation alone, without reading engine source.

This document is the complete specification. NO other documents will be attached. Where something is ambiguous, state your assumption in a numbered list at the top of your answer and continue; do not stop to ask, and do not invent behavior silently. Everything marked "default" lives in JSON config so it can be tuned.

Be honest about fidelity. This is a high-fidelity game and educational simulator, not a licensed safety-analysis code. Label every model as: (a) first-principles, (b) correlation-based, (c) empirical/gameplay-tuned. Where the specification says "derived, not from the RBWR guide", the original RBWR guides were not available and standard BWR practice was used.

---

## 1. GOLDEN RULES

1. The operator can manipulate ONLY the controls listed in Section 7 (the main control room). Everything else (protection logic, valve sequencing, EDG sequencing, pump interlocks, autocontrol loops, ADS, RCIC auto start, HCU behavior, jet pumps, steam seals, lube oil, MSR, rupture disk, transformer thermal state, etc.) is simulated automatically inside the engine. Components with no operator control must still exist, hold state, and be able to fail.
2. Everything the frontend displays comes from engine state. The frontend never computes a gauge value, never derives an annunciator, never simulates anything.
3. Every annunciator emits an event when it TRIGGERS and another when it CLEARS (with simulation timestamp), plus events for ACKNOWLEDGED and SILENCED. A full snapshot is available on connect and on request.
4. The engine validates every command and rejects it with a machine-readable reason code (Section 4.9) when it is interlocked, de-energized, blocked by reactor mode, blocked by a malfunction, out of range, rate-limited, or sent by a client without the required role.
5. Deterministic fixed-timestep simulation (default 50 ms, configurable) with multi-rate subcycling:
   - FAST (default 10 ms): point kinetics, electrical network, generator synchronization, rod motion, protection logic
   - MEDIUM (default 50 ms): thermal-hydraulics, turbine, condenser, feedwater, deaerator, recirculation, valves, pumps
   - SLOW (default 1 s): xenon/iodine/samarium chains, burnup, water chemistry/polishers, cooling tower basin, battery state of charge, large thermal masses (transformer oil, EDG block, turbine casing and rotor metal, suppression pool)
   Seedable RNG so every run is reproducible. Full snapshot save/load. Pause, resume, speed multiplier (0.1x to 20x), single-step.
6. All plant parameters, setpoints, limits, rates, curves and tuning constants live in external JSON config files (one per system plus a master). No magic numbers in code except physical constants.
7. Pumps, motorized valves and breakers only operate when their supply bus is energized. Every energized load draws real power from the electrical model, and the electrical totals drive Site Usage, Sent To Grid, and the transformer gauges.
8. SI units internally. Display units follow the original panels: kPa for pressures (condenser in mbar), MW for power, RPM for speeds, percent where the original shows percent, degrees Celsius for temperatures, micrometers for vibration.
9. Sensors sit between truth and everything else. Controllers, RPS, annunciators and the frontend read sensor outputs (with configurable noise, lag, drift, range clipping and failure modes), never raw truth. The only raw-truth access is a debug/instructor channel.
10. No silent clamping. If a value is clamped, flag it in the quality field or log it.

---

## 2. TECH STACK AND ARCHITECTURE

### 2.1 Stack
- C++20, CMake. Dependencies limited to: nlohmann/json, ONE WebSocket library (Boost.Beast preferred; uWebSockets or websocketpp acceptable), a small HTTP layer for REST (same library), GoogleTest or Catch2. No other third-party dependencies. Provide JSON Schema validation either via a tiny in-house validator or a single header library.
- Steam and water properties: implement IAPWS-IF97 regions 1, 2, 4 (and 3 if feasible) yourself, or generate tabulated interpolation with documented error bounds.
- Build targets: `rbwr_core` (static library, no network), `rbwr_server` (WebSocket/REST server), `rbwr_cli` (headless scenario runner), `rbwr_docgen` (generates the API reference from the live tag/control/annunciator registries), `rbwr_tests`.
- Library API: `Engine::load_config(path)`, `Engine::step(dt)`, `Engine::apply_command(Command, Role)`, `Engine::snapshot()`, `Engine::diff_since(tick)`, `Engine::inject_malfunction(id, params)`, `Engine::save(path)`, `Engine::load(path)`.

### 2.2 Architecture requirements
- Component model: `Component` base (id, name, kind, health: OK/DEGRADED/FAILED/TRIPPED/ISOLATED/OFFLINE, fault flags, `update(dt, ctx)`, `serialize()`), derived classes `Valve`, `Pump`, `Breaker`, `Transformer`, `Bus`, `Load`, `Tank/Volume`, `HeatExchanger`, `Turbine`, `Generator`, `Diesel`, `Battery`, `Sensor`, `Controller`, `Annunciator`, `ProtectionChannel`.
- `Valve`: Cv-based flow, characteristic (linear / equal-percentage / quick-opening), stroke time, actuator type (motor / air / solenoid / hydraulic, determining which supply it needs), fail mode (fail-open / fail-closed / as-is), faults (stuck at position, leaking by, slow, fails to open, fails to close, position feedback lost).
- `Pump`: head-flow curve, affinity laws for variable speed, NPSH/cavitation model, inertia (start ramp and coastdown), bearing/winding/oil temperatures, trip conditions, electrical draw from its bus.
- `Breaker`: OPEN / CLOSED / TRIPPED, interlocks, synchronization permissive where applicable, trip causes (overcurrent, overload, differential, protection), reset requirement.
- Plant graph: nodes (volumes, tanks, headers, pools) and edges (pipes with valves or pumps). Flow solved by a pressure-driven network solver with lumped mass and energy balance per node. Document the solver algorithm and its stability limits.
- Annunciator engine: each annunciator is a rule (condition expression over tags, on-delay, off-delay, latching flag, requires-reset flag, acknowledge behavior). Emits TRIGGERED / CLEARED / ACKNOWLEDGED / SILENCED events. ACK and SIL buttons exist on the electrical panel. Maintain `acknowledged` and `silenced` flags per annunciator and a global audible-alarm state.
- Tag registry: every input and output tag has id, type, unit, range, nominal, panel, update group, writable flag, plus the documentation fields in Section 4.2. Exposed over REST and as a WebSocket `tag_registry` message.
- Threading model: one simulation thread (deterministic), one network thread pool, lock-free command queue into the simulation thread, double-buffered state snapshot out. Commands are applied at the start of the next FAST tick and are time-stamped with the tick at which they took effect.

---

## 3. NAMING CONVENTIONS

- Control IDs: `<panel>.<name>`, lowercase snake_case, for example `crd.pump1`, `mcc.fw_pump1_speed`.
- Annunciator IDs: `ann.<panel>.<name>`. Telemetry tag IDs: `<system>.<name>[_<unit>]`. Component IDs: `<system>.<type>.<name>` (for example `elec.breaker.gen_52`, `proc.valve.msiv_like.turbine_inlet`).
- Normalize all spelling from the source material (preassure to pressure, dearator to deaerator, emrgency to emergency, and so on). Keep the exact human-readable label as it appears on the panel in the `label` field.
- Duplicate annunciator texts that appear on multiple panels (LOW REACTOR WATER LEVEL, REACTOR WATER DRAIN, SDC-1 ACTIVE, SDC-2 ACTIVE, HIGH RPV PRESSURE, REACTOR TRIP STATE, CONDENSER HOTWELL WATER DRAIN, GENERATOR SHORT CIRCUIT, BUS A UNPOWERED, DEAERATOR WATER DRAIN, LOW DEAERATOR WATER LEVEL, RCIC and LPCI related, and so on) are ONE annunciator with a `panels: [...]` list, never copies.
- Panel prefixes: `crd`, `cst`, `elec`, `eccs`, `srv`, `ads`, `wt`, `mcc`, `rps`, `aux` (reactor auxiliary control panel), `rmcp` (reactor main control panel), `recirc`, `turb`, `cond`, `tower`, `dea`, `rad`, `sim`, `diag`.
- Rod numbering: define a rod map in config (row/column, group 1 to 24 membership). Rod tag IDs: `rod.<nn>.pos_pct` (0 = fully inserted, 100 = fully withdrawn), `rod.<nn>.hcu_pressure_kpa`, `rod.<nn>.state`.
- Fuel assembly numbering: `fa.<nn>.l<k>.temp_c` for axial level k = 1..10 (1 = bottom).

---

## 4. API SPECIFICATION AND API DOCUMENTATION REQUIREMENTS (MANDATORY, EXTREMELY DETAILED)

### 4.1 Required documentation deliverables

Produce all of the following as files under `docs/api/` (Markdown, plus machine-readable schema files). Nothing may be left as "see source".

1. `00_overview.md`: purpose, architecture diagram (Mermaid), glossary of every BWR and plant term used (RPV, HCU, MSRV, ADS, RCIC, LPCI, SDC, SJAE, CAR, MSR, APRM, LPRM, IRM, SRM, DNB-like terms, and so on), how simulation time relates to wall time, determinism guarantees, versioning policy.
2. `01_quickstart.md`: a 15-minute walkthrough from connecting a WebSocket client to starting a CRD pump and observing the resulting tags and annunciators, with copy-paste Python.
3. `02_transport_and_envelope.md`: WebSocket endpoint, subprotocol, framing, the message envelope, sequence numbers, ordering and delivery guarantees, heartbeat, reconnect and resync, backpressure, compression, maximum message size, binary-vs-text policy.
4. `03_roles_auth_and_limits.md`: roles (observer, operator, instructor), auth token handshake, per-role command permissions, rate limits and what happens when exceeded.
5. `04_message_reference.md`: EVERY server-to-client and client-to-server message type, with all fields, types, optionality, units, constraints, example JSON, and the JSON Schema file name.
6. `05_tag_reference.md`: EVERY telemetry and state tag (see 4.2).
7. `06_control_reference.md`: EVERY operator control (see 4.3).
8. `07_annunciator_reference.md`: EVERY annunciator (see 4.4).
9. `08_component_reference.md`: EVERY component (see 4.5), including state machines.
10. `09_error_codes.md`: every error code (see 4.9) with causes, examples and recommended frontend behavior.
11. `10_protocols.md`: detailed sub-protocols: rate-switch protocol with dead-man timer, synchroscope protocol, snapshot/delta/resync protocol, ack/silence protocol, scram two-button protocol, multi-select rod group protocol, scenario/simulation control protocol.
12. `11_sequence_diagrams.md`: Mermaid sequence diagrams for at least: connect and initial snapshot; operator toggle accepted; operator toggle rejected (interlock); rate-switch hold and release; dead-man timeout; annunciator trigger, ack, clear; scram; loss of offsite power with EDG start; generator synchronization; reconnect with resume; instructor malfunction injection; save and load.
13. `12_transcripts.md`: complete annotated message transcripts (real JSON, with commentary) for at least: cold-start power-up of Bus A and B; CRD start; rod withdrawal; reaching criticality; turbine run-up and sync; manual scram; loss of offsite power; EDG start and loading; RCIC auto start; offgas event recovery.
14. `13_frontend_guide.md`: panel-by-panel guide mapping each physical control-room panel (Section 7) to widgets, with the exact control IDs, tag IDs and annunciator IDs per widget, recommended widget type (lamp, gauge, bar, digital readout, selector, spring-return switch, guarded pushbutton, LED ring), recommended refresh rates, color semantics, and what to show when a tag has BAD quality.
15. `14_config_reference.md`: every JSON config file, every key, units, defaults and effects.
16. `15_malfunction_reference.md`: every malfunction id (see Section 14 and Appendix L) with parameters, symptoms, annunciators, expected recovery and clear conditions.
17. `16_scenarios.md`: every built-in scenario, its initial conditions and its success criteria.
18. `17_changelog_and_versioning.md`.
19. `schemas/`: one JSON Schema (draft 2020-12) per message type, plus `tag_registry.schema.json`, `control_registry.schema.json`, `annunciator_registry.schema.json`.
20. `openapi.yaml` (REST) and `asyncapi.yaml` (WebSocket channels and messages).
21. `examples/python_client/`: a typed Python reference client (dataclasses or pydantic), a CLI monitor, and a minimal test GUI stub, all exercising every command type.
22. `docs/GENERATED/`: output of `rbwr_docgen` (tag, control, annunciator and component tables) so the reference tables are generated from the live registries and can never drift from the code.

### 4.2 Per-tag documentation (every single tag, no exceptions)

For every tag record ALL of: `id`; `label` (as on the panel, if shown); `description` (what physically it is, one to three sentences); `panel` and `widget_hint`; `data_type` (float, int, bool, enum, string, object, array); `unit`; `range_min` and `range_max` (instrument range); `nominal_100pct` and `nominal_cold_shutdown`; `alarm_low` and `alarm_high` with the annunciator id they drive; `update_group` and default publish rate in Hz; `deadband` for delta publishing; `source` (which sensor/component produces it, and the model class a)/b)/c)); `quality_semantics` (when GOOD, BAD, STALE, SUBSTITUTED); `invalid_when` (for example polisher data invalid with no flow, SRM invalid when probes fully withdrawn); `related_controls`; `related_annunciators`; `related_tags`; `formula_summary` (one line of the governing equation or correlation); `example_values` (cold, startup, 100% power, post-scram, faulted); `since_version`.

### 4.3 Per-control documentation (every single control)

For every control record ALL of: `id`; `label` (exact panel text); `panel`; `type` (toggle / selector / rate_switch / percent / button / guarded_button / multi_button); `positions` or `range`; `default_state` for cold shutdown and for 100% power; `rate_config` (rate per position in units per second, dead-man timeout); `actuated_component_ids`; `validation_rules` (power supply required, interlocks, reactor-mode blocks, sequence permissives, malfunction blocks) expressed both in prose and as a machine-readable expression; `effects` (what changes in the plant, in what order, with expected time constants); `resulting_tags` and `resulting_annunciators`; `possible_error_codes`; `example_request`, `example_success_response`, `example_failure_response`; `role_required`; `notes_for_frontend` (for example "must send neutral on release", "show lamp amber while valve is stroking").

### 4.4 Per-annunciator documentation (every single annunciator)

For every annunciator record ALL of: `id`; `text` (exact label); `panels` (list, with tile position hint); `priority` (info / caution / warning / critical); `condition_expression` (machine-readable over tag ids) plus a prose description; `on_delay_s`; `off_delay_s`; `latching`; `reset_requirement`; `acknowledge_behavior`; `audible` (yes/no, tone class); `typical_causes`; `typical_operator_response`; `related_tags`; `related_controls`; `related_malfunctions`; `example_event_sequence` (a short list of TRIGGERED, ACKNOWLEDGED, CLEARED messages).

### 4.5 Per-component documentation

For every component record: `id`; `type`; `description`; `supply` (bus and breaker, or "process driven"); `state_machine` (states, transitions, guards, timings, in a Mermaid state diagram); `health_states`; `faults` and `malfunction_ids` that can affect it; `parameters` (config keys); `published_tags`; `interlocks`; `trip_conditions`; `failure_modes`.

### 4.6 Envelope, ordering, resync

Every server message carries: `v` (protocol version, semver string), `type`, `seq` (monotonic uint64 per connection), `tick` (uint64 simulation tick), `sim_time` (seconds, float64), `wall_time_ms` (int64 unix ms). Document and implement:

- On connect: server sends `hello` (version, sim status, role granted, tick rate, publish groups), then `tag_registry` (unless the client sends `{"type":"hello","skip_registry":true}`), then `snapshot`.
- `telemetry` messages carry `base_tick` and delta values; a client that sees a `seq` gap must send `request_snapshot`.
- `ping` / `pong` every 5 seconds (configurable); server closes silent connections after 3 missed pongs.
- `resume`: `{"type":"resume","last_seq":N}` replays buffered messages (ring buffer of at least 10,000 messages) or answers with a fresh snapshot if too old.
- Backpressure: if a client falls behind by more than a configurable queue depth, the server coalesces telemetry (latest value wins) and sets `coalesced:true`; annunciator and event_log messages are NEVER dropped or coalesced.
- Ordering guarantees: within a tick, the order is (1) `command_result` for commands applied this tick, (2) `annunciator` events, (3) `event_log`, (4) `telemetry`.
- Timestamps: all event messages carry the tick and sim_time in which they occurred, not the time they were sent.

### 4.7 Roles and permissions

- `observer`: receive everything, send only `request_snapshot`, `ping`, `resume`.
- `operator`: all Section 7 controls plus `elec.ack` and `elec.sil`. NOT malfunctions, sim control, or debug tags.
- `instructor`: everything operator can do plus `malfunction`, `sim` commands, `scenario`, `set_tag` (debug write to truth), and `debug` tags (true, unsensed values).
Auth: token in the WebSocket query string or `Authorization` header, tokens and roles in a config file. Document the mapping and the failure behavior (HTTP 401 / close code 4401).

### 4.8 Message catalogue (document each with full field tables, JSON Schema and examples)

Server to client: `hello`, `tag_registry`, `control_registry`, `annunciator_registry`, `snapshot`, `telemetry`, `annunciator`, `event_log`, `command_result`, `sim_status`, `scenario_status`, `malfunction_status`, `pong`, `error`, `bye`.
Client to server: `hello`, `request_snapshot`, `request_registry`, `toggle`, `selector`, `rate_switch`, `set_percent`, `button`, `multi_select` (rod groups), `malfunction`, `sim`, `scenario`, `set_tag` (instructor), `subscribe` / `unsubscribe` (tag groups and rates), `resume`, `ping`.

Example shapes (extend these with every field in the real docs):

```json
{"v":"1.0.0","type":"snapshot","seq":1,"tick":0,"sim_time":0.0,"wall_time_ms":1767000000000,
 "tags":{"reactor.pressure_kpa":{"v":101.3,"u":"kPa","q":"GOOD"}},
 "controls":{"crd.pump1":{"v":false,"state":"OFF","enabled":true}},
 "annunciators":{"ann.elec.bus_a_unpowered":{"state":"TRIGGERED","ack":false,"since_tick":0}}}

{"v":"1.0.0","type":"telemetry","seq":2,"tick":200,"base_tick":199,"sim_time":10.0,
 "d":{"turb.rpm":3.1},"coalesced":false}

{"v":"1.0.0","type":"annunciator","seq":3,"tick":205,"sim_time":10.25,
 "id":"ann.rps.reactor_level_low_trip","state":"TRIGGERED","priority":"critical",
 "panels":["rps","rmcp"],"ack":false,"silenced":false}

{"v":"1.0.0","type":"command_result","seq":4,"tick":206,"req_id":"c-17","ok":false,
 "error":{"code":"ERR_UNPOWERED","control_id":"crd.pump1","detail":"Bus S (480VAC) is de-energized","hint":"Close elec.bus_a_tr and elec.lvac_dc_tr"}}

{"v":"1.0.0","type":"event_log","seq":5,"tick":206,"sim_time":10.3,"level":"WARN",
 "source":"elec.breaker.gen_52","code":"EVT_BREAKER_TRIP","text":"Generator breaker tripped: reverse power","data":{"mw":-12.4}}
```

Client to server:

```json
{"type":"toggle","req_id":"c-1","control_id":"crd.pump1","value":true}
{"type":"selector","req_id":"c-2","control_id":"eccs.rhr1_mode","position":"LPCI"}
{"type":"rate_switch","req_id":"c-3","control_id":"mcc.fw_pump1_speed","position":"++"}
{"type":"set_percent","req_id":"c-4","control_id":"mcc.fw_pump1_speed","value":62.5}
{"type":"button","req_id":"c-5","control_id":"elec.ack"}
{"type":"multi_select","req_id":"c-6","control_id":"rmcp.rod_groups","groups":[3,4]}
{"type":"malfunction","req_id":"c-7","id":"mf.elec.loop","active":true,"params":{"delay_s":0}}
{"type":"sim","req_id":"c-8","cmd":"speed","value":4.0}
```

### 4.9 Error codes (document each with cause, example, frontend behavior)

`ERR_PROTOCOL`, `ERR_SCHEMA`, `ERR_UNKNOWN_CONTROL`, `ERR_UNKNOWN_TAG`, `ERR_INVALID_VALUE`, `ERR_OUT_OF_RANGE`, `ERR_ROLE`, `ERR_AUTH`, `ERR_RATE_LIMIT`, `ERR_SIM_PAUSED`, `ERR_UNPOWERED`, `ERR_INTERLOCK`, `ERR_MODE_BLOCK` (reactor mode or rod block), `ERR_SEQUENCE_PERMISSIVE` (for example sync not permitted), `ERR_MALFUNCTION` (component failed), `ERR_ALREADY_IN_STATE`, `ERR_BUSY` (valve currently stroking and interlocked against reversal), `ERR_GUARD_NOT_OPEN` (guarded button), `ERR_TWO_BUTTON_TIMEOUT` (scram), `ERR_MUTUALLY_EXCLUSIVE` (for example both EDGs, or DC bus source selection), `ERR_UNSUPPORTED`, `ERR_INTERNAL`. Every `command_result` failure includes `code`, `control_id`, `detail` (human sentence naming the blocking component) and `hint` (what the operator could do). Success results include `applied_tick`, `new_value`, and `settling_estimate_s`.

### 4.10 Sub-protocol details to document precisely

- RATE SWITCH: five-position `--, -, /, +, ++` and three-position `-, /, +`. The client sends the position on press and `/` on release; the engine integrates the target at `rate_config[position]` units per second while the position is held, subject to the actuator slew limit and limits (0 to 100 percent or the control's range). The client must resend the held position every 250 ms as a keep-alive; the engine reverts to `/` after the dead-man timeout (default 600 ms) and emits an `event_log` entry with code `EVT_DEADMAN_RELEASE`. Document the exact behavior for rejected presses, simultaneous presses on the same control, and press during sim pause.
- SET_PERCENT: direct target setting. The commanded target is stored immediately, the actual position follows at slew rate. Both `target` and `actual` tags are published (`<control>.target_pct`, `<control>.actual_pct`).
- SCRAM: two guarded buttons `rmcp.scram_button_a` and `rmcp.scram_button_b`; both must be pressed within 2 seconds (config). A single press raises `HALF REACTOR TRIP` only if configured; otherwise it arms and times out with `ERR_TWO_BUTTON_TIMEOUT`.
- ROD GROUPS: `rmcp.rod_groups` multi-select of groups 1 to 24, `rmcp.group_clear` clears the selection; selection state is published as `rmcp.selected_groups` (array of ints).
- SYNCHROSCOPE (LED ring, see 7.13): published as one object tag, updated at 20 Hz.
- ACK/SIL: `elec.ack` acknowledges all TRIGGERED annunciators (they stop flashing and go steady), `elec.sil` silences the audible alarm only. Published `alarm.audible_active`.
- SNAPSHOT/DELTA: snapshot is authoritative; deltas apply on top; delta publishing uses per-tag deadband (documented per tag).
- SIM CONTROL: pause, resume, speed (0.1 to 20), single step N ticks, reset to scenario, save to slot, load from slot, set scenario. All instructor only, all echoed as `sim_status`.

### 4.11 Documentation quality bar and tests

- Every tag, control, annunciator, component and malfunction in the code MUST have complete documentation fields. `rbwr_docgen` fails the build if any required field is empty.
- A "docs coverage" unit test iterates the live registries and asserts 100 percent coverage and that every control's `example_request` validates against its JSON Schema and, when replayed in a headless engine from the documented initial state, produces the documented `example_success_response`.
- Every Mermaid diagram must parse (test with a mermaid CLI if available, else lint syntactically).
- Documentation examples must be generated from real engine runs, not hand-written, wherever possible.
- Include a "frontend developer checklist" at the end of `13_frontend_guide.md` that lists every item the GUI must implement for full parity with the control room.

---

## 5. PROCESS PLANT TOPOLOGY (FLOW DIAGRAM, UNIT 2): EVERY ITEM MUST EXIST AS A SIMULATED COMPONENT

Flow-diagram line colors: teal = steam, dark blue = water (condensate/feedwater), red = CST water supply lines, green = RHR/shutdown cooling loops, brown = offgas, black = atmosphere/air, dark red / salmon = RCIC and relief discharge lines.

Diagram legend: RHR residual heat removal; SDC shutdown cooling; RPV reactor pressure vessel; HX heat exchanger; RCIC reactor core isolation cooling; CST condensate storage tank; HPT high pressure turbine; LPT low pressure turbine; MSR moisture separator reheater; SJAE steam jet air ejector; CAR condenser air removal; SRV steam relief valve; "Suction = inlet, Discharge = outlet". Diagram note: "There are 8 SRVs in total" (6 MSRV plus 2 ADS SRV). Diagram title: "Unit 2 Flow Diagram".

### 5.1 Reactor vessel and recirculation
- RPV: core (fuel assemblies on an axial x radial grid, 10 axial levels, per-assembly temperature), control rods (each with its own HCU), steam separators and dryers, downcomer, lower plenum, narrow and wide range level instruments, pressure instruments.
- Two recirculation loops: SIDE 1 (left) and SIDE 2 (right). Each loop: RC outlet valve, RC pump, RC inlet valve, 8 jet pumps (indicators JP S1-1..8 and JP S2-1..8). Per-side pump speed control. Cavitation occurs if recirculation flow is too high relative to NPSH. Jet-pump indicators also illuminate during shutdown cooling flow.
- Two water pools at the lobes of the vessel drawing (treated as suppression pools with temperature and level). The SRV discharge line terminates in the right pool; dark-red lines link the pools toward the RCIC. (Assumption: list it.)

### 5.2 Main steam and turbine island
- Main steam line from the RPV: relief valve group (6 MSRV + 2 ADS SRV), RCIC turbine inlet valve feeding the RCIC turbine, "Turbine Inlet Valve" to the turbine, "Turbine Bypass Valve" to the condenser, steam to the deaerator through "Deaerator Inflow Valve", steam to the three HP feedwater preheaters, steam to the SJAEs, steam to the MSR and steam seals.
- Turbine train on one shaft: HPT, then MSR, then LPT 1, LPT 2, LPT 3, then the generator (T/G 2). Auxiliaries: turning gear, main lube oil pump, main hydraulic pump, emergency lube pump, emergency hydraulic pump (DC powered), steam sealing system, oil coolers.
- Turbine stop valve, control valve, precise inlet control (fine positioning), bypass valve, acceleration control (auto run-up).

### 5.3 Condenser and vacuum
- Main condenser with hotwell and tube bundle fed by circulation water from the cooling pond/tower; lines "To Cooling Pond" and "From Cooling Pond"; two Condenser Circulation Pumps with a circulation valve.
- Vacuum systems: two CAR pumps, SJAE 1 and SJAE 2 each with an SJAE isolation valve, Offgas Valve (to Offgas System, brown line), "From Atmosphere" valves (air in-leakage/vacuum break paths), "Cond. To Atm. Valve" (condenser to atmosphere), "To Atmosphere" (black line from the CAR discharge).
- Hotwell: Hotwell Pump, Hotwell Drain Valve, Hotwell Makeup Valve, CST selector valves.
- Condensate path: Condensate Pumps (2) with Condensate Valve, Polisher 1 and Polisher 2 (each with inlet and outlet valves), Polisher Bypass Valve, Deaerator.

### 5.4 Feedwater
- Deaerator: steam inflow valve, outlet valve (water to feed pumps), gas out valve, steam in valve, rupture disk, internal water level, pressure, temperature, dissolved-gas content.
- Two Feedwater Pumps with Suction valves (panel: FW INLET) and Discharge valves (panel: FW OUTLET); three HP Feedwater Pre-Heaters in series with isolation valves ("HP Feedwater Pre-Heater Isolation Valves") fed by extraction steam; feedwater returns to the RPV.

### 5.5 CSTs and water treatment
- CST 1 and CST 2, each with a Makeup Pump (from Water Treatment) and a Drain Pump (to Water Treatment); four CST selector valves linking CSTs to RHR CST suction valves, RCIC suction and the hotwell lines.
- Water Treatment: Polisher 1 and 2 conductivity, differential pressure, resin exhaustion, regeneration cycles (regeneration takes the polisher out of service for a configurable time), bypass valve.

### 5.6 RHR / SDC / LPCI (two trains)
- Train 1 (left, RHR PUMP L) and Train 2 (right, RHR PUMP R): pump, HX with HX bypass valve, SDC suction valve (from the recirculation loop), RHR CST suction valve, injection valve (to the RPV). Modes: LPCI (injection from CST/suppression water into the RPV) and RHR (shutdown cooling loop, SDC). SDC-1 and SDC-2 active indicators per train.

### 5.7 RCIC
- Steam-turbine-driven pump; steam inlet valve from main steam (inlet valve percent); suction from CST (annunciator RCIC SUCTION FROM CST); discharge to the RPV; turbine exhaust to the suppression pool. Auto-start on low-low reactor level; isolation on high steam-line flow or high exhaust pressure (config).

### 5.8 Cooling tower
- Basin with water level, makeup pump (breaker plus speed valve), evaporation and drift losses depending on outside temperature and humidity, blowdown. Fed from and returning to the condenser circulation system.

---

## 6. ELECTRICAL SYSTEM TOPOLOGY (ELECTRICAL DISTRIBUTION PANEL): FULL MODEL

Line colors on the panel: RED = external/switchyard-level (360 kVAC), ORANGE = 6.6 kVAC, GREEN = 480 VAC, WHITE = 125 VDC emergency, BLUE = 125 VDC DC bus.

Panel displays and calibration targets (from screenshots; model full-power operation consistently): Generator Load about 911 MW, Site Usage about 41.3 MW, Sent To Grid about 869.7 MW. Identity: Sent To Grid = Generator Load - Site Usage (within transformer losses). Main transformer gauge 0 to 1500 MW. Startup transformer gauge 0 to 50 MW. Battery pack gauge 0 to 100 percent with sticky-note rule: "Keep the battery level above 35% and check if you are using it periodically." EDG RPM gauges (EDG-1, EDG-2): scale about 0 to 2000+ RPM with rated about 1800 RPM (assumption: list it). Panel annunciator-like indicators: "MAIN PACK CHARGING" (battery), SIL and ACK pushbuttons, EDG RDY STATUS lamps 1 and 2, EDG STARTUP REQUEST lamp, red/green status lamp pair on every load and breaker.

### 6.1 External network and generator
- External Switchyard 3L5 supplies two lines:
  - LINE 3L5(B2) 360 kVAC: feeds breaker 35A which connects the external switchyard to the STARTUP TRANSFORMER (STARTUP TR., load gauge 0 to 50 MW). The startup transformer powers Bus A through the BUS A TR. breaker.
  - LINE 3L5(A2) 360 kVAC: associated with the MAIN TRANSFORMER (MAIN TR., load gauge 0 to 1500 MW), fed by Turbine Generator 2 (T/G 2) through the GEN. BREAKER (52). When the turbine generates, power flows T/G 2, main transformer, line 3L5(A2) to the grid and to Bus A and Bus B. When the generator is offline and the main circuit breaker is closed, line 3L5(A2) can back-feed (assumption: list it).
- ROLLDOWN PROTECTION breaker at the T/G 2 output. Sticky-note behavior: rolldown protection is DISABLED by default when the turbine is not synchronized, to allow powering the recirculation pumps from the turbine's rundown generator output (the "rolldown"). The warning label text on the panel is partly illegible; implement: with the turbine not synced, protection disabled by default shows the indicator "PROTECTION DISABLED"; enabling it incorrectly (or disabling it while synced) raises ROLLDOWN MAIN BUS BREAKER PROTECTION TROUBLE. Model rolldown: after turbine trip and generator breaker open, generator output decays with turbine speed (rundown), briefly keeping some buses alive.
- BREAKER 52 UNIT INTERLOCK (orange line, Bus C 6.6 kVAC): connects this unit to Unit 1's Bus C; it can RECEIVE power from Unit 1 or SEND power to Unit 1 (annunciators UNIT INTERLOCK RECEIVING POWER / SENDING POWER). Unit 1 is a simple external source/sink with configurable capacity.

### 6.2 Buses and loads (each load has a configurable MW draw and runs only when its bus is energized)
- BUS A (yellow, 6.6 kVAC), breaker "BUS A TR.": powered by the startup transformer OR line 3L5(A2). Loads: CST 1 MAKEUP, CST 2 MAKEUP, CST 1 DRAIN, CST 2 DRAIN, RC PUMP 1, RC PUMP 2, RF PUMP 1, COND. PUMP 1, CC PUMP 1, LVL. CTRL PUMP, TOWER MAKEUP. Also feeds the safety bus through the LVAC/DC TRANSFORMER and breaker. CST pumps draw a lot of power (large defaults; they can overload the startup transformer).
- BUS B (orange, 6.6 kVAC), breaker "BUS B TR.": powered by line 3L5(A2). Loads: RF PUMP 2, COND. PUMP 2, CC PUMP 2.
- BUS C 6.6 kVAC: the unit interlock bus.
- GREEN SAFETY BUS ("BUS S", 480 VAC): powered by Bus A via the LVAC/DC TR.; in emergency, by emergency diesel generators EDG-1 or EDG-2 (BOTH EDGs CANNOT RUN AT THE SAME TIME; interlock). Loads: EMERGENCY BUS (white), DC BUS (blue), CRD 1, CRD 2, RH CRANE/REFUELER, RHR PUMP L, RHR PUMP R, CAR N1, CAR N2, TB. HYDR PUMP, TB. LUBR PUMP, TB. TURN. GEAR.
- EMERGENCY BUS (white, 125 VDC): loads: LP LIGHTS, CR PANELS, EMG. HYD. PUMP, EMG. LUB. PUMP. Powered by Bus A/safety bus (via AC/DC) or by the EMERGENCY BATTERY (BATTERY PACK). Breaker "AC-DC 1 Interlock": when opened, the emergency bus is powered from the battery and drains it. Annunciators: DC BUS MAIN BATTERY ACTIVE, MAIN BATTERY CRITICALLY LOW.
- DC BUS (blue, 125 VDC): powered by either the emergency bus OR the safety bus, never both at once (a selector breaker chooses). Powers LIGHTS, VENTILATION, PMS (plant messaging system).
- BREAKER 72DCG: when closed, powers the diesel-generator section of the safety bus: EDG REMOTE STARTUP (auto-starts whichever EDG is READY), SAFETY BUS TO DG GENERATORS (powers the safety bus from the diesels), EDG EMERGENCY TRIP (trip the running EDG from the control room), EDG STARTUP REQUEST (manual startup request). EDG RDY STATUS lamps 1 and 2. EDG preheat state ("EDG NOT PREHEATED"). Start-process annunciators EDG 1 / EDG 2 STARTUP PROCESS. DIESEL GENERATOR ACTIVE when one carries load. AUTO STARTUP NOT AVAILABLE when no EDG is ready, the DC control supply is lost, or 72DCG is open.

### 6.3 Electrical physics requirements
- Real power balance every FAST tick: sum of energized loads (MW per load in config) = Site Usage. Generator output minus Site Usage and transformer losses = Sent To Grid. With the generator offline, the site is supplied from the grid via the startup and/or main transformer.
- Transformer overload model: load greater than rating heats the winding; STARTUP TRANSFORMER OVERLOAD annunciates above 90 percent of the 50 MW rating and the transformer trips on a configurable thermal integral.
- Loss of offsite power (OFFSITE POWER CONNECTION LOSS): startup transformer and line 3L5(A2) go dead; with the generator offline Bus A and Bus B go dead; the safety bus loses power; RPS OFFSITE POWER LOSS TRIP; EDG auto start if remote startup is armed and an EDG is READY, with start/load sequencing; the battery picks up the emergency bus. Station blackout if no EDG starts.
- Bus annunciators: BUS A UNPOWERED, BUS B UNPOWERED, BUS S UNPOWERED, DC BUS UNPOWERED.
- ISLANDING MODE CONFIGURATION: plant isolated from the grid (main circuit breaker open) with the turbine carrying house loads (generator breaker closed). Implement house-load operation, frequency droop, overspeed and overload risks.
- Generator: synchronization requires matching voltage, frequency and phase; the synchroscope shows phase angle. Closing the generator breaker out of sync causes GENERATOR SHORT CIRCUIT (damage proportional to angle error). GENERATOR REVERSE POWER when steam flow is too low while synced. NO ROTOR EXCITATION FIELD when the exciter AVR breaker is open or fails. HIGH TURBINE GENERATOR LOAD above rated.
- Battery model: state of charge percent, discharge with load, charging when AC-DC is closed and powered (MAIN PACK CHARGING), CRITICALLY LOW annunciator at a configurable threshold (default 35 percent), emergency loads shed at 0 percent.
- EDG model: preheat state, crank, start delay, RPM ramp to rated, voltage build-up, load sequencing (safety bus loads connect in steps), fuel and running time, failure-to-start and failure-to-load malfunctions. Only one EDG may run.

---

## 7. OPERATOR CONTROLS (THE ONLY INPUTS THE FRONTEND MAY SEND) AND ANNUNCIATORS

Control type codes: [T] toggle, [S] selector, [R] rate switch (spring-return), [P] percent (set_percent capable), [B] button. Rate positions: five-position `--, -, /, +, ++`, three-position `-, /, +`. "/" is neutral. The source's spring-return "neutral" is the same as "/".

### 7.1 CRD / ROD DRIVE (Hydraulic Control Rod Drive System; each rod has its own HCU accumulator)
- `crd.accumulator_valve` [S] CLOSED, REFILL
- `crd.pump1` [T], `crd.pump2` [T] (CRD pump 1 and 2)
- `crd.flow_selector` [R] +, -, neutral (adjusts drive water flow; label "Flow selector")
- Annunciators: REACTOR TRIP STATE, HCU ACCUMULATORS PRESSURE LOW, WATER PRESSURE LOW

### 7.2 CONDENSATE STORAGE
- `cst.cst1_makeup` [T], `cst.cst1_drain` [T], `cst.cst2_makeup` [T], `cst.cst2_drain` [T]
- Annunciators: CST 1 HIGH LEVEL, CST 2 HIGH LEVEL, BUS A UNPOWERED, CST 1 LOW LEVEL, CST 2 LOW LEVEL, RCIC SUCTION FROM CST, CST 1 DRAIN, CST 2 DRAIN, CST 1 MAKEUP, CST 2 MAKEUP
- Note: CST pumps use a lot of power.

### 7.3 ELECTRICAL DISTRIBUTION PANEL
- Breakers and switches [T]: `elec.startup_tr_breaker` (35A), `elec.bus_a_tr`, `elec.bus_b_tr`, `elec.unit_interlock_52`, `elec.gen_breaker_52`, `elec.main_circuit_breaker`, `elec.lvac_dc_tr`, `elec.ac_dc_1_interlock`, `elec.dc_bus_source` [S] EMERGENCY, SAFETY, `elec.breaker_72dcg`, `elec.rolldown_protection`, `elec.edg_remote_startup`, `elec.safety_bus_to_dg`.
- Buttons [B]: `elec.edg_emergency_trip` (guarded), `elec.edg_startup_request`, `elec.ack`, `elec.sil`.
- Load toggles [T] for every bus load listed in 6.2, one per lamp pair on the panel (for example `elec.load.cst1_makeup`, `elec.load.rc_pump1`, `elec.load.tb_turn_gear`). Each shows a red (tripped/fault) and green (running) lamp.
- Annunciators: ISLANDING MODE CONFIGURATION, STARTUP TRANSFORMER BREAKER CLOSED, STARTUP TRANSFORMER OVERLOAD, MAIN CIRCUIT BREAKER OPEN, EDG NOT PREHEATED, DC BUS MAIN BATTERY ACTIVE, MAIN BATTERY CRITICALLY LOW, BUS A UNPOWERED, BUS B UNPOWERED, AUTO STARTUP NOT AVAILABLE, DC BUS UNPOWERED, BUS S UNPOWERED, ROLLDOWN MAIN BUS BREAKER PROTECTION TROUBLE, UNIT INTERLOCK RECEIVING POWER, EDG 2 STARTUP PROCESS, OFFSITE POWER CONNECTION LOSS, DIESEL GENERATOR ACTIVE, GENERATOR SHORT CIRCUIT, UNIT INTERLOCK SENDING POWER, EDG 1 STARTUP PROCESS.

### 7.4 EMERGENCY CORE COOLING SYSTEM
- RCIC: `eccs.rcic_turbopump` [T]; `eccs.rcic_steam_inlet` [R] +, -, neutral
- RHR (RESIDUAL HEAT REMOVAL) PUMP 1 (left, RHR PUMP L): `eccs.rhr1_mode` [S] LPCI, RHR; `eccs.rhr1_pump` [T]; `eccs.rhr1_injection_valve` [S] OPEN, CLOSED
- RHR PUMP 2 (right, RHR PUMP R): `eccs.rhr2_mode`, `eccs.rhr2_pump`, `eccs.rhr2_injection_valve` (same shapes)
- `eccs.cst_selector` [S] BOTH, 1, 2
- Annunciators: SDC-1 ACTIVE, SDC-2 ACTIVE, LPCI AVAILABLE, RCIC TURBOPUMP ENABLED, RCIC TURBINE RUNNING, LPCI ACTIVE, LOW REACTOR WATER LEVEL

### 7.5 SAFETY VALVES AND ADS
- MAIN STEAM RELIEF VALVES: `srv.msrv1` ... `srv.msrv6` [T] ON/OFF
- ADS (Automatic Depressurization System): `ads.system_inhibit` [T]. Indicators: ADS ARM FAILURE DISARMED indicator, ADS SRV 1 indicator, ADS SRV 2 indicator, MSRV 1..6 indicators.
- Annunciators: HIGH RPV PRESSURE, ADS SYSTEM OUT OF SERVICE, SRV 1/2 OPEN, ADS SYSTEM ARMED, SRV 3/4 OPEN, SRV 5/6 OPEN, ADS SRV 1/2 OPEN
- ADS logic: automatic depressurization on low-low reactor level plus permissive signal or timer (config), unless inhibited; SRV lift setpoints staggered (config, defaults about 7650 / 7700 / 7750 / 7800 / 7850 / 7900 kPa) with reseat hysteresis.

### 7.6 WATER TREATMENT PANEL
- `wt.pol_info_selector` [S] POL1, POL2 (selects which polisher's data to display; the polisher must have flow for the data to be valid)
- `wt.polisher_selector` [S] 1, 2, BOTH (which polisher or polishers the core water path uses)
- Annunciators: POLISHER 1 CONDUCTIVITY HIGH, POLISHER 2 CONDUCTIVITY HIGH, POLISHER 1 DIFF. PRESSURE HIGH, POLISHER 2 DIFF. PRESSURE HIGH, CONDENSATE WATER POLISHER BYPASS, POLISHER 1 REGEN IN PROGRESS, POLISHER 2 REGEN IN PROGRESS

### 7.7 MAIN COOLING CIRCUIT (MCC)
- Hotwell pump: `mcc.hotwell_pump` [T]; `mcc.to_cst_selector` [S] BOTH, 1, 2 (to which CST the drained liquid goes); `mcc.from_cst_selector` [S] BOTH, 1, 2 (which CST supplies the hotwell); `mcc.hotwell_drain_ctrl` [R] NEUTRAL, +, - (suction percent of the hotwell pump); `mcc.hotwell_makeup_ctrl` [R] NEUTRAL, +, - (makeup percent)
- Autocontrol: `mcc.autocontrol` [T] (TOGGLE MCC AUTOCONTROL); `mcc.reset_setpoints` [B] (reset setpoints to default); `mcc.hotwell_level_setpoint` [R] +, -, NEUTRAL; `mcc.deaerator_level_setpoint` [R]; `mcc.reactor_level_setpoint` [R]
- Condensate pumps: `mcc.cond_pump1` [T], `mcc.cond_pump2` [T]; `mcc.condenser_outflow` [R] --, -, NEUTRAL, +, ++
- Feedwater: `mcc.fw_pump1` [T], `mcc.fw_inlet1` [T], `mcc.fw_outlet1` [T], `mcc.fw_pump1_speed` [R] --, -, NEUTRAL, +, ++ (also [P]); same set for pump 2 (`mcc.fw_pump2*`)
- HP feedwater preheaters: `mcc.hp_fw_preheater1`, `mcc.hp_fw_preheater2`, `mcc.hp_fw_preheater3` [T]
- Annunciators (whole MCC): LOW CONDENSER HOTWELL WATER LEVEL, CONDENSER HOTWELL WATER DRAIN, HIGH PRESSURE FEEDWATER HEATER DISABLED, HIGH REACTOR WATER LEVEL, LOW DEAERATOR WATER LEVEL, DEAERATOR WATER DRAIN, LOW FEEDWATER TEMPERATURE, LOW REACTOR WATER LEVEL, ECCS/MCC CONFLICT, REACTOR WATER DRAIN, EXPOSED FUEL RODS, CONDENSER HOTWELL MAKEUP PUMPS ACTIVE, FEEDWATER PUMP TEMPERATURE LOW, FEEDWATER PUMP OVERHEAT, FEEDWATER PUMP TRIP

### 7.8 REACTOR PROTECTION SYSTEM INDICATOR PANEL
RPS MANUAL TRIP, RPS AUTO TRIP, RPS REACTOR PRESSURE HIGH TRIP, RPS OFFSITE POWER LOSS TRIP, RPS APRM UPSCALE TRIP, RPS APRM DOWNSCALE TRIP, RPS LPRM UPSCALE TRIP, RPS LPRM LOW PERIOD TRIP, RPS LOW PERIOD TRIP, RPS REACTOR LEVEL LOW TRIP, RPS MODE SWITCH IN SHUTDOWN TRIP.

### 7.9 REACTOR AUXILIARY CONTROL PANEL
- `aux.irm_range_selector` [S] 1..8 (intermediate power range selector); `aux.fuel_assembly_view_level` [S] AUTOMATIC, 1..10; SRM probes: SRM period (display), SRM depth 0 to 100 (display), SRM reading (decades "10'0 to 10'4", display); `aux.srm_probes_depth` [R] +, -, NEUTRAL
- Annunciators: SDC-1 ACTIVE, SDC-2 ACTIVE, SHUTDOWN COOLING IN-SERVICE FAULT, SCRAM NOT AVAILABLE, ROD DRIVE WATER PRESSURE LOW, INSUFFICIENT DRIVE WATER PRESSURE, HIGH I-135 CONCENTRATION, HIGH Xe-135 CONCENTRATION

### 7.10 REACTOR MAIN CONTROL PANEL
- Control rod movement: `rmcp.rod_control_mode` [S] GROUP, ALL RODS; `rmcp.group_movement` [R] NEUTRAL, INSERT, WITHDRAW; `rmcp.group_speed` [S] SLOW, AVG, FAST
- Control rod selection panel: 24 group buttons `rmcp.group_select_1..24` [B] (multi-select allowed, also via `multi_select`) and `rmcp.group_clear` [B]
- `rmcp.apr_control` [T] (automatic power setpoint control: withdraws rods or controls the recirculation pumps to reach the desired power); `rmcp.apr_setpoint` [P] percent
- `rmcp.auto_rod_balancer` [S] OFF, SLOW, FAST (balances rods to keep temperature stable)
- `rmcp.control_mode` [S] RECIRC, RODS, GROUP (what the APR drives: recirculation pumps, all rods, or group rods, to achieve the desired APRM)
- `rmcp.reactor_mode` [S] SD, SRM, IRM, RUN
- Reactor mode warnings/blocks: SRM mode: WITHDRAW, INSERT; IRM mode: GROUP SELECTION (IPR group selection); RUN mode: GROUP BLOCK, SRM BLOCK, IPR BLOCK. Publish these as block indicator tags and enforce them as rod blocks (`ERR_MODE_BLOCK`).
- Manual scram: `rmcp.scram_button_a`, `rmcp.scram_button_b` [B] (MUST push BOTH to scram)
- Annunciators: LOW REACTOR THERMAL POWER, LOW REACTOR PERIOD, RPS TRIP CHANNEL A, HIGH REACTOR WATER LEVEL, LOW LOCAL REACTOR PERIOD, RPV MECHANICALLY INOPERABLE, HIGH APR THERMAL POWER, HIGH LPRM THERMAL POWER, HIGH ROD POWER, RPS TRIP CHANNEL B, LOW REACTOR WATER LEVEL, RPV ROD DRIVE UNSERVICEABLE, RODS TEMPERATURE IMBALANCE, REACTOR TRIP STATE, HALF REACTOR TRIP, REACTOR WATER DRAIN, HIGH RPV PRESSURE, CORE NEUTRON FLUX LOW, RCIC TURBINE OPERATIONAL, LPCI SYSTEM ACTIVE, REACTOR AUTOMATIC CONTROL, SHUTDOWN COOLING SYSTEM

### 7.11 RADIATION WARNINGS (annunciators)
TURBINE HALL HIGH RADIATION, CONDENSER AREA HIGH RADIATION, REACTOR HALL HIGH RADIATION, TURBINE SECONDARY SENSOR HALL HIGH RADIATION, CONDENSER SECONDARY SENSOR AREA HIGH RADIATION, REACTOR HALL SECONDARY SENSOR HIGH RADIATION. (HIGH RADIATION SENSOR 8, the turbine hall main sensor, is annunciated on the turbine panel.)

### 7.12 RECIRCULATION PANEL
- Side 1 (left): `recirc.outlet1` [T], `recirc.inlet1` [T], `recirc.pump1` [T], `recirc.pump1_speed` [R] --, -, NEUTRAL, +, ++ (also [P])
- Side 2 (right): `recirc.outlet2`, `recirc.inlet2`, `recirc.pump2`, `recirc.pump2_speed`
- Indicators: JET PUMPS S1 (8), JET PUMPS S2 (8)
- Annunciators: RECIRCULATION PUMP 1 CAVITATION, RECIRCULATION PUMP 2 CAVITATION, NO RECIRCULATION WATER FLOW, LOW RECIRCULATION WATER FLOW, RECIRCULATION FLOW BALANCE
- Cavitation occurs when flow is too high; jet-pump indicators also turn on with shutdown cooling.

### 7.13 TURBINE CONTROL PANEL
- Synchroscope (display only, LED ring)
- `turb.auto_pressure_control` [T] (automatically maintains turbine pressure at 7100 kPa)
- `turb.gen_breaker_sync` [T] (closes and opens the generator breaker; if closed out of sync the engine applies the out-of-sync consequences instead of rejecting, unless `strict_sync_interlock` is true in config)
- `turb.stop_valve` [T], `turb.exciter_avr_breaker` [T]
- `turb.precise_inlet` [R] --, -, /, +, ++ (adjusts the turbine control valve more precisely); `turb.control_valve` [R] -, /, +; `turb.steam_bypass_valve` [R] -, /, +
- Automatic acceleration control: `turb.auto_runup` [T]; `turb.accel_rate` [S] SLOW, AVERAGE, FAST (rate at which auto run-up proceeds); `turb.runup_target_rpm` [S] STOP, 900, 1800, 2700, 3600
- Annunciators: NO CONDENSER VACUUM, HIGH TURBINE VIBRATION, BYPASS VALVE OPEN, MAIN CIRCUIT BREAKER OPEN LINE 3L5(A), HIGH TURBINE RPM, HIGH TURBINE GENERATOR LOAD, HIGH RADIATION SENSOR 8 (TURBINE HALL MAIN SENSOR), GENERATOR REVERSE POWER, GENERATOR SHORT CIRCUIT, NO ROTOR EXCITATION FIELD, TURBINE TRIP, HIGH MAIN STEAM TEMPERATURE, HIGH MAIN STEAM PRESSURE, SMOKE DETECTED IN TURBINE HALL
- Synchroscope protocol (tag `turb.sync`, 20 Hz): `{"phase_angle_deg": -180..180, "slip_hz": float, "freq_diff_hz": float, "voltage_diff_pct": float, "led_index": 0..23 (12 = in phase, 0..11 = slow side, 13..23 = fast side), "direction": "FAST"|"SLOW"|"SYNC", "sync_permissive": bool, "breaker_closed": bool}`. Permissive default: |phase| < 10 deg, |freq diff| < 0.1 Hz, |voltage diff| < 5 percent.

### 7.14 MAIN CONDENSER PANEL
- `cond.auto_control` [T] (adjusts SJAEs and circulation pumps to maintain optimal pressure)
- Circulation: `cond.circ_pump1_breaker` [T], `cond.circ_pump2_breaker` [T], `cond.circ_pump_valve` [R] --, -, /, +, ++
- Condenser electric air removal (CAR; steam ejected from CARs is lost from the circuit): `cond.car_pump1_breaker` [T], `cond.car_pump2_breaker` [T]
- Steam jet air ejection: `cond.sjae1_breaker` [T], `cond.sjae1_valve` [R] -, /, +, `cond.sjae2_breaker` [T], `cond.sjae2_valve` [R] -, /, +
- Annunciators: SJAE DISABLED, HIGH CONDENSER VACUUM, LOW CONDENSER VACUUM, CONDENSER TO OFF-GAS OPEN (when offgas opens, the operator must turn the SJAEs, condenser circulation and CARs off until it clears; model this as a hazard state with recovery), STEAM CONDENSATION NOT EFFECTIVE, CONDENSER HOTWELL WATER DRAIN, CONDENSER CIRC. PUMP DEMAND TROUBLE, HOTWELL LEVEL CONTROL VALVE OPEN, HOTWELL LEVEL MAKEUP VALVE OPEN, LOW CONDENSER WATER LEVEL

### 7.15 COOLING TOWER PANEL
- `tower.makeup_pump_breaker` [T], `tower.makeup_speed_valve` [R] --, -, /, +, ++
- `tower.cond_to_atm_valve` [R] --, -, /, +, ++ (condenser to atmosphere; used to raise pressure during shutdown or emergencies)
- Annunciators: COOLING TOWER WATER LEVEL HIGH, COOLING TOWER WATER LEVEL LOW, OUTSIDE TEMPERATURE HIGH

### 7.16 DEAERATOR CONTROL PANEL
- `dea.auto_control` [T] (automatically controls deaerator pressure and temperature); `dea.emergency_steam_inlet_fast_close` [B] (guarded); `dea.gas_out_valve` [R] -, /, +; `dea.steam_in_valve` [R] -, /, +
- Annunciators: DEAERATOR PRESSURE LOW, DEAERATOR PRESSURE HIGH, DEAERATOR TEMPERATURE HIGH, DEAERATOR TEMPERATURE LOW, DEAERATOR WATER DRAIN, LOW LOW DEAERATOR WATER LEVEL, RUPTURE DISK TROUBLE, LOW DEAERATOR WATER LEVEL, DEAERATION NOT EFFECTIVE, DEAERATOR AUTOCONTROL

### 7.17 Annunciator trigger defaults (thresholds in config; first-pass defaults, document your final values)
- LOW/HIGH REACTOR WATER LEVEL: level outside the narrow-range band around the setpoint (plus/minus 0.3 m); RPS REACTOR LEVEL LOW TRIP at a level-3 equivalent; RCIC and ECCS auto-start at a level-2 equivalent; ADS permissive at level-1; EXPOSED FUEL RODS below top of active fuel; REACTOR WATER DRAIN when net coolant outflow exceeds inflow beyond a threshold.
- HIGH RPV PRESSURE above about 7600 kPa; RPS REACTOR PRESSURE HIGH TRIP at about 7900 kPa; SRV lifts per staggered setpoints.
- HIGH MAIN STEAM PRESSURE/TEMPERATURE above the band around 7100 kPa / about 286 C; BYPASS VALVE OPEN when bypass position is above 1 percent.
- NO CONDENSER VACUUM above a high-pressure threshold (about 150 mbar absolute); LOW/HIGH CONDENSER VACUUM outside the optimal band (about 40 to 80 mbar); STEAM CONDENSATION NOT EFFECTIVE when condenser heat rejection is inadequate; CONDENSER TO OFF-GAS OPEN on excessive non-condensable load or event.
- HCU ACCUMULATORS PRESSURE LOW when any accumulator is below threshold; SCRAM NOT AVAILABLE when too few rods have adequate scram energy; WATER PRESSURE LOW / INSUFFICIENT DRIVE WATER PRESSURE on low drive header pressure.
- LOW REACTOR PERIOD / LOW LOCAL REACTOR PERIOD below configured period thresholds; HIGH APR / LPRM THERMAL POWER above setpoints; CORE NEUTRON FLUX LOW when SRM/IRM flux is below detector sensitivity during startup; LOW REACTOR THERMAL POWER when RTP is below a threshold in RUN.
- RODS TEMPERATURE IMBALANCE when assembly temperature spread exceeds a threshold (auto rod balancer addresses it); HIGH ROD POWER when a rod's local power exceeds the limit.
- HIGH I-135 / Xe-135 CONCENTRATION above configured percent of equilibrium (xenon pit after scram).
- ECCS/MCC CONFLICT when ECCS injection is active while MCC feedwater is also commanding level, or their valves fight over the same path.
- FEEDWATER PUMP TEMPERATURE LOW / OVERHEAT / TRIP per pump temperature limits; LOW FEEDWATER TEMPERATURE below the band (preheaters isolated or disabled); HIGH PRESSURE FEEDWATER HEATER DISABLED if any preheater is isolated or tripped.
- HIGH TURBINE VIBRATION above the microns limit (trip higher); HIGH TURBINE RPM above 3600 plus margin (overspeed trip at about 110 percent); HIGH TURBINE GENERATOR LOAD above rated.
- Radiation annunciators when the sensor exceeds its setpoint; secondary sensors have separate setpoints and independent failure.
- Electrical annunciators as in Section 6.

---

## 8. TELEMETRY THE ENGINE MUST PUBLISH (EVERY ITEM IS A TAG; ALL VALUES FROM ENGINE STATE)

Also publish for every pump, valve and breaker: state, position percent, commanded target, and health. Publish every annunciator state, component health for the whole plant, and `sim.*` meta tags (tick, sim_time, speed, paused, scenario). Every tag below needs the full documentation fields of Section 4.2.

Table columns: tag id | label | unit | range | nominal at 100 percent power (values are defaults; tune in config).

### 8.1 Turbine
| tag | label | unit | range | nominal |
|---|---|---|---|---|
| turb.oil_temp_c | Turbine oil temperature | C | 0-120 | 48 |
| turb.oil_pressure_kpa | Turbine oil pressure | kPa | 0-600 | 250 |
| turb.hydraulic_pressure_kpa | Turbine hydraulic pressure | kPa | 0-15000 | 9500 |
| env.outside_temp_c | Outside temperature | C | -30-50 | 18 |
| env.outside_humidity_pct | Outside humidity | % | 0-100 | 55 |
| turb.vibration_um | Turbine vibration (the source says "PIKOM", read as microns) | um | 0-300 | 35 |
| turb.diff_expansion_mm | Differential expansion | mm | -10-20 | 3 |
| turb.axial_pressure_kpa | Turbine axial (thrust) pressure | kPa | 0-5000 | 1800 |
| turb.radial_pressure_kpa | Turbine radial (bearing) pressure | kPa | 0-5000 | 1500 |
| turb.steam_seal_pressure_kpa | Steam sealing pressure | kPa | 0-300 | 130 |
| turb.generator_temp_c | Turbine generator temperature | C | 0-200 | 85 |
| turb.rpm | Turbine RPM | rpm | 0-4000 | 3600 |
| turb.hpt1_intake | HPT 1 intake | % valve/flow | 0-100 | 85 |
| turb.lpt1_intake / lpt2_intake / lpt3_intake | LPT 1/2/3 intake levels | % | 0-100 | 80 |
| turb.main_steam_temp_c | Main steam temperature | C | 0-350 | 286 |
| turb.main_steam_pressure_kpa | Main steam pressure | kPa | 0-9000 | 7100 |
| turb.bypass_valve_flow_kgs | Bypass steam valve flow | kg/s | 0-800 | 0 |
| turb.valve_steam_flow_kgs | Turbine valve steam flow | kg/s | 0-2000 | 1500 |
| turb.control_valve_pct / turb.stop_valve_state | valve position and state | % / enum | 0-100 | 85 / OPEN |
| turb.sync | Synchroscope object | object | see 7.13 | n/a |
| turb.turning_gear_state | Turning gear | enum | ENGAGED / DISENGAGED / FAULT | DISENGAGED |

### 8.2 Electrical
| tag | label | unit | range | nominal |
|---|---|---|---|---|
| elec.startup_tr_load_mw | Startup transformer load | MW | 0-50 | 8 (varies with who feeds the site) |
| elec.main_tr_load_mw | Main transformer load | MW | 0-1500 | 911 |
| elec.battery_pct | Battery pack percentage | % | 0-100 | 100 |
| elec.battery_charging | Main pack charging | bool | | true |
| elec.edg1_rpm / elec.edg2_rpm | EDG-1 / EDG-2 RPM | rpm | 0-2200 | 0 (standby) |
| elec.site_usage_mw | Site usage | MW | 0-200 | 41.3 |
| elec.sent_to_grid_mw | Sent to grid | MW | -200-1500 | 869.7 |
| elec.generator_load_mw | Generator load | MW | 0-1500 | 911 |
| elec.bus.<a/b/c/s/emerg/dc>.voltage / .state | Bus voltage and state | kV/V / enum | | nominal |
| elec.<breaker>.state | Every breaker | enum | OPEN/CLOSED/TRIPPED | per breaker |
| elec.<load>.lamp_red / lamp_green | Load lamps | bool | | per load |
| elec.edg1_rdy / elec.edg2_rdy / elec.edg_preheated | EDG ready/preheat lamps | bool | | true |
| elec.grid_freq_hz / elec.gen_freq_hz | Grid and generator frequency | Hz | 45-65 | 60 |
| alarm.audible_active | Audible alarm active | bool | | false |

### 8.3 CST and CRD
| tag | label | unit | range | nominal |
|---|---|---|---|---|
| cst.cst1_level_pct / cst.cst2_level_pct | CST 1 / 2 level | % | 0-100 | 70 |
| cst.inflow_kgs | CST inflow | kg/s | 0-100 | 0 |
| cst.outflow_kgs | CST outflow | kg/s | 0-200 | 0 |
| reactor.temp_change_rate_cph | Rate of reactor temperature change | C/h | -100-100 | 0 |
| crd.drive_water_pressure_kpa | Drive water pressure | kPa | 0-12000 | 8500 |
| crd.flow_kgs | CRD flow | kg/s | 0-30 | 6 |
| crd.accumulator_valve_state | Accumulator valve | enum | CLOSED/REFILL | CLOSED |
| reactor.pressure_kpa | Reactor pressure | kPa | 0-10000 | 7100 |
| reactor.level_nr_m / reactor.level_wr_m | Narrow and wide range level | m | | setpoint |

### 8.4 Feedwater and condensate
| tag | label | unit | range | nominal |
|---|---|---|---|---|
| fw.flow_kgs | Feedwater flow | kg/s | 0-2500 | 1500 |
| fw.total_flow_kgs | Total feedwater flow | kg/s | 0-3000 | 1500 |
| fw.temp_c | Feedwater temperature | C | 0-300 | 215 |
| fw.pump1_temp_c / fw.pump2_temp_c | FW pump temperatures | C | 0-200 | 140 |
| fw.pump1_flow_kgs / fw.pump2_flow_kgs | FW pump flows | kg/s | 0-1600 | 750 |
| fw.pump1_valve_pct / fw.pump2_valve_pct | FW pump speed valve percent | % | 0-100 | 70 |
| wt.pol1_level_pct / wt.pol2_level_pct | Polisher levels | % | 0-100 | 90 |
| wt.pol1_conductivity / wt.pol2_conductivity | Polisher conductivity | uS/cm | 0-5 | 0.08 |
| wt.pol1_dp_kpa / wt.pol2_dp_kpa | Polisher differential pressure | kPa | 0-500 | 80 |
| wt.polisher_outflow_kgs | Polisher outflow | kg/s | 0-2500 | 1500 |
| mcc.hotwell_makeup_pump_pct | Hotwell makeup pump | % | 0-100 | 0 |
| mcc.hotwell_level_ctrl_valve_pct | Hotwell level control valve | % | 0-100 | 10 |
| cond.hotwell_level_pct | Hotwell water level | % | 0-100 | 55 |
| dea.level_pct | Deaerator water level | % | 0-100 | 60 |
| mcc.sp_hotwell_level_pct / mcc.sp_reactor_level_m / mcc.sp_dea_level_pct | Auto-control setpoints | % / m / % | | defaults |
| cond.steam_flow_kgs | Condenser steam flow | kg/s | 0-2500 | 1100 |
| cond.outflow_kgs | Condenser outflow | kg/s | 0-3000 | 1500 |
| cond.valve_pct | Condenser (outflow) valve | % | 0-100 | 60 |

### 8.5 Reactor
| tag | label | unit | range | nominal |
|---|---|---|---|---|
| reactor.xe135_pct | Xenon-135 | % of equilibrium | 0-400 | 100 |
| reactor.i135_pct | Iodine-135 | % of equilibrium | 0-400 | 100 |
| fuel.remaining_pct | Fuel remaining | % | 0-100 | 100 at beginning of cycle |
| rod.<nn>.pos_pct | Each control rod level | % withdrawn | 0-100 | per pattern |
| fa.<nn>.l<k>.temp_c | Each fuel assembly temperature, per axial level | C | 0-2500 | 300-600 |
| nuc.irm_pct (with nuc.irm_range) | IRM gauge | % of range | 0-125 | per mode |
| rmcp.apr_setpoint_pct | Automatic reactor setpoint | % RTP | 0-120 | 100 |
| nuc.period_avg_s | Average reactor period | s | -1000-1000 / inf | inf |
| nuc.aprm_pct | APRM | % | 0-150 | 100 |
| nuc.rtp_pct | Reactor thermal power | % | 0-150 | 100 |
| nuc.srm_period_s / nuc.srm_depth_pct / nuc.srm_reading | SRM period, depth, reading | s / % / decades | | per state |
| recirc.pump1_valve_pct / recirc.pump2_valve_pct | Recirc pump valve percent | % | 0-100 | 70 |
| recirc.pump1_flow_kgs / recirc.pump2_flow_kgs | Recirc pump flow | kg/s | 0-9000 | 4500 |
| recirc.jp_s1[8] / recirc.jp_s2[8] | Jet pump indicators | bool[8] | | on at power |
| rmcp.selected_groups | Selected rod groups | int[] | 1-24 | [] |
| rmcp.rod_block_flags | Rod block indicators (SRM withdraw/insert, IPR, group, SRM block, IPR block) | object | | per mode |

### 8.6 Condenser and cooling
| tag | label | unit | range | nominal |
|---|---|---|---|---|
| cond.circ_valve_pct | Condenser circulation valve | % | 0-100 | 70 |
| cond.circ_flow_kgs | Condenser circulation flow | kg/s | 0-40000 | 22000 |
| cond.pressure_mbar | Condenser pressure | mbar | 0-1013 | 60 |
| cond.sjae1_pct / cond.sjae2_pct | SJAE 1 / 2 | % | 0-100 | 50 |
| tower.makeup_pct | Tower makeup | % | 0-100 | 20 |
| tower.level_change_pct_per_min | Cooling tower water level change | %/min | -5-5 | 0 |
| tower.level_pct | Cooling tower water level | % | 0-100 | 70 |
| tower.cond_to_atm_valve_pct | Condenser to atmosphere valve | % | 0-100 | 0 |

### 8.7 Deaerator
| tag | label | unit | range | nominal |
|---|---|---|---|---|
| dea.level_pct | Water level | % | 0-100 | 60 |
| dea.outlet_valve_pct / dea.inlet_valve_pct | Outlet / inlet valve | % | 0-100 | 70 |
| dea.gas_inflow_kgs / dea.gas_outflow_kgs | Air (non-condensable gas) inflow and outflow | kg/s | 0-1 | small |
| dea.temp_c | Temperature | C | 0-250 | 170 |
| dea.pressure_kpa | Pressure | kPa | 0-1500 | 800 |
| dea.flow_kgs | Flow | kg/s | 0-2500 | 1500 |
| dea.dissolved_o2_ppb | Dissolved O2 (feeds deaeration effectiveness and corrosion/chemistry) | ppb | 0-500 | 5 |

### 8.8 Radiation, ECCS, protection and misc (all published, all documented)
`rad.turbine_hall_primary_msvh`, `rad.turbine_hall_secondary_msvh`, `rad.condenser_primary_msvh`, `rad.condenser_secondary_msvh`, `rad.reactor_hall_primary_msvh`, `rad.reactor_hall_secondary_msvh`, `rad.sensor8_msvh`; `eccs.rcic_flow_kgs`, `eccs.rcic_turbine_rpm`, `eccs.rhr1_flow_kgs`, `eccs.rhr2_flow_kgs`, `eccs.rhr1_hx_outlet_temp_c`, `eccs.rhr2_hx_outlet_temp_c`; `srv.msrvN_open`, `ads.srv1_open`, `ads.srv2_open`, `ads.armed`, `ads.inhibited`; `rps.channel_a_tripped`, `rps.channel_b_tripped`, `rps.scram_active`, `rps.mode_switch_position`; `pool.supp_temp_c`, `pool.supp_level_pct`; `diag.mass_imbalance_kgs`, `diag.energy_imbalance_mw`, `diag.step_time_ms`, `sim.*`.

---

## 9. DEFAULT RATE-SWITCH AND ACTUATOR SLEW CONFIG (tune in config; document every one)

| control class | "-" / "+" | "--" / "++" | actuator slew limit |
|---|---|---|---|
| FW pump speed valve | 0.5 %/s | 2.0 %/s | 5 %/s |
| Recirc pump speed | 0.3 %/s | 1.0 %/s | 2 %/s |
| Condenser outflow, circulation valve | 1 %/s | 4 %/s | 8 %/s |
| Tower makeup speed, cond-to-atm valve | 1 %/s | 4 %/s | 8 %/s |
| Turbine precise inlet | 0.1 %/s | 0.5 %/s | 2 %/s |
| Turbine control valve ("-" / "+") | 1.5 %/s | n/a | 5 %/s |
| Steam bypass valve | 2 %/s | n/a | 10 %/s |
| SJAE valves, deaerator valves | 2 %/s | n/a | 10 %/s |
| RCIC steam inlet | 3 %/s | n/a | 10 %/s |
| Hotwell drain/makeup control | 2 %/s | n/a | 10 %/s |
| Level setpoint adjusters | hotwell 0.5 %/s, deaerator 0.5 %/s, reactor 0.005 m/s | n/a | n/a |
| CRD flow selector | 0.2 kg/s per s | n/a | n/a |
| SRM probes depth | 2 %/s | n/a | 5 %/s |
| Rod group movement | SLOW 0.5 %/s, AVG 1.5 %/s, FAST 3.0 %/s | | scram: full insertion about 3 to 4 s (accumulator and pressure dependent) |

Rod withdrawal and insertion respect rod blocks and per-rod HCU limits. Valves have per-valve stroke times in config (motorized: 10 to 60 s; air operated: 2 to 10 s; MSRV/ADS: under 0.5 s).

---

## 10. COMPONENT ID SCHEME AND MINIMUM COMPONENT LIST

Every component below exists as a `Component` with id, health, supply, and published state tags. IDs follow `<system>.<type>.<name>`.

- Electrical: `elec.xfmr.startup`, `elec.xfmr.main`, `elec.xfmr.bus_a`, `elec.xfmr.bus_b`, `elec.xfmr.lvac_dc`; `elec.breaker.35a`, `.bus_a_tr`, `.bus_b_tr`, `.gen_52`, `.main_circuit`, `.unit_interlock_52`, `.lvac_dc`, `.ac_dc_1_interlock`, `.dc_source_sel`, `.72dcg`, `.rolldown`, `.exciter_avr`; `elec.bus.a|b|c|s|emerg|dc`; `elec.edg.1|2`; `elec.battery.main`; `elec.gen.tg2`; all bus loads listed in 6.2.
- Reactor: `rx.core`, `rx.rod.<nn>` (each with `rx.hcu.<nn>`), `rx.jp.s1.<1..8>`, `rx.jp.s2.<1..8>`, `rx.sep_dryer`, `rx.downcomer`, `rx.supp_pool.left|right`; instruments `inst.srm.*`, `inst.irm.*`, `inst.aprm.*`, `inst.lprm.*`, `inst.level.*`, `inst.pressure.*`.
- Recirc: `recirc.pump.1|2`, `recirc.valve.outlet1|inlet1|outlet2|inlet2`.
- CRD: `crd.pump.1|2`, `crd.header`, `crd.accumulator_valve`.
- Steam/ECCS: `srv.msrv.1..6`, `ads.srv.1|2`, `eccs.rcic.turbine`, `eccs.rcic.pump`, `eccs.rcic.inlet_valve`, `eccs.rhr.pump.l|r`, `eccs.rhr.hx.1|2`, `eccs.rhr.hx_bypass.1|2`, `eccs.rhr.sdc_suction.1|2`, `eccs.rhr.cst_suction.1|2`, `eccs.rhr.injection.1|2`.
- Turbine: `turb.stop_valve`, `turb.control_valve`, `turb.inlet_valve`, `turb.bypass_valve`, `turb.hpt`, `turb.msr`, `turb.lpt.1|2|3`, `turb.generator`, `turb.turning_gear`, `turb.lube_pump.main|emerg`, `turb.hyd_pump.main|emerg`, `turb.steam_seal`.
- Condenser: `cond.shell`, `cond.hotwell`, `cond.hotwell_pump`, `cond.hotwell_drain_valve`, `cond.hotwell_makeup_valve`, `cond.condensate_pump.1|2`, `cond.condensate_valve`, `cond.circ_pump.1|2`, `cond.circ_valve`, `cond.car_pump.1|2`, `cond.sjae.1|2`, `cond.sjae_iso.1|2`, `cond.offgas_valve`, `cond.from_atm_valve.1|2`, `cond.to_atm_valve`.
- Feedwater: `wt.polisher.1|2`, `wt.polisher_bypass`, `dea.vessel`, `dea.inflow_valve`, `dea.outlet_valve`, `dea.gas_out_valve`, `dea.steam_in_valve`, `dea.rupture_disk`, `fw.pump.1|2`, `fw.suction_valve.1|2`, `fw.discharge_valve.1|2`, `fw.preheater.1|2|3`, `fw.preheater_iso.1|2|3`.
- CST/tower: `cst.tank.1|2`, `cst.makeup_pump.1|2`, `cst.drain_pump.1|2`, `cst.selector.1..4`, `tower.basin`, `tower.makeup_pump`, `tower.fan` (optional).

---

## 11. PHYSICS MODELS TO IMPLEMENT (REAL MODELS, NOT PLACEHOLDERS)

Reference calibration (tunable): turbine inlet pressure setpoint 7100 kPa; main steam about 286 C saturated; feedwater about 215 C with all three preheaters; generator about 911 MW gross at 100 percent RTP with about 41 MW site usage; choose thermal power so overall efficiency is about 33 percent (about 2,750 to 2,800 MWt); feedwater and steam flow about 1500 kg/s at 100 percent power; total core flow about 9000 to 10000 kg/s.

1. NEUTRONICS (a/b): point kinetics with 6 delayed-neutron groups (stiff solver: implicit or analytic prompt-jump). Reactivity = control rods (per-rod worth curves, group worth, bank patterns, rod-position-dependent S-curve) + Doppler (fuel temperature) + moderator density/void (strongly negative void coefficient; recirculation flow changes void and power) + Xe-135 + Sm-149 + burnup depletion. Xe-135/I-135/Pm-149/Sm-149 chains driven by flux (xenon pit after scram, peak at about 8 to 12 h). Decay heat per ANS-5.1 (sum of exponentials). Fuel remaining percent decreases with burnup. Detectors: SRM (counts, decades, retractable probes depth 0 to 100 with flux dependence), IRM (8 ranges with selectable range and auto range logic), APRM (average of LPRMs, percent of rated thermal power), LPRM (local), reactor period (derivative of log flux, filtered).
2. CORE THERMAL-HYDRAULICS AND FUEL (b): per-assembly heat transfer with 10 axial levels (pellet, gap, cladding, coolant), two-phase void fraction (drift-flux or Zuber-Findlay), natural circulation, recirculation plus jet-pump flow (M-ratio), core pressure drop, reactor water level with swell/shrink, exposed-fuel logic (clad temperature rises when uncovered), simple critical power ratio check, rate of reactor temperature change.
3. RPV PRESSURE (a): steam production from the core versus steam removal (turbine, bypass, SRVs, RCIC, deaerator extraction, preheater extraction, SJAE); mass and energy balance with saturation via IAPWS-IF97; flashing on depressurization.
4. RECIRCULATION (b): pump affinity laws, jet-pump drive/suction flow, flow balance between loops, cavitation (NPSH from subcooling and level), coastdown, backflow with a pump off and valves open.
5. CRD/HCU (b/c): drive-water header pressure from CRD pumps; per-rod hydraulic drive; per-rod HCU accumulator (nitrogen precharge, pressure, leakage); scram insertion time depends on accumulator and reactor pressure; scram discharge water; refill via the accumulator valve REFILL. CRD flow.
6. RPS (a): two channels A/B with trip logic: manual (both buttons), auto, high pressure, offsite power loss, APRM upscale, APRM downscale (in RUN), LPRM upscale, LPRM low period, low period, reactor level low, mode switch in shutdown. HALF REACTOR TRIP when one channel trips. REACTOR TRIP STATE latches until reset conditions (config). Trip inserts all rods via the HCUs.
7. TURBINE-GENERATOR (b/c): turning gear at rest (safety bus powered), lube and hydraulic oil pumps (main and emergency), steam seals, vacuum, warm-up and differential expansion, roll to 900/1800/2700/3600 RPM with critical-speed vibration peaks, auto run-up with acceleration rate, sync and load, load-dependent steam flow HPT then MSR then LPTs, valve stroking, oil temperature/pressure, hydraulic pressure, axial thrust, radial load, vibration from imbalance/bearing/resonance/water induction, generator temperature, protection trips (overspeed, vibration, loss of vacuum, loss of lube oil, reverse power, generator trip), post-trip coastdown with rolldown, return to turning gear.
8. CONDENSER (b): pressure in mbar from steam load, circulation flow and water temperature, air in-leakage, SJAE and CAR capacity curves, hotwell mass balance, condenser-to-offgas logic, loss-of-vacuum dynamics, effect of outside temperature and humidity on cooling-tower outlet temperature, condenser-to-atmosphere dump.
9. FEEDWATER/CONDENSATE (b): pump curves with speed control, suction/discharge valves, pump temperature (overheat if low flow or suction lost), preheater heat transfer with extraction steam (feedwater temperature), deaerator pressure/temperature/level and dissolved-gas dynamics and deaeration effectiveness, three-element reactor level control, hotwell and deaerator level controllers with operator-adjustable setpoints, polisher conductivity and differential pressure (clogging) and regeneration.
10. COOLING TOWER (b/c): basin mass balance (makeup, evaporation, drift, blowdown), thermal performance versus wet-bulb temperature (outside temperature and humidity).
11. RADIATION (c): monitors for turbine hall (primary and secondary), condenser area, reactor hall; sources: steam-line leaks, fuel failure (activity release), offgas, N-16 in steam (scales with power), shine; independent sensor failure.
12. SUPPRESSION POOL (b): temperature and level from SRV and RCIC exhaust discharge; heat limits.

---

## 12. STATE MACHINES THE ENGINE MUST IMPLEMENT AND DOCUMENT (with Mermaid state diagrams in docs)

- Breaker: OPEN, CLOSING, CLOSED, OPENING, TRIPPED (requires reset), with guards (supply, sync permissive, interlocks).
- Pump: STOPPED, STARTING (ramp), RUNNING, COASTING, TRIPPED, FAILED; trips on loss of suction, overheat, overcurrent, bus loss.
- Valve: CLOSED, OPENING, OPEN, CLOSING, STUCK, FAULTED; per-valve stroke times.
- EDG: STANDBY (preheated or cold), STARTING (crank, build-up), RUNNING_UNLOADED, LOADING (sequenced), RUNNING_LOADED, TRIPPED, FAILED_TO_START.
- Turbine: ON_TURNING_GEAR, SEALS_AND_VACUUM, WARMING, ROLLING (with target RPM), AT_SPEED, SYNCED_LOADED, TRIPPED, COASTDOWN.
- RPS: NORMAL, HALF_TRIP_A, HALF_TRIP_B, FULL_SCRAM_ACTIVE, SCRAM_LATCHED, RESETTING.
- Reactor mode: SD, SRM, IRM, RUN, with mode-dependent rod blocks, trips (mode switch in SD trips), and permissives.
- Annunciator: NORMAL, TRIGGERED_UNACKED (flashing), TRIGGERED_ACKED (steady), CLEARED_UNACKED (optional, config), NORMAL.
- Rate switch: NEUTRAL, HELD_<position>, DEADMAN_TIMEOUT.
- Polisher: IN_SERVICE, BYPASSED, REGENERATING, EXHAUSTED, FAULT.
- ADS: INHIBITED, ARMED, TIMER_RUNNING, ACTUATED, FAILED.

---

## 13. AUTOMATIC SYSTEMS TO SIMULATE (NOT OPERATOR-CONTROLLED UNLESS LISTED)

MCC autocontrol (hotwell, deaerator and reactor level setpoints, three-element feedwater control, feedwater pump speed); APR (automatic power setpoint using rods or recirculation per `rmcp.control_mode`); auto rod balancer; turbine auto pressure control at 7100 kPa; turbine auto run-up; auto condenser control (SJAEs, circulation pumps, valves); auto deaerator control; ADS logic; RCIC auto start and isolation; RHR/LPCI auto logic and valve sequencing; EDG auto start and load sequencing on loss of offsite power; HCU and scram logic; RPS channel logic with half-trip; reactor mode logic (SD/SRM/IRM/RUN with rod blocks); ECCS/MCC conflict detection; pump trips on low suction/overheat; transformer thermal protection; breaker protection; cooling tower level control; polisher regeneration scheduling.

---

## 14. MALFUNCTIONS (INJECTABLE VIA THE INSTRUCTOR CHANNEL ONLY; OPTIONAL STOCHASTIC MODE WITH A HAZARD-RATE MODEL)

Provide an injectable framework plus an optional stochastic mode where each malfunction has a probability per unit time that depends on operating conditions (accumulated stress, wear, water chemistry, vibration, overheating). For EVERY malfunction document: id, parameters, trigger, immediate effect, symptoms and annunciators that appear, operator recovery procedure, clear conditions, and the tags that change. The RBWR Universal Malfunction Guide was NOT supplied: derive entries from standard BWR behavior and label each one "derived, not from the RBWR guide".

Minimum catalog (ids are fixed, expand each fully in the docs):
- ELECTRICAL: `mf.elec.loop` (loss of offsite power), `mf.elec.startup_tr_overload`, `mf.elec.main_tr_fault`, `mf.elec.bus_a_fault`, `mf.elec.bus_b_fault`, `mf.elec.bus_s_fault`, `mf.elec.dc_bus_fault`, `mf.elec.breaker_fail_close`, `mf.elec.breaker_fail_open`, `mf.elec.edg1_fail_start`, `mf.elec.edg2_fail_start`, `mf.elec.edg_fail_load`, `mf.elec.edg_preheat_lost`, `mf.elec.battery_degraded`, `mf.elec.gen_short_circuit`, `mf.elec.gen_reverse_power`, `mf.elec.gen_loss_excitation`, `mf.elec.unit_interlock_fault`, `mf.elec.rolldown_protection_trouble`, `mf.elec.grid_freq_excursion`.
- REACTOR: `mf.rx.rod_stuck`, `mf.rx.rod_drift`, `mf.rx.hcu_leak`, `mf.rx.drive_water_loss`, `mf.rx.crd_pump_trip`, `mf.rx.aprm_fail`, `mf.rx.lprm_fail`, `mf.rx.irm_fail`, `mf.rx.srm_fail`, `mf.rx.recirc_pump_trip`, `mf.rx.recirc_pump_seize`, `mf.rx.recirc_cavitation`, `mf.rx.jet_pump_fail`, `mf.rx.fuel_failure`, `mf.rx.level_instrument_fail`, `mf.rx.scram_fail_partial` (ATWS-like).
- STEAM/PRESSURE/ECCS: `mf.steam.srv_stuck_open`, `mf.steam.srv_fail_open`, `mf.steam.main_steam_leak`, `mf.steam.bypass_stuck`, `mf.ads.spurious_actuation`, `mf.eccs.rcic_fail_start`, `mf.eccs.rcic_false_isolation`, `mf.eccs.rhr_pump_trip`, `mf.eccs.hx_tube_leak`, `mf.eccs.sdc_suction_fault`.
- TURBINE/CONDENSER: `mf.cond.loss_vacuum`, `mf.cond.sjae_fail`, `mf.cond.car_fail`, `mf.cond.tube_leak`, `mf.cond.circ_pump_trip`, `mf.cond.offgas_event`, `mf.turb.high_vibration`, `mf.turb.bearing_failure`, `mf.turb.lube_oil_loss`, `mf.turb.hyd_pressure_loss`, `mf.turb.steam_seal_failure`, `mf.turb.smoke_hall`, `mf.turb.overspeed`, `mf.turb.turning_gear_fail`, `mf.turb.msr_fault`.
- FEEDWATER/WATER: `mf.fw.pump_trip`, `mf.fw.pump_overheat`, `mf.fw.valve_stuck`, `mf.fw.valve_leak`, `mf.fw.preheater_tube_leak`, `mf.fw.preheater_isolate`, `mf.dea.rupture_disk`, `mf.dea.level_excursion`, `mf.dea.pressure_excursion`, `mf.wt.polisher_conductivity`, `mf.wt.polisher_dp_high`, `mf.wt.regen_fail`, `mf.cond.hotwell_ctrl_fail`, `mf.cst.pump_trip`, `mf.cst.overflow`, `mf.tower.makeup_fail`, `mf.tower.level_high`, `mf.tower.level_low`.
- INSTRUMENTATION: `mf.sensor.<tag>.{stuck|zero|high|low|drift|noise}` generic for any sensor.

---

## 15. REQUIRED SCENARIOS (RUNNABLE FROM CONFIG WITH INITIAL CONDITIONS, DOCUMENTED IN `16_scenarios.md`)

1. Cold shutdown (SD mode, rods in, turbine on turning gear, buses de-energized) to startup: energize buses (startup transformer, Bus A/B), safety bus, DC and emergency buses, CST and water systems, CRD, vacuum (CAR, SJAE, circulation), feedwater/condensate/deaerator warm-up, rod withdrawal in SRM then IRM, criticality, heat-up, pressurization, turbine run-up, synchronization, power ascension to RUN, APR automatic control.
2. Hot standby: turbine roll, sync, load, unload, trip, return to turning gear.
3. Load changes with recirculation flow and rods.
4. Scram recovery: xenon pit, decay heat removal via RCIC, bypass, SDC; rod reset.
5. Loss of offsite power with EDG start; station blackout (battery depletion).
6. Loss of vacuum; loss of feedwater (RCIC injection); SRV stuck open; low level with ADS.
7. Shutdown cooling alignment with RHR trains (SDC suction valve, HX bypass).
8. Offgas event procedure (turn SJAEs, condenser circulation and CARs off until the annunciator clears).
9. Full-power steady state (all autocontrols on) for soak and regression tests.

---

## 16. KNOWN AMBIGUITIES (LIST YOUR ASSUMPTIONS; DO NOT ASK)

- The flow diagram shows a "cooling pond" while the panel says "cooling tower": model a cooling tower basin on the circulation loop.
- RC pump 1/2 on Bus A are recirculation pumps; RF pump 1/2 are reactor feed (feedwater) pumps; CC pump is the condenser circulation pump; LVL. CTRL PUMP is the level control (hotwell or deaerator makeup) pump (assumption).
- "LVAC/DC TR." is the link from 6.6 kV to 480 V and the AC/DC rectifier feeding the safety bus and charger.
- "BUS S UNPOWERED" refers to the green safety bus; BUS C is the unit interlock bus.
- The pools in the lobes of the vessel drawing and the dark-red lines toward RCIC are treated as suppression pools.
- The rolldown-protection label text is partly illegible: implement 6.1 and list your assumption.
- The operator guides (turning gear, startup procedures) were not supplied: implement turbine startup from standard practice (turning gear, lube oil, hydraulic oil, steam seals, vacuum, warm-up, roll, sync, load).
- "PIKOM" is read as micrometers of vibration; "Dearator air inflow/outflow" is read as non-condensable gas flow.
- "Site Usage" is the plant's own consumption; "Sent To Grid" is net export.
- Spelling and duplicate annunciators are normalized per Section 3.

---

## 17. NUMERICS, QUALITY AND TESTING

- Stiff systems (point kinetics, xenon, thermal-hydraulics) use implicit or semi-implicit integration with adaptive substepping or stability checks. Guard against NaN, negative mass, negative pressure, divide-by-zero. Conserve mass and energy, with runtime diagnostics exposed as `diag.*` tags.
- Unit tests per model: steam tables, point kinetics step response, decay heat curve, xenon chain, pump curves, valve Cv, breaker interlocks, transformer overload, battery discharge, annunciator on/off delays, rate-switch dead-man, command validation for every error code.
- Integration tests: cold start to criticality; scram decay-heat curve; xenon pit after power reduction; LOOP with EDG start and loading; loss of vacuum trip chain; turbine sync and load with out-of-sync consequence; SRV stuck open depressurization; ECCS/MCC conflict; offgas event recovery; 24 h soak at power with stable values; determinism test (two identical runs produce byte-identical snapshots).
- A protocol test client (Python) exercises every command type, verifies annunciator TRIGGERED/CLEARED events, verifies reconnect/resume, and verifies backpressure coalescing never drops annunciator events.
- Documentation tests: see 4.11.
- State clearly which behaviors are validated against reference values and which are approximations.

---

## 18. APPENDICES (BINDING PARTS OF THE SPECIFICATION)

All values in the appendices are DEFAULTS that must live in config. Where an appendix item is marked ASSUMPTION, list it in your assumptions list. Where an appendix conflicts with an earlier section, the appendix wins for numeric defaults and the earlier section wins for structure.

### APPENDIX A. CONTROL VALIDATION AND INTERLOCK MATRIX

Every control below is validated in this order: (1) role, (2) sim running, (3) schema/range, (4) supply energized, (5) interlocks and permissives, (6) reactor-mode blocks, (7) malfunction state. The first failing check produces the error code. "Needs" means the supply or condition required for the control to be accepted AND to have an effect (if the control is accepted but has no effect, report `applied:true, effective:false, reason:...` in the result).

| control | needs | blocked or interlocked when |
|---|---|---|
| crd.pump1 / crd.pump2 | Bus S energized (CRD 1 / CRD 2 loads) | trips on low suction (CST/condensate unavailable), overcurrent; both pumps may run simultaneously |
| crd.accumulator_valve | CRD header pressure above 4000 kPa for REFILL to be effective | REFILL during a scram or with drive pressure low is accepted but ineffective |
| crd.flow_selector | at least one CRD pump running | clamps at flow limits; ineffective with no pump |
| cst.cstN_makeup / cst.cstN_drain | Bus A energized | makeup blocked above 98 percent level (auto-off); drain blocked below 5 percent; makeup and drain of the same tank are mutually exclusive (`ERR_MUTUALLY_EXCLUSIVE`) |
| elec.startup_tr_breaker | switchyard line 3L5(B2) live | trips on overload; reset needs load below 80 percent |
| elec.bus_a_tr | startup transformer energized OR line 3L5(A2) energized | closing onto two live sources not in sync is blocked (`ERR_INTERLOCK`, detail: "Sources not paralleled; open one source first") unless `allow_paralleling` config is true |
| elec.bus_b_tr | line 3L5(A2) energized | none else |
| elec.gen_breaker_52 (via turb.gen_breaker_sync) | turbine at 3500 to 3700 RPM, AVR breaker closed for excitation | sync outside the permissive window causes the out-of-sync consequence (7.13) |
| elec.main_circuit_breaker | line 3L5(A2) tie available | opening while generator is loaded raises ISLANDING MODE CONFIGURATION and a frequency excursion |
| elec.unit_interlock_52 | Unit 1 bus C energized to receive, Bus A energized to send | sending and receiving at once is impossible; direction follows which side is energized |
| elec.lvac_dc_tr | Bus A energized | opening drops safety bus unless an EDG carries it |
| elec.ac_dc_1_interlock | safety bus or battery present | opening forces the emergency bus onto the battery |
| elec.dc_bus_source | emergency or safety bus energized, selected source must be live | selecting a dead source drops the DC bus; never both sources |
| elec.breaker_72dcg | DC bus energized | opening makes `AUTO STARTUP NOT AVAILABLE` and disables remote start |
| elec.edg_remote_startup | 72DCG closed | with no EDG READY the arm is accepted but `AUTO STARTUP NOT AVAILABLE` stays on |
| elec.safety_bus_to_dg | an EDG running at rated voltage and frequency | blocked while Bus A is live and `lvac_dc_tr` closed (would parallel two sources) unless config allows |
| elec.edg_emergency_trip | guarded; EDG running | immediate EDG trip, lockout until reset |
| elec.edg_startup_request | 72DCG closed | rejected if an EDG is already running (both cannot run) |
| elec.rolldown_protection | none | see 6.1; wrong state raises the trouble annunciator after a delay |
| eccs.rcic_turbopump | DC control power; steam above 1000 kPa for effective operation | auto-isolates on high steam flow or high exhaust pressure |
| eccs.rcic_steam_inlet | RCIC turbopump enabled | stroke-limited |
| eccs.rhrN_pump | Bus S energized; suction path lined up | trips on loss of suction; in RHR (SDC) mode requires the SDC suction valve open and reactor pressure below the SDC permissive (default 1000 kPa) |
| eccs.rhrN_mode | pump stopped to change mode | changing mode with the pump running is `ERR_INTERLOCK` |
| eccs.rhrN_injection_valve | none | opening above 1500 kPa reactor pressure is accepted but flow is zero until pressure falls (low-pressure injection only) |
| eccs.cst_selector | none | alters suction lineup; selecting a CST below 5 percent level raises LOW LEVEL |
| srv.msrvN | DC solenoid supply | manual open while reactor pressure is under 600 kPa has no effect; repeated cycling limited by a minimum reseat time |
| ads.system_inhibit | none | inhibit during an active ADS timer aborts the timer |
| wt.polisher_selector | none | selecting a polisher that is regenerating or exhausted bypasses it and annunciates |
| mcc.hotwell_pump | Bus A | trips on low hotwell level |
| mcc.cond_pump1 / 2 | Bus A / Bus B | trips on low hotwell level; at least one pump required for feedwater suction |
| mcc.fw_pumpN, fw_inletN, fw_outletN | Bus A / Bus B (RF pumps) | pump start requires inlet open and deaerator level above low-low; closing outlet with pump running at speed above 20 percent raises overheat risk |
| mcc.fw_pumpN_speed | pump running | ineffective with pump stopped |
| mcc.hp_fw_preheaterN | steam extraction available | isolating a preheater lowers feedwater temperature; annunciates HIGH PRESSURE FEEDWATER HEATER DISABLED |
| mcc.autocontrol | none | with autocontrol on, manual hotwell/deaerator/feedwater rate controls are accepted but overridden next control cycle unless the loop is in manual (document the exact override behavior) |
| rmcp.group_movement | CRD drive water pressure above 5500 kPa, rod not blocked, RPS not scrammed | rod blocks per Appendix D |
| rmcp.rod_control_mode | none | ALL RODS blocked in RUN mode (`ERR_MODE_BLOCK`) |
| rmcp.reactor_mode | none | moving the switch to SD while rods are withdrawn trips the reactor; RUN requires reactor pressure above the permissive (default 5000 kPa) and IRM on the high range |
| rmcp.apr_control | mode is RUN or IRM above range 8 | accepted only with a valid APRM; drops to manual on APRM failure |
| rmcp.auto_rod_balancer | at least one rod movable | disabled when scram |
| rmcp.scram_button_a / b | none | both within 2 s; scram cannot be blocked |
| recirc.outletN, inletN | Bus A not required (valve motor supply: configurable) | closing both valves on a side with the pump running trips the pump |
| recirc.pumpN | Bus A energized, inlet and outlet open | start permissive requires level above L3 and loop dT under 80 C; trip on L2 low level and on cavitation if sustained |
| recirc.pumpN_speed | pump running | speed capped by cavitation protection |
| turb.stop_valve | hydraulic pressure above 8000 kPa | closes automatically on trip |
| turb.exciter_avr_breaker | DC supply | closing below 3000 RPM is blocked |
| turb.auto_runup | turning gear disengaged or disengageable, vacuum below 150 mbar, steam seal pressure ok, oil pressure ok, hydraulic pressure ok, stop valve open | holds at critical speeds if vibration is above the hold limit |
| turb.runup_target_rpm | none | STOP commands deceleration to turning gear speed |
| turb.control_valve / precise_inlet | stop valve open and hydraulic pressure ok | auto pressure control overrides unless it is off |
| turb.steam_bypass_valve | hydraulic or air supply | auto pressure control overrides when on |
| cond.circ_pumpN_breaker | Bus A / Bus B (CC pumps) | trips on low tower or basin level |
| cond.carN_breaker | Bus S (CAR N1 / N2) | none |
| cond.sjaeN_breaker, sjaeN_valve | steam above 1000 kPa for effective operation | must be OFF during the offgas event recovery or the event persists |
| tower.makeup_pump_breaker | Bus A | none |
| dea.emergency_steam_inlet_fast_close | guarded | closes the deaerator steam inflow valve at maximum rate; auto control then cannot reopen it until reset |

### APPENDIX B. ELECTRICAL DEFAULTS

Load defaults at running (MW): RC pump 1: 4.5, RC pump 2: 4.5 (at 100 percent speed; scale with speed cubed), RF pump 1: 7.0, RF pump 2: 7.0 (scale with speed cubed), COND. PUMP 1: 2.5, COND. PUMP 2: 2.5, CC PUMP 1: 3.0, CC PUMP 2: 3.0, CST 1 MAKEUP: 5.0, CST 2 MAKEUP: 5.0, CST 1 DRAIN: 5.0, CST 2 DRAIN: 5.0 (these draw a lot of power), LVL. CTRL PUMP: 0.8, TOWER MAKEUP: 1.2 (scale with speed), CRD 1: 0.8, CRD 2: 0.8, RHR PUMP L: 2.0, RHR PUMP R: 2.0, CAR N1: 0.5, CAR N2: 0.5, TB. HYDR PUMP: 0.6, TB. LUBR PUMP: 0.4, TB. TURN. GEAR: 0.1, RH CRANE/REFUELER: 0.3, LP LIGHTS: 0.05, CR PANELS: 0.05, EMG. HYD. PUMP: 0.3 (DC), EMG. LUB. PUMP: 0.2 (DC), DC bus lights/ventilation/PMS: 0.5 total, SJAE auxiliaries: 0.3, condenser circulation valve and misc plant load: 1.0. With the 100 percent power lineup and the CST pumps off this must sum to about 41 MW (Site Usage 41.3 MW); tune the misc constant to hit it.
Startup transformer rating 50 MW, overload annunciator at 45 MW, trip when thermal state reaches 100 percent (thermal time constant 120 s at 120 percent load). Main transformer rating 1500 MW, overload annunciation at 105 percent. Transformer losses 0.3 percent of throughput.
Nominal voltages: line 360 kV, Bus A/B/C 6.6 kV, safety bus 480 V, DC 125 V (battery floats at 130 V, loads drop out below 100 V). Bus voltage sags in proportion to loading and during motor starts (starting current 6x for 2 s, which can trip the startup transformer when large pumps are started in a bunch).
Battery: capacity such that the emergency-bus load discharges 100 to 0 percent in 4 h (about 0.0069 percent per second at nominal load, proportional to load), charge rate 0.01 percent per second when AC-DC is closed and the emergency bus is powered, CRITICALLY LOW at 35 percent, DC-powered emergency pumps lose function below 20 percent, total blackout of DC at 0 percent (panels dark, annunciators lose power, only the audible alarm tag `alarm.audible_active` goes false; the frontend must show the "PANEL DARK" state via `elec.panel_power_ok:false`).
EDG: rated 1800 RPM, rated output 7.5 MW, preheat takes 600 s from cold and cools in 1800 s; start sequence: crank 2 s, ramp to 1800 RPM in 8 s, voltage and frequency stable by 10 s, READY lamp true when preheated and no fault; loading sequence after the safety bus is connected: step 1 (t+0 s) emergency bus and DC bus loads, step 2 (t+3 s) CRD 1 and CRD 2, step 3 (t+6 s) RHR pumps L and R, step 4 (t+9 s) CAR N1/N2, TB. HYDR, TB. LUBR, TB. TURN. GEAR, RH crane. Overload above 110 percent trips the EDG on a thermal integral. Fuel for 8 h at full load. Starting with a cold, not-preheated EDG works but sets "EDG NOT PREHEATED" and degrades the start (longer ramp, 20 percent chance of trip, config). Only one EDG may be RUNNING.
Standard power-flow cases the electrical model must resolve correctly (each is a unit test): (1) cold dead site then close 35A and Bus A TR.; (2) generator online, grid connected; (3) generator online, main circuit breaker open (islanded house load); (4) loss of offsite power with generator offline; (5) loss of offsite power with generator online at power (generator trips, rolldown, EDG start); (6) startup transformer overloaded by CST pumps; (7) unit interlock sending to Unit 1; (8) unit interlock receiving from Unit 1; (9) battery-only emergency bus; (10) DC bus on emergency source versus safety source.
Electrical solver rule: do a graph energization pass each FAST tick (source to bus through closed breakers), then a power-balance pass, then a thermal/protection pass; document the algorithm and its handling of loops (two live sources into one bus).

### APPENDIX C. PROTECTION SETPOINTS (DEFAULTS; ALL IN CONFIG)

Level reference: normal level is the reactor level setpoint (a level of 0.0 m relative to normal); top of active fuel (TAF) is -4.6 m relative to normal. Levels are narrow/wide range as appropriate. ASSUMPTION: list it.

| function | default setpoint | action |
|---|---|---|
| High level (L8) | +0.6 m | annunciate HIGH REACTOR WATER LEVEL, trip turbine and feedwater pumps at +0.9 m |
| Level band alarm | plus or minus 0.3 m from setpoint | HIGH/LOW REACTOR WATER LEVEL annunciator |
| Low level (L3) | -0.9 m | RPS REACTOR LEVEL LOW TRIP, scram |
| Low-low (L2) | -1.9 m | RCIC auto start, recirc pump trip, ECCS ready |
| Low-low-low (L1) | -3.4 m | ADS permissive (with timer), LPCI initiation |
| EXPOSED FUEL RODS | below -4.6 m | annunciate; fuel heat-up model engaged |
| APRM upscale scram | 118 percent RTP (flow-biased option) | RPS APRM UPSCALE TRIP |
| APRM downscale | below 3 percent in RUN | RPS APRM DOWNSCALE TRIP |
| LPRM upscale | 125 percent local | RPS LPRM UPSCALE TRIP; HIGH LPRM THERMAL POWER annunciates at 115 percent |
| Reactor period | less than 20 s annunciates LOW REACTOR PERIOD; less than 10 s trips (RPS LOW PERIOD TRIP) | LPRM LOW PERIOD TRIP uses local period under 15 s |
| High pressure | 7600 kPa annunciates HIGH RPV PRESSURE; 7900 kPa trips (RPS REACTOR PRESSURE HIGH TRIP) | SRV lift per staggered setpoints |
| Offsite power loss | Bus A and Bus B both unpowered for more than 2 s | RPS OFFSITE POWER LOSS TRIP |
| Mode switch in SD with rods out | immediate | RPS MODE SWITCH IN SHUTDOWN TRIP |
| Scram not available | fewer than 80 percent of rods with adequate accumulator energy | SCRAM NOT AVAILABLE |
| HCU accumulator low | below 9000 kPa on any rod | HCU ACCUMULATORS PRESSURE LOW (more than 10 rods low gives SCRAM NOT AVAILABLE) |
| Drive water low | header below 7000 kPa annunciates; below 5500 kPa blocks rod movement and annunciates INSUFFICIENT DRIVE WATER PRESSURE | rod withdrawal blocked |
| Turbine overspeed | 110 percent (3960 RPM) | TURBINE TRIP, stop and control valves close |
| High vibration | 100 um annunciates, 200 um trips | HIGH TURBINE VIBRATION / TURBINE TRIP |
| Low vacuum | above 150 mbar NO CONDENSER VACUUM; above 250 mbar trips turbine and closes bypass (steam dump blocked) | |
| Low lube oil pressure | below 150 kPa annunciates, below 120 kPa trips; emergency lube pump auto-starts at 150 kPa | |
| Low hydraulic pressure | below 8000 kPa trips (valves cannot be held open) | |
| Differential expansion | beyond plus or minus 8 mm | TURBINE TRIP |
| Generator reverse power | below -2 percent rated for 10 s | GENERATOR REVERSE POWER, generator breaker opens |
| Generator temperature | above 130 C annunciates, above 150 C trips | |
| Feedwater pump | overheat 180 C annunciates, 200 C trips; low temperature below 60 C annunciates | FEEDWATER PUMP OVERHEAT/TRIP/TEMPERATURE LOW |
| Deaerator | pressure low below 500 kPa, high above 1100 kPa; temperature low below 150 C, high above 190 C; level low 40 percent, low-low 25 percent (feed pump trip) | rupture disk bursts at 1400 kPa |
| Hotwell | low level 25 percent annunciates, 15 percent trips condensate pumps | |
| Condenser circ pump | demand trouble if commanded flow cannot be met for more than 30 s | |
| Cooling tower | low level 30 percent, high level 90 percent | |
| CST | low level 15 percent, high level 90 percent | |
| Polisher | conductivity high above 0.2 uS/cm, differential pressure high above 200 kPa | |
| Radiation | per sensor in config (defaults: normal 0.05 to 0.5 mSv/h; alarm at 10x normal) | |
| Battery | critically low 35 percent | |
| Transformer | startup overload at 45 MW, trip on thermal integral | |

### APPENDIX D. REACTOR MODE AND ROD BLOCK LOGIC (ASSUMPTION: the source panel names the blocks but not their setpoints, so these are derived defaults)

- SD: all rod motion except insertion blocked; `RPS MODE SWITCH IN SHUTDOWN TRIP` is active (scram signal held); SRM, IRM monitoring; shutdown cooling allowed.
- SRM: rod movement allowed; SRM WITHDRAW block when SRM reading is above 10^4 counts per second equivalent or SRM period is under 20 s (SRM WITHDRAW indicator); SRM INSERT block when SRM count is below the minimum 3 counts per second (detector low). SRM probes may be retracted only when the IRM is on range 3 or higher.
- IRM: rod motion allowed; IRM upscale block above 108 percent of the selected range (select a higher range); IRM downscale block below 5 percent of the selected range on ranges above 1 in IRM mode; IPR (intermediate power range) group-selection constraint: only groups in the withdrawal sequence order can be selected (IPR - GROUP SELECTION indicator); the block remains until the correct group is selected.
- RUN: GROUP BLOCK when the selected group movement would exceed the rod worth minimizer limit above 20 percent power (group movement blocked, single group steps only); SRM BLOCK (SRM bypassed and retracted, SRM block flag); IPR BLOCK when APRM above 20 percent and IRM invalid. In RUN, ALL RODS control mode is blocked.
- Transitions: SD to SRM requires no scram signal; SRM to IRM requires IRM on range 3 or higher with SRM above 10^2 counts; IRM to RUN requires reactor pressure above 5000 kPa and APRM above 10 percent; moving to a lower mode is always allowed. APRM downscale trip is active only in RUN.
- The engine publishes every block as a boolean in `rmcp.rod_block_flags` and the human-readable reason.

### APPENDIX E. TURBINE-GENERATOR OPERATING PROCEDURE MODEL (ASSUMPTION: derived from standard practice; the original guides were not available)

Preconditions to leave turning gear (all required): safety bus energized (turning gear, lube pump and hydraulic pump are Bus S loads), main lube pump running with oil pressure above 150 kPa and oil temperature above 30 C, hydraulic pressure above 8000 kPa, steam seal pressure above 100 kPa (seal steam needs reactor pressure above about 1000 kPa or auxiliary steam), condenser pressure below 200 mbar for rolling above turning-gear speed (below 150 mbar for the NO VACUUM annunciator to be clear), stop valve open, no trip latched.
State sequence: ON_TURNING_GEAR (3 RPM) then SEALS_AND_VACUUM then WARMING (differential expansion rises with casing heating; hold times scale with metal temperature, minimum 120 s at 900 RPM) then ROLLING (accelerate at the acceleration rate: SLOW 5 RPM/s, AVERAGE 15 RPM/s, FAST 40 RPM/s; turning gear auto-disengages above 10 RPM) then AT_SPEED (3600 RPM) then SYNCED_LOADED.
Critical speed ranges: first 1100 to 1300 RPM, second 2300 to 2500 RPM. Vibration rises 3x inside a critical range and 8x if the target RPM holds inside it; auto run-up passes critical ranges at least at AVERAGE rate, and holds below a critical range if vibration exceeds the hold limit (70 um).
Steam admission: bypass valve and control valve share steam; control valve opens against pressure control; stop valve opens first (hydraulic). Steam flow at no load 4 percent, at full load 100 percent.
Generator: AVR breaker closes only above 3000 RPM; field builds in 10 s; voltage matches grid within 5 percent; synchroscope phase angle sweeps at the slip frequency; permissive window |phase| under 10 degrees, |slip| under 0.1 Hz; after breaker closes the initial load is 5 percent (about 45 MW) and the operator must raise steam flow within 60 s to avoid reverse power. Load ramp rate limit 1 percent per minute without penalty; faster ramps raise differential expansion and vibration.
Shutdown: reduce load to 5 percent, open breaker (generator trip if open at high load), close stop valve, coastdown (about 50 min for a tuned rotor with vacuum, much shorter if vacuum is lost and brakes), the rolldown generator output powers recirc pumps during the coastdown if the rolldown protection is left disabled, return to turning gear at 3 RPM; the emergency lube and hydraulic pumps keep oil flowing on DC if AC is lost.
Oil: oil temperature rises with load and bearing friction; the oil cooler rejects to the cooling water; high oil temperature above 65 C annunciates (derived).

### APPENDIX F. EQUIPMENT CLASS PARAMETER DEFAULTS

| class | examples | stroke or ramp time | supply | fail mode |
|---|---|---|---|---|
| large motor-operated valve | FW inlet/outlet, deaerator valves, circulation valve, polisher valves, SDC suction, RHR injection, CST selectors, recirc inlet/outlet | 30 to 60 s (recirc 45 s) | Bus A/B/S per valve (config) | as-is |
| small motor-operated valve | hotwell drain/makeup, HX bypass, preheater isolation, SJAE isolation | 10 to 20 s | Bus A or S | as-is |
| hydraulic or air-operated control valve | turbine stop (0.3 s trip close), turbine control, bypass, condenser flow, hotwell level control, tower makeup valve, feedwater regulation | 2 to 10 s | hydraulic header or air | fail-closed (turbine), fail-open (bypass optional) |
| solenoid SRV/ADS | MSRV 1 to 6, ADS SRV 1/2 | 0.3 s open, 1 s close | DC | fail-closed |
| pumps | recirc, CRD, FW, condensate, hotwell, circulation, CST pumps, RHR, CAR | startup ramp 3 to 15 s (recirc 10 s, FW 8 s), coastdown inertia constants in config | per bus | trip |
| relief valve | SRVs | spring setpoint plus hysteresis (blowdown 5 percent), staggered 7650 to 7900 kPa | none | reseat |

Valve flow: Cv model with equal-percentage characteristic default; document the valve coefficient table in config for every valve (Cv at 100 percent, characteristic, stroke time, supply, fail mode). Pumps: document head-flow, efficiency and NPSH-required curve for each pump.

### APPENDIX G. SENSOR DEFAULTS

| sensor class | noise (1 sigma) | lag (time constant) | range clip | notes |
|---|---|---|---|---|
| pressure | 0.25 percent FS | 0.2 s | instrument range | reactor, steam, condenser, drive water, deaerator |
| temperature | 0.5 C | 3 s (thermocouple in thermowell 10 s) | instrument range | fuel assembly temperature lags 5 s |
| level | 0.02 m | 0.5 s | narrow/wide range switchover | reference-leg errors under high drywell temperature (optional) |
| flow | 1 percent reading | 0.5 s | instrument range | |
| neutron SRM | Poisson counting noise | 0.5 s | decades | invalid above range |
| neutron IRM | 1 percent of range | 0.5 s | 125 scale, 8 ranges | |
| APRM | 0.5 percent RTP | 1 s | 0 to 150 | average of valid LPRMs |
| electrical | 0.1 percent | 0.05 s | | MW, kV, frequency |
| radiation | 5 percent | 2 s | | log scale |
Failure modes: STUCK (holds last), ZERO, HIGH (to range max), LOW (to range min), DRIFT (ramp at configured rate), NOISY (sigma times 10), DEAD (quality BAD, value null). The engine publishes quality `BAD` for DEAD and `SUBSTITUTED` when a redundant channel is used.

### APPENDIX H. ANNUNCIATOR PRIORITY CLASSES, TILE BEHAVIOR AND ALARM STATES

- CRITICAL (red tile, continuous audible): REACTOR TRIP STATE, HALF REACTOR TRIP, all RPS trip indicators, EXPOSED FUEL RODS, LOW REACTOR WATER LEVEL below L2, SCRAM NOT AVAILABLE, TURBINE TRIP, GENERATOR SHORT CIRCUIT, SMOKE DETECTED IN TURBINE HALL, DEAERATOR LOW LOW LEVEL, FEEDWATER PUMP TRIP, MAIN BATTERY CRITICALLY LOW, BUS S UNPOWERED, OFFSITE POWER CONNECTION LOSS, HIGH RPV PRESSURE above scram setpoint, NO CONDENSER VACUUM, all radiation annunciators.
- WARNING (amber tile, repeating audible): HIGH/LOW REACTOR WATER LEVEL (band), HIGH RPV PRESSURE (alarm level), HIGH MAIN STEAM PRESSURE/TEMPERATURE, HIGH TURBINE VIBRATION, HIGH TURBINE RPM, GENERATOR REVERSE POWER, NO ROTOR EXCITATION FIELD, STARTUP TRANSFORMER OVERLOAD, BUS A/B UNPOWERED, DC BUS UNPOWERED, EDG NOT PREHEATED while in standby with LOOP, HCU ACCUMULATORS PRESSURE LOW, WATER PRESSURE LOW, INSUFFICIENT DRIVE WATER PRESSURE, LOW/HIGH RECIRC FLOW, RECIRCULATION PUMP CAVITATION, ECCS/MCC CONFLICT, FEEDWATER PUMP OVERHEAT, deaerator pressure/temperature alarms, CONDENSER TO OFF-GAS OPEN, STEAM CONDENSATION NOT EFFECTIVE, RUPTURE DISK TROUBLE, polisher conductivity/differential pressure, SHUTDOWN COOLING IN-SERVICE FAULT, RODS TEMPERATURE IMBALANCE, HIGH ROD POWER.
- CAUTION (yellow tile, single beep): CST levels, hotwell levels, deaerator low level, feedwater temperature low, LOW REACTOR PERIOD, HIGH APR/LPRM THERMAL POWER (below trip), cooling tower levels, OUTSIDE TEMPERATURE HIGH, HIGH I-135 / Xe-135 CONCENTRATION, BYPASS VALVE OPEN, SJAE DISABLED, HIGH PRESSURE FEEDWATER HEATER DISABLED, REACTOR WATER DRAIN, CONDENSER HOTWELL WATER DRAIN, DEAERATOR WATER DRAIN, CONDENSER CIRC. PUMP DEMAND TROUBLE.
- INFO (white/green tile, no audible): ISLANDING MODE CONFIGURATION, STARTUP TRANSFORMER BREAKER CLOSED, MAIN CIRCUIT BREAKER OPEN, DC BUS MAIN BATTERY ACTIVE, UNIT INTERLOCK RECEIVING/SENDING POWER, EDG 1/2 STARTUP PROCESS, DIESEL GENERATOR ACTIVE, SDC-1/2 ACTIVE, LPCI AVAILABLE/ACTIVE, RCIC TURBOPUMP ENABLED, RCIC TURBINE RUNNING/OPERATIONAL, RCIC SUCTION FROM CST, REACTOR AUTOMATIC CONTROL, DEAERATOR AUTOCONTROL, POLISHER REGEN IN PROGRESS, CONDENSATE WATER POLISHER BYPASS, HOTWELL LEVEL CONTROL/MAKEUP VALVE OPEN, CONDENSER HOTWELL MAKEUP PUMPS ACTIVE, CST MAKEUP/DRAIN active, ADS SYSTEM ARMED, SRV open annunciators (caution while open).
- Tile states: NORMAL (dark), TRIGGERED_UNACKED (flashing), TRIGGERED_ACKED (steady), RETURN_TO_NORMAL (optional: flashing slow until acknowledged, config `ringback`). The first-out annunciator of a trip group is flagged `first_out:true` for 1 s of simulation time resolution (the engine records order within a tick by evaluation order and documents it).
- Audible: continuous tone for CRITICAL, repeating for WARNING, single beep for CAUTION, none for INFO. `alarm.audible_active` and `alarm.audible_class` tags; SIL silences until a new annunciator triggers.
- Annunciator evaluation uses sensor tags, not truth, and supports hysteresis on thresholds to avoid chattering (documented per rule). The panel loses power with the DC bus (see Appendix B).

### APPENDIX I. JSON CONFIG SHAPES (EXAMPLES THE ENGINE MUST ACCEPT; DOCUMENT EVERY KEY IN `14_config_reference.md`)

```json
// config/master.json
{
  "schema_version": 1,
  "tick_ms": 50,
  "fast_subcycles": 5,
  "slow_period_s": 1.0,
  "seed": 12345,
  "publish_groups": {
    "electrical": {"hz": 10}, "reactor_fast": {"hz": 10}, "reactor_slow": {"hz": 1},
    "fuel_grid": {"hz": 2}, "turbine": {"hz": 10}, "condenser": {"hz": 5},
    "feedwater": {"hz": 5}, "sync": {"hz": 20}, "annunciators": {"event": true}
  },
  "network": {"port": 8765, "queue_depth": 2000, "ring_buffer": 10000, "ping_s": 5, "max_msg_bytes": 1048576},
  "limits": {"commands_per_s": 50, "burst": 100},
  "files": ["electrical.json", "reactor.json", "turbine.json", "condenser.json",
            "feedwater.json", "eccs.json", "annunciators.json", "sensors.json",
            "malfunctions.json", "scenarios/cold_start.json"]
}

// config/electrical.json (fragment)
{
  "loads": [
    {"id": "elec.load.rc_pump1", "label": "RC PUMP 1", "bus": "elec.bus.a",
     "mw_running": 4.5, "speed_scaling": "cubic", "start_inrush": {"mult": 6.0, "s": 2.0},
     "control_id": "elec.load.rc_pump1", "lamp_red": "fault", "lamp_green": "running"}
  ],
  "transformers": [
    {"id": "elec.xfmr.startup", "rating_mw": 50, "alarm_mw": 45, "thermal_tau_s": 120, "loss_pct": 0.3}
  ],
  "edg": [{"id": "elec.edg.1", "rated_rpm": 1800, "rated_mw": 7.5, "preheat_s": 600, "cool_s": 1800,
           "crank_s": 2, "ramp_s": 8, "load_steps": [[0, ["emerg", "dc"]], [3, ["crd1", "crd2"]],
           [6, ["rhr_l", "rhr_r"]], [9, ["car_n1", "car_n2", "tb_hydr", "tb_lubr", "tb_turn_gear", "rh_crane"]]]}]
}

// config/annunciators.json (fragment)
{
  "id": "ann.mcc.low_reactor_water_level", "text": "LOW REACTOR WATER LEVEL",
  "panels": ["mcc", "eccs", "rmcp"], "priority": "warning",
  "condition": "reactor.level_nr_m < (mcc.sp_reactor_level_m - 0.3)",
  "hysteresis": {"clear_when": "reactor.level_nr_m >= (mcc.sp_reactor_level_m - 0.25)"},
  "on_delay_s": 1.0, "off_delay_s": 2.0, "latching": false, "audible": "repeating",
  "typical_causes": ["Feedwater pump trip", "Feedwater valve closed", "Steam flow exceeds feed flow"],
  "typical_operator_response": ["Check FW pumps and valves", "Raise reactor level setpoint or feed flow", "Prepare RCIC"]
}

// config/malfunctions.json (fragment)
{
  "id": "mf.steam.srv_stuck_open", "label": "SRV stuck open", "category": "steam",
  "params": {"valve": {"type": "enum", "values": ["msrv1","msrv2","msrv3","msrv4","msrv5","msrv6","ads1","ads2"]},
             "open_pct": {"type": "float", "min": 10, "max": 100, "default": 100},
             "delay_s": {"type": "float", "default": 0}},
  "effects": [{"component": "srv.msrv.{valve}", "fault": "stuck_open", "position_pct": "{open_pct}"}],
  "expected_symptoms": ["ann.srv.srv_{pair}_open", "reactor.pressure_kpa falling", "pool.supp_temp_c rising"],
  "recovery": ["Cycle the valve switch", "Reduce power by rods and recirc", "If pressure continues to fall consider scram and RCIC/RHR"],
  "clear_conditions": ["instructor clears", "valve repaired timer elapsed"],
  "stochastic": {"hazard_per_hour": 0.0004, "stress_tags": ["reactor.pressure_kpa"], "source": "derived"}
}

// config/sensors.json (fragment)
{"id": "sensor.reactor.pressure", "tag": "reactor.pressure_kpa", "truth": "rx.pressure",
 "class": "pressure", "noise_pct_fs": 0.25, "lag_s": 0.2, "range": [0, 10000],
 "failure_modes": ["stuck", "zero", "high", "low", "drift", "noisy", "dead"]}
```

### APPENDIX J. EXTRA TRANSPORT DETAILS

REST endpoints (all under `/api/v1`, JSON): `GET /health`, `GET /version`, `GET /tags` (with `?panel=`, `?group=`, `?q=` filters and pagination), `GET /tags/{id}`, `GET /controls`, `GET /controls/{id}`, `GET /annunciators`, `GET /annunciators/{id}`, `GET /components`, `GET /components/{id}`, `GET /malfunctions`, `GET /scenarios`, `GET /snapshot`, `GET /sim/status`, `POST /sim/{pause|resume|speed|step|reset|save|load}`, `POST /scenario/{id}/load`, `POST /malfunction/{id}` (instructor), `GET /events?since_tick=`, `GET /schemas/{message}`. Each endpoint documented in OpenAPI with request/response schemas and error responses `{error:{code,detail,hint}}`.
WebSocket close codes: 1000 normal, 4400 protocol error, 4401 auth failed, 4403 role not permitted, 4408 ping timeout, 4429 rate limit exceeded, 4500 internal error, 4503 server shutting down (client should reconnect with `resume`).
Subscription: by default the client receives all groups; `subscribe` can restrict to a list of groups or tag-id patterns with `*` wildcards (for example `rod.*.pos_pct`) and a per-subscription rate cap. Annunciator, event_log, command_result and sim_status messages are never filtered out.
Rate limits: default 50 commands per second per connection, burst 100; rate-switch keep-alives count as 1/4 weight. Exceeding produces `ERR_RATE_LIMIT` with `retry_after_ms`; repeated violations close the connection with 4429.
Idempotency: `req_id` echoes back; a repeated `req_id` within 60 s returns the original result without re-applying (documented).
Time: `sim_time` is seconds since scenario start; `wall_time_ms` is unix time; `speed` factor published in `sim_status`; the frontend should render with `sim_time`, not wall time.
Snapshot size budgeting: the full tag set is about 2000 to 4000 tags (rods, fuel grid); keep the snapshot under 1 MB and use per-message compression (permessage-deflate) when negotiated.
Server logs: structured JSON logs per connection, command audit trail (who, role, command, result, tick) written to `logs/commands.ndjson`, and a deterministic replay file (seed, config hash, ordered commands with ticks) so any session can be replayed headlessly (`rbwr_cli --replay file`).

### APPENDIX K. SCENARIO SCRIPTS (EACH WRITTEN AS A STEP LIST WITH EXPECTED OBSERVABLES; THE ENGINE MUST BE ABLE TO REPLAY THEM AS TESTS)

K.1 COLD START TO POWER (approximate; tune; the test asserts the milestones, not exact times)
1. Initial: all buses dead except the switchyard lines, battery 100 percent, reactor SD, rods fully inserted, reactor pressure 101 kPa, temperature 30 C, turbine on turning gear (stopped), all breakers open. Expected annunciators: BUS A/B/S UNPOWERED, DC BUS UNPOWERED, MAIN CIRCUIT BREAKER OPEN, DC BUS MAIN BATTERY ACTIVE, EDG NOT PREHEATED (if cold), AUTO STARTUP NOT AVAILABLE.
2. Close `elec.startup_tr_breaker`: STARTUP TRANSFORMER BREAKER CLOSED; startup transformer load tag rises from 0; close `elec.bus_a_tr`: BUS A UNPOWERED clears; Bus A loads still off.
3. Close `elec.lvac_dc_tr`: safety bus energizes (BUS S clears), emergency bus and DC bus power; DC BUS MAIN BATTERY ACTIVE clears when AC-DC interlock is closed and the battery charges (MAIN PACK CHARGING).
4. Start CRD pump 1 (needs CST/condensate suction): drive water pressure rises toward 8500 kPa; HCU accumulators charge via REFILL (`crd.accumulator_valve`); ROD DRIVE WATER PRESSURE LOW clears above 7000 kPa.
5. Close `elec.bus_b_tr` only after line 3L5(A2) is live (not available until generator syncs or main circuit breaker connects; in the cold start Bus B is fed from Bus A only if the interlock is configured, else leave it).
6. Water systems: CST makeup/drain pumps as needed (watch the 50 MW startup transformer: two CST pumps add 10 MW). Hotwell pump and condensate pumps to establish hotwell and deaerator level; polisher selector to BOTH; start circulation pumps and cooling tower makeup pump.
7. Vacuum: start CAR pumps, then SJAEs once steam is available; condenser pressure falls from 1013 mbar toward 60 mbar; NO CONDENSER VACUUM clears below 150 mbar.
8. Recirculation: open recirc outlet and inlet valves, start RC pump 1 and 2 at low speed.
9. Mode switch SD to SRM; the mode switch change clears the SD scram signal; reset scram (config reset sequence); REACTOR TRIP STATE clears.
10. Withdraw rods by group in sequence in SRM mode; SRM count rises; SRM period annunciates; criticality at a rod pattern defined by config (default group 6 partially withdrawn); positive period. Hold; IRM range up; transfer to IRM mode at SRM 10^4.
11. Heat-up and pressurize at less than 55 C per hour (assumption); reactor pressure rises; use bypass valve to control pressure; at 1000 kPa the SJAEs become effective and steam seals are available.
12. Turbine: follow Appendix E; auto run-up to 3600 RPM; close AVR breaker; match synchroscope; close generator breaker; GEN. BREAKER 52 closes; the main circuit breaker connects line 3L5(A2); Bus B energizes; Generator Load rises.
13. Raise power by recirc flow and rods; feedwater pumps on with level control (three-element) at above 15 percent power; mode switch to RUN above 5000 kPa and APRM above 10 percent; enable APR control; reach 100 percent RTP; verify Generator Load about 911 MW, Site Usage about 41 MW.

K.2 LOSS OF OFFSITE POWER AT 100 PERCENT: inject `mf.elec.loop`; expected sequence within seconds: OFFSITE POWER CONNECTION LOSS, generator trips on loss of load (or islands), RPS OFFSITE POWER LOSS TRIP, scram, turbine trip, bus A/B UNPOWERED, bus S loses power then EDG auto starts (if armed) and carries safety loads in sequence, RCIC auto starts on low level, SRVs lift on pressure transient, rolldown briefly powers recirc if protection disabled; the operator restores power by closing breakers when the grid returns.
K.3 LOSS OF CONDENSER VACUUM at power: inject `mf.cond.loss_vacuum`; condenser pressure rises, LOW CONDENSER VACUUM then NO CONDENSER VACUUM; bypass valve closes at the trip setpoint; turbine trips at 250 mbar; reactor pressure rises; scram on high pressure or high flux; SRVs lift; operator uses condenser-to-atmosphere valve and RCIC/RHR.
K.4 SRV STUCK OPEN, K.5 LOSS OF FEEDWATER, K.6 SHUTDOWN COOLING ALIGNMENT, K.7 OFFGAS EVENT: each written in the same step-list form in the docs with the exact inject command, expected annunciators in order and the recovery actions.

### APPENDIX L. DETAILED MALFUNCTION ENTRIES (TEMPLATE; WRITE EVERY CATALOG ENTRY IN THIS FORMAT; ALL ARE "derived, not from the RBWR guide")

- `mf.elec.loop` (loss of offsite power). Params: delay_s. Effect: line 3L5(B2) and 3L5(A2) go dead. Symptoms: OFFSITE POWER CONNECTION LOSS, STARTUP TRANSFORMER load to 0, BUS A/B UNPOWERED if the generator is not carrying the plant, RPS OFFSITE POWER LOSS TRIP, turbine trip. Recovery: confirm scram, verify EDG start, maintain level with RCIC, restore switchyard, re-close breakers in order 35A, Bus A TR., Bus B TR.
- `mf.steam.srv_stuck_open`. See Appendix I.
- `mf.cond.loss_vacuum`. Params: severity (0 to 1), rate_s. Effect: air in-leakage grows, condenser pressure rises. Symptoms: LOW CONDENSER VACUUM, NO CONDENSER VACUUM, STEAM CONDENSATION NOT EFFECTIVE, BYPASS blocked, turbine trip at 250 mbar. Recovery: start CAR pumps and SJAEs, close condenser-to-atmosphere valve, check circulation pumps, restore tower level.
- `mf.fw.pump_trip`. Params: pump (1 or 2). Effect: pump trips, discharge flow collapses. Symptoms: FEEDWATER PUMP TRIP, LOW FEEDWATER flow, reactor level falling, LOW REACTOR WATER LEVEL, possible ECCS/MCC CONFLICT if RCIC starts. Recovery: raise the other pump speed, check suction (deaerator level), restart the pump when its temperature normalizes, use RCIC if level continues to fall.
- `mf.rx.hcu_leak`. Params: rod, leak_rate. Effect: accumulator pressure falls. Symptoms: HCU ACCUMULATORS PRESSURE LOW, per-rod accumulator tag declining, SCRAM NOT AVAILABLE if many rods. Recovery: REFILL the accumulators, keep the CRD pumps running.
- `mf.turb.bearing_failure`. Params: severity. Effect: vibration rises, bearing and oil temperature rises. Symptoms: HIGH TURBINE VIBRATION, rising oil temperature, TURBINE TRIP at 200 um. Recovery: reduce load, trip the turbine, return to turning gear (if the bearing is damaged the turning gear fails and the engine blocks restarts until repair).
- `mf.dea.rupture_disk`. Effect: rupture disk bursts, deaerator pressure drops to atmosphere, steam release. Symptoms: RUPTURE DISK TROUBLE, DEAERATOR PRESSURE LOW, DEAERATION NOT EFFECTIVE, LOW FEEDWATER TEMPERATURE, radiation not affected. Recovery: reduce power, isolate the steam supply (emergency close), repair timer.
- `mf.cond.offgas_event`. Effect: offgas valve opens (radioactive gas bypasses), CONDENSER TO OFF-GAS OPEN. Symptoms: condenser area radiation rises, CONDENSER AREA HIGH RADIATION. Recovery: turn SJAEs, condenser circulation pumps and CAR pumps off until the annunciator clears, then restore in order CAR, SJAE, circulation.
- `mf.eccs.rcic_fail_start`, `mf.rx.recirc_cavitation`, `mf.wt.polisher_conductivity`, `mf.elec.edg1_fail_start`, and every other catalog entry: written the same way (params, effect, symptoms with exact annunciator ids and tags, recovery steps, clear conditions, stochastic hazard definition).
Stochastic mode: hazard per hour per malfunction, multiplied by stress factors from the tags named in `stress_tags` (for example high vibration raises bearing failure hazard, high conductivity raises polisher exhaustion), reproducible via the seeded RNG; instructor can disable stochastic mode globally.

### APPENDIX M. PERFORMANCE AND ENGINEERING REQUIREMENTS

- Step budget: a MEDIUM tick (50 ms sim time) must compute in under 5 ms on a typical desktop core at speed 1x, and under 25 ms at 10x; expose `diag.step_time_ms` and `diag.overrun_count`. If a step overruns, degrade gracefully (skip optional publishing, never skip protection logic).
- Memory under 300 MB. No dynamic allocation in the FAST loop. Fixed-capacity containers where possible.
- Determinism: bitwise-identical results for identical seed, config and command sequence on the same platform; document any known cross-platform floating-point differences; no wall-clock dependence inside the simulation.
- Save format: versioned binary or JSON snapshot with config hash; loading a snapshot with a different config hash is refused with `ERR_UNSUPPORTED` unless forced.
- Logging: structured logs with levels, rotating files, a trace mode that logs every component state change.
- Error handling: the engine never crashes on bad input; invalid commands yield `command_result` errors; invalid config fails fast at load with a precise path (`file:key`) in the message.
- Build: CMake presets for Debug, Release and ASAN/UBSAN; CI script runs unit tests, integration tests, docs coverage, and a 24-hour simulated soak at 20x.
- Code quality: no raw owning pointers, `-Wall -Wextra -Wpedantic -Werror`, clang-format and clang-tidy configs included, public headers documented with Doxygen comments.

### APPENDIX N. ACCEPTANCE CHECKLIST (THE PROJECT IS NOT DONE UNTIL ALL ARE TRUE; PRODUCE A SIGNED-OFF CHECKLIST AT THE END OF PHASE 9)

1. Every control in Section 7 exists, is validated per Appendix A, and is documented per 4.3.
2. Every annunciator in Section 7 exists once (deduplicated), has a priority per Appendix H, trigger logic per 7.17 and Appendix C, and is documented per 4.4.
3. Every telemetry item in Section 8 exists as a tag, comes from a sensor, and is documented per 4.2.
4. Every flow-diagram component in Section 5 and every electrical element in Section 6 exists as a component with a state machine and documented faults.
5. The cold-start scenario K.1 completes under script control with the milestones met; K.2 and K.3 behave as described.
6. Generator Load, Site Usage and Sent To Grid are consistent at power (about 911, 41 and 870 MW).
7. A new Python client written only from `docs/api` can connect, list the registry, operate every control type, and display every annunciator (verified by the reference client).
8. Documentation coverage test passes with 100 percent; every Mermaid diagram parses; every JSON example validates against its schema.
9. Determinism, soak, conservation (mass imbalance under 0.1 percent over 1 h at steady state) and performance tests pass.
10. A final "approximations and limitations" document lists each model and its fidelity class (a/b/c).

---

## 19. WORKFLOW (STRICT)

Work in PHASES. Do NOT advance until I say "next". Each phase must produce: complete compilable code (no TODO stubs, no "implement here"), brief equation explanations, assumptions and simplifications, the file tree, how to build and test, AND the documentation files relevant to that phase (docs are written WITH the code, not afterwards).

- Phase 1: Architecture and specification. Treat Sections 1 to 18 and Appendices A to N as binding. Repo structure, class hierarchy, data flow, timestep and subcycling scheme, JSON config schema and file list, full component list with ids, the COMPLETE tag registry, control registry and annunciator registry tables (every id, unit, range, nominal, panel, condition expression), WebSocket and REST message schemas, JSON Schema files, error code table, the documentation skeleton for all 22 deliverables in 4.1 (with `00_overview.md`, `02_transport_and_envelope.md`, `03_roles_auth_and_limits.md`, `04_message_reference.md`, `09_error_codes.md` and `10_protocols.md` fully written), and the numbered assumptions list.
- Phase 2: Core infrastructure. Units, state store, config loader, IAPWS-IF97, base classes (Component, Valve, Pump, Breaker, Transformer, Bus, Volume, Sensor, Controller), plant network solver, tag registry, command validator (including rate-switch handling with dead-man), annunciator engine, event log, `rbwr_docgen`, and the docs coverage test.
- Phase 3: Electrical system (switchyard, transformers, buses, loads, battery, EDGs, generator coupling, synchroscope math). Docs: electrical component, tag, control and annunciator references plus the electrical transcripts.
- Phase 4: Neutronics, fuel, decay heat, xenon/iodine/samarium, control rods, CRD/HCU, detectors (SRM/IRM/APRM/LPRM). Docs: reactor and CRD references and transcripts.
- Phase 5: RPV, recirculation and jet pumps, steam, SRV/ADS, RCIC, RHR/LPCI/SDC, suppression pools. Docs accordingly.
- Phase 6: Turbine-generator (turning gear, lube/hydraulic systems, steam seals, run-up), bypass, MSR, condenser, SJAE/CAR, offgas, cooling tower. Docs accordingly.
- Phase 7: Feedwater, condensate, polishers/water treatment, deaerator, hotwell, CSTs, preheaters, three-element control. Docs accordingly.
- Phase 8: RPS, reactor modes and rod blocks, all autocontrol loops, malfunction framework with the full catalog, radiation model. Docs: `15_malfunction_reference.md` complete.
- Phase 9: WebSocket server and REST finalization, scenarios, tests, performance profiling, remaining documentation (`13_frontend_guide.md` with the frontend developer checklist, `12_transcripts.md`, `11_sequence_diagrams.md`, `14_config_reference.md`, `17_changelog_and_versioning.md`, OpenAPI and AsyncAPI files), the Python reference client, and a final docs coverage report.

Output rules for every phase: put the assumptions list first; then the file tree; then each file in full in its own code block labeled with its path; then build/test instructions; then a short "what is approximated" list. If a phase would exceed the response length, split it into labeled parts (Phase 3a, 3b, ...) and say "continue" is needed; never truncate a file silently and never replace code with comments.

Start with Phase 1 now.
