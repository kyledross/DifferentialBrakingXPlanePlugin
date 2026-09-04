#!/usr/bin/env bash
#
# Copyright 2026 Kyle D. Ross
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="diff-brake-plugin-builder"
BUILD_DIR="${SCRIPT_DIR}/docker-build"
OUTPUT_DIR="${SCRIPT_DIR}/docker-output"
USER_NAMESPACE_ARGS=()

if ! command -v docker >/dev/null 2>&1; then
    echo "Error: Docker is not installed or not available on PATH." >&2
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    echo "Error: cannot connect to the Docker daemon." >&2
    exit 1
fi
# Rootless Podman maps the host user to container root by default. Preserve
# the caller's user mapping so CMake can write to the mounted directories.
if docker info --format '{{.Host.BuildahVersion}}' >/dev/null 2>&1; then
    USER_NAMESPACE_ARGS=(--userns=keep-id)
fi

rm -rf "${BUILD_DIR}" "${OUTPUT_DIR}"
mkdir -p "${BUILD_DIR}" "${OUTPUT_DIR}"

docker build --tag "${IMAGE_NAME}" "${SCRIPT_DIR}"

docker run --rm \
    "${USER_NAMESPACE_ARGS[@]}" \
    --user "$(id -u):$(id -g)" \
    --volume "${SCRIPT_DIR}:/source:ro" \
    --volume "${BUILD_DIR}:/build" \
    --volume "${OUTPUT_DIR}:/output" \
    "${IMAGE_NAME}" \
    bash -c '
        set -euo pipefail
        cmake -S /source -B /build -DCMAKE_BUILD_TYPE=Release
        cmake --build /build --parallel
        ctest --test-dir /build --output-on-failure
        install -m 755 /build/DiffBrakePlugin.xpl /output/DiffBrakePlugin.xpl
        install -m 755 /source/install.sh /output/install.sh
        install -m 644 /source/LICENSE /output/LICENSE
        install -m 644 /source/NOTICE /output/NOTICE
    '

printf 'Build complete. Release files are in %s\n' "${OUTPUT_DIR}"
