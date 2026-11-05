#!/usr/bin/env bash
set -euo pipefail
odin_project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
odin_idf_dir="${ODIN_IDF_PATH:-/home/logitexx/.espressif/v5.5/esp-idf}"
if [[ ! -f "$odin_idf_dir/export.sh" ]]; then
    echo "ESP-IDF 5.5.0 not found. Set ODIN_IDF_PATH to its installation." >&2
    exit 1
fi
# IDF's activation script manages its own unset variables.
set +u
source "$odin_idf_dir/export.sh"
set -u
odin_idf_version="$(idf.py --version)"
if [[ "$odin_idf_version" != "ESP-IDF v5.5" && "$odin_idf_version" != "ESP-IDF v5.5.0" ]]; then
    echo "Expected ESP-IDF 5.5.0; got $odin_idf_version" >&2
    exit 1
fi
cd -- "$odin_project_dir"
if (( $# == 0 )); then
    set -- build
fi
idf.py "$@"
