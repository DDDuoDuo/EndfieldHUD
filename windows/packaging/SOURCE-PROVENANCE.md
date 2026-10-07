# Restart source authority

The restart uses GitHub `https://github.com/DDDuoDuo/EndfieldHUD.git`, branch `codex/windows-migration`, and immutable source baseline `4036174a3facf935260f4d0a9c63bfff33b98c37`. The previous clone's `Sources`, `Resources` and `scripts` Git trees matched this commit. The incorrect presentation came from loading the raw game scene without the current desktop adapter; there is no evidence that its assets came from another branch.

`source_provenance.py` refuses another repository argument, a different or ambiguous origin, a wrong branch/detached checkout, or a checkout/fetched branch that does not descend from the baseline. Windows implementation commits may descend from that baseline. Every canonical runtime input must still match both the baseline Git blob and the current committed blob. SHA-1 identifies a Git object; SHA-256 separately records exact source, staged and decoded bytes.

Before the unchanged desktop packer runs, every present `Resources/WatchSource` file is verified against the pinned tree. Missing, untracked, modified, symbolic/reparse or newline-converted source files are rejected. The check covers selection catalogs that are read but omitted from the package. Approved additional assets and ownership notices receive the same exact-byte proof. Configure the disposable clone with `core.autocrlf=false`; a Git-clean CRLF conversion can still violate canonical byte equality.

`Resources/source-provenance.json` records origin/ref, immutable baseline/tree, checkout/fetched commits, source audits, per-input Git blob/size/SHA-256, current Windows selection-policy bytes, and every approved output's source dependencies/encoding/size/SHA-256/decoded size and hash. The editable Windows selection policy has its own exact checkout hash and whether it matches HEAD; it does not replace the canonical asset authority. The manifest contains no account data, machine username or absolute workspace path.

Verification refuses an old/external staging folder, missing provenance, changed source or policy, stale checkout metadata, extra outputs, wrong source bindings and unknown derivations. It reruns the canonical packer's verification under UTF-8 to prove the complete `desktop` selection and generated material-token containers. Native metadata inventory is recomputed from exact source bytes. The ordinary resources inventory hashes the provenance manifest itself.

Twelve approved metadata files include `Scene/desktop-profile-card.json`. Desktop integration must mount that canonical card and merge its components, sprites and material records. The unchanged desktop packer already includes its transitive textures/material bindings. Geometry/animation JSON is never reserialized; native metadata copies preserve original tokens. Native desktop navigation, modules and text must follow the current Swift adapter separately from this byte proof.

Run read-only source auditing or verify a staged folder from this checkout:

```powershell
python -B -X utf8 windows/packaging/source_provenance.py audit
python -B -X utf8 windows/packaging/source_provenance.py verify --resources windows/build/x64/Release/Resources
```

Every restart provenance report says `restart-build-only-unverified`. Exact source provenance is a packaging prerequisite; it establishes no visual, module, data, IME or performance acceptance. The rejected preview remains withdrawn. These tools neither publish a replacement nor choose a version, certificate or paid signing service.
