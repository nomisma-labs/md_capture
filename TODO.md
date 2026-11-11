# Market-data capture restart checklist

The [MD capture reference design](docs/md_capture/md_capture_reference_design.md) records current behavior. The
[Improvement proposal](docs/md_capture/improvement_proposal.md) separates agreed decisions from
proposed contracts. Expected scope is around 20 data sources, with further extension possible;
this is not a claim that their capture and recovery behavior is already validated.

## 1. Settle the package contract

- [ ] Define package/process/stream/session identities, directory ownership and the descriptor
  for every file. Set versioning rules and the initial data/descriptor contents; allow symbol
  information, measured statistics and analytics to be added later.
- [ ] Select the timestamp basis and timezone for precise calendar-hour/day buckets. Define
  boundary inclusion, clock changes, delayed records and source-session changes across periods.
- [ ] Define which symbols and snapshot information belongs in a package, including observation
  times, effective times where known, request identity and links to subscription sessions.
- [ ] Separate accepted, written, finalized, durably stored and archived states. Define failure
  reporting and the evidence required before a package can be declared complete.

## 2. Implement and validate package storage

- [ ] Allocate a unique root per process incarnation and coordinate period folders through a
  FolderRoller. Give each source logger its own files; do not split symbols in the writer.
- [ ] Route each record to its exact time period, including batches crossing a boundary.
  Coordinate all active, idle and one-off loggers before closing a package. Support hour/day
  rotation only; size and record count do not trigger a roll. Record-count thresholds may
  still control batching.
- [ ] Guarantee progress for the final producer-pending records even if no later message arrives.
  Define queue and spool capacity limits, overload behavior and visibility of unwritten data.
- [ ] Preserve `stop()` as drain/join plus error reporting, extending it to package finalization.
  Define incomplete-tail recovery, recovery ownership and publication without replacing evidence.
- [ ] Choose synchronization and file-descriptor policies after testing the deployment storage.
  Exercise concurrent restarts, process/host loss, partial writes, disk full, network-storage
  uncertainty and clock changes. Unique paths alone do not establish durability.

## 3. Qualify source capture

- [ ] Define acknowledgement, readiness, useful-data freshness, reconnect and gap recording for
  each source; include rate limits and capture-session boundaries.
- [ ] Add symbol/catalog requests and independent snapshot requesters. Periodic and one-off
  request streams each need their own logger, lifecycle and association with the streamed data.
- [ ] Define binary/SBE framing and FIX record/session information before accepting those feeds.
  Preserve required sequence and protocol context; text validation cannot identify every binary payload.
- [ ] Measure reception latency, allocations, queueing and throughput under bursts and slow storage.
  State what each timing metric measures and distinguish clock error from transport delay.

## 4. Build backend and product capabilities

- [ ] Implement verified archive transfer and an inventory of package contents, quality and provenance;
  define local retention, upload retries and when local copies may be deleted.
- [ ] Exercise independent hot-hot collectors and exchange-specific reconciliation, preserving both
  raw observations, conflicts and unresolved gaps. See [Backend processing](docs/md_capture/backend_processing.md).
- [ ] Define versioned normalization, symbol partitioning, temporal symbol history and reproducible
  derived datasets without rewriting raw evidence.
- [ ] Choose initial delivery formats, retention tiers, aggregation contracts and coverage commitments;
  assess storage, processing and delivery costs. See [Market data services](docs/md_capture/md_services.md).

## 5. Add interactive application controls

- [ ] Add an optional console/TUI for log flushing, configuration reload, status queries and
  application-specific commands such as sending orders or stopping trading. Enable it only
  when operational logging writes to a file, so log output does not interfere with the console.
  Route commands to the responsible components and display their results in the console.
