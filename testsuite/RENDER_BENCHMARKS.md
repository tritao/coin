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
End-to-end retained measurements split manager-owned draw-list construction
and render-plan construction from backend command preparation, state setup,
program binding, and draw submission. A cached retained frame reports zero
draw-list construction time and `drawlist_rebuilds: 0`. Unchanged frames also
reuse their render plan; command-content changes invalidate that plan so
ordering-sensitive transform, material, transparency, and geometry edits are
replanned without rebuilding the DrawList.

The core-profile `feature_rich_rebuild_500`, `_5000`, and `_50000` workloads
invalidate the retained frame before every sample. These scaling points expose
the cost of scene traversal and IR construction separately from transparent
sorting, GL submission, and GPU completion. Full runs cap these larger curves
at ten samples; smoke runs execute only a small rebuild case.
For profiler runs, `--rebuild-only N` skips unrelated workloads and executes
only the core-profile forced-rebuild scene at the requested object count.
`--assembly-rebuild-only N` does the same for the shared assembly recipe.
Add `--no-phase-timing` when sampling with an external profiler. This disables
the intrusive per-command clocks so they do not distort the sampled workload;
the aggregate CPU and GPU measurements remain available, while the disabled
phase fields are reported as zero. The top-level `phase_timing` JSON field
records whether those detailed measurements were enabled. For example:

```sh
perf record -e cycles:u --call-graph dwarf -- \
  CoinRenderGLBenchmarks --assembly-rebuild-only 1000 --samples 500 \
  --no-phase-timing
```

Picking is split into one-time cold target creation, target refresh after a
changed frame, and warm repeated-hover latency. Retained runs also report
asynchronous readback submission, time-to-ready, and maximum nonblocking poll
call time. Async readback uses a three-slot reusable pixel-buffer ring. The
assembly interaction benchmark bounds allocations to those three slots so
sustained hover cannot silently return to per-request buffer allocation.
Readback preserves only the framebuffer and pixel-pack state it changes;
render passes continue to use the complete renderer-state guard.
The GL benchmark also records draw calls, actual and skipped program/viewport
changes, actual and skipped frame-matrix and material uniform batches, and
actual and skipped depth/raster/blend/texture state changes. The counters make
driver-call reductions deterministic even when wall-clock timings are noisy.
VAO binds are reported separately because they describe geometry submission,
not semantic pipeline state.
Instanced runs report batches, absorbed commands, avoided draw calls, and
transient instance bytes. Batch-size buckets and the largest batch expose
fragmentation that a single average would hide. Batching is deliberately
limited to adjacent triangle commands with identical geometry, texture,
lighting, depth, blend, and raster state. It supports indexed and non-indexed
opaque geometry plus already-sorted transparent geometry. Lit transparent
batches require identical material state; unlit batches may vary diffuse RGBA
through per-instance data. Incompatible state retains the ordinary command
path.

The `mixed_retained_scene` workload combines repeated and unique indexed
geometry, opaque and transparent groups, depth segments, a 10% selected subset,
cached and refreshed picking, and shared-resource revision churn. It reports
selection and mutation latency separately from ordinary rendering so the next
optimization target is chosen from measured phase costs.
Retained runs additionally enable opt-in per-command CPU phase timing for
command preparation, state setup, program/uniform binding, and draw submission.
Normal rendering leaves this intrusive timing disabled.

The `shared_assembly_{expanded,sources,recipe}` workloads render the same
deterministic collection of reusable part definitions and transformed
occurrences with three ownership models. `expanded` duplicates the generated
geometry, `sources` shares coordinate and normal nodes between distinct shape
nodes, and `recipe` repeats the complete shape recipe. The retained variants
force reconstruction for every measured frame and report both retained command
and geometry-resource counts. This makes resource sharing observable even when
the backend can batch byte-identical geometry in every ownership model.

Each definition contains indexed triangles and a separate indexed edge shape.
Faces and edges have independent material branches and therefore produce two
commands per occurrence. Their natural face/edge interleaving also exposes
batch fragmentation: shared resources do not imply adjacent commands that can
be instanced together. The render planner therefore places ordinary unlit,
opaque, depth-writing surfaces into geometry-resource order before opaque
edges and points. Stages and depth segments remain hard barriers, while
transparent commands retain back-to-front depth order. This turns shared face
definitions into adjacent instance batches without moving ordering-sensitive
commands into the surface pass.

Default-width solid indexed edges use the same per-instance transform, color,
and picking-identity records as surfaces. Wide, patterned, transparent, or
otherwise specialized lines remain on the individual raster path. In the
shared assembly workload this permits both the face pass and edge pass to
collapse to approximately one draw per definition.

Use `--assembly-only N` to run only these three scenes with `N` occurrences.
The normal benchmark uses 500 occurrences and smoke mode uses 24. Each scene
runs through LegacyGL when available and through DrawList compatibility and
core contexts.

Each retained assembly variant also measures three isolated edits:
`placement_1`, `material_1`, and `geometry_definition_1`. Placement and
material edits must update the two geometry commands or the one face command,
respectively, without rebuilding the DrawList. A geometry-definition edit
updates both commands of one occurrence in the expanded baseline and both
commands of every occurrence of the first definition in the shared variants.
The benchmark rejects unexpected rebuilds or dependency counts and reports the
mutation median and p95 separately from forced-rebuild timing.

