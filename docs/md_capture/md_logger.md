# MD logger implementation guide

This guide describes the current one-file logger API. Start with the
[MD capture reference design](md_capture_reference_design.md) for architecture and the
[Improvement proposal](improvement_proposal.md) for future package storage.

`nom::md_logger` collects received market-data messages into one configured file. It copies
messages into an in-memory queue, with a producer-owned pending buffer for contention, and uses one
writer thread to serialize and append batches.
It has no symbol/channel table, channel distribution, inheritance or per-symbol files.

Import `nom.md_logger` and link `nom::md_logger`. Applications also link `nom::core`
to initialize the application logger. Public declarations and private state are in
[md_logger.cppm](../../src/md_logger/md_logger.cppm); function definitions and POSIX file I/O
are in [md_logger.cpp](../../src/md_logger/md_logger.cpp). There is no implementation header.

The repository installs a CMake package named `md_capture` with two separate targets:
`nom::md_logger` and `nom::md_logger_ai`. Link only the writer you use. The OKX app links
`nom::md_logger`; neither writer target imports or links the other.

## First use

```cpp
import std;
import nom.md_logger;
import nom.core;

nom::init_logger(nom::main_logger);
const nom::md_logger_config config{.file = {"data/okx_spot.txt"}};
nom::md_logger logger(config);
const std::string message = R"({"arg":{"instId":"BTC-USDT"},"data":[]})";
if (!logger.log({std::chrono::system_clock::now(), message}))
    throw std::runtime_error("MD logger is stopped or failed");

// After stopping the producers:
if (!logger.stop())
    throw std::runtime_error(*logger.error());
```

`log()` copies the borrowed payload before returning. `true` means accepted into the shared
queue or pending buffer, not written or synchronized to storage. `false` means the logger has stopped accepting messages, either through
`stop()` or a writer failure. There is no exception handling on this path: allocation failures
from payload copies or buffer growth propagate rather than becoming logger status.

`stop()` must follow producer quiescence. It prevents further acceptance, transfers the
pending buffer, wakes the writer, drains accepted messages and joins the thread. It returns false on
storage failure; `error()` supplies the diagnostic. Repeated calls are allowed. One lifecycle owner must call `stop()` and destroy the logger; concurrent `stop()`
calls and use after destruction are outside the contract. `stop()` can throw on locking,
pending buffer allocation or joining errors. The destructor catches these exceptions, requests stop,
and lets `std::jthread` join before worker state is destroyed. It reports an exceptional cleanup
through `LERROR` on the lifecycle owner's thread. Initialize `nom::main_logger` before creating
the MD logger and keep it available through destruction. `LERROR` contains logging exceptions
and falls back to stderr. Records in the pending buffer may be lost if their transfer failed;
call `stop()` explicitly to observe shutdown errors.

The writer is owned by `std::jthread` and receives its `std::stop_token`. A stop-aware
`std::condition_variable_any` wakes immediately when stop is requested. `stop()` transfers
the pending buffer and requests stop while holding the queue mutex, then unlocks and explicitly
joins the writer. The worker drains the shared queue before exiting. Its final status is read after
joining without copying an error string.

## Configuration

The logger retains a `const md_logger_config&`; it does not copy the configuration.
Keep that object unchanged and alive until the logger is destroyed and its writer has joined.
Do not construct a logger with a temporary configuration. In the OKX assembly, `config_owner`
owns and validates the application configuration before constructing the nodes, and outlives
the sink and its logger. Standalone callers use a named configuration as in the example above.
The logger also validates its configuration before allocating queue storage or starting its worker.

| `md_logger_config` field | Default | Meaning |
| --- | --- | --- |
| `file` | Required | One output path, relative to the process working directory unless absolute |
| `flush_records` | `16384` | Wake the writer when the queued buffer reaches this many messages |
| `flush_ms` | `2000` | Writer wait in milliseconds for a smaller shared buffer; excludes producer-pending records and I/O delay |

`flush_records` must be positive; `flush_ms` must be in 1..60000. The record threshold is a batching trigger, **not a
capacity limit**. The queue may grow while storage is slow; this implementation does not drop
messages or impose a backlog limit. The interval does not bound filesystem or retry delays.

`md_logger_config` uses `base`'s config types and owns its validation. `file` is required; the
flush settings use `cfg_defaulted`. Its `NOM_JCONFIG` mapping is exported from `nom.md_logger`,
which publicly links `nom::core`. The mapping macro comes from `<json_config/config.h>`;
`import nom.core;` supplies its helpers in the module interface. Consumers can use
`md_logger_config` through `nom.md_logger`; code that names the core configuration or logging
APIs directly imports `nom.core` too.

The interface contains a GCC 16 compatibility guard that loads the core logging configuration
before declaring the MD configuration. This avoids a compiler failure when importing the
compiled module; it does not change configuration or runtime behavior.

The [OKX sink config](../../apps/okx/okx_md_capture/md_sink.h) embeds `md_logger_config`.
The [application config](../../apps/okx/okx_md_capture/main.cpp) exposes it at `md_sink.md_logger`,
and the sink passes it straight to the logger constructor.

The JSON keys are `md_sink.md_logger.file`, `md_sink.md_logger.flush_records` and
`md_sink.md_logger.flush_ms`. Missing or null flush settings preserve their initialized defaults.
`file` must be present and nonempty; the sample app config explicitly sets `data/okx_spot.dat`.
Both configuration checking and the logger constructor call `md_logger_config::validate()`.

