#include <benchmark/benchmark.h>

// Placeholder benchmark — replace with real benchmarks as modules are implemented.

static void bm_placeholder(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(42);
    }
}

BENCHMARK(bm_placeholder);
