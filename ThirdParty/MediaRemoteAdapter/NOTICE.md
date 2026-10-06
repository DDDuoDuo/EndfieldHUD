# MediaRemote Adapter

Source: https://github.com/ungive/mediaremote-adapter
Pinned commit: 29718252613a5b0e210bdc64de0bd944ab379706
License: BSD-3-Clause; see LICENSE (also bundled with EndfieldHUD).

The source and launcher are vendored for reproducible offline builds. Only the
small adapter framework and Perl launcher ship; no upstream test application,
build tools, precompiled downloads or package manager is required at runtime.
The helper exists only while the Now Playing module is active.

Local compatibility patch: retain the image UTI before releasing its ImageIO
source, and guard UniformTypeIdentifiers with macOS 11 availability while keeping
the CoreServices fallback on Catalina. Built with weak UniformTypeIdentifiers
linkage; the HUD's original 10.15.4/11 minimum versions are unchanged.