## Queue and thread ownership

`log()` uses `try_lock` on the shared queue. If the mutex is busy, it copies the message into
its producer-owned pending buffer and returns without waiting for that mutex. The next successful
queue lock transfers older pending records before the new record. Buffer swaps avoid moving
records when the shared queue is empty; otherwise records move to its end.

Calls to `log()` must be serialized by one producer context. The pending buffer is not synchronized
for concurrent producers; the OKX app uses one I/O thread. The writer never accesses it, and
`stop()` may transfer it only after the producer has stopped. With the queue mutex held,
`log()` checks a cached stop token and the writer failure status. Caching avoids copying the
token on every record. The queued-message counter is also protected by that mutex; the
pending buffer path does not check shutdown state or update the counter. Direct enqueues increment the
counter once; transferring a pending buffer batch adds its size after the transfer succeeds.

A record left in the pending buffer needs a later successful `log()` call or `stop()` to reach the
writer. In particular, `flush_ms` applies to the shared queue, not the producer pending buffer.
The worker swaps the shared queue with its flush buffer under the mutex, then releases the lock
before serialization and file I/O. Vectors retain capacity between batches. This avoids waiting
for the queue mutex; payload allocation and buffer growth can still delay the producer.

`stats()` locks the queue mutex to read a consistent snapshot of all counters and can run
concurrently with the producer and writer. It reports the total number of records added to the
shared queue, successfully flushed messages and flushed bytes. Records still in the pending buffer
are excluded until transferred, including during `stop()`. These counters belong to this
instance and exclude bytes already present in the output file. The message-count difference
includes shared-queue and in-flight work, but not records in the pending buffer. A failed batch may
have written a prefix; it is not included in the successfully flushed counters.

## Text and file handling

Each record is:

```text
<timestamp-microseconds><space><received-data><LF>
```

The timestamp is local receive time since the Unix epoch, truncated to microseconds. The
caller supplies it at reception. Each literal CR or LF in the payload becomes one space;
JSON backslash escapes, numeric formatting and all other bytes remain unchanged.

The logger does not parse JSON, inspect symbols, validate UTF-8 or calculate message hashes.
The OKX adapter supplies received JSON objects and handles plain-text `pong` separately.
Binary/SBE data needs a separate recording contract.

On the first nonempty batch, the writer creates missing parent directories. For every batch
it opens the configured file with append/create flags, writes the serialized bytes and closes
it. Empty captures create no file or directories. Existing bytes remain, and a subsequent
logger instance using the same path appends to them.

This version does not rotate, rename or synchronize files with `fsync`, and does not produce
metadata sidecars. Successful shutdown means writes and closes succeeded; it is not a
power-loss durability guarantee. Use one active logger per file: separate instances are not
coordinated and their partial writes may interleave. Appending after an incomplete final line
does not repair that line. Recovery and metadata policy remain separate design work.

## Write failures

Open and write operations each allow up to five non-interruption errors, with 100ms between
retries. `EINTR` retries immediately. Short writes advance the buffer pointer; subsequent
retries use only the unwritten suffix. A zero-byte write counts as an error, so it cannot
cause an infinite busy loop. Close errors are terminal because retrying close can affect a
reused descriptor.

An exhausted retry budget or a directory/serialization/close failure stops the worker.
`log()` rejects messages when it acquires the queue mutex and observes a stop request or
recorded writer failure; until then, contended calls can still append to the pending buffer. `error()` exposes the failure even when no
new messages arrive.
Accepted data that was not successfully flushed remains reflected in the counters; `stop()`
does not replay a failed batch. The OKX capture sink checks this status on each
`nom::event::app_health_check` and requests application shutdown on failure. It forwards the health
check to downstream handlers even when it reports failure. The `nom::app_control`
assembly node emits these events at `app_control.health_check_period_msec` (2000 ms by default;
0 disables periodic checks). The sink creates its private writer on each accepted
`nom::event::app_start`, before starting the subscriber. Each run has fresh counters and appends
to the same configured file. On `nom::event::app_stop`, the sink stops the subscriber first,
then drains its writer and reports any final-flush error through `nom::event::app_failed` to
`app_control`. The top-level app never operates on the writer directly.

Ctrl-\ (`SIGQUIT`) flushes the operational application log only. It does not request an
MD-writer flush or change its batching and shutdown behavior.

## Other implementation and verification

Import `nom.md_logger_ai` and link `nom::md_logger_ai` for `nom::md_logger_ai`. It owns
`md_logger_ai_config`, `md_logger_ai_stats`, `capture_result` and its input type `capture_event_ai`.
`nom.md_logger` owns `nom::event::market_data` for `md_logger`; the two event types have the same timestamp
and payload fields but are independent, so both modules can be imported together. Each writer
has its own unit suite.

`md_logger_ai` provides bounded queues, rotating segments, temporary-file publication and
metadata; its API remains `append()` / `finish()`. It is not selected by the OKX app.
Its independent implementation and tests remain available for experimentation. Future package
storage follows the [Improvement proposal](improvement_proposal.md), whose calendar-only rotation
and shared-folder coordination have not yet been implemented in the current app.

Unit tests cover byte ownership, append behavior, batching, shutdown, pending buffer ordering and draining,
producer progress during blocked writes, short/interrupted/zero writes and retry failures.
Local TLS tests exercise OKX capture, heartbeats, signals, runtime limits and storage failures.
These checks do not establish production throughput or network-filesystem crash behavior.
