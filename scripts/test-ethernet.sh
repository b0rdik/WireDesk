#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
TEST_WORK="${BETTERCAST_TEST_WORK:-work/ethernet-tests}"
mkdir -p "$TEST_WORK"
swiftc Sources/BetterCastSender/EthernetProtocol.swift Tests/Ethernet/ProtocolTests.swift -o "$TEST_WORK/protocol-tests"
"$TEST_WORK/protocol-tests"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=1920x1080:rate=120 -frames:v 240 \
    -c:v libx264 -preset ultrafast -tune zerolatency -bf 0 -g 120 -pix_fmt yuv420p \
    -x264-params aud=1:repeat-headers=1 -f h264 "$TEST_WORK/moving-1080p120.h264"
swiftc Sources/BetterCastSender/InputEvent.swift Sources/BetterCastSender/ReceiverVideoDecoder.swift \
    Sources/BetterCastSender/ReceiverVideoRenderer.swift Tests/Ethernet/DecodeTests.swift \
    -framework VideoToolbox -framework AVFoundation -framework AppKit -o "$TEST_WORK/decode-tests"
"$TEST_WORK/decode-tests" "$TEST_WORK/moving-1080p120.h264"
if [ "${BETTERCAST_TEST_TRANSPORT:-0}" = 1 ]; then
    python3 Tests/Ethernet/run_transport.py "$TEST_WORK"
fi
