# AGENTS.md

<!-- repo-description:begin -->
`md_capture` is the application repository for production cryptocurrency market-data capture
and symbol discovery. Its sole direct repository dependency is `asio_comm`, which supplies
communication and the `nom.core` module / `nom::core` target through `nom_core`.
`nom.services` provides shared lifecycle nodes and events, `md_sink`, and `md_logger`
with one serialized producer, a contention
pending buffer, one writer thread and one configured append-only file. The independent `nom.md_logger_ai` module provides `md_logger_ai`
with bounded queues and segment publication. The first app, `okx_md_capture`, captures configured
Spot instruments using JSON configuration. Production follow-up work is tracked in `TODO.md`.
<!-- repo-description:end -->

Read and follow the rules every Nomisma repo shares: @docs/kit/agents.md

Rules for this repo alone follow below.

- Describe the current implementation on its own terms; do not refer to previous implementations
  or archived projects in source comments or documentation.
- Applications here are intended for production capture. Keep implemented behavior and
  validation evidence distinct from planned reliability and storage guarantees.
- Keep exchange-specific subscription, heartbeat, reconnection, freshness and persistence
  policy in this application layer; transport protocol mechanics belong in `asio_comm`.
- Keep exchange applications under `apps/<exchange>/<app>/`, with per-app configuration and
  a README. Extract shared capture code into `src/` when its contract is established.
- Relay delivery is synchronous. Borrowed transport payloads must be consumed within their
  callback or copied into owned storage before deferred processing.
