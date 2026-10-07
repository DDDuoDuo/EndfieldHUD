# Windows build and package boundary

Authorship remains **DDDuoDuo**. The default build produces a Windows feasibility preview. The chosen consumer distribution is an unsigned portable ZIP, released only when its full acceptance evidence passes. Packaging does not publish a release, change the Mac appcast, or replace Mac v1.2.0 assets.

From the repository root, run:

```powershell
.\windows\packaging\build.ps1 -Python C:\path\to\python.exe
```

Use Windows 11 x64, Python 3.10+, and Visual Studio Build Tools with **Desktop development with C++**, a Windows SDK, and **C++ CMake tools**. The script locates Visual Studio using its installed `vswhere.exe`, prefers that installation's bundled CMake, selects the supported Visual Studio generator, and requires an x64 toolchain. CMake/MSVC/SDK/Python versions are recorded in `windows/build/x64/build-metadata.json`. It does not install a toolchain; CMake downloads the hash-pinned zlib 1.3.2 source archive and builds six decoder files statically. See Microsoft's [CMake project documentation](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio) and [`vswhere` documentation](https://github.com/microsoft/vswhere).

The native target is `windows/build/x64/Release/EndfieldHUDWindows.exe`. Its `Resources` directory is a sibling. `-Configuration Debug` and `-Configuration RelWithDebInfo` select their respective folders. `-CMake`, `-Python`, and `-Git` accept explicit executable paths. `-SkipResources -SkipPackage` permits a compiler-only check while assets are being fetched. `-SkipTests` is recorded as skipped and does not satisfy any release acceptance requirement.

The default build runs native CTest checks and synthetic Python tests, stages assets, and creates a developer preview under `windows/dist/`. ZIP names contain `Windows-x64-preview` and the source commit, rather than an invented consumer version. The package includes `PREVIEW.txt`, MIT/credits notices, complete file hashes, actual installed/download bytes, architecture, signature status, and toolchain metadata. Debug symbols stay outside the package. A matching SHA-256 sidecar and an expanded inventory report remain beside the ZIP. Consumers must not be told that this preview provides module, visual, import, IME, accessibility, or performance parity.

## Runtime resource inventory

`windows/resources/runtime-assets.json` is the explicit additional-asset selection. `stage_resources.py` invokes the **unchanged** `scripts/package-watch-resources.py` twice, first to stage and then to verify its real `desktop` transitive inventory. Selected material JSON tokens, signed identifiers, float words, authored textures/mips, and animation data remain byte-exact. The staged Watch inventory and hashes are retained; no extracted scene/reference directory is copied wholesale.

The strict decoder audits the eight-byte `EHUDZ01\0` magic, little-endian uint64 length, the **1..128 MiB** output limit, raw DEFLATE, exact decoded length, end of stream, and absence of trailing/concatenated data. The Python tests use generated in-memory payloads and temporary directories; they never read real app data, tokens, or account IDs.

Eleven approved scene/material/mesh/controller metadata files are decoded into `Resources/NativeScene` without reserializing JSON. Its separate `native-scene-inventory.json` proves equality to source bytes. The native renderer reads the original packed texture/mip payloads under `Resources/WatchSource`; PNGs remain in the approved inventory for consumers that use them. Textures are not duplicated. The main inventory reports the precise metadata duplication overhead. A native bounded decoder uses static zlib for direct packed-resource loading; the decoded adapter remains available during integration and testing. This adapter supplies no altered timing curves or replacement artwork.

Additional staged assets are approved icon cells, Watch sprites, WorldMap runtime vectors, Media Assembly filter LUTs/stickers, OrbiPom rules/artwork, and their ownership/license manifests. `FactionAtlas.png` is excluded because prepared cells replace it. Documentation GIFs/media, private recordings, raw reference exports, source SDF font atlases, shader extraction intermediates, Mac frameworks/plists, caches, symbols, and user data are excluded.

## Dependencies, sizes, and licenses

| Dependency | Pin and distribution | Package overhead / license |
| --- | --- | --- |
| Win32, D3D11/DXGI, Direct2D/DirectWrite, DirectComposition, WIC | OS supplied; selected SDK and actual OS build must be recorded | 0 copied DLL bytes; Windows terms apply |
| MSVC C++ runtime | `/MT` in Release, compiler/toolset version recorded per build | Static runtime is included in the measured EXE bytes; Microsoft redistribution terms apply |
| Python and CMake | Build/staging tools only; actual versions recorded per build | 0 consumer package bytes; no tool binaries are redistributed |
| Matter.js | Existing `matter-0.20.0.js`, exact SHA-256/bytes recorded in the resource/package inventory | MIT; `Resources/OrbiPom/Matter-LICENSE.txt` is packaged. The Windows game adapter is still unimplemented |
| zlib | **1.3.2**, official source archive SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`; six decoder files compiled statically | No DLL or source archive shipped; decoder is included in measured EXE bytes. Exact 1,002-byte zlib notice is packaged as `Resources/zlib-LICENSE.txt` |
| Original OrbiPom/game scripts, artwork, motion, and Photo Mode assets | Existing source manifests plus exact staged SHA-256/bytes | HYPERGRYPH and respective owners' terms; excluded from the app's MIT grant |
| NOAA ETOPO 2022 / Natural Earth 5.1.1 vectors | Existing WorldMap manifests plus exact staged SHA-256/bytes | CC0/public domain notices in `Resources/CREDITS.md` and manifests |

No third-party native binary, browser runtime, SQLite library, Sparkle framework, MediaRemote helper, or always-running web renderer is added by this prototype. A future SQLite, WebView2, JavaScript-engine, or codec dependency requires a selected version, exact hashes, redistribution license, measured installed/download overhead, and its own capability tests before it is shipped. The native app uses OS libraries, the static MSVC runtime, and the six-file static zlib decoder. Build metadata records current development tool versions; a consumer release still requires a reproducible chosen toolchain/dependency lock.

The asset manifest explicitly pins Matter.js 0.20.0 to **83,318 bytes**, SHA-256 `928d059868201b2c4c270818fda44e5d26c099e4df407043f018f9bbe2c598cf`, with its **1,092-byte** MIT notice pinned to SHA-256 `ed182087be5b26734aa6d4789743de3a97417950e8c1e3ff2e3d19c6462720d3`. Staging refuses an altered library or notice. The development build verified on 2026-10-07 used Visual Studio **17.14.37710.0**, MSVC tools **14.44.35207**, SDK **10.0.26100.0**, bundled CMake **3.31.6-msvc6**, and Python **3.12.14**. Its historical preliminary unsigned, dirty-source preview measured **17.92 MiB download / 29.19 MiB installed**; the final package inventory reports its actual sizes and hash. These are packaging measurements, not Windows runtime performance or consumer support claims.

Keep binary/runtime inputs byte-exact when checking out the repository; automatic LF-to-CRLF conversion can invalidate pinned JavaScript/license bytes. The Windows CI disables that conversion on its disposable runner before checkout. The zlib source archive is a build-only download (about 1.47 MiB); only the compiled decoder and exact license notice ship, and native decoder overhead must be measured as part of the final EXE size.

## Chosen unsigned portable ZIP release

The user chose **unsigned portable ZIP distribution**. It requires no paid signing service, certificate, MSIX registration, or publisher identity. `--format portable` is the default. `Get-AuthenticodeSignature` records the executable's actual observed status in every inventory; a `NotSigned` result stays `NotSigned`. Windows may show an unknown-publisher warning for that executable. No command in this pipeline disables Windows security or changes execution policy.

Prepare a full portable consumer release only after the chosen version, clean source commit, exact executable hash, real target hardware, and every applicable app/data/parity/accessibility/account/performance gate have measured, hashed evidence:

```powershell
python windows/packaging/package.py --release --format portable --version CHOSEN_VERSION --executable windows/build/x64/Release/EndfieldHUDWindows.exe --resources windows/build/x64/Release/Resources --build-metadata windows/build/x64/build-metadata.json --evidence C:\release-evidence\portable-evidence.json
```

Start from `portable-release-evidence.template.json`, whose entries are entirely unverified. The evidence explicitly chooses `distribution: portable`. The format replaces the MSIX-specific `install_update_uninstall_consent_rollback` gate with `portable_manual_update_uninstall_consent_rollback`. All other acceptance gates remain required, including account request signing (which is separate from executable Authenticode). A passed label without its actual measurement artifacts does not satisfy the gate. Current incomplete app gates still refuse a full consumer release; an unsigned developer preview keeps `PREVIEW.txt` and no consumer version.

The resulting `EndfieldHUD-Windows-x64-portable-VERSION.zip` contains only the executable, verified resources and notices, `PORTABLE.txt`, the portable/data snapshot helpers, and its exact package inventory. Adjacent inventory and SHA-256 files record actual download/installed sizes, source/evidence hashes, and observed signature status. Evidence files, user data, keys, recordings, symbols and build tools are excluded. All output remains under the Windows-only `windows/dist` boundary.

Verify the ZIP's SHA-256 against the chosen published Windows release before extracting it. For a manual update, save and quit, extract into a new app folder, preserve the previous app folder, and keep application data outside both executable/resource folders. Returning to the retained previous folder is a manual payload rollback. Restoring old data is a separate explicit choice because a newer app may migrate its data.

The included `portable-update.ps1` automates these steps when the local PowerShell policy permits the script. Its default `Validate` mode checks the chosen ZIP hash, bounded inventory, identity/version, every payload hash, and absence of extra or escaping entries without deployment or app-data access:

```powershell
.\portable-update.ps1 -PackagePath C:\Downloads\EndfieldHUD-Windows-x64-portable-VERSION.zip -ExpectedSHA256 PUBLISHED_SHA256 -ExpectedVersion CHOSEN_VERSION
```

Explicit `Install`, `Update`, `Rollback` and `Uninstall` modes additionally require an app-specific `-InstallRoot`. They prompt for consent and support `-WhatIf`; save and quit before proceeding. Update accepts only a newer chosen version, while downgrade requires `Rollback` and the older ZIP/hash. Payload promotion occurs on the same volume as `-BackupRoot`, preserves the prior version, rechecks the installed inventory, and returns the prior payload if promotion fails. Uninstall moves the payload to the backup directory and leaves original data intact. The helper never starts the app or polls for updates.

`-DataRoot` defaults to `%LOCALAPPDATA%\EndfieldHUD`; `-DataBackupRoot` defaults to the separate `%LOCALAPPDATA%\EndfieldHUD.UpdateBackups`. Select explicit alternate roots if the completed app's verified data layout differs. The helper refuses overlapping roots, drive roots and reparse paths. Before a change, it snapshots data with file hashes. Only explicit `Rollback -RestoreDataSnapshot PATH` restores a matching root/version snapshot after verifying every file and preserving current data separately. Data snapshots and data promotion share their own volume; a portable app may live on another volume. Keep the script and `data-snapshots.ps1` together when running a helper outside its ZIP folder.

Synthetic tests cover complete portable ZIP generation without a certificate, unchanged acceptance gates, install/update/explicit data rollback/uninstall, and rejection of wrong ZIP hashes, altered payloads and extra entries. These tests use temporary synthetic folders and do not establish full app migration, real data compatibility or laptop performance.

## Optional signed MSIX format

`package.py --release --format msix --version VERSION --evidence EVIDENCE.json ...` additionally validates the chosen DDDuoDuo Windows version, clean source commit, exact executable hash, actual Windows Authenticode validation and chosen signing identity, hardware details, every named feasibility/parity/performance/install/update gate, and hashed evidence artifacts. Missing, failed, unverified, changed, or malformed evidence cannot become a passed release claim. The evidence template starts entirely unverified; replacing its labels is not a measurement. These signature requirements apply only to MSIX. The portable format above requires no certificate or signing service.

The consumer path invokes the installed Windows SDK **MakeAppx** with semantic validation and SHA-256 block maps, verifies every payload against the explicit staging inventory, signs the package, verifies it with **SignTool** and Windows trust, and audits the payload again. It creates a signed x64 `.msix`, Windows-only `.appinstaller`, release inventory, and checksum. Original runtime assets stay byte-exact; only installer logo copies are sized to Windows' 44/150/50-pixel requirements from an explicitly chosen, staged source asset. The package uses the full-trust desktop identity `DDDuoDuo.EndfieldHUD.Windows`, keeps symbols/tools/user data outside its payload, and requests no administrator entry point.

Supply these release settings in addition to the executable/resources/evidence paths:

```powershell
python windows/packaging/package.py --release --format msix --version CHOSEN_VERSION --package-version MAJOR.MINOR.PATCH.REVISION --publisher 'EXACT_CERTIFICATE_SUBJECT' --certificate-thumbprint CHOSEN_THUMBPRINT --timestamp-uri https://APPROVED_RFC3161_ENDPOINT --asset-base-uri https://HOST/windows/CHOSEN_VERSION --feed-uri https://HOST/windows/EndfieldHUD-Windows-x64.appinstaller --logo-source Resources/Watch/ui_mid_ring.png --executable windows/build/x64/Release/EndfieldHUDWindows.exe --resources windows/build/x64/Release/Resources --evidence C:\release-evidence\evidence.json
```

The EXE must already be signed and trusted. The MSIX publisher must exactly equal the certificate subject, and its chosen thumbprint must match the EXE's validated signer. The default signing path uses a current-user certificate with its private key; no PFX/password is placed in the repository or command. An explicitly chosen `--signer-script PATH.ps1` can wrap an authorized signing service instead: it receives `PackagePath`, `Publisher`, `CertificateThumbprint`, and `TimestampUri`, must sign that package, and must not alter the payload. Output still undergoes actual SignTool/Windows trust, signer, and payload checks. Signing-service account/key setup remains user-controlled. This optional format is not needed for the selected portable release. See Microsoft's [MakeAppx documentation](https://learn.microsoft.com/en-us/windows/msix/package/create-app-package-with-makeappx-tool) and [package-signing documentation](https://learn.microsoft.com/en-us/windows/msix/package/sign-app-package-using-signtool).

The default App Installer descriptor enables **manual updates with user consent**. Passing `--automatic-updates` explicitly opts into Windows-managed launch checks at most once per 24 hours with its optional prompt; it adds no `AutomaticBackgroundTask` or custom polling service. Windows may apply an opted-in update later when the user elects to launch without updating, so this option is different from per-update manual consent. Shared GitHub `latest` download URLs are rejected because they can select Mac releases; the Windows feed must have a dedicated Windows path/asset. See Microsoft's [App Installer settings](https://learn.microsoft.com/en-us/windows/msix/app-installer/update-settings).

`install-update.ps1` and its sibling `data-snapshots.ps1` support the optional MSIX format. The installer defaults to signature/hash/identity validation without installation. Explicit `Install`, `Update`, `Rollback`, and `Uninstall` modes use PowerShell's high-impact confirmation/`-WhatIf`, require the app to be closed, and snapshot local data outside the app's data directory. Update requires the previous signed MSIX and hash, verifies the installed version/publisher after deployment, and attempts registration rollback on failure. Explicit downgrade requires `Rollback`; a chosen data snapshot must match the package version/root and every file hash before an atomic same-volume restore. Current data is preserved in a separate backup before restoration. The helper never force-stops the app, follows data links, trusts preview identity, launches the app after deployment, or removes original data during uninstall.

The unsigned SDK-validation command is separate:

```powershell
python windows/packaging/msix_release.py --candidate --executable windows/build/x64/Release/EndfieldHUDWindows.exe --resources windows/build/x64/Release/Resources --logo-source Resources/Watch/ui_mid_ring.png
```

Its identity is `DDDuoDuo.EndfieldHUD.Windows.Preview`, version `0.0.0.0`, and filename says `unsigned-candidate`; it is not a consumer release or installer. The target SDK validated an earlier isolated candidate containing the approved assets at **18.39 MiB**; this is a historical measurement, and the current candidate inventory records its actual size. Synthetic tests verified data snapshot restore and tamper rejection. Actual trusted signing, installation/update/uninstall, MSIX data-path behavior, rollback, and packaged desktop capability checks remain **unverified** for this optional format. They do not block the selected portable ZIP; the app acceptance requirements still apply. The Windows CI validates the native build, resource inventory, tests, and unsigned MSIX format only; it does not fabricate signing or publish a release. No bypass of Windows security/signing is part of this pipeline.

Private recordings and account diagnostics may remain local evidence; do not commit or package them. Release evidence should use sanitized measurements. Packaging does not publish, bump release notes, or access real user data. The optional signed MSIX command uses only an explicitly selected existing signing identity; it does not create, export or commit a private key. The selected portable path never invokes signing.
