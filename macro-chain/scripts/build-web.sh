#!/bin/bash
# Build the web config bundle into lib/httpd/fsdata.c (needs Node 20).
# Run once, and again whenever anything under www/ changes; the firmware build then
# runs with SKIP_WEBBUILD=TRUE.
set -e

REPO="${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"

export NVM_DIR="$HOME/.nvm"
# shellcheck source=/dev/null
[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh" && nvm use 20 >/dev/null

cd "$REPO/www"
npm ci --no-audit --no-fund
CI=false npm run build

ls -l "$REPO/lib/httpd/fsdata.c"
