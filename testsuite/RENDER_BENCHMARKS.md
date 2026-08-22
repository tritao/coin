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
build-bench/bin/CoinRenderGLBenchmarks --rebuild-only 5000 \
  --samples 10 --output rebuild-5000.json
build-bench/bin/CoinRenderGLBenchmarks --mutation-only 50000 \
  --samples 20 --output mutations-50000.json
build-bench/bin/CoinRenderGLBenchmarks --interaction-only 50000 \
  --samples 50 --output interactions-50000.json
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
pan. `M` toggles mutation playback, Space pauses rendering, and `R` forces a
retained rebuild. The viewer prints frame rate once per second. LegacyGL requires a
compatibility-profile build and `--gl-profile compat`; DrawList supports
compatibility and core profiles. On stable scenes, the DrawList path queues a
nonblocking hover pick and polls it on later frames. Mutation playback suspends
hover sampling because each animation update would make an outstanding result
stale.

CTest also registers `CoinRenderWorkloadViewerSmoke`. It uses a hidden core
context to render a small shared-recipe scene, resize its framebuffer, animate
one occurrence, force a retained rebuild, and complete a hover pick. This
checks viewer integration without opening a window or
introducing a timing threshold.

`RenderWorkloadParityTest` renders the expanded, shared-source, and
shared-recipe assembly representations through the same DrawList core context.
It requires visible, equivalent pixel output while independently checking
the mutation handles exposed by each representation. A failure reports the mismatched-pixel count
and maximum channel difference rather than maintaining screenshot baselines.

The viewer and parity test share `GLRenderTestSession`. The session accepts an
arbitrary scene and camera and centralizes context/profile selection,
LegacyGL or DrawList setup, viewport resize propagation, rendering, readback,
and teardown. Workload generation and viewer interaction remain
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
changed frame, and warm repeated-hover latency.

Closest-hit queries read the retained frontmost pick target directly. They do
not perform depth peeling or restore the target afterward; those phases are
reserved for `pickDepthStack()`, where overlapping hits are requested
explicitly. This keeps the common hover path distinct from the more expensive
selection-cycling operation.

The GL benchmark explicitly enables renderer phase timing. JSON schema version
13 identifies each result as `per_frame_traversal`, `steady_state`,
`forced_rebuild`, or `incremental_update`. It separates draw-list construction
into primitive generation, geometry packing, and command emission, and also
reports incremental command updates, render-plan construction, and backend
submission. Command emission includes command state capture and path retention.
Work outside these nested shape phases remains visible as the difference from
total draw-list construction. Backend submission is divided into frame setup,
resource preparation, command execution, and selection overlays. Picking
reports target preparation and rendering, depth rendering and peeling,
readback, hit processing, target restoration, and final scene-result resolution.

The shared-assembly interaction curves distinguish cold hover-target creation,
warm hover queries, and target refresh after an incremental occurrence update.
Refresh results report median and p95 wall, GPU timer-query, and synchronous
readback time; each sample performs a distinct transform update and verifies
one target rebuild.
The asynchronous hover curves report nonblocking request and immediate-poll
cost separately from eventual GPU completion. The manager curve includes dirty
target refresh and scene-result resolution; the backend curve isolates PBO
submission and completion. UI ownership and request coalescing remain with the
caller, while the manager rejects results made stale by a target update.
Pick draw-call and instance counters verify that primitive-mapped occurrences
remain batched instead of silently falling back to one draw per occurrence.
When pick topology and render-plan order remain stable, the backend reuses the
classified submission batches while refreshing their per-instance matrices.
Selection curves render deterministic 1% and 10% selected sets, replace 10% of
the selected occurrences between samples, select deterministic face and edge
ranges, and exercise one preselection. They report logical targets, physical
draw calls, and instanced coverage without moving producer-owned selection
policy into `SoRenderManager`. The subelement curve requires instanced coverage
for nontrivial sets so range selection cannot silently regress to one draw per
target.

The shared-assembly depth-stack curves place every occurrence on one view ray
and request 1, 8, 32, and 128 primitive depth layers. They verify resolved
front-to-back ordering, distinct occurrence identities, zero retained rebuilds,
and instanced peel-pass coverage. Results separate initial depth rendering,
peeling, synchronous readback, hit processing, target restoration, and scene
result resolution. The expensive 32- and 128-layer curves use a bounded sample
count so focused interaction runs remain practical.

`incremental_update_median_ms` isolates retained dependency lookup and command
patching from plan construction and backend submission.

