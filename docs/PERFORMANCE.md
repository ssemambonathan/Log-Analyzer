# Performance Notes

All numbers below were measured on this machine with the included tools. They are
representative of this hardware only; absolute throughput depends heavily on CPU,
storage, page-cache state, and line length.

## Test system

| Component | Value |
| --- | --- |
| CPU | Intel Core i5-4300U, 2 cores / 4 logical threads, 1.9-2.5 GHz |
| OS | Windows 10 x64 (build 19045) |
| Compiler | Clang 23.1.4 (LLVM-MinGW UCRT), `-O3 -DNDEBUG` |
| Build | CMake 4.4.4, Ninja 1.13.2 |
| Input | `hpla_genlog`, Combined format, 175,736,416 bytes (167.60 MiB) |
| Records | 1,004,984 lines: 1,000,000 valid, 4,984 malformed |
| Cardinality | 50,000 distinct client IPs, 5,000 distinct resources |
| Cache state | Files written and then read repeatedly, so page cache is warm |

The machine is a 2013 ultra-low-power dual-core, so per-core throughput is far
below a current desktop CPU. The scaling behavior and the relative cost of the
modes are the useful takeaways.

## End-to-end CLI results

Each mode was run three times on the 167.60 MiB log; the table reports the median.
`Total` is wall-clock time from process start through report generation, with
the default 1 MiB per-reader buffer and exact unique-IP counting.

| Mode | Time | Throughput | Lines/s | Threads | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| `--sequential` | 2.171 s | 77.19 MiB/s | 462,895 | 1 | 1.00x |
| `--threads 2` | 1.381 s | 121.33 MiB/s | 727,529 | 2 | 1.57x |
| `--threads 4` | 1.098 s | 152.58 MiB/s | 914,934 | 4 | 1.98x |
| `--mmap --threads 4` | 1.068 s | 156.92 MiB/s | 940,970 | 4 | 2.03x |

Run-to-run variance on this laptop is noticeable (thermal throttling and
background work): the best observed `--threads 4` run was **0.845 s,
198.32 MiB/s, 1,189,200 lines/s** (2.57x over the median sequential run), and the
slowest was 1.279 s. Treat single runs as noisy and use medians or best-of-N.

`--threads 1` (single worker, parallel engine) is within noise of `--sequential`;
the sequential path exists mainly for debugging and as a correctness reference.

## Component benchmark (`hpla_bench`)

`hpla_bench` builds a 1,000,000-line / 135.57 MiB Combined-format corpus in
memory, benchmarks the parser alone, writes the corpus to a temporary file, and
runs all engine modes over it. Values are best of three.

```text
parser:                    660.3 ns/line, 1.51 M lines/s
sequential buffered:       78.98 MiB/s, 582,605 lines/s, 1.716 s
parallel buffered (4):    228.12 MiB/s, 1,682,687 lines/s, 0.594 s, 2.89x
parallel mmap (4):        226.72 MiB/s, 1,672,316 lines/s, 0.598 s
consistency check:         OK
```

The parser microbenchmark is CPU-only and cache-resident-ish, so it is the upper
bound for parsing on one core. The engine adds line reading, filtering checks,
and per-record aggregation (IP/path/minute hash-map updates plus HyperLogLog
hashing), which roughly halves single-core end-to-end throughput.

The parallel engine reaches about 2.9x on two physical cores with SMT in the
benchmark, and about 2.0x in the CLI matrix. Four logical CPUs do not give a 4x
speedup because hyperthreads share execution resources, and memory/cache traffic
becomes a limit.

## Memory

Peak working set for the CLI run above (`hpla` with four workers, exact counting,
50,000 distinct IPs) was approximately **28.2 MiB**, sampled every 20 ms with
PowerShell.

Breakdown:

- 4 workers x 1 MiB input buffers = 4 MiB (the default `--buffer-size`);
- per-worker accumulators with bounded unordered maps for IPs, resources, and
  minute buckets (the dominant cost with high cardinality);
- 4 x 4 KiB HyperLogLog sketches (one per worker), merged at the end;
- top-N lists and the report itself (small).

