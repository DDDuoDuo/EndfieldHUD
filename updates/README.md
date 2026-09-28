# Update feed

`appcast.xml` is an Ed25519-signed feed. Do not hand-edit it: its signature covers the XML bytes. The release ZIP must be public and verified before its feed entry is published.

Generate new entries with `scripts/prepare-update.sh`; publish with a separate explicit `scripts/publish-update.sh` invocation after creating the GitHub release. See [the release workflow](../docs/updates.md). The private signing key belongs in the original signing Mac's login Keychain, never here.
