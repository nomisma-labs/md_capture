# AGENTS.md

<!-- repo-description:begin -->
`md_capture` is the application repository for production cryptocurrency market-data capture
and symbol discovery. It depends on `base`, `assembly`, `spd_logger`, `asio_comm` and
`json_config`. The current scaffold provides the build and pinned dependencies; capture
applications and storage are not implemented yet.
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
  a README. Extract shared capture code into `core/` when its contract is established.
- Relay delivery is synchronous. Borrowed transport payloads must be consumed within their
  callback or copied into owned storage before deferred processing.
