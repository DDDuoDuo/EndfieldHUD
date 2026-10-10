This directory contains unmodified QuickJS-NG v0.17.0 runtime sources at commit
6d46d07d04041b40f4f49eaa7fdebe44c314c699. `provenance.json` pins every upstream
file and the official source archive. Preserve LICENSE and source notices.

Compile dtoa.c, libregexp.c, libunicode.c and quickjs.c as a static C11 library.
Use `_GNU_SOURCE`, `__STDC_NO_ATOMICS__=1`, and unsigned char (`/J` on MSVC,
`-funsigned-char` elsewhere). The upstream-supported no-atomics branch omits
unused blocking Atomics wait support. On Windows use WIN32_LEAN_AND_MEAN and
_CRT_SECURE_NO_WARNINGS. Do not link quickjs-libc, the CLI, worker module, or an
OS/event-loop bridge. The adapter supplies a 256KiB VM stack ceiling; it does
not require upstream CLI's process-wide 8MiB stack linker setting.

The application must continue shipping the original Resources/OrbiPom assets;
the runtime loads and SHA256-checks the two original scripts from that directory.
No JavaScript or artwork is duplicated here.
