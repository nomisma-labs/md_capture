# md_capture

<!-- repo-description:begin -->
`md_capture` is the application repository for production market-data capture and symbol
discovery on cryptocurrency exchanges. It composes `asio_comm`, `assembly`, `base`,
`spd_logger` and `json_config` to collect market data for analysis and replay.
<!-- repo-description:end -->

The repository currently contains the initialized build, pinned dependencies and development
scope. Capture executables, capture-specific library code and application tests are not yet
implemented. This scaffold does not collect or persist market data.

## Application scope

Exchange feeders and symbol providers will use Assembly/relay with `asio_comm` WebSocket,
HTTPS and timer components. Their initial structure follows the current
[asio_comm applications](https://github.com/nomisma-labs/asio_comm/blob/master/apps/README.md).
The applications here own configuration, exchange-specific behavior, lifecycle policy,
capture output and operational diagnostics.

The [application-layer plan](https://github.com/nomisma-labs/handbook/blob/master/master_plan/application-layer.md)
defines the boundary: transport mechanics stay in `asio_comm`; subscription and application
heartbeat handling, reconnection, useful-data freshness, gap recording and persistence belong
to the capture application or its exchange adapters. Storage format, raw versus normalized
records and the first exchange set remain design decisions in [TODO.md](TODO.md).

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
| `json_config` | `v0.3.0` | JSON loading, validation, overrides and configuration generation |

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
For everyday work, use `cmake --workflow --preset dev`. At this stage the builds compile the
dependency libraries; CTest has no capture tests to run.

## Layout and development

- [apps/](apps/README.md): exchange applications under `apps/<exchange>/<app>/` as they are added.
- `core/`: reusable capture code when an application establishes a shared contract.
- `tests/unit/` and `tests/integration/`: future capture tests and local fixtures.
- `external/`: pinned component repositories.
- [docs/](docs/README.md): build and development documentation.
- [TODO.md](TODO.md): decisions and implementation work before production operation.
- [AGENTS.md](AGENTS.md): repository instructions.
- [CHANGELOG.md](CHANGELOG.md): repository changes.
