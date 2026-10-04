# Performance investigation — 2026-10-03

This pass targets CPU use, retained memory and opening time without changing the
released interface, motion curves, frame cadence, render resolution or effects.
It does not change saved-data formats or feature behavior. The earlier
[integration measurements](integration-preservation.md#performance-comparison)
remain historical results, not measurements of this patch.

## Method

Comparisons use optimized native arm64 builds on an M2 MacBook Air with 8 GB RAM,
macOS 15.7.4, SDK 15.5, and a 1470 × 956-point display at 2× backing scale.
The released integration baseline is `fb88699`; the stable native-shell baseline
is `a8770680044c4f7b664c6c8adecc0c02bed02a2c`. All runs use the same isolated
`HUDIntegrationPerformance` workload and the packaged resources for their build.
No real notes, profile images, clipboard contents or application preferences are
used. Compilers, other graphical fixtures and sampling tools do not run during
the timed comparisons.

CPU percentages describe the app process as a percentage of **one core**.
Physical footprint includes memory that RSS alone misses, including compressed
and graphics allocations. It is still a process measurement, not the total cost
of the app plus WindowServer. Submitted source frames are not measured display
FPS. Instruments is unavailable on this host; separate `sample` and `vmmap`
sessions identify hot paths and memory categories. Their perturbed timing is
excluded from comparisons.

The first baseline run encountered cold driver shader compilation. Repeated
baseline/candidate comparisons with a warm driver cache are required for opening
claims; a shader-cache improvement caused by running the app twice is not a code
optimization. The fixture measures opening calls and first GPU presentation,
not physical keyboard-event latency.

## Findings and changes

- **Repeated geometry uploads:** regenerated vertices also allocated identical
  index buffers while an earlier frame was in flight. Exact index equality now
  reuses immutable buffers; changed topology retains the original in-flight
  safety rules.
- **Repeated draw preparation:** adjacent batches rebuilt merge partitions,
  member lists and structural dictionary comparisons every frame. A bounded
  retained plan now reuses them. Explicit producer tokens identify structurally
  unchanged ambient frames; dynamic backdrops, changed geometry, material policy
  and incompatible world transforms still invalidate the relevant work.
- **Repeated uniform comparisons:** encoder-local cell identities and payload
  revisions avoid comparing identical byte buffers repeatedly. Shader stage and
  binding slot remain separate, and camera/time changes still update payloads.
- **Repeated layout work:** immutable graph order, ancestor/mask relationships,
  local image geometry and slant configuration are retained. Layout sizing is
  separated from material/color changes. Canvas inverses and shared clip regions
  are calculated once per frame. CanvasGroup alpha inheritance is reused only
  while all exact scalar inputs match, including inactive groups.
- **Repeated work during tilt:** indexed scene resolution avoids rebuilding
  unchanged transforms. Plain image nodes reuse their Canvas-local geometry and
  draw data when their exact inputs match, while updating world transforms and
  hit masks. Text, meshes, soft masks and ambient nodes retain their full paths.
  Forced rebuilds remain an independent oracle. An early dictionary-based cache
  copied too much data; profiling caught this and it was replaced before the
  final measurements.
- **Hidden fallback artwork:** the source shell previously constructed the
  unused legacy mechanical layer tree and its bitmap variants. It now constructs
  that artwork only if the renderer needs the native fallback.
- **Metadata duplication:** raw material catalogs duplicated values already
  represented by typed material inputs. The retained raw projection preserves
  identity, shader and pass-state fields used by remaining consumers; an exact
  fresh-parse oracle checks every typed input. Pipeline caches retain derived
  uniform sizes instead of the full reflection object, while preserving ABI
  validation and compiled programs for reopening.
- **JSON loading:** testing common string and numeric values before booleans
  avoids repeated failed decoder probes. The same decoder preserves exact values
  and types; 347,938 catalog values and numeric/string edge cases were compared
  with the original order. The isolated five-run median improved by 25.7%; this
  is decoder time, not overall startup time.
- **Portrait retention:** cached crops previously kept full-resolution originals
  alive. Weak identity-checked source references now allow replaced originals to
  be released while preserving the bounded crop cache and its rendered pixels.
- **Closed memory:** presentation surfaces are released on close. A single
  cancellable utility task returns unused allocator pages after closing, without
  evicting live caches or changing reopening. It is cancelled on reopening and
  skipped during quit/update teardown; no recurring cleanup timer is introduced.
- **Small idle work:** settled finite-button demand checks are memoized until
  state changes. Unchanged accessibility geometry skips redundant projection
  and accessibility updates.

The packaged Watch resource closure was already trimmed: the verifier selects
572 files, reducing the 354 MiB authoring tree to 11.65 MiB in the app, with every
retained source payload verified. Reference assets and fallback code remain
needed for reconstruction, verification or failure recovery; they are not
deleted solely because the normal desktop path does not use them.
After verification, 3.09 GiB of this investigation's disposable compiler caches,
superseded benchmark builds and raw frame dumps were removed. The final native
and benchmark apps, source snapshots, measurements, logs and pixel hashes remain
available; released bundles and authoring/reference assets were preserved.

## Measured result

[Raw runs, source/binary fingerprints and pixel hashes](performance-parity-benchmark.json)
identify the tested implementation. Ranges below are two runs per integrated
build with a warm driver cache. They are observations, not confidence intervals.

| Measurement | Released integration | This pass |
| --- | ---: | ---: |
| Source preparation before opening | 1.27–1.58 s | 1.06–1.07 s |
| First synchronous opening call | 184–194 ms | 145–174 ms |
| First GPU presentation after opening | 398–431 ms | 307–365 ms |
| Warm synchronous reopening | 119–137 ms | 117–126 ms |
| Continuous tilt CPU, one core | 41.1–42.3% | 36.9–37.0% |
| Open idle CPU across four sections | 7.9–11.7% | 8.3–10.4% |
| Continuous tilt physical footprint | 183–197 MiB | 176–181 MiB |
| Closed physical footprint | 115–130 MiB | 97–98 MiB |
| Closed CPU, one core | 0.04–0.05% | 0.07% |

The two-run means show about 11% lower tilt CPU, 19% faster first GPU presentation
and 20% less closed memory. Warm reopening and idle CPU remain noisy; this pass
does not establish a reliable idle CPU improvement. Closing stops the source
clock and frame submissions. The one-shot allocator task took 14–15 ms on a
utility queue, outside the steady closed measurement interval.

**Old-version performance parity has not been reached.** The freshly built old
native shell measured 0.14–0.26% idle CPU, 5.17% during tilt, 44 MiB closed and
64–70 MiB open. Its synchronous opening/reopening took 103/61 ms. The integrated
renderer still updates ambient geometry and submits multipass Metal drawing
continuously. Idle frames already avoid full layout reconstruction; more generic
caching is unlikely to close this gap. Reusing GPU draw commands is a potential
next renderer investigation, requiring the same visual verification. Lowering
resolution, reducing animation cadence or changing effects was not used here.

## Verification

- 66 baseline/current GPU frames matched byte for byte, including batch states,
  used textures and the combined-depth fallback. The baseline used the entire
  released renderer/layout source closure. Allocation and dependency-invalidation
  checks exercised the new buffer, merge-plan and submission-token paths.
- Cached versus forced full builds matched across 43 poses, plus inherited-alpha,
  clip, image-output, input-mask and invalidation cases.
- 50,486 core assertions passed, including JSON equivalence, incremental scene
  dependencies, portrait lifetime and unchanged portrait pixels.
- 623 navigation assertions covered all 16 sections; 110 lifecycle assertions
  covered pointer tracking, cursor ownership, closing/reopening, quit/update
  ordering and cleanup cancellation. The 26 backdrop preparation checks and
  forced source-failure/native-fallback fixture also passed.
- The stable compatibility guard and 19 mutation checks passed: all 51 guarded
  functional/localization files, 575 translated entries, saved-data paths and
  bundle/update identities retain their existing contract.
- The offline Metal compiler probe could not run because that toolchain is not
  installed. Runtime Metal compilation and the full pixel comparison succeeded.
- A live-process `leaks` scan after closing reported **0 leaks / 0 leaked bytes**
  across 242,502 allocated nodes. An earlier at-exit-instrumented lifecycle run
  tripped a timing-sensitive cursor frame-count check and produced no leak
  report; the clean lifecycle run passed. Instrumented timings are excluded.

## Reproduction

Build and run isolated optimized fixtures with `scripts/benchmark-hud.sh`.
Use `HUD_BENCHMARK_DIR` for separate before/after outputs and
`HUD_BENCHMARK_RESOURCE_APP` to select the corresponding packaged app. Run
`--verify-only` separately from timing to compare cached versus full geometry,
draw state, masks and pointer hit regions.
Use each revision's matching benchmark fixture; new verification hooks are not
present in the old renderer. The optional `--relieve-closed-heap` diagnostic
predates the production cleanup and must not be used for final timings, since
it would run an additional allocator sweep.

`scripts/verify-desktop-renderer-parity.py` accepts
`--baseline-source-directory` to freeze and compare the entire prior source
closure, rather than only the Metal renderer. `--optimization geometry` enables
allocation/invalidation checks. It compares raw GPU pixels, batch state and
used texture identities; source hashes identify both inputs. `--jobs 1` limits
the fixture to one compiler when memory is constrained.

`scripts/test-source-metadata-cache.sh` checks retained values against fresh
parsing without creating a GPU device or window. The native lifecycle fixture
also tests on-demand fallback construction and cleanup.