The shared-recipe variant also exercises interaction without changing scene
structure. `shared_assembly_hover_pick` measures synchronous closest-hit
latency and asynchronous identity submission and completion. The selection
workloads measure stable 1% and 10% selected sets, a changing 10% selection,
and a changing single preselection highlight. Every interaction workload
requires occurrence-specific pick identity, a visible overlay for selection,
zero DrawList rebuilds, stable command counts, and bounded retained geometry
resources.

Selection results report overlay draws, instanced batches and entries, and
separate selected/highlighted entry counts. Compatible whole-command overlays
carry frame-local color and transform in instance records. Subelement targets,
whose ranges map to one triangle or line primitive additionally carry a
per-instance primitive selector; the fragment stage rejects the other
primitives in that instance. Partial multi-primitive ranges, specialized
raster paths, and incompatible coverage retain explicit draws.
The 1%, 10%, churn, and preselection workloads bound overlay draws by target
count while requiring every requested target to be represented.

`shared_assembly_subelement_selection` selects individual faces and edges
across 10% of occurrences. It requires repeated compatible elements to
participate in selection batches, visible coverage for every requested target,
and zero DrawList rebuilds. Singleton geometry/state groups retain one explicit
draw. Selection planning groups compatible targets by retained geometry and
render state, so naturally interleaved face and edge targets do not fragment
batches. Selected and highlighted targets remain separate overlay passes.

The `subelement_selection_{explicit,shared}_{8,64,1000,10000}_targets_40`
curve isolates geometry complexity from scene traversal. Forty commands each
select one face from an indexed mesh of the stated primitive count. A second
`subelement_selection_{explicit,shared}_8_targets_{10,100,1000,10000}` curve
holds geometry complexity fixed while scaling the selected-target count. The
explicit variants use unique geometry and the shared variants expose
primitive-selection instancing.

Shared target-count points also have a `_churn` variant that rotates target
order every sample. Selection plans are rebuilt because selection is
frame-local, but backend-owned scratch vectors and geometry buckets retain
their capacity. Each curve requires capacity growth during its cold sample and
zero growth in subsequent stable or churned samples.

`SoSelectionState::revision` enables opt-in plan reuse. Revision zero always
rebuilds. A nonzero unchanged revision reuses batch membership and primitive
IDs while refreshing colors and reading current command transforms during
submission. Callers increment the revision when target identity, order, or
element type changes. The backend also requires the same DrawList identity,
frame generation, and command-content revision; mutable command or geometry
access therefore invalidates a cached plan even if selection itself is stable.
Cache hits, misses, and reused entries are reported separately. The stable
curves require a warm hit, while `_churn` increments the revision and requires
a miss on every measured frame.

Selection submission also retains the CPU instance-record vector at its
high-water capacity. The curves report record-build and existing GL buffer
upload time separately, along with capacity growth, retained bytes, and bytes
uploaded. Instanced curves require cold growth and zero warm growth; the GL
upload mechanism intentionally remains the portable `GL_STREAM_DRAW` path.

The curves report total CPU and GPU selection time, selection planning time,
planned batches, explicit and instanced entries, candidate count, projected
primitive amplification, rejected batches, scratch-capacity growth events, and
the scratch high-water mark in bytes. This separates grouping and allocation
cost from the submission work they avoid.

Unlike whole-object instancing, the primitive selector redraws the complete
source mesh for every instance and discards non-selected primitives in the
fragment shader. Its amplification budget is `256 + 64 * avoided_draws`, based
on compatibility and core hardware curves. Batches above the budget use
explicit range draws. Crediting avoided draws keeps large selections of small
shared shapes instanced while rejecting expensive full-mesh amplification.

Picking statistics are intentionally reported alongside hover latency. For
contiguous triangle, line, and point subelement ranges, the picking shader uses
the GPU primitive index to resolve the frame-local lookup entry. Repeated
commands can therefore preserve per-face and per-edge identity while using the
same instance groups as visual rendering. Irregular ranges and specialized
raster paths retain the range-by-range fallback. The assembly benchmark
requires every command to join an instanced pick batch and bounds pick draws by
the retained geometry-resource count.

`shared_assembly_depth_stack` moves the shared occurrences onto one overlapping
view ray and measures bounded front-to-back depth peeling. Its counters cover
all rendered peel layers separately from ordinary hover-target construction.
Each layer uses the same primitive-ID instance batches, while depth segments
and irregular mappings remain ordering barriers. The benchmark requires
multiple resolved hits, zero DrawList rebuilds, bounded draw calls per layer,
and nonzero instanced coverage.

The deterministic workloads currently cover traversal/IR construction, render
plan construction (including transparent sorting and depth segments), retained
pick-table construction and resolution, selection churn, and repeated frame
resource rebuilds. GPU submission/completion and LegacyGL/DrawList A/B runs
belong in an optional GL-backed extension so controlled runners can select the
required compatibility or core context explicitly. `CoinRenderGLBenchmarks`
provides that controlled A/B layer; the dependency-free executable remains the
preferred benchmark smoke test on machines without suitable GL contexts.
