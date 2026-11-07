# md_capture

<!-- repo-description:begin -->
`md_capture` is the application repository for production market-data capture and symbol
discovery on cryptocurrency exchanges. It composes `asio_comm`, `assembly`, `base`,
`spd_logger` and `json_config` to collect market data for analysis and replay.
<!-- repo-description:end -->

The first application, [okx_md_capture](apps/okx/okx_md_capture/README.md), captures OKX Spot
messages into one configured file using the asynchronous [MD logger](docs/md_capture/md_logger.md).
Each record contains a local microsecond timestamp and the received JSON. The app uses JSON
configuration and drains its single writer thread during shutdown.

The [design proposal](docs/md_capture/design_proposal.md) describes the three capture components
and ideas for growing into a historical-data service. The rotating `nom::md_logger_ai`
implementation remains available separately. Operational work is tracked in [TODO.md](TODO.md).

## Application scope

Exchange feeders and symbol providers use Assembly/relay with `asio_comm` WebSocket,
HTTPS and timer components. They emit raw events to an MD logger that performs no symbol
separation. A symbol provider is planned; the first app takes explicit Spot symbols.
The applications here own configuration, exchange-specific behavior, lifecycle policy,
capture output and operational diagnostics.

The [application-layer plan](https://github.com/nomisma-labs/handbook/blob/master/master_plan/application-layer.md)
defines the boundary: transport mechanics stay in `asio_comm`; subscription and application
heartbeat handling, reconnection, useful-data freshness, gap recording and persistence belong
to the capture application or its exchange adapters. The first writer records received text with
local microsecond timestamps; metadata is deferred, and normalization and symbol partitioning
belong downstream.

## Dependencies

The application pins its complete set of Nomisma components as Git submodules. The top-level
CMake project declares them before loading any dependency, so shared transitive components
use these same selected versions.

| Repository | Pin | Purpose |
| --- | --- | --- |
| `base` | `v0.3.0` | Common types, configuration types and utilities |
| `assembly` | `v0.4.0` | Component composition and synchronous event routing |
| `spd_logger` | `v0.5.0` | Operational logging |
| `asio_comm` | `v0.2.0` | TLS WebSocket, asynchronous HTTPS and timers |
| `json_config` | `v0.4.0` | JSON loading, validation, overrides and configuration generation |

Recursive submodules provide Metal, fmt, spdlog and nlohmann/json. Boost and OpenSSL are
system dependencies; the shared kit discovers Boost, and `asio_comm` discovers OpenSSL and
system thread support. See the [build guide](docs/kit/build.md#toolchain) for the C++23,
CMake and compiler requirements. Install OpenSSL development files and CA certificates too.

Dependency apps and tests are disabled by default when composed into this project. In
particular, the `json_config` configuration example named `md_capture` is not built as this
repository's capture application. Catch2 is discovered when this project's tests are enabled.

## Build

```bash
git submodule update --init --recursive
./bootstrap.sh
```

Bootstrap performs fresh builds and quality checks for all standard presets except `release`.
For everyday work, use `cmake --workflow --preset dev`. Use `./bootstrap.sh complete` for logger,
configuration and local TLS integration checks. See the [app guide](apps/okx/okx_md_capture/README.md)
for capture and archive inspection commands.

## Layout and development

- [apps/](apps/README.md): exchange applications under `apps/<exchange>/<app>/` as they are added.
- `core/md_logger/`: the `nom.md_logger` module and `md_logger`.
- `core/md_logger_ai/`: the independent `nom.md_logger_ai` module and `md_logger_ai`.
- `tests/unit/` and `tests/integration/`: storage, configuration and local TLS capture checks.
- `external/`: pinned component repositories.
- [docs/md_capture/](docs/md_capture/README.md): logger documentation and capture design.
- [docs/kit/](docs/kit/README.md): shared build and development documentation.
- [TODO.md](TODO.md): decisions and implementation work before production operation.
- [AGENTS.md](AGENTS.md): repository instructions.
- [CHANGELOG.md](CHANGELOG.md): repository changes.
