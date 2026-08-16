#!/bin/bash
# Build the Pico firmware. Assumes prep-sdk.sh and build-web.sh have run.
# Output: build/GP2040-CE_<version>_<board>.uf2
set -e

REPO="${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$HOME/pico-sdk}"
BOARD="${GP2040_BOARDCONFIG:-Pico}"

cd "$REPO"
[ -f lib/httpd/fsdata.c ] || { echo "lib/httpd/fsdata.c missing - run build-web.sh first" >&2; exit 1; }

arm-none-eabi-gcc --version | head -1

GP2040_BOARDCONFIG="$BOARD" SKIP_WEBBUILD=TRUE \
    cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
GP2040_BOARDCONFIG="$BOARD" \
    cmake --build build --config Release --parallel "$(nproc)"

ls -l build/*.uf2
