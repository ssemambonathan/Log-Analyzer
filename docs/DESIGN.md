# HPLA Design

This document explains how the analyzer is put together, why the concurrency is
safe, and where to extend it. It assumes familiarity with C++17/20 basics.

## 1. Goals and constraints

Primary goals:

1. Parse multi-gigabyte logs with bounded memory.
2. Avoid copies and per-record allocations in the hot path.
3. Keep parallel parsing correct for every line, including very long lines.
4. Produce the same statistics in sequential, parallel, and mmap modes.
5. Fail gracefully for malformed input and I/O errors.
6. Keep the sequential engine available as a reference implementation.

Non-goals: compressed input, log rotation, live tailing, or arbitrary custom log
formats at run time.

## 2. Module map

```
             argv
              |
          src/cli  (parse + validate options)
              |
          src/engine  (chunk plan, workers, merge)
           /        \
   src/io            src/filter
 (buffered/mmap)    (record predicate)
           \        /
          src/parser  (string_view fields)
              |
          src/stats   (per-worker Accumulator -> Statistics)
              |
      src/report / src/export
```

`src/core` provides timestamps, IP parsing/formatting, and number formatting
shared by all layers. `src/main.cpp` wires the pieces together and decides where
the output goes.

## 3. Parsing without copies

`LogEntry` holds `std::string_view` fields for the IP, identity, user, request
method/target/protocol, referrer, and user agent, plus parsed integers for the
timestamp and status/size. `parse_log_line` walks the input with a small cursor:

- `token()` scans until whitespace;
- `quoted()` scans until an unescaped `"`, preserving backslash escapes;
- `skip_spaces()` tolerates extra spaces and tabs.

No `std::string` is constructed on the success path. The only allocations in the
whole hot path happen in the accumulator's maps, and only when a new key appears.

**Lifetime contract:** every view points into the caller's line buffer and is
valid only until that buffer is overwritten or the reader advances. The engine
consumes each line immediately: it never stores a `LogEntry` view. Aggregation
copies new keys into owned `std::string`s. Record export writes the record before
reading the next line. Because the parser never retains a view, the contract is
simple to audit.

Errors are values (`ParseError`), not exceptions; malformed input cannot throw
out of the parser. The parser is `noexcept` and is safe on arbitrary bytes
(including UTF-8 and control bytes inside quoted fields, except that CR/LF end a
line before parsing).

Format support is centralized in `parse_log_line`: a new format can be added by
introducing another parser and selecting it in the engine, leaving statistics,
filters, and exporters unchanged.

## 4. Reading files

### 4.1 Buffered reader

`FileLineReader(path, start, end, buffer_size)` reads a byte range of a file in
chunks of `buffer_size` (default 1 MiB). It keeps a `std::vector<char>` buffer,
uses `std::memchr` to find `\n`, returns a `string_view` into the buffer, and
compacts unconsumed bytes to the front before the next read. If a line is longer
than the buffer, the buffer grows; memory is therefore bounded by the longest
line, not by the file size.

The reader stops before starting a line at or beyond `end`, so a chunk never
owns a line that starts in a later chunk. A final line without a trailing newline
is returned normally. CRLF is normalized by removing one trailing `\r`.

### 4.2 Chunk start alignment

A chunk begins at an arbitrary byte offset. If the byte before `start` is not
`\n`, the chunk starts in the middle of a line and the reader advances to the
byte after the next `\n`. A line belongs to the chunk that contains its **first
byte**:

- if offset `start` is inside a line, that line's first byte is before `start`,
  so this chunk skips it and the previous chunk processes it;
- the previous chunk's reader continues until it has read the line whose first
  byte is before its own `end`.

This is the key invariant. It prevents both duplication and loss at boundaries.
CRLF is handled because alignment looks for `\n`; a boundary between `\r` and
`\n` still finds the same newline.

### 4.3 Memory mapping

`MappedFile` maps a whole file read-only (`CreateFileMappingW` +
`MapViewOfFile` on Windows, `mmap` on POSIX). `MappedLineReader` implements the
same range semantics with `memchr` over the mapped bytes. Mapping is optional:
`--mmap` tries every input file; if any cannot be mapped, all files fall back to
the buffered reader. Buffered I/O is always available and is the default.

Trade-offs:

- Mapping avoids copying bytes into a user buffer and lets the OS page cache
  serve pages directly. It also maps the entire file into virtual address space.
- Buffered reads copy data into the reader buffer but keep the address-space
  footprint to the buffer size and work on every platform/filesystem.
- On the test machine both modes are close for warm files; mmap can win slightly
  on large inputs and lose when the file is cold or on network storage.

## 5. Parallel engine

### 5.1 Chunk plan

For each file, the engine creates chunks of `chunk_size` bytes. With automatic
sizing, `chunk_size = max(1 MiB, ceil(file_size / (threads * 8)))`, i.e. about
eight chunks per worker, so workers finish at roughly the same time even if line
lengths vary. Empty files produce no chunks. Chunks are stored in a flat vector
with their file index and byte range.

### 5.2 Workers

`LogAnalyzer::run` creates `active_threads = min(threads, chunk_count, 256)`
`std::jthread`s. Each worker repeatedly claims the next chunk with a relaxed
`std::atomic<size_t>::fetch_add`, opens a reader for that range, and adds every
line to its **own** `Accumulator`. There is no lock on the hot path.

Two details matter:

1. The worker vector calls `join()` explicitly before leaving scope.
   `std::jthread`'s destructor requests a stop and then joins; if the vector were
   simply destroyed, the workers could be cancelled before doing any work.
   Explicit `join()` waits without requesting stop.
