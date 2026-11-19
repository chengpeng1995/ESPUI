#!/bin/bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$PROJECT_ROOT/scripts/toolchain.sh"
PYTHON="$IDF_PYTHON_ENV_PATH/bin/python"
if [[ ! -x "$PYTHON" ]]; then
    echo "Project Python environment is missing: $PYTHON" >&2
    exit 1
fi
exec "$PYTHON" "$PROJECT_ROOT/host/quota_bridge.py" "$@"
