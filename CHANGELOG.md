# Changelog

Notable changes to this repo. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org); how to release:
[docs/kit/build.md](docs/kit/build.md#versioning).

## [Unreleased]

- Shared application nodes, lifecycle events, `md_logger` and `md_sink` are available through
  `nom.services` / `nom::services`. `nom.md_logger_ai` remains independent.
- GCC 16 Debug builds share Asio declarations between service partitions to avoid duplicate
  `query`, `require` and `prefer` symbols.

## [0.1.0] - 2026-11-12

Initial release.

### Included

- `okx_md_capture`: OKX Spot capture for configurable symbols and `tickers`, `trades` and
  `books` channels, with JSON config, operational logging and application heartbeat checks.
- Assembly-based application control with graceful shutdown, runtime limits, periodic health
  checks and console signals for stopping capture or flushing the operational log.
- `nom.md_logger`: buffered capture through one writer thread into one configured append-only
  file, with batching, write retries, counters and final-write error reporting. Records contain
  a local receive timestamp in microseconds followed by the received JSON; symbols and channels
  share the same file, without rotation or metadata sidecars.
- Independent `nom.md_logger_ai` implementation with bounded queues, unique run directories,
  size/age rotation, per-file JSON metadata and segment publication without replacement.
  The OKX app uses `nom.md_logger`.
- Text capture reader with valid-prefix recovery, logger and application-control unit tests,
  config validation tests and a local TLS capture integration test.
- Reference and design documentation for capture, backend processing and market-data services.
- C++23 modules and the `cmake_template v0.14.0` build kit. The sole direct repository dependency
  is `asio_comm v0.6.1`, which supplies `nom.core` through `nom_core v0.3.1`.
- Sustainable Use License 1.0 for project-owned code and documentation; the shared build kit
  and declared MIT exceptions retain their MIT notices. Installed artifacts include the license.
