# Transport and Envelope

## Current transports

The repository currently provides two local transports:

- `rbwr_server`: newline-delimited diagnostic JSON over stdin/stdout. It supports commands, snapshots, malfunctions, scenarios, and five save slots.
- `rbwr_http`: dependency-free read-only HTTP server bound to loopback by default on port 8765.

Start the REST adapter with:

```sh
./build/rbwr_http --port 8765
```

Available GET endpoints:

- `/api/v1/health`
- `/api/v1/version`
- `/api/v1/snapshot`
- `/api/v1/tags`
- `/api/v1/controls`
- `/api/v1/annunciators`
- `/api/v1/malfunctions`
- `/api/v1/scenarios`
- `/api/v1/slots`

Available POST endpoints:

- `/api/v1/command` with body `{"type":"toggle","req_id":"r1","control_id":"elec.startup_tr_breaker","value":"true"}` routes an operator command through normal engine validation.
- `/api/v1/malfunction` with body `{"id":"mf.cond.loss_vacuum","active":true}` injects or clears a supported instructor fault in the local adapter.
- `/api/v1/sim/step` with body `{"seconds":0.05}` advances the engine and returns a snapshot.
- `/api/v1/scenario/{name}/load` loads `cold_shutdown`, `full_power`, or `scram_recovery`.
- `/api/v1/slots/{1..5}/save` saves the selected game slot.
- `/api/v1/slots/{1..5}/load` loads the selected game slot.

The HTTP adapter returns JSON and closes each connection after one response. It binds to loopback and has no authentication yet, so it is intended for local inspection and headless integration while the full authenticated WebSocket/REST transport is developed.

## Remaining protocol work

The full specification still requires WebSocket command transport, authentication, protocol envelopes with sequence numbers and wall-clock timestamps, heartbeat and resume, subscriptions, rate limiting, backpressure, and mutating REST endpoints. Those features are intentionally not implied by the current read-only adapter.
