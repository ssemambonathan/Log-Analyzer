# HPLA - High-Performance Log Analyzer

HPLA is a command-line tool that parses Apache/Nginx Common Log Format (CLF) and
Combined Log Format access logs, filters records, computes traffic statistics,
and exports reports as text, JSON, or CSV. It is written in C++20, uses
zero-copy `std::string_view` parsing, buffered or memory-mapped file I/O, and a
line-aligned multithreaded engine. It never loads a whole log file into memory.

```
============================================================
                 HIGH-PERFORMANCE LOG ANALYZER  v1.0.0
============================================================

INPUT
  Files processed:                  1
  Total bytes examined:             175,736,416  (167.60 MiB)
    access.log  (167.60 MiB)

FILTERS
  None - every successfully parsed record is counted.

PROCESSING
  Lines examined:                   1,004,984
  Successfully parsed:              1,000,000  (99.50% of lines)
  Malformed records:                4,984
  Matching records:                 1,000,000  (100.00% of parsed)
...
PERFORMANCE
  Elapsed time:                     1.098 s
  Throughput:                       152.58 MiB/s
  Processing rate:                  914,934 lines/s
  Worker threads:                   4
  I/O mode:                         buffered (parallel chunks)
============================================================
```

The excerpt above is real output from the included generator (1,004,984 lines,
167.60 MiB) on a 4-thread Intel i5-4300U. See
[docs/PERFORMANCE.md](docs/PERFORMANCE.md) for the full measurement matrix.

## Features

- **CLF and Combined Log Format** parsing with no per-field allocations.
- **Streaming buffered I/O**: configurable buffer size, LF and CRLF, a final line
  without a newline, empty files, and lines of arbitrary length.
- **Optional memory-mapped I/O** (`--mmap`) with a buffered fallback.
- **Multithreaded chunk processing**: chunks are aligned to real line
  boundaries; a line is never split between workers. Sequential mode is
  available for comparison (`--sequential`).
- **Filters**: status code/list/range, IPv4/IPv6 address, method, exact path,
  path substring, user-agent substring, inclusive time range, and response-size
  range. Options are ANDed; values repeated within one option are ORed.
- **Statistics**: line/record counts, response sizes, status and method
  distributions, top client IPs and resources, per-minute traffic, status
  classes, and unique client IPs (exact, or HyperLogLog when the configured
  distinct-key limit is reached).
- **Exports**: text report, JSON summary or records, CSV summary or records, with
  correct escaping.
- **88 automated tests** (GoogleTest) and a standalone benchmark/generator.
- No third-party runtime dependencies. GoogleTest is fetched only for tests.

## Requirements

- A C++20 compiler: MSVC 19.30+, GCC 11+, or Clang 14+ (tested with Clang
  23.1.4, LLVM-MinGW UCRT, on Windows 10 x64).
- CMake 3.20 or newer.
- Ninja is recommended but not required.
- The test build downloads GoogleTest v1.17.0 with CMake `FetchContent`, so the
  first configure needs network access. Use `-DHPLA_BUILD_TESTS=OFF` for an
  offline build of just the tool.

`std::jthread`, `std::stop_token`, and `std::string_view` are used directly.
`std::jthread` is available in all three supported compiler families at the
versions above.

## Build

### Windows, Clang / LLVM-MinGW (validated)

```bat
cmake -S . -B build -G Ninja ^
      -DCMAKE_CXX_COMPILER=C:/path/to/llvm-mingw/bin/clang++.exe ^
      -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows, MSVC

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

With a multi-config generator the executables are written to
`build/bin/Release/`.

### Linux / macOS

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Build options:

| Option | Default | Meaning |
| --- | --- | --- |
| `HPLA_BUILD_TESTS` | `ON` | Build the GoogleTest suite |
| `HPLA_BUILD_BENCHMARKS` | `ON` | Build `hpla_bench` and `hpla_genlog` |
| `HPLA_WARNINGS_AS_ERRORS` | `OFF` | Turn warnings into errors |

The project builds with `-Wall -Wextra -Wpedantic -Wshadow -Wconversion
-Wsign-conversion -Wold-style-cast -Wnon-virtual-dtor` (or `/W4 /permissive-`
under MSVC) and also builds cleanly with `HPLA_WARNINGS_AS_ERRORS=ON`.

Executables:

- `build/bin/hpla` - the analyzer.
- `build/bin/hpla_genlog` - deterministic synthetic log generator.
- `build/bin/hpla_bench` - parser and engine benchmark.

## Quick start

```sh
# Generate a 1,000,000-record test log (~168 MiB) with 0.5% malformed lines.
build/bin/hpla_genlog --out access.log --lines 1000000 --seed 42

