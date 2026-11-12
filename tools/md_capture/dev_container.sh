#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

# Use the latest toolchain image published by cmake_template, matching GitHub CI.
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
image=ghcr.io/nomisma-labs/ci:latest
mkdir -p "$repo_root/build/docker"

options=(
    --rm --init --pull=always --platform linux/amd64
    --user "$(id -u):$(id -g)"
    --mount "type=bind,source=$repo_root,target=/workspace"
    --mount "type=bind,source=$repo_root/build/docker,target=/workspace/build"
    --workdir /workspace
)
if [[ -t 0 && -t 1 ]]; then
    options+=(-it)
fi
if (( $# == 0 )); then
    set -- bash --rcfile /workspace/tools/md_capture/dev_bashrc.sh
fi

exec docker run "${options[@]}" "$image" "$@"
