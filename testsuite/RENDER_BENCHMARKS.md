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

The deterministic workloads currently cover traversal/IR construction, render
plan construction (including transparent sorting and depth segments), retained
pick-table construction and resolution, selection churn, and repeated frame
resource rebuilds. GPU submission/completion and LegacyGL/DrawList A/B runs
belong in an optional GL-backed extension so controlled runners can select the
required compatibility or core context explicitly. `CoinRenderGLBenchmarks`
provides that controlled A/B layer; the dependency-free executable remains the
preferred benchmark smoke test on machines without suitable GL contexts.
