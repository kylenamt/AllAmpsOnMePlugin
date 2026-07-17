# Wavetable vs std::sin Benchmark Report

## Benchmark Setup

[benchmark_sweep.cpp](benchmark_sweep.cpp) simulates the real audio loop and sweeps the
oscillator load, comparing the 2048-point linearly-interpolated wavetable (mirrors
`SineWavetable.h`) against `std::sin`.

- **Sweep:** voices `1..8` × partials-per-voice `{2,4,…,1024}`. The x-axis quantity is the
  **total oscillator count = voices × partials** (2 → 8192).
- **Per measurement:** 512-sample block × 100 repetitions, fastest of 3 trials.
- **Access pattern:** flat contiguous phase/delta arrays; each partial is a harmonic of its
  voice's base frequency at 48 kHz, phase accumulated and wrapped per sample.
- **Output:** CSV (`voices,partials,total,method,ms`), plotted by [plot_sweep.py](plot_sweep.py)
  as time vs total oscillators (both axes log).

```sh
g++ -O3 -std=c++17 -o benchmark_sweep benchmark_sweep.cpp && ./benchmark_sweep > sweep.csv
python plot_sweep.py sweep.csv sweep.png -O3
```

> **Pitfall:** the timed result (`acc`) must be consumed (here via a `volatile` sink), or
> `-O2`/`-O3` dead-code-eliminates the `sin` calls and you end up timing an empty loop.

## Results

The wavetable is faster across the **entire** range at both `-O2` and `-O3`
([sweep.png](sweep.png), [sweep_O2.png](sweep_O2.png)). No crossover.

| Total osc. (V×P) | `-O2` speedup | `-O3` speedup |
|---|---|---|
| 1024 (4×256)  | 1.9× | 2.8× |
| 4096 (8×512)  | 2.8× | 4.1× |
| 8192 (8×1024) | 3.6× | 5.2× |

`std::sin` is unchanged between `-O2` and `-O3` (an opaque libm call), while the wavetable's
interpolation arithmetic tightens at `-O3` — so its lead widens with both optimization level
and load.
