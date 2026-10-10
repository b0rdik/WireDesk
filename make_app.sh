#!/bin/bash
# Compatibility entry point for the renamed Mac application.
set -euo pipefail
cd "$(dirname "$0")"
export WIREDESK_UNIVERSAL="${WIREDESK_UNIVERSAL:-1}"
exec scripts/build-wiredesk-mac.sh "${1:-dist/WireDesk.app}"
