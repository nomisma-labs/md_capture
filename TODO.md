# Market-data capture work

The repository scaffold and dependency composition are in place. No capture application or
storage contract is implemented yet. Follow the
[application-layer plan](https://github.com/nomisma-labs/handbook/blob/master/master_plan/application-layer.md).

## Define the first capture workflow

- Select the first exchange and market, required channels, symbol selection and catalog scope.
- Define JSON configuration, validation and `--check-config` behavior using `json_config`.
- Decide raw versus normalized records, capture timestamps, source/session identity and the
  relationship between market data and catalog snapshots. `nom::network_timestamp`
  is a monotonic time point; durable records need an explicit wall-clock/replay time contract.
- Choose storage format, file rotation and retention, bounded buffering/backpressure, output
  failure policy, and shutdown behavior that drains or accounts for pending records.

## Implement the applications

- Add exchange feeders and symbol providers using the current `asio_comm` apps as integration
  references. Keep catalog acquisition and streaming behavior explicit; introduce shared code
  when supported by their actual needs.
- Define subscription success, API/schema validation, readiness and useful-data freshness.
  A connection, acknowledgement or heartbeat alone does not establish market-data readiness.
- Add reconnection/resubscription policy with bounded retry pacing, exchange rate-limit
  handling and recorded gaps. Define recovery behavior for any stateful stream.
- Separate durable market-data output from operational logging. Preserve binary payloads and
  define ownership before any buffering; borrowed callback views cannot outlive callbacks.
- Handle SIGINT/SIGTERM, cancel owned timers and drain transport callbacks before destroying
  nodes. Account for pending storage separately from transport completion.

## Validate operational behavior

- Add reproducible local fixtures for catalogs, subscriptions, malformed responses, missing
  useful data, disconnect/reconnect, gaps, storage failures and shutdown.
- Validate configuration generation/loading and capture output/replay against the chosen
  record contract. Add per-app build/run/configuration documentation with each application.
- Add opt-in live connectivity checks and sustained capture checks after local validation;
  short connectivity samples do not establish lossless or production-ready capture.
- Configure private-submodule access for hosted CI through the shared template workflow
  before relying on GitHub Actions validation; see [new-repo setup](docs/kit/new_repo.md#per-repo-settings).