The final merge combines the per-worker key maps, so peak key memory can be up
to `workers x --max-distinct` entries even though each worker is individually
bounded. This is why `--max-distinct` is exposed: lower it for adversarial or
very high-cardinality logs, and accept approximate unique counts when the cap is
hit.

Memory does not grow with file size. It grows with the number of distinct keys
the maps retain and with the longest line (the reader grows its buffer for lines
longer than `--buffer-size`). `--max-distinct` caps the key maps per worker; when
the cap is hit, exact unique-IP counting switches to HyperLogLog (about 1.6%
relative standard error) and top lists may miss rare keys, both reported.

## Why not always faster?

- **Branchy byte scanning.** The parser validates every field and timestamp with
  character-level checks. That is fast and allocation-free, but on this old CPU it
  costs roughly 1,300 cycles per 140-byte record. SIMD delimiter scanning or a
  specialized fixed-layout parser could reduce this, at the cost of complexity
  and portability.
- **Aggregation maps.** Exact top-IP/top-resource counting requires a hash lookup
  (and occasional insertion) per record, plus a minute-bucket lookup and a
  HyperLogLog update. This is the main single-core overhead after parsing.
- **Physical cores.** Two cores cap scaling. SMT adds some throughput but not
  linear scaling.
- **Page cache.** First reads from disk are slower; repeated runs benefit from the
  OS cache.

## Tuning

| Setting | Effect |
| --- | --- |
| `-t/--threads N` | Default = hardware concurrency. Try 2-8; beyond physical cores gains shrink. |
| `--buffer-size` | 1 MiB default. Larger buffers reduce read calls for buffered I/O but cost `threads x size` memory. Very small buffers increase syscall overhead. |
| `--mmap` | Avoids copying into user buffers; performance was near parity here, slightly better on the larger file. May be worse on cold/network storage. Fails over to buffered I/O if any file cannot be mapped. |
| `--max-distinct N` | Lower values bound memory for adversarial/high-cardinality logs but enable approximate unique-IP counting and incomplete top lists. `0` means unlimited and exact. |
| `--top N` | Only affects final sorting/printing, not the hot loop. |
| `--sequential` | Slower, but useful to compare results or when debugging worker behavior. |

Filters run before aggregation, so a selective filter (for example
`--status 500` on a mostly-200 log) reduces map work for non-matching records and
can improve throughput noticeably. IP filters add one `parse_ip` per record.

## Reproducing

```sh
# Build (see README for toolchain setup)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 1M-line, ~168 MiB log with 0.5% malformed lines
build/bin/hpla_genlog --out build/data/access.log --lines 1000000 \
    --unique-ips 50000 --unique-paths 5000 --seed 42

# CLI matrix (run each three times; compare the PERFORMANCE section)
build/bin/hpla build/data/access.log --sequential
build/bin/hpla build/data/access.log --threads 2
build/bin/hpla build/data/access.log --threads 4
build/bin/hpla build/data/access.log --mmap --threads 4

# Component benchmark
build/bin/hpla_bench --lines 1000000 --threads 4 --repeat 3

# Independent correctness check of statistics and filters
python tools/verify_reference.py build/data/access.log build/bin/hpla
```

On first use, `build/bin/hpla_genlog` and the test executables may need the
compiler runtime directory on `PATH` (for LLVM-MinGW builds, its `bin` directory
contains the UCRT import libraries' runtime dependencies). System-installed
MSVC/GCC builds do not have this requirement.

## Possible future optimizations

These are not implemented; they are directions that would need to preserve the
current correctness and lifetime guarantees:

- SIMD (SSE2/AVX2/NEON) scanning for quotes, spaces, and brackets, with scalar
  fallbacks.
- A purpose-built open-addressing map or interning arena for IP/resource keys.
- Parsing timestamps with a precomputed table instead of per-field branches.
- Batching HyperLogLog updates or making it optional even when exact counting is
  possible.
- Larger read chunks with explicit prefetching, or platform-specific overlapped
  I/O / `io_uring`.
- Profile-guided optimization and LTO for release builds.
