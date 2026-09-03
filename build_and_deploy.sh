#!/usr/bin/env bash
#
#   Copyright 2026 Kyle D. Ross
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.

set -euo pipefail

PLUGIN_NAME="DiffBrakePlugin"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
INSTALL_FILE="${HOME}/.x-plane/x-plane_install_12.txt"

# Determine the platform-specific subfolder name X-Plane expects.
case "$(uname -s)" in
    Linux*)   PLATFORM_DIR="lin_x64" ;;
    Darwin*)  PLATFORM_DIR="mac_x64" ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM_DIR="win_x64" ;;
    *)
        echo "Unsupported platform: $(uname -s)" >&2
        exit 1
        ;;
esac

# --- 1. Build the plugin. ---
echo "==> Building ${PLUGIN_NAME}..."
mkdir -p "${BUILD_DIR}"
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --config Release

BUILT_PLUGIN_PATH="${BUILD_DIR}/${PLUGIN_NAME}.xpl"
if [ ! -f "${BUILT_PLUGIN_PATH}" ]; then
    echo "Error: build did not produce ${BUILT_PLUGIN_PATH}" >&2
    exit 1
fi

# --- 2. Find the X-Plane 12 install location. ---
if [ ! -f "${INSTALL_FILE}" ]; then
    echo "Error: could not find X-Plane install file at ${INSTALL_FILE}" >&2
    echo "Make sure X-Plane 12 has been run at least once so it can create this file." >&2
    exit 1
fi

echo "==> Reading X-Plane install location from ${INSTALL_FILE}..."
# The file may contain multiple lines (e.g. one per X-Plane installation);
# use the first non-empty line that points at an existing directory.
XPLANE_ROOT=""
while IFS= read -r line || [ -n "${line}" ]; do
    line="$(echo "${line}" | xargs)" # trim whitespace
    if [ -n "${line}" ] && [ -d "${line}" ]; then
        XPLANE_ROOT="${line}"
        break
    fi
done < "${INSTALL_FILE}"

if [ -z "${XPLANE_ROOT}" ]; then
    echo "Error: no valid X-Plane installation directory found in ${INSTALL_FILE}" >&2
    exit 1
fi

echo "==> Found X-Plane 12 installation at: ${XPLANE_ROOT}"

# --- 3. Create the plugin's directory if it doesn't already exist. ---
PLUGIN_DEST_DIR="${XPLANE_ROOT}/Resources/plugins/${PLUGIN_NAME}/${PLATFORM_DIR}"
echo "==> Ensuring plugin directory exists: ${PLUGIN_DEST_DIR}"
mkdir -p "${PLUGIN_DEST_DIR}"

# --- 4. Copy the compiled plugin into place. ---
echo "==> Copying ${BUILT_PLUGIN_PATH} -> ${PLUGIN_DEST_DIR}/"
cp -f "${BUILT_PLUGIN_PATH}" "${PLUGIN_DEST_DIR}/"

echo "==> Done. Plugin deployed to ${PLUGIN_DEST_DIR}/${PLUGIN_NAME}.xpl"