2. A worker exception is caught, its message is stored under a mutex, and a
   `failed` atomic tells the other workers to stop claiming chunks. After joining,
   the engine rethrows a `std::runtime_error`, so a broken file is never silently
   reported as an empty result.

### 5.3 Cancellation and shutdown

`install_interrupt_handler` maps SIGINT/Ctrl+C to an atomic flag. `consume`
checks it every 1024 lines and the worker loop checks it between chunks. Workers
finish or stop early, the remaining threads are joined, and the partial statistics
plus a `cancelled` flag are still returned. The process exits with code 130.

### 5.4 Merging and determinism

Each worker's accumulator is merged into a fresh accumulator after all workers
join. Merging adds counters with saturation, merges maps, keeps min/max, and
merges HyperLogLog registers. The final top-N lists are sorted by count
descending, then key ascending, so output does not depend on hash-map iteration
order or timing.

Per-worker distinct-key caps mean that "exact unique IPs" is exact only while no
worker hits its cap. When a cap is hit, the merged result is approximate and the
report says so. This trade-off avoids a global, locked map on every record.

## 6. Filtering

`FilterSet` is immutable during a run. It stores:

- a 600-entry `bool` lookup for statuses/ranges;
- canonical IP byte arrays (IPv4 and IPv6, with `::ffff:a.b.c.d` normalized to
  IPv4 and text compared through `parse_ip`);
- uppercase method strings, compared case-insensitively;
- exact paths, path substrings, and user-agent substrings;
- optional inclusive time and size bounds.

`matches` short-circuits as soon as one option fails. Options are ANDed. Values
within an option are ORed, which matches the documented CLI behavior (`--status
200,301`, repeated `--path`). Filtering happens in `Accumulator::add_line` after
parsing and before any distribution, size, or map work. IP filters parse the
record's address once per record and compare bytes, so `::1` and
`0:0:0:0:0:0:0:1` match. Filters never allocate.

The status lookup table is 600 entries rather than 500 so indexing with the raw
status is simple; the parser has already constrained statuses to 100-599.

## 7. Aggregation details

- Counters are `uint64_t` and use `sat_add`: they stop at `UINT64_MAX` and set a
  `counter_overflow` flag instead of wrapping.
- Response-size statistics only include records with a numeric byte count; `-`
  leaves `sized_records` unchanged.
- Minute buckets are `epoch / 60` with floor division (correct for pre-1970
  timestamps).
- `requests_per_minute` is based on matching records and the observed span
  between the first and last matching timestamp, not on wall-clock processing
  time.
- `HllSketch` uses 4096 registers with a splitmix64-style avalanche after FNV-1a.
  It applies the standard alpha constant and linear counting for small
  cardinalities.

## 8. Reporting and export

`write_text_report` formats an ANSI-styled report when colors are enabled and
sanitizes keys before printing them, so control bytes from a hostile log cannot
inject terminal sequences. Percentages use the relevant denominator (lines,
parsed records, or matched records).

The exporters write straight to a `std::ostream`; they do not build a document in
memory. JSON strings escape quotes, backslashes, standard control escapes, and
other control bytes as `\u00XX`. CSV fields are quoted only when needed and
embedded quotes are doubled. Records exports re-read the input files through the
same buffered reader and parser, applying the same filter, so summaries and
records are consistent without retaining every record.

`src/main.cpp` opens the output file only after analysis, checks stream state
after writing, and refuses an output path that resolves to an input path.

## 9. Error handling

- CLI errors: descriptive message, usage hint for option errors, exit 2.
- Cannot open/stat an input, read failure, worker failure, or output write
  failure: message on stderr, exit 1.
- Malformed log lines: counted, never fatal.
- Ctrl+C: partial report, exit 130.

All resource-owning types are RAII (`MappedFile` releases mappings/handles,
readers own their buffers, `std::jthread` joins). There are no manual `new` or
`delete` calls in the project.

## 10. Tests and verification

`tests/` contains 88 GoogleTest cases:

- timestamp parsing/formatting (offsets, leap years/seconds, pre-epoch dates);
- IPv4/IPv6 parsing, canonical comparison, and formatting;
- CLF/Combined parsing and every error category;
- buffered reader behavior at every byte-offset partition of a corpus, CRLF,
  missing final newline, binary bytes, and long lines;
- filters (AND/OR semantics, canonical IPs, inclusive ranges, size unknowns);
- statistics, merge equivalence, top-N ordering, saturation, HLL limits;
- engine equivalence: sequential vs parallel, mmap vs buffered, multiple files,
  filter application, empty and malformed inputs;
- CLI validation and format inference;
- JSON validity (strict parser in the test), escaping, CSV quoting, and empty
  exports.

`tools/verify_reference.py` independently recomputes core statistics and several
filter counts in Python for a generated log and compares them with the tool's
JSON output.

## 11. Extension points

- **New log format:** add a parser that fills `LogEntry` and expose a format
  option in the CLI/engine. Statistics and exports are format-agnostic.
- **New filter:** add state to `FilterSet`, have `matches` short-circuit, and add
  CLI parsing/description. Filtering already happens before aggregation.
- **New statistic:** add counters to `Accumulator`, merge them, expose them in
  `Statistics`, and print/export them. Keep saturation in mind.
- **Faster scans:** `memchr` already does the heavy lifting for line splitting.
  SIMD delimiter scanning or a custom open-addressing map are possible next
  steps, but must preserve the exact-count and lifetime contracts.
