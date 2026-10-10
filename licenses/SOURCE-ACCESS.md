# Corresponding sources and dependency notices

WireDesk source and its build/install scripts: https://github.com/b0rdik/WireDesk (each release also has GitHub source archives). GPL-3.0, derived from BetterCast by Stephen Lovino. See bundled LICENSE and ACKNOWLEDGMENTS.md.

Qt Windows binaries are unmodified Qt6.7.0 SDK DLLs. Complete source: https://download.qt.io/archive/qt/6.7/6.7.0/single/qt-everywhere-src-6.7.0.tar.xz . Qt notices/license texts are bundled under Qt/. These shared libraries may be replaced/relinked by the user; the installation directory is user-writable.

FFmpeg, x264 and their vcpkg dependencies: the shipped versions.txt, vcpkg-status.txt and build-source.txt identify exact package versions, enabled features and the vcpkg commit. The corresponding ports provide exact upstream source URLs/checksums, any patches and complete build scripts. Source servers are public and accessible without a paid account. Rebuild using the documented vcpkg toolchain; GPL/x264 is enabled, nonfree features are not enabled. Dependency copyright texts are bundled alongside these records.

WinSparkle0.9.4: https://github.com/vslavik/winsparkle/tree/v0.9.4 . Sparkle2.10.0: https://github.com/sparkle-project/Sparkle/tree/2.10.0 . Both ship license notices; Sparkle is bundled in the Mac framework, WinSparkle in the Windows runtime.

Report a broken source-access link using the repository issue tracker. Sources and build scripts must remain available when redistributing these binaries; preserve this notice, licenses and upstream authorship.
