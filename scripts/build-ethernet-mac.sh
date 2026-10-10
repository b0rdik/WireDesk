#!/bin/bash
# Compatibility entry point. The renamed product is WireDesk.
set -euo pipefail
exec "$(dirname "$0")/build-wiredesk-mac.sh" "${1:-outputs/WireDesk.app}"
