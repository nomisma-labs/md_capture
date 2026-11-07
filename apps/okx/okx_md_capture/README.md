# OKX market-data capture

`okx_md_capture` captures OKX Spot WebSocket messages through `asio_comm`, composes the app
with Assembly/relay, loads JSON settings with `json_config`, and writes one ordered capture
stream through `nom::md_logger` from the `nom.md_logger` module. Operational logs contain status and counters, not raw payloads.
It is the first implementation of the [capture design](../../../docs/md_capture/design_proposal.md).

## Build and run

From the repository root:

```bash
cmake --workflow --preset dev
mkdir -p data
build/dev/bin/okx_md_capture --config build/dev/config/okx_md_capture/okx_md_capture.json --check-config
build/dev/bin/okx_md_capture --config build/dev/config/okx_md_capture/okx_md_capture.json \
    --set /exit_after_sec=10
```

## Configuration

Configuration follows the assembly nodes:

- `log` configures the application's operational logger (`nom::log_config`).
- `md_subscriber.connection` configures the endpoint and heartbeat.
- `md_subscriber.subscription` selects symbols and channels.
- `health_checker.period_msec` controls the shared health-check period.
- `md_sink.md_logger` configures capture storage.
- `exit_after_sec` remains an application setting used by the terminator.

Symbols and the capture file are required. The smallest config is:

```json
{
    "md_subscriber": {
        "subscription": {
            "symbols": ["BTC-USDT"]
        }
    },
    "md_sink": {
        "md_logger": {
            "file": "data/okx_spot.txt"
        }
    }
}
```

Other settings retain their C++ defaults when omitted. There is no implicit symbol selection.
The [sample config](config/okx_md_capture.json) shows the full structure. Override subscriptions with
`--set '/md_subscriber/subscription/symbols=["BTC-USDT","ETH-USDT"]'` and
`--set '/md_subscriber/subscription/channels=["trades"]'`.
Use the full sample for these overrides: `--set` updates paths that already exist in the input JSON.
The sample jcfg section generates `build/dev/genconfig/dev/okx_md_capture.json`.

`--check-config` (or `-k`) performs loading, overrides and validation, then prints lookup details
and the final JSON before capture starts. Use `--check-config=view` for JSON only or `-k silent`
for just `OK`. JSON includes C++ defaults and reflects runtime substitutions and overrides.
Checks do not initialize logging, create capture output or open network connections.
`--help` (`-h`) prints generated option help before config loading; `-c FILE` is the short form
of `--config FILE`. Unknown arguments and invalid config fail.

| Setting | Default | Meaning |
| --- | --- | --- |
| `log.level` | `info` | Operational log threshold: `debug`, `info`, `warn`, `error` or `always`; uppercase aliases are also accepted |
| `log.file` | `null` | Console output; an object selects an operational log file |
| `log.file.name` | Required when `log.file` is supplied | File to append operational logs to |
| `log.file.rotation_size` | `null` | Positive size in bytes enables rotation with ten older files retained |
| `md_subscriber.connection.endpoint` | `wss://ws.okx.com/ws/v5/public` | Verified TLS endpoint; override for local fixtures |
| `md_subscriber.connection.heartbeat_sec` | `20` | Silence interval and subsequent pong deadline; 1..29 seconds |
| `md_subscriber.subscription.symbols` | Required | Nonempty Spot instrument IDs; normalized to uppercase; duplicates rejected |
| `md_subscriber.subscription.channels` | `["tickers","trades","books"]` | Nonempty API-native channel names |
| `md_sink.md_logger.file` | Required | One file to append capture data to, relative to the working directory |
| `md_sink.md_logger.flush_records` | `4096` | Queued-message threshold that wakes the writer; not a capacity limit |
| `md_sink.md_logger.flush_ms` | `200` | Flush a smaller buffer after this wait; 1..60000 milliseconds |
| `health_checker.period_msec` | `2000` | Period between shared health-check events; 0..60000 milliseconds; 0 disables checks |
| `exit_after_sec` | `null` | Unlimited; 0 stops on connection; positive seconds start at connection |

For file-based operational logging, set `log.file` to
`{"name":"logs/okx_md_capture.log","rotation_size":10485760}`. Omit `rotation_size` to append
without rotation. This logger receives status messages and counters; received market data goes
to `md_sink.md_logger.file`. JSON adapters for the existing `spd_logger` types live in the
application's [log_config.h](log_config.h), using `NOM_JCONFIG_ENUM` for levels and
`NOM_JCONFIG` for both logger config structures. Level matching is exact: use the listed
lowercase names or their all-uppercase aliases; mixed-case names are rejected. Config checks
serialize levels as lowercase. Omitted or null levels retain the default `info`.

