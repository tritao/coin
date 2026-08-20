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
draw-list construction time and `drawlist_rebuilds: 0`.

The core-profile `feature_rich_rebuild_500`, `_5000`, and `_50000` workloads
invalidate the retained frame before every sample. These scaling points expose
the cost of scene traversal and IR construction separately from transparent
sorting, GL submission, and GPU completion. Full runs cap these larger curves
at ten samples; smoke runs execute only a small rebuild case.
For profiler runs, `--rebuild-only N` skips unrelated workloads and executes
only the core-profile forced-rebuild scene at the requested object count.

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
