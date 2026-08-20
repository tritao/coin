# Renderer benchmarks

Configure Coin with both test and benchmark targets enabled:

```sh
cmake -S . -B build-bench \
  -DCOIN_BUILD_TESTS=ON \
  -DCOIN_BUILD_BENCHMARKS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench --target CoinRenderBenchmarks
```

The benchmark executable writes a stable JSON schema containing median and
p95 timings, workload sizes, sample counts, and sanity checksums:

```sh
build-bench/bin/CoinRenderBenchmarks --output results.json
build-bench/bin/CoinRenderBenchmarks --samples 50 --output results.json
build-bench/bin/CoinRenderGLBenchmarks --samples 50 --output gl-results.json
```

## Viewing generated workloads

`CoinRenderWorkloadViewer` displays the same deterministic scene graphs used
by the hardware benchmarks. This makes scene construction, camera framing,
transparency, and retained batching behavior inspectable without maintaining
separate visual copies of the workloads.

```sh
build-bench/bin/CoinRenderWorkloadViewer \
  --workload shared_assembly_recipe --objects 10000 \
  --renderer drawlist --gl-profile core

build-bench/bin/CoinRenderWorkloadViewer \
  --workload feature_rich_scene_end_to_end --objects 1000 \
  --renderer legacy --gl-profile compat
```

Available workload names are `many_small_draws`, `many_material_changes`,
`transparent_sorting`, `single_pick_dense_scene`,
`feature_rich_scene_end_to_end`, `shared_assembly_expanded`,
`shared_assembly_sources`, and `shared_assembly_recipe`. Close the window or
press Escape to exit. Use the mouse wheel to zoom, right- or middle-drag to
pan, and left-click a retained hover target to select it. `M` toggles mutation
playback, Space pauses rendering, `R` forces a retained rebuild, and `C` clears
the selection. The viewer prints frame rate, draw count, retained-command
count, and retained-resource count once per second. LegacyGL requires a
compatibility-profile build and `--gl-profile compat`; DrawList supports
compatibility and core profiles. Retained hover and selection visualization
are available on the DrawList path.

CTest also registers `CoinRenderWorkloadViewerSmoke`. It uses a hidden core
context to render a small shared-recipe scene, resize its framebuffer, animate
one occurrence, force a retained rebuild, and complete an asynchronous hover
identity query. This checks viewer integration without opening a window or
introducing a timing threshold.

`RenderWorkloadParityTest` renders the expanded, shared-source, and
shared-recipe assembly representations through the same DrawList core context.
It requires visible, equivalent pixel output while independently checking
mutation handles, retained-command counts, and the resource-count bounds that
distinguish the ownership models. A failure reports the mismatched-pixel count
and maximum channel difference rather than maintaining screenshot baselines.

The viewer and parity test share `GLRenderTestSession`. The session accepts an
arbitrary scene and camera and centralizes context/profile selection,
LegacyGL or DrawList setup, viewport resize propagation, rendering, readback,
statistics, and teardown. Workload generation and viewer interaction remain
separate so other GL integration tests can reuse the session without depending
on benchmark scenes or UI policy.

CTest exposes a tiny non-gating smoke run and a separate 10,000-frame stress
run. Timing results are informational; neither test applies wall-clock pass/fail
thresholds.

```sh
ctest --test-dir build-bench -L benchmark
ctest --test-dir build-bench -L stress
```

When GLFW and OpenGL 3.3 are available, `CoinRenderGLBenchmarks` runs the same
semantic scenes through DrawList compatibility and core contexts. Builds with
`COIN_BUILD_LEGACY_GL_RENDERER=ON` additionally run LegacyGL in a compatibility
context. It reports CPU render-call time, GPU timer-query time, end-to-end GPU
completion time, dense-scene closest-pick latency, and a non-empty-frame pixel checksum. Unsupported profiles are
reported in the JSON `unavailable` array rather than being mistaken for results.
Picking is split into one-time cold target creation, target refresh after a
changed frame, and warm repeated-hover latency. Retained runs also report
asynchronous readback submission, time-to-ready, and maximum nonblocking poll
call time.
The GL benchmark also records draw calls, actual and skipped program/viewport
changes, actual and skipped frame-matrix and material uniform batches, and
actual and skipped depth/raster/blend/texture state changes. The counters make
driver-call reductions deterministic even when wall-clock timings are noisy.
VAO binds are reported separately because they describe geometry submission,
not semantic pipeline state.
Instanced runs report batches, absorbed commands, avoided draw calls, and
transient instance bytes. The initial batching contract is deliberately
limited to adjacent opaque, untextured, non-indexed triangle commands with
identical geometry, material, lighting, depth, and raster state.
Retained runs additionally enable opt-in per-command CPU phase timing for
command preparation, state setup, program/uniform binding, and draw submission.
Normal rendering leaves this intrusive timing disabled.

The deterministic workloads currently cover traversal/IR construction, render
plan construction (including transparent sorting and depth segments), retained
pick-table construction and resolution, selection churn, and repeated frame
resource rebuilds. GPU submission/completion and LegacyGL/DrawList A/B runs
belong in an optional GL-backed extension so controlled runners can select the
required compatibility or core context explicitly. `CoinRenderGLBenchmarks`
provides that controlled A/B layer; the dependency-free executable remains the
preferred benchmark smoke test on machines without suitable GL contexts.
