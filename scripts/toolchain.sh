#!/bin/bash
# Shared tool locations. An explicit override is authoritative; see scripts/README.md.
ESPUI_PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -z "${ESPUI_TOOLCHAIN_ROOT:-}" ]]; then
    for ESPUI_CANDIDATE in "$ESPUI_PROJECT_ROOT/.toolchain" "$ESPUI_PROJECT_ROOT/../.toolchain" "/Users/chengpeng/Documents/Codex/2026-10-03/es/.toolchain"; do
        if [[ -f "$ESPUI_CANDIDATE/esp-idf/export.sh" ]]; then
            export ESPUI_TOOLCHAIN_ROOT="$(cd "$ESPUI_CANDIDATE" && pwd)"
            break
        fi
    done
fi
if [[ -z "${ESPUI_TOOLCHAIN_ROOT:-}" || ! -f "$ESPUI_TOOLCHAIN_ROOT/esp-idf/export.sh" ]]; then
    echo "ESP-IDF toolchain not found. Set ESPUI_TOOLCHAIN_ROOT to a directory containing esp-idf/ and espressif/." >&2
    return 1
fi
export ESPUI_TOOLCHAIN_ROOT="$(cd "$ESPUI_TOOLCHAIN_ROOT" && pwd)"
echo "ESPUI toolchain: $ESPUI_TOOLCHAIN_ROOT" >&2
export IDF_TOOLS_PATH="$ESPUI_TOOLCHAIN_ROOT/espressif"
export XDG_CACHE_HOME="$ESPUI_TOOLCHAIN_ROOT/cache"
export PIP_CACHE_DIR="$ESPUI_TOOLCHAIN_ROOT/cache/pip"
export IDF_COMPONENT_CACHE_PATH="$ESPUI_TOOLCHAIN_ROOT/cache/components"
if [[ -z "${IDF_PYTHON_ENV_PATH:-}" ]]; then
    for ESPUI_ENV in "$IDF_TOOLS_PATH"/python_env/idf5.5_*_env; do
        if [[ -x "$ESPUI_ENV/bin/python" ]]; then export IDF_PYTHON_ENV_PATH="$ESPUI_ENV"; break; fi
    done
fi
if [[ -z "${IDF_PYTHON_ENV_PATH:-}" || ! -x "$IDF_PYTHON_ENV_PATH/bin/python" ]]; then
    echo "ESP-IDF 5.5 Python environment not found under $IDF_TOOLS_PATH/python_env." >&2
    return 1
fi
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
