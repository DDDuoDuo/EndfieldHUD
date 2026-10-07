# Windows rebuild

The previous Windows preview was rejected and removed from this build. This is
a fresh port of the **macOS v1.2.0 build 18 application**, pinned in
[`source-authority.json`](source-authority.json). Current macOS `Sources/` and
`Resources/` define the UI and behavior. The raw game scene alone is not the app.

The current target builds portable scene, motion and data components with isolated
tests. It does **not** yet produce a runnable HUD or a release candidate.

```sh
python windows/tools/check_source_authority.py
cmake -S windows -B build/windows-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows-core --config Release
ctest --test-dir build/windows-core -C Release --output-on-failure
```

Tests use synthetic fixtures and temporary directories. They do not read the
installed Mac app's data, clipboard, account session or windows. Windows uses its
system SQLite library; the portable tests use system SQLite on macOS/Linux.

Completion requires the actual desktop shell, every module and Windows service,
same data contracts, projected editing, current animation behavior, and live
visual/input/performance checks on the test laptop. Passing core tests establishes
none of those broader acceptance results. See [the migration requirements](../WINDOWS-MIGRATION.md).