Timing remains disabled for normal `SoRenderManager` users, so clock reads do
not affect ordinary rendering. A zero-valued phase means it did not run; for
example, a warm hover pick normally reuses its existing pick buffer, and a
frame without selected objects performs no selection-overlay work.

Opaque matrix updates and diffuse-color updates preserve render-plan
classification and order, so their plan-construction time remains zero.
Geometry, visibility, and transparent-depth changes conservatively advance the
plan revision and rebuild the derived operation sequence.

GPU-resource revision is tracked separately from dynamic command state. Matrix
and material patches therefore retain validated geometry resources, while
geometry and structural mutations still force resource validation.

The timed render samples represent steady-state frames. The retained manager
reuses its draw list until a scene, camera, layer, viewport-dependent traversal
setting, or explicit `invalidateDrawList()` call invalidates it. The
`drawlist_rebuilds` field makes that distinction visible in benchmark output;
it is normally zero after warmup.

Use `--rebuild-only N` to isolate the feature-rich scene at `N` semantic
draws. The focused run compares LegacyGL's normal per-frame traversal with
steady-state and forced-rebuild DrawList rendering in compatibility and core
profiles. Before each forced-rebuild sample, the benchmark invalidates the
retained draw list; `drawlist_rebuilds` must therefore equal the sample count.
This mode reuses the feature-rich workload rather than maintaining a separate
benchmark scene.

Use `--mutation-only N` to isolate retained update scaling in an `N`-command
scene. The benchmark applies deterministic batches of 1, 10, and 100
translation and diffuse-material edits, plus one geometry edit, through both
DrawList compatibility and core contexts. Every sample must report the exact
number of incrementally updated commands, with no DrawList reconstruction.
Results include median and p95 frame time, the update count, and construction
time so a silent fallback cannot appear to be a valid incremental result. The
normal and smoke benchmark runs include the same curves at their standard
scene sizes.

The same mutation run toggles visibility for batches of 1, 10, 100, and 1,000
occurrences. These curves verify exact incremental command counts and expose
the difference between inexpensive dependency updates and the subsequent
whole-scene plan/submission work. A separate child insertion/removal curve must
perform one full retained rebuild per sample and restore the original pixels
after the edit is removed.

The mutation run also edits one geometry definition in each assembly ownership
model. Expanded geometry updates one face/edge pair; shared-source and
shared-recipe geometry update every face/edge resource owned by the first
definition. These samples exercise transactional multi-resource regeneration,
assert the exact owner count, and compare the final pixels with a forced
rebuild outside the measured interval.

`shared_assembly_depth_stack` moves the shared occurrences onto one overlapping
view ray and measures bounded front-to-back depth peeling. Its counters cover
all rendered peel layers separately from ordinary hover-target construction.
Each layer uses the same primitive-ID instance batches, while depth segments
and irregular mappings remain ordering barriers. The benchmark requires
multiple resolved hits, zero DrawList rebuilds, bounded draw calls per layer,
and nonzero instanced coverage.

The assembly scenes also measure batches of 1, 10, and 100 occurrence
translations. Each occurrence must patch exactly its face and edge command
without rebuilding the DrawList, and every final image is compared with an
explicit rebuild. A forced-rebuild curve for each ownership model provides a
direct baseline for deciding whether further matrix-replay optimization is
worthwhile.

Occurrence-local assembly materials use the same 1, 10, and 100 batch sizes
across expanded, shared-source, and shared-recipe geometry ownership. Each
material must patch exactly its face command, preserve edge state, rebuild the
render plan without rebuilding the DrawList, and match a forced-rebuild image.
A single-occurrence opacity curve crosses the opaque/transparent boundary to
include blend classification and transparent ordering in the same invariants.

Incremental notification batches are classified before retained state changes.
Transform, material, geometry, and disjoint stable-switch batches commit only
after every replacement is available. Mixed, shared-node, structural, and
overlapping-switch changes retain the full-rebuild path; parent-occurrence
classification is cached only for the lifetime of the current DrawList.

The deterministic workloads currently cover traversal/IR construction, render
plan construction (including transparent sorting and depth segments), retained
pick-table construction and resolution, selection churn, and repeated frame
resource rebuilds. GPU submission/completion and LegacyGL/DrawList A/B runs
belong in an optional GL-backed extension so controlled runners can select the
required compatibility or core context explicitly. `CoinRenderGLBenchmarks`
provides that controlled A/B layer; the dependency-free executable remains the
preferred benchmark smoke test on machines without suitable GL contexts.

Longer-term pick-result layering and instrumentation constraints are recorded
in `docs/retained-renderer-roadmap.txt`. Those are evidence-gated API follow-ups,
not requirements for the benchmark suite or the current renderer stack.