# Text report on stdout.
build/bin/hpla access.log

# 404s only, top 20, JSON to a file.
build/bin/hpla access.log --status 404 --top 20 --format json -o report.json

# POST requests exported as CSV records.
build/bin/hpla access.log --method POST --format csv --records -o posts.csv

# Time window, path filter, sequential engine.
build/bin/hpla access.log ^
  --from "30/Oct/2026:08:00:00 +0000" ^
  --to   "30/Oct/2026:18:00:00 +0000" ^
  --path-contains /api --sequential
```

`hpla --help` prints the built-in reference.

## Command-line reference

```
hpla [OPTIONS] FILE [FILE ...]
```

### Filters

Different options are combined with **AND**. Values repeated within one option
are combined with **OR**.

| Option | Example | Notes |
| --- | --- | --- |
| `--status LIST` | `--status 200,301,302` | Comma-separated; 100-599; repeatable (appends) |
| `--status-range MIN MAX` | `--status-range 400 499` | Inclusive; `--status-range=400,499` also accepted |
| `--ip LIST` | `--ip 192.168.1.10,2001:db8::1` | IPv4/IPv6; compared as canonical addresses |
| `--method LIST` | `--method POST,get` | Case-insensitive; stored uppercase |
| `--path PATH` | `--path /login` | Exact target; repeatable (OR) |
| `--path-contains TEXT` | `--path-contains api` | Substring of the target; repeatable (OR) |
| `--user-agent TEXT` | `--user-agent Mozilla` | Substring; repeatable (OR) |
| `--from TIMESTAMP` | `--from "30/Oct/2026:08:00:00 +0000"` | Inclusive lower bound |
| `--to TIMESTAMP` | `--to "30/Oct/2026:18:00:00 +0000"` | Inclusive upper bound |
| `--min-size BYTES` | `--min-size 1K` | Requires a byte count; K/M/G are binary |
| `--max-size BYTES` | `--max-size 2M` | Requires a byte count; K/M/G are binary |

Timestamps use the HTTP log format `dd/Mon/yyyy:HH:MM:SS +HHMM`. Offsets are
applied, so comparisons are absolute even across different zones. `--from` later
than `--to`, or `--min-size` greater than `--max-size`, is rejected.

Invalid statuses, IP addresses, methods, timestamps, and sizes are rejected with
a diagnostic and exit code 2. Filters run *before* aggregation, so filtered
records do not consume aggregation work.

### Processing and output

| Option | Default | Meaning |
| --- | --- | --- |
| `-t, --threads N` | hardware concurrency | Worker threads, `0` = automatic, max 256 |
| `--sequential` | off | Single-threaded engine |
| `--mmap` | off | Map files read-only instead of buffered reads |
| `--buffer-size BYTES` | `1 MiB` | Per-reader buffer; 4 KiB to 1 GiB |
| `--top N` | `10` | Entries in top-IP/top-resource lists (1-1000) |
| `--max-distinct N` | `2,000,000` | Distinct IP/path keys per worker; `0` = unlimited |
| `--format text\|json\|csv` | `text` | Output format; inferred from `.json`/`.csv` |
| `-o, --output FILE` | stdout | Write the report to a file |
| `--records` | off | Include matching records (JSON/CSV only) |
| `--color auto\|always\|never` | `auto` | ANSI colors; `NO_COLOR` and `TERM=dumb` honored |
| `-h, --help`, `-v, --version` | | |

The analyzer refuses to write the output over one of its inputs.

## What counts as a record

A line is parsed according to CLF:

```
client_ip ident user [dd/Mon/yyyy:HH:MM:SS +HHMM] "METHOD TARGET PROTOCOL" status bytes
```

and Combined format adds two quoted fields:

```
... "referrer" "user-agent"
```

A request of `"-"` is accepted as an unparsable request with empty
method/target/protocol. Response bytes may be `-`, meaning "not reported".
Parsing fails on empty lines, missing fields, a malformed timestamp, a request
that does not have exactly three tokens, a status outside 100-599, a
non-numeric byte count, unbalanced quotes, or trailing garbage after the record.
Quoted fields honor backslash escapes; escape sequences are preserved verbatim
in the reported values. A file containing a leading header line, for example,
will count that line as malformed. Every malformed line only increments a
counter; it never aborts the run.

## Statistics definitions

- **Lines examined** - every line returned by the reader, including empty and
  malformed lines.
- **Successfully parsed / Malformed** - records accepted or rejected by the
  parser.
- **Matching records** - valid records that pass all active filters. All
  distributions and totals below use matching records only.
- **Total bytes examined** - sum of file sizes, not bytes consumed by filters.
- **Sized records / Total response bytes / Average / Min / Max** - based only on
  records whose response size is a number; `-` records are excluded from all
  four.
- **Status and method distributions** - counts per status code and per known
  method (`GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS`); any other method is
  reported as `OTHER`. Percentages use matching records.
- **Top client IPs / resources** - per-key counts, sorted by count descending
  and then lexicographically for stable output. Empty targets are not counted as
  resources. The lists are truncated to `--top N`.
- **Traffic** - first/last record timestamps (UTC) and their observable span.
  `requests/minute` is `matching records * 60 / (last - first seconds)`. If all
  records share one timestamp it falls back to the matching-record count.
  "Busiest minutes" shows the top five UTC minute buckets.
- **Status classes** - 1xx through 5xx totals.
- **Unique client IPs** - exact count of distinct client-IP strings unless a
  distinct-key limit is hit. With `--max-distinct`, each worker caps its own map;
  when any cap is reached the result switches to a HyperLogLog estimate
  (4096 registers, about 1.6% relative standard error) and the report warns that
  the exact top lists may be incomplete. Set `--max-distinct 0` for exact
  counting with unbounded memory.
- **Counter overflow** - all counters are 64-bit and saturate instead of wrapping;
  saturation is reported as a warning.

## Exports

### JSON

The summary is one object containing `tool`, `version`, `generated_at`,
`input`, `filters`, `processing`, `status_distribution`,
`method_distribution`, `response_sizes`, `top_client_ips`, `top_resources`,
`unique_client_ips`, `traffic`, `status_classes`, and `performance`.
`--records` appends a `records` array with one object per matching record:
`client_ip`, `ident`, `user`, `timestamp_epoch`, `timestamp_iso`, `method`,
`target`, `protocol`, `status`, `bytes` (`null` when absent), `referrer`, and
`user_agent`. Strings are escaped, control characters become `\u00XX`, and
numbers are emitted directly. `test_export.cpp` validates reports with a strict
JSON parser and checks escaping of quotes, backslashes, tabs, and control bytes.

### CSV

The summary is a long-format table with the header `section,metric,value`
(sections include `input`, `processing`, `filter`, `response_size`, `status`,
`method`, `top_ip`, `top_resource`, `unique_client_ips`, `traffic`, and
`performance`). `--records` instead writes the header
`client_ip,ident,user,timestamp_epoch,timestamp_iso,method,target,protocol,status,bytes,referrer,user_agent`
followed by matching records. Fields containing commas, quotes, CR, or LF are
quoted and embedded quotes are doubled, per RFC 4180. Missing byte counts are
written as an empty field.

### Output handling

Reports go to stdout by default; `-o` writes to a file. Inferred formats are
`.json` and `.csv`. File write errors are reported on stderr and exit code 1.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Success |
| `1` | Input/output or processing error |
| `2` | Invalid command line |
| `130` | Cancelled with Ctrl+C (partial report is still produced) |

## Memory and lifetime model

- The parser returns a `LogEntry` whose fields are `std::string_view`s into the
  line buffer. They are valid only until the next read or buffer reuse. Nothing
  in the tool stores those views: aggregation copies keys into owned strings,
  and record export writes each record immediately.
- Per worker, memory is one input buffer (default 1 MiB), one accumulator, and
  bounded maps for distinct IPs, paths, and minute buckets. There is no
  unbounded queue and no whole-file buffer. During the final merge the worker
  maps are combined, so the peak key count can reach up to `workers x
  --max-distinct` entries before the report is printed; lower `--max-distinct`
  if memory is tight.
- `--mmap` maps the whole file into the virtual address space (fine on 64-bit).
  Page faults populate physical memory on demand; the OS can evict pages.
- Peak working set for the 167.60 MiB / 1,004,984-line test log with 50,000
  distinct IPs and 5,000 resources was about **28 MiB** with four workers.

## Testing

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

The 88 tests cover timestamp conversion, IPv4/IPv6 parsing, CLF/Combined parsing
and error cases, line readers at buffer/chunk boundaries, filters and their AND/OR
semantics, statistics and overflow behavior, sequential/parallel/mmap equivalence
across chunk sizes (including CRLF without a final newline and very long lines),
CLI validation, and JSON/CSV validity and escaping.

In addition to the automated suite, the reference implementation output for a
201,035-line generated log was independently recomputed with Python; all core
statistics and ten filter combinations matched exactly (apart from the JSON
report's six-digit display rounding of requests/minute).

## Tools

### `hpla_genlog`

```text
hpla_genlog --out FILE [--lines N] [--malformed PERCENT] [--seed N]
            [--format combined|clf] [--crlf] [--unique-ips N]
            [--unique-paths N] [--start TIMESTAMP] [--no-trailing-newline]
