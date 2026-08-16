#!/bin/bash
# One-time host setup: pico-sdk 2.1.1 with the submodules this project actually needs,
# plus Node 20 via nvm for the web config bundle.
#
# ARM toolchain (needs root, install separately):
#   sudo apt install -y gcc-arm-none-eabi binutils-arm-none-eabi \
#        libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
#        build-essential ninja-build pkg-config
#
# Behind a proxy (e.g. WSL reaching a Windows-side proxy), export first:
#   export https_proxy=http://$(ip route show default | awk '{print $3}'):7890
set -e

SDK_DIR="${PICO_SDK_PATH:-$HOME/pico-sdk}"
SDK_TAG="${SDK_TAG:-2.1.1}"

echo "=== pico-sdk $SDK_TAG -> $SDK_DIR ==="
if [ ! -d "$SDK_DIR/.git" ]; then
    git clone --depth 1 --branch "$SDK_TAG" https://github.com/raspberrypi/pico-sdk.git "$SDK_DIR"
fi

# Only these three are required for a board=pico build:
#   lwip     - web config server
#   tinyusb  - lib/lwip-port and lib/rndis reference it by SDK path
#   mbedtls  - pico_mbedtls -> pico_rand -> pico_unique_id (pico/unique_id.h)
for sub in lib/lwip lib/tinyusb lib/mbedtls; do
    echo "--- submodule $sub ---"
    git -C "$SDK_DIR" submodule update --init --depth 1 "$sub"
done

echo "=== node 20 via nvm ==="
export NVM_DIR="$HOME/.nvm"
if [ ! -s "$NVM_DIR/nvm.sh" ]; then
    curl -fsSL https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash
fi
. "$NVM_DIR/nvm.sh"
nvm install 20
node -v && npm -v

echo "=== toolchain check ==="
for t in cmake ninja arm-none-eabi-gcc arm-none-eabi-g++ python3; do
    printf '%-22s %s\n' "$t" "$(command -v "$t" || echo MISSING)"
done
