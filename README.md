# md_capture

<!-- repo-description:begin -->
`md_capture` provides applications for collecting cryptocurrency market data and symbol
information for historical analysis and replay.
<!-- repo-description:end -->

## Key components

| Component | Purpose |
| --- | --- |
| [okx_md_capture](apps/okx/okx_md_capture/README.md) | OKX Spot MD capture application with configurable subscriptions and output. |
| [nom.services](docs/md_capture/app_control.md) | Shared configuration ownership, Asio context, application control, events, capture sink and MD logger. |
| [md_logger](docs/md_capture/md_logger.md) | Buffered file writer in `nom.services` for received market data and local receive timestamps. |

See the [documentation index](docs/md_capture/README.md) for the reference design,
operating guides and improvement plans.

## Dependencies

[`asio_comm`](external/asio_comm/README.md) (`v0.6.1`) is the sole direct repository dependency,
pinned at `external/asio_comm`. It supplies communication services and brings `nom_core`
transitively for core facilities.

System prerequisites include Boost, OpenSSL development files and CA certificates.
See the [build guide](docs/kit/build.md#toolchain) for compiler and build-tool requirements.

## Build

```bash
git submodule update --init --recursive
./bootstrap.sh ci
```

This performs a fresh build, tests and quality checks. For incremental builds and tests,
use `cmake --workflow --preset dev`. Other presets are described in the
[build guide](docs/kit/build.md).

## License

Project-owned code and documentation use the [Sustainable Use License 1.0](LICENSE)
and are source available. Copyright belongs to Alexey Tkachenko. The synchronized
build kit and any MIT exceptions listed in `project_license.json` retain their
[MIT notice](cmake/kit/LICENSE); dependencies retain their own licenses.
