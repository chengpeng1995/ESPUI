#!/bin/bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$PROJECT_ROOT/scripts/toolchain.sh"
TOOLS="$IDF_TOOLS_PATH/tools"
CMAKE="$TOOLS/cmake/3.30.2/CMake.app/Contents/bin/cmake"
CTEST="$TOOLS/cmake/3.30.2/CMake.app/Contents/bin/ctest"
NINJA="$TOOLS/ninja/1.12.1/ninja"
PYTHON="$IDF_PYTHON_ENV_PATH/bin/python"
ACTION="${1:-run}"
if [[ $# -gt 0 ]]; then shift; fi
if [[ "$ACTION" == "stop" ]]; then
    exec "$PYTHON" "$PROJECT_ROOT/host/simulator.py" --stop
fi
if [[ "$ACTION" != "run" && "$ACTION" != "build" && "$ACTION" != "test" && "$ACTION" != "capture" && "$ACTION" != "direct" ]]; then
    echo "Usage: $0 {run|build|test|stop|capture|direct} [--mock|--scenario SCENARIO|--capture-dir DIR|--input-script FILE]" >&2
    exit 2
fi
SDL_ROOT="${SDL2_ROOT:-/opt/homebrew/opt/sdl2}"
"$CMAKE" -S "$PROJECT_ROOT/simulator" -B "$PROJECT_ROOT/build-simulator" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_BUILD_TYPE=Debug -DSDL2_DIR="$SDL_ROOT/lib/cmake/SDL2" -DESPUI_TOOLCHAIN_ROOT="$ESPUI_TOOLCHAIN_ROOT"
"$CMAKE" --build "$PROJECT_ROOT/build-simulator" --parallel 8
if [[ "$ACTION" == "test" ]]; then
    exec "$CTEST" --test-dir "$PROJECT_ROOT/build-simulator" --output-on-failure
fi
if [[ "$ACTION" == "build" ]]; then exit 0; fi
if [[ "$ACTION" == "capture" || "$ACTION" == "direct" ]]; then
    cd "$PROJECT_ROOT"
    if [[ "$ACTION" == "capture" ]]; then
        export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
        if [[ $# -eq 0 ]]; then set -- --capture-dir "$PROJECT_ROOT/.runtime/captures"; fi
    fi
    exec "$PROJECT_ROOT/build-simulator/assistant_simulator.app/Contents/MacOS/assistant_simulator" "$@"
fi
exec "$PYTHON" "$PROJECT_ROOT/host/simulator.py" "$@"
