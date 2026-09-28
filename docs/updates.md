# Signed updates

EndfieldHUD uses the official **Sparkle 2.9.6** binary framework. It is the compatible release line for Intel macOS 10.15.4 and Apple silicon macOS 11; Sparkle 2.10 requires macOS 12. The version and GitHub release asset's SHA-256 are pinned in `scripts/sparkle-config.sh`. `fetch-sparkle.sh` verifies the download and caches it under ignored `build/dependencies/`. Offline builds work after the dependency is cached.

Release builds link and embed Sparkle, strip its unused sandbox XPC services, and sign the remaining nested helpers before the framework and host application. `verify-bundle.sh` checks their signatures and architectures. `CODE_SIGN_IDENTITY` applies consistently to all of them. The default is still ad-hoc signing: **Ed25519 update signatures do not make the app Developer ID signed or notarized**. Apple signing/notarization remains a separate distribution step.

Only a `HUD_RELEASE` build installed directly in `/Applications` or `~/Applications` may install an update. Copies in Downloads, a source checkout, a temporary folder or a mounted DMG cannot automatically replace themselves. Native `dev.sh` builds may check GitHub version metadata but cannot install themselves over the development copy. Release checks and downloads default to on, with a six-hour interval. The app coordinates installation with its own idle state; see the updater implementation for HUD/session/audio safeguards. No system profile is sent. Sparkle may need macOS authorization if the installation is not writable, and updates cannot install from a read-only DMG.

## Signing key

The private Ed25519 key stays in the macOS login Keychain under the account `io.github.endfieldcharge.EndfieldHUD.updates`. `Resources/Info.plist` contains only its public key. No private key is stored in this repository, build products, command arguments, environment variables, or CI.

On the original signing Mac, `./scripts/setup-update-key.sh` verifies the public-key match. It creates a key only when that Keychain account has no key. On another Mac, restore the **original** signing Keychain/key using a secure process first; this script refuses to replace a different published public key. macOS may ask you to approve access when Sparkle's signing tools first use the Keychain. Never commit a private-key export or put it in a release asset.

Back up the signing Keychain securely. Losing the key can break future updates for already installed copies, especially for ad-hoc builds without a Developer ID key-rotation route. Archive verification is required before extraction, feed signatures are required, and feed-signature failures do not expire into an unsigned fallback.

## Prepare an update locally

1. Increase `CFBundleVersion` (an integer) for every published build. Set `CFBundleShortVersionString` and matching `HUDReleaseTag`, for example `1.0.0`, `8`, and `v1.0.0`. Never replace an already published build or ZIP with different bytes. A `-preview.N` tag generates the `preview` Sparkle channel; a stable tag uses the default channel. Stable clients receive stable entries only; preview clients may receive preview and stable entries.
2. Run the normal tests and `./scripts/build.sh`, then `./scripts/package.sh --skip-build --dmg`. Packaging creates the universal application ZIP, source ZIP, optional DMG and checksum manifest; it does not sign a feed or publish anything.
3. Run `./scripts/prepare-update.sh dist/EndfieldHUD-1.0.0-macOS.zip [release-notes.txt]`. Optional notes are embedded plain text. The script checks archive paths, bundle identity, version, architecture, code signatures, resources and key identity, then runs Sparkle's `generate_appcast` with the Keychain key. It preserves existing feed entries, disables deltas, sets the displayed version to the complete release tag without `v` (including any `-preview.N` suffix), re-signs the feed, and verifies the resulting feed and ZIP with the public key before replacing `updates/appcast.xml`.
4. Review the signed feed and the release files. Editing a signed feed invalidates its signature; regenerate instead. Run `./scripts/test-update-tools.sh` to check the feed and tamper-rejection fixtures without any private-key access.

The feed is intended for:
`https://raw.githubusercontent.com/DDDuoDuo/EndfieldHUD/main/updates/appcast.xml`

The feed contains only releases whose archives have been signed and verified. Publish each archive before its feed entry so clients can always download an advertised version. The project does not contain placeholder release signatures.

## Publish explicitly

Create/publish the matching GitHub release separately, with its version tag and release notes. With GitHub CLI authenticated, run:

```sh
./scripts/publish-update.sh dist/EndfieldHUD-1.0.0-macOS.zip
```

This is an explicit publishing action. It uploads the ZIP to the existing public release if absent, refuses to replace an existing asset, downloads the public asset and checks the exact bytes, then commits the signed appcast to `main` using GitHub's contents API. Existing published feed entries must be preserved. A concurrent change to the remote feed is rejected by its Git SHA. Pull `main` afterward to include that feed commit in your local checkout.

Upload the DMG, source ZIP and checksum manifest separately if wanted for manual downloads. None of the build, package, test or CI scripts publish a release or feed. CI never needs the private key.

## Validation and limits

`test-update-tools.sh` checks Ed25519 verification with an ephemeral in-memory test key, rejection of altered archives/feeds, release metadata and ZIP traversal fixtures, the authentic bootstrap/feed signature, and a tiny native app that loads the embedded framework without starting an updater. These checks do not perform a live installation or exercise macOS authorization prompts. No live upgrade has been performed. A first end-to-end update requires a newly published real release ZIP and its matching signed feed, plus an older installed signed release build; validate that separately before describing installation as tested. Local feed generation may require the user to authenticate Keychain access for Sparkle’s `generate_appcast` tool.

Official references: [Sparkle setup and signed feeds](https://sparkle-project.org/documentation/), [configuration](https://sparkle-project.org/documentation/customization/), [manual code signing](https://sparkle-project.org/documentation/sandboxing/), [2.9.6 release](https://github.com/sparkle-project/Sparkle/releases/tag/2.9.6).
