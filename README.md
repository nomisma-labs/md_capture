# md_capture

<!-- repo-description:begin -->
`md_capture` provides applications for collecting cryptocurrency market data and symbol
information for historical analysis and replay.
<!-- repo-description:end -->

[![CI](https://github.com/nomisma-labs/md_capture/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/nomisma-labs/md_capture/actions/workflows/ci.yml)

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
./bootstrap.sh ci # you can use dev, complete or other presets here
```

This performs a fresh build, tests and quality checks. For incremental builds and tests,
use `cmake --workflow --preset dev`. Other presets are described in the
[Presets section of the build guide](docs/kit/build.md#presets).

### Development shell and build in the published container

```bash
./tools/md_capture/dev_container.sh
# Inside the container, at /workspace:
./bootstrap.sh dev
```

The shell stays open after the build. Continue working there and type `exit` when done.

See [Development and CI](docs/md_capture/development.md) for Docker prerequisites,
interactive development and the GitHub build checks.

#### Run `okx_md_capture`
```
# Set config search folder for convenience
ubuntu@3cd950142a39:/workspace$ cd build/dev/config/
ubuntu@3cd950142a39:/workspace/build/dev/config$ cfg_set
/workspace/build/dev/config
ubuntu@3cd950142a39:/workspace/build/dev/config$ cd ..
ubuntu@3cd950142a39:/workspace/build/dev$ bin/okx_md_capture
Press CTRL-C after some time
## you can see data in the data folder:
ubuntu@3cd950142a39:/workspace/build/dev$ ls -la data/
total 856
drwxr-xr-x  2 ubuntu ubuntu   4096 Nov 12 20:55 .
drwxr-xr-x 13 ubuntu ubuntu   4096 Nov 12 20:55 ..
-rw-r--r--  1 ubuntu ubuntu 864649 Nov 12 20:56 okx_spot.dat
```
The docker build is mapped to host folder `md_capture/build/docker` so you can access captured market data directly from you host machine as `md_capture/build/docker/dev/data/okx_spot.dat`

## License

Project-owned code and documentation use the [Sustainable Use License 1.0](LICENSE)
and are source available. Copyright belongs to Alexey Tkachenko. The synchronized
build kit and any MIT exceptions listed in `project_license.json` retain their
[MIT notice](cmake/kit/LICENSE); dependencies retain their own licenses.
