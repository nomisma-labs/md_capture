# Market-data capture work

The first implementation consists of the MD logger, `okx_md_capture`, a text reader and
local tests. The [design proposal](docs/md_capture/design_proposal.md) distinguishes implemented
behavior from recommendations for a historical-data service.

## Next application work

- Review the app-local service nodes and event types for reuse across capture apps and
  feeders; consider moving the common nodes into `asio_comm` once their contract is settled.
- Add an OKX symbol provider using the same raw capture-event boundary. Define catalog refresh,
  request/status metadata, observed/effective times and subscription selection.
- Validate subscription acknowledgements and API errors; define readiness and useful-data
  freshness per channel, separately from application heartbeat monitoring.
- Add bounded reconnect/resubscription with explicit session and gap events and rate-limit
  handling. Keep order-book snapshot/reconstruction policy for its own design review.
- Design metadata for `md_logger`, including subscriptions, collector identity/region,
  build/configuration provenance and clock quality. Keep credentials out of capture metadata.

## Storage and performance

- Define a backlog limit/overflow policy for `md_logger` before production deployment; its
  current queue is unbounded and `flush_records` controls batching only.
- Define incomplete-tail handling, file ownership and rotation for the one-file logger.
  `md_logger_ai` retains an alternative segment/publication implementation for review.
- Review the text record/metadata contract, reader size limit, periodic synchronization policy,
  segment size/age and compression strategy before committing to a public format.
- Design binary/SBE framing separately and expose WebSocket payload type through the transport.
  UTF-8 validation cannot distinguish every binary message from text.
- Add archive content hashes; line framing detects incomplete records but not all valid-looking corruption.
- Measure throughput, callback latency, allocations and backlog under bursts and slow storage;
  optimize the ingress queue only against those measurements.
- Extend abrupt-exit/host-loss testing and validate the target storage/mount deployment.
  Direct NFS durability and rename support are not established by local filesystem tests.
- Define recovery ownership/fencing, spool capacity limits, retention and upload retries.
  Add an immutable archive uploader with content verification and committed manifests.

## Historical-data service

- Preserve independent collector streams and build versioned reconciled views with provenance.
- Implement exchange-specific coverage checks, duplicate/conflict policy and incident reporting.
- Design time-indexed catalog history, raw replay and normalized/partitioned datasets.
- Benchmark storage, replication, compression and delivery costs before expanding coverage.
- Configure private-submodule access for hosted CI through the shared template workflow.