```

Writes a deterministic corpus with realistic status/method distributions and a
configurable percentage of genuinely malformed lines. Useful for tests,
benchmarks, and manual experiments.

### `hpla_bench`

```text
hpla_bench [--lines N] [--threads N] [--repeat N]
```

Measures parser nanoseconds/line, then runs the sequential buffered engine, the
parallel buffered engine, and the parallel mmap engine over the same corpus, and
verifies that all three produce identical counts. Results for this machine are in
[docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Project layout

```
CMakeLists.txt          Build definition and warning flags
.clang-format           LLVM base style, 4-space indent, 100 columns
src/main.cpp            CLI entry point, output routing, Ctrl+C handling
src/core/               Timestamp, IP, and number formatting utilities
src/parser/             LogEntry and the zero-copy CLF/Combined parser
src/io/                 Buffered FileLineReader and optional MappedFile
src/filter/             FilterSet and filter evaluation
src/stats/              Accumulator, statistics builder, HyperLogLog
src/engine/             Chunk planning, worker pool, result aggregation
src/report/             ANSI-aware text report
src/export/             Streaming JSON and CSV exporters
src/cli/                Argument parsing and validation
tests/                  GoogleTest suite
tools/hpla_genlog.cpp   Deterministic log generator
bench/bench.cpp         Parser/engine benchmark
docs/                   Design and performance notes
```

## Documentation

- [docs/DESIGN.md](docs/DESIGN.md) - architecture, chunking proof, concurrency,
  filtering, error handling, and extension points.
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) - measured throughput, memory,
  benchmark method, and tuning trade-offs.

## Limitations

- Only CLF and Combined Log Format are recognized; other formats require a new
  parser implementation behind `parse_log_line`.
- Compressed logs must be decompressed first.
- The analyzer does not validate that the client-IP field is syntactically an IP
  address. This keeps parsing fast; `--ip` filters parse and compare addresses
  canonically and simply do not match records with an invalid address.
- ANSI colors are enabled only for interactive terminals; on Windows the console
  virtual-terminal mode is enabled when possible.
- `--mmap` is all-or-nothing across the input files: if any file cannot be
  mapped, every file is read with the buffered reader.
