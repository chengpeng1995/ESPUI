#!/bin/bash
set -eo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$PROJECT_ROOT/scripts/toolchain.sh"
source "$ESPUI_TOOLCHAIN_ROOT/esp-idf/export.sh" >/dev/null
set -u
cd "$PROJECT_ROOT"
ACTION="${1:-build}"
case "$ACTION" in
    build|reconfigure|menuconfig|size) exec idf.py "$ACTION" ;;
    flash|monitor|flash-monitor)
        "$PROJECT_ROOT/scripts/bridge.sh" --stop
        PORT="${2:-${ESPPORT:-}}"
        if [[ -z "$PORT" ]]; then
            PORT="$(python "$PROJECT_ROOT/scripts/find_port.py")"
        fi
        echo "ESP32 port: $PORT"
        if [[ "$ACTION" == "flash-monitor" ]]; then
            exec idf.py -p "$PORT" -b 460800 flash monitor
        fi
        exec idf.py -p "$PORT" -b 460800 "$ACTION"
        ;;
    *) echo "Usage: $0 {build|flash|monitor|flash-monitor|menuconfig|size} [PORT]" >&2; exit 2 ;;
esac
