#!/usr/bin/env python3
from pathlib import Path
import plistlib, re, sys
root=Path(__file__).resolve().parent.parent
version=(root/"VERSION").read_text().strip()
assert re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+",version), "Invalid VERSION"
assert (root/"Sources/BetterCastReceiverDesktop/VERSION").read_text().strip()==version
info=plistlib.loads((root/"BetterCastSender-Info.plist").read_bytes())
assert info["CFBundleShortVersionString"]==info["CFBundleVersion"]==version
if len(sys.argv)>1: assert sys.argv[1]=="v"+version, "Release tag must match VERSION"
print(version)
