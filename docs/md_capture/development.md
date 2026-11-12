# Development and CI

The [GitHub CI workflow](https://github.com/nomisma-labs/md_capture/actions/workflows/ci.yml)
builds and tests `master`, pull requests and `v*` tags. It can also be run manually.
The build job uses the `ci` preset, including the local TLS capture integration test.
The checks job validates Python, license headers, formatting and naming, building the Clang
preset for the naming check. Release tags are checked against the CMake project version.

Each run records its source commit, jobs and logs. Use a successful run for the commit you
intend to build as the build evidence. The capture integration test uses a local server;
it does not require exchange credentials or connect to a live exchange.

## Native development

Install the [documented toolchain and dependencies](../kit/build.md#toolchain), then:

```bash
git clone --recurse-submodules https://github.com/nomisma-labs/md_capture.git
cd md_capture
./bootstrap.sh dev
```

`dev` runs configuration and unit tests. Use `./bootstrap.sh ci` for the optimized build with
warnings as errors and integration tests, or `./bootstrap.sh complete` for all tests in Debug.

## Docker development

Install Git, Bash and Docker Engine, or Docker Desktop configured for Linux containers.
The published Nomisma image supplies the compilers, CMake, Ninja, Python, Boost, OpenSSL,
Catch2 and quality-check tools. Clone the repository and its submodules as above, then run:

```bash
./tools/md_capture/dev_container.sh
```

This opens Bash at `/workspace`. Run builds and applications in that shell; it stays open
after each command finishes:

```bash
./bootstrap.sh dev
cmake --build --preset dev
./build/dev/bin/okx_md_capture --help
```

Type `exit` when done. The container is removed when the shell exits; a new invocation starts
a fresh container with the same checkout and saved build outputs.

The interactive shell loads the config commands from the checkout's `json_config` dependency:

| Command | Effect |
| --- | --- |
| `cfg_set` | Set `NOM_CONFIG_DIR` to the current directory. |
| `cfg_add` | Append the current directory to the config search path. |
| `cfg_print` | Print the config search path. |
| `cfg_clear` | Unset `NOM_CONFIG_DIR`. |

These commands use container paths and affect the current shell. No config directory is
selected automatically; use `cfg_set` from the directory you want to search.

To run one command from the host and return to the host shell afterward, pass that command
to the helper:

```bash
./tools/md_capture/dev_container.sh ./bootstrap.sh dev
./tools/md_capture/dev_container.sh cmake --workflow --preset ci
```

Edit files with your usual host editor; the checkout is mounted at `/workspace`. The helper
runs with your user and group IDs so generated files keep your ownership. Container builds
are stored in `build/docker/` on the host, mounted as `/workspace/build`, and survive exiting
the container. Native `build/dev`, `build/ci` and `build/clang` directories are separate.
Running bootstrap inside the container clears only the selected container build directories.

The helper uses `ghcr.io/nomisma-labs/ci:latest`, matching GitHub CI. Each invocation checks
the registry for updates with `--pull=always` and downloads any changed layers. After an image
update, use bootstrap for a fresh build. The image is built from
[cmake_template's Dockerfile](https://github.com/nomisma-labs/cmake_template/blob/master/docker/ci/Dockerfile).
It is a Linux `amd64` toolchain image; running it on an ARM host requires Docker's `amd64`
emulation. Native Linux `amd64` is the validated container platform.

The container supplies development tools. Repository and capture-data access use the mounted
checkout, so files written outside `build/`, such as `data/`, are also visible on the host.
