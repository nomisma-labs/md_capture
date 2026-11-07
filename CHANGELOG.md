# Changelog

Notable changes to this repo. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org); how to release:
[docs/kit/build.md](docs/kit/build.md#versioning).

## [Unreleased]

### Added

- Initialize the application repository from `cmake_template` with pinned `base`, `assembly`,
  `spd_logger`, `asio_comm` and `json_config` dependencies.
- Document production capture scope, application layout and outstanding implementation work.
- Add the `nom.md_capture` module and bounded asynchronous MD logger with readable timestamped text records,
  per-file JSON metadata, size/age rotation, exclusive run directories and segment publication without replacement.
- Add `okx_md_capture` with JSON configuration, application heartbeat deadlines and graceful
  capture finalization; add a reader with valid-prefix recovery and local storage/TLS tests.
- Document capture-layer design and proposals for an immutable historical-data archive.

### Changed

- Export the MD logger's JSON mapping from its module and declare `json_config` as a public
  build/package dependency. Keep OKX subscription construction in application code and use
  standard uppercase conversion for symbols.
- Declare `md_logger` in its module interface and move function definitions to `md_logger.cpp`.
  Map JSON directly to its shared `md_logger_config`; remove the app's duplicate writer config.
- Split the writers into independent `nom.md_logger` and `nom.md_logger_ai` modules and CMake
  targets; remove the combined `nom.md_capture` module and update app, tests and installation.
- Keep the OKX writer private to `capture_sink` and drain it through `nom::event::app_stop`,
  forwarding stop to the subscriber first and reporting final-flush failures to the terminator.
- Preserve the rotating logger as `md_logger_ai` and add `md_logger` with
  one producer buffer, one writer thread, append-to-file batches and bounded write retries.
- Use `md_logger` in the OKX app with `writer.file`, `flush_records` and `flush_ms` settings.
- Collect capture documentation under `docs/md_capture/` and add the MD logger guide.
- Reuse a local copy of the `asio_comm` feeder's I/O and termination nodes in `okx_md_capture`;
  keep writer ownership and storage health monitoring in the capture sink.
- Pin `json_config` v0.5.0 and `spd_logger` v0.6.0. Map application log levels and configs
  with `NOM_JCONFIG_ENUM` and `NOM_JCONFIG`, preserving the default `info` level. Accept
  lowercase names and uppercase aliases, with lowercase serialization and strict rejection
  of other spellings and values. Use generated help, short aliases, and `full`, `view`,
  and `silent` config checks in `okx_md_capture`.
- Store `<timestamp-microseconds><space><JSON-object><LF>` records, preserving the received
  JSON bytes except for replacing literal CR/LF with spaces. Keep plain-text pong in heartbeat handling and
  connection/error diagnostics in the application log.
- Add UTF-8 validation, per-file format/subscription metadata, a text reader, and tests for
  metadata publication failures and incomplete text records. Binary/SBE capture remains out of scope.