The subscriber constructs the subscription request and checks its 64 KiB limit in its
constructor. `--check-config` validates configuration without constructing the subscriber or
its request. The subscriber sends the request and preserves received JSON objects,
including acknowledgements and error responses. Plain-text `pong` stays in heartbeat handling. After receive silence it
sends text ping and fails if pong does not arrive before the deadline. This follows the
[OKX connection protocol](https://www.okx.com/docs-v5/en/#overview-websocket-connect).
It does not parse symbols from received messages or split their storage.

## Output and shutdown

The logger appends to `md_sink.md_logger.file`, creating parent directories on the first batch. Every
record is `<timestamp-microseconds><space><JSON-object><LF>`, with local receive time since the
Unix epoch. Literal CR/LF bytes become spaces; other bytes and JSON escapes remain unchanged.
All symbols share the same file. Existing contents are retained on restart.

This logger does not rotate files or produce metadata sidecars. Its producer buffer has no
capacity limit. See [MD logger](../../../docs/md_capture/md_logger.md) for queue ownership,
retry behavior, counters and durability limits. Binary/SBE support remains future work.

```bash
cat data/okx_spot.txt
python3 tools/md_capture/read_capture.py data/okx_spot.txt
# Inspect complete records before a malformed or unfinished tail:
python3 tools/md_capture/read_capture.py --recover data/okx_spot.txt
```

SIGINT/SIGTERM and the optional runtime limit send `nom::event::app_stop`. The sink forwards
it to stop transport work, then drains and joins the writer. Remaining I/O callbacks drain afterward. Success exits 0; transport failures, missing pong and writer failures exit 1.
With health checks enabled, storage errors are checked even during feed silence. A successful write/close does not imply
power-loss durability; this logger does not call `fsync`.

No automatic reconnect, symbol provider, subscription/readiness validation, order-book
reconstruction, archive upload or offline reconciliation is implemented yet. Exchange API error
messages are recorded as received; they are not yet interpreted as an application failure.
An exit code of 0 means the requested stop and writer drain completed, not that the
exchange delivered complete or useful market data. Local filesystem tests do not establish NFS
crash guarantees or production throughput.

## Composition and validation

The app composes `config_owner`, `nom::asio_service`, `nom::app_terminator`, `nom::health_checker`,
`md_sink` and `md_subscriber`.
[config_owner.h](config_owner.h) copies `Traits::config` into the assembly context and calls
its `validate() const` before constructing any nodes. The method must return
`std::expected<void, std::string>`; validation failure throws `std::invalid_argument` with the
reported error. This also protects direct assembly construction outside the JSON loader.
Nodes access the immutable copy through `nom::ctx(this).cfg()`. The context outlives the nodes,
so components can retain references to their configuration sections without copying them.
The caller's configuration can be changed or destroyed independently of the assembly.

Each configurable node owns its configuration type and JSON mapping in its header:
[health_checker.h](health_checker.h), [md_sink.h](md_sink.h) and
[md_subscriber.h](md_subscriber.h). The subscriber also owns its connection and subscription
configuration. [main.cpp](main.cpp) defines `app_config` and combines the nodes through
`md_capture_assembly`. Its `config` type alias supplies `config_owner` without naming the concrete
application type in the nodes. `nom::app_terminator`, `health_checker`, `md_sink` and `md_subscriber`
use default constructors and require `config_owner` in the assembly. Each provides a private
`cfg()` accessor: capture and health-check nodes select their own section, while the terminator
reads its application settings. Configuration validation definitions follow the node classes.

[asio_service.h](asio_service.h) defines `nom::asio_service`, which provides the I/O context.
`nom::app_terminator` in [app_terminator.h](app_terminator.h) owns exit status, signals and the runtime limit.
The terminator comes before every stop handler. [app_events.h](app_events.h) defines the empty
`nom::event::app_start`, `nom::event::app_stop` and `nom::event::app_health_check` structs.
Nodes handle each event through a separate `on_request` overload and must forward it with
`nom::relay<Traits>::pass_request(this, event)`. These are application-wide requests: even a handler
that triggers shutdown must forward the event already being delivered. Nodes without a matching
handler are skipped; the sink's lifecycle handler participates only in shutdown. The assembly's
`event_suppressors` list includes all three events and ends delivery after the last participant.

`nom::health_checker` owns one timer and sends `nom::event::app_health_check` down the request
chain every `health_checker.period_msec` milliseconds. A zero period disables health checks
without blocking application lifecycle events. `nom::event::app_start` schedules the first check after
one period when enabled; `nom::event::app_stop` cancels the pending check. The next
tick is armed before dispatch so a handler that triggers shutdown also cancels that tick.
Checks run synchronously on the application's I/O context and should be short.

`md_sink` is the first health-check handler. It checks its private writer's error status and
relays failures to the terminator, then forwards the health check whether healthy or failed.
Other nodes can participate by defining `on_request(nom::event::app_health_check)` and forwarding
the event after their check. A failure cancels future ticks, but the current check still reaches
all downstream participants.

[feed_events.h](feed_events.h) defines `nom::event::feed_event` for connection, completion and
failure responses to the terminator.

The subscriber relays borrowed capture events synchronously to the sink, which transfers
ownership to the writer before returning. Only the writer thread performs data file I/O.

The health checker cancels its timer before forwarding `nom::event::app_stop` to the sink.
The sink forwards the command to the subscriber, drains and stops the writer, then logs its
counters. A `feed_event::stop_failed` response reports a final-flush failure to the local
terminator, which changes the exit result to failure
without generating another stop. Every explicitly received `app_stop` is still forwarded.
The app only starts the assembly, runs its I/O context and
returns the exit code; it does not access the writer.

`./bootstrap.sh complete` runs the health-check lifecycle tests, both logger unit suites,
generated configuration validation and a local TLS integration test. The app fixture covers
mixed-symbol JSON, CR/LF replacement, heartbeats, transport/storage/final-flush failures, signal
draining, runtime limits, append behavior, configuration checks, required symbols, operational
log levels/file output, nested defaults/overrides and reader recovery. The `dev` preset skips
the TLS integration test.
