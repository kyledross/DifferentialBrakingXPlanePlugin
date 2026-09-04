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
PLUGIN_NAME="DiffBrakePlugin"
PLUGIN_SOURCE="${SCRIPT_DIR}/${PLUGIN_NAME}.xpl"
INSTALL_FILE="${HOME}/.x-plane/x-plane_install_12.txt"

if [[ ! -f "${PLUGIN_SOURCE}" ]]; then
    echo "Error: ${PLUGIN_SOURCE} is missing from the release package." >&2
    exit 1
fi

if [[ ! -f "${INSTALL_FILE}" ]]; then
    echo "Error: could not find X-Plane's installation file at ${INSTALL_FILE}." >&2
    exit 1
fi

XPLANE_ROOT=""
while IFS= read -r line || [[ -n "${line}" ]]; do
    line="${line//$'\r'/}"
    if [[ -n "${line}" && -d "${line}" ]]; then
        XPLANE_ROOT="${line}"
        break
    fi
done < "${INSTALL_FILE}"

if [[ -z "${XPLANE_ROOT}" ]]; then
    echo "Error: no valid X-Plane installation was listed in ${INSTALL_FILE}." >&2
    exit 1
fi

PLUGIN_DIR="${XPLANE_ROOT}/Resources/plugins/${PLUGIN_NAME}/lin_x64"
install -D -m 755 "${PLUGIN_SOURCE}" "${PLUGIN_DIR}/${PLUGIN_NAME}.xpl"

printf 'Installed %s to %s\n' "${PLUGIN_NAME}" "${PLUGIN_DIR}"
