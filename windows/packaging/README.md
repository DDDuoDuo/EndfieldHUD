# Windows build and package boundary

Authorship remains **DDDuoDuo**. This is the Windows feasibility pipeline; it does not assign a Windows consumer version, publish a release, change the Mac appcast, or replace Mac v1.2.0 assets.

From the repository root, run:

```powershell
.\windows\packaging\build.ps1 -Python C:\path\to\python.exe
```

Use Windows 11 x64, Python 3.10+, and Visual Studio Build Tools with **Desktop development with C++**, a Windows SDK, and **C++ CMake tools**. The script locates Visual Studio using its installed `vswhere.exe`, prefers that installation's bundled CMake, selects the supported Visual Studio generator, and requires an x64 toolchain. CMake/MSVC/SDK/Python versions are recorded in `windows/build/x64/build-metadata.json`. It does not install or download a dependency. See Microsoft's [CMake project documentation](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio) and [`vswhere` documentation](https://github.com/microsoft/vswhere).

The native target is `windows/build/x64/Release/EndfieldHUDWindows.exe`. Its `Resources` directory is a sibling. `-Configuration Debug` and `-Configuration RelWithDebInfo` select their respective folders. `-CMake`, `-Python`, and `-Git` accept explicit executable paths. `-SkipResources -SkipPackage` permits a compiler-only check while assets are being fetched. `-SkipTests` is recorded as skipped and does not satisfy any release acceptance requirement.

The default build runs native CTest checks and synthetic Python tests, stages assets, and creates a developer preview under `windows/dist/`. ZIP names contain `Windows-x64-preview` and the source commit, rather than an invented consumer version. The package includes `PREVIEW.txt`, MIT/credits notices, complete file hashes, actual installed/download bytes, architecture, signature status, and toolchain metadata. Debug symbols stay outside the package. A matching SHA-256 sidecar and an expanded inventory report remain beside the ZIP. Consumers must not be told that this preview provides module, visual, import, IME, accessibility, or performance parity.

## Runtime resource inventory

`windows/resources/runtime-assets.json` is the explicit additional-asset selection. `stage_resources.py` invokes the **unchanged** `scripts/package-watch-resources.py` twice, first to stage and then to verify its real `desktop` transitive inventory. Selected material JSON tokens, signed identifiers, float words, authored textures/mips, and animation data remain byte-exact. The staged Watch inventory and hashes are retained; no extracted scene/reference directory is copied wholesale.

The strict decoder audits the eight-byte `EHUDZ01\0` magic, little-endian uint64 length, the **1..128 MiB** output limit, raw DEFLATE, exact decoded length, end of stream, and absence of trailing/concatenated data. The Python tests use generated in-memory payloads and temporary directories; they never read real app data, tokens, or account IDs.

Until the native runtime owns a raw-DEFLATE decoder, ten approved scene/material/mesh metadata files are decoded into `Resources/NativeScene` without reserializing JSON. Its separate `native-scene-inventory.json` proves equality to source bytes; texture PNGs continue to resolve under `Resources/WatchSource/Scene`, so textures are not duplicated. The main inventory reports the precise metadata duplication overhead. This adapter supplies no altered timing curves or replacement artwork.

Additional staged assets are approved icon cells, Watch sprites, WorldMap runtime vectors, Media Assembly filter LUTs/stickers, OrbiPom rules/artwork, and their ownership/license manifests. `FactionAtlas.png` is excluded because prepared cells replace it. Documentation GIFs/media, private recordings, raw reference exports, source SDF font atlases, shader extraction intermediates, Mac frameworks/plists, caches, symbols, and user data are excluded.

## Dependencies, sizes, and licenses

| Dependency | Pin and distribution | Package overhead / license |
| --- | --- | --- |
| Win32, D3D11/DXGI, Direct2D/DirectWrite, DirectComposition, WIC | OS supplied; selected SDK and actual OS build must be recorded | 0 copied DLL bytes; Windows terms apply |
| MSVC C++ runtime | `/MT` in Release, compiler/toolset version recorded per build | Static runtime is included in the measured EXE bytes; Microsoft redistribution terms apply |
| Python and CMake | Build/staging tools only; actual versions recorded per build | 0 consumer package bytes; no tool binaries are redistributed |
| Matter.js | Existing `matter-0.20.0.js`, exact SHA-256/bytes recorded in the resource/package inventory | MIT; `Resources/OrbiPom/Matter-LICENSE.txt` is packaged. The Windows game adapter is still unimplemented |
| Original OrbiPom/game scripts, artwork, motion, and Photo Mode assets | Existing source manifests plus exact staged SHA-256/bytes | HYPERGRYPH and respective owners' terms; excluded from the app's MIT grant |
| NOAA ETOPO 2022 / Natural Earth 5.1.1 vectors | Existing WorldMap manifests plus exact staged SHA-256/bytes | CC0/public domain notices in `Resources/CREDITS.md` and manifests |

No third-party native binary, browser runtime, SQLite library, Sparkle framework, MediaRemote helper, or always-running web renderer is added by this prototype. A future SQLite, WebView2, JavaScript-engine, codec, installer, or updater dependency requires a selected version, exact hashes, redistribution license, measured installed/download overhead, and its own capability tests before it is shipped. The initial native app only uses OS libraries and the static MSVC runtime. Build metadata records current development tool versions; a consumer release still requires a reproducible chosen toolchain/dependency lock.

The asset manifest explicitly pins Matter.js 0.20.0 to **83,322 bytes**, SHA-256 `914ba8d41aacb2addb490dc2e011cf862e0233edba0eb893b4401733fb883622`, with its **1,112-byte** MIT notice hashed separately. Staging refuses an altered library or notice. The development build verified on 2026-10-07 used Visual Studio **17.14.37710.0**, MSVC tools **14.44.35207**, SDK **10.0.26100.0**, bundled CMake **3.31.6-msvc6**, and Python **3.12.14**. Its preliminary unsigned, dirty-source preview measured **17.92 MiB download / 29.19 MiB installed**; the final package inventory reports its actual sizes and hash. These are packaging measurements, not Windows runtime performance or consumer support claims.

## Consumer release gate

`package.py --release --version VERSION --evidence EVIDENCE.json ...` validates the chosen DDDuoDuo Windows version, clean source commit, exact executable hash, actual Windows Authenticode validation and chosen signing identity, hardware details, every named feasibility/parity/performance/install/update gate, and hashed evidence artifacts. Missing, failed, unverified, changed, or malformed evidence cannot become a passed release claim. The evidence template starts entirely unverified; replacing its labels is not a measurement.

**The command still refuses consumer release even after evidence validation:** a signed Windows consumer installer/updater has not been chosen, implemented, or tested. The current ZIP producer supports developer previews only. Signed MSIX/App Installer versus a small signed installer remains an explicit engineering decision after desktop capability tests. Installation, data preservation, consent, architecture checks, signed updates, safe replacement/rollback and uninstall require real tests. No bypass of Windows security/signing is part of this pipeline.

Private recordings and account diagnostics may remain local evidence; do not commit or package them. Release evidence should use sanitized measurements. Nothing in this pipeline publishes, bumps version/release notes, signs with a private key, or accesses real user data.
