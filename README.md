# pg_z: High-Performance PostgreSQL Compression Extension

<!-- toc -->

- [Project Documentation](#project-documentation)
- [Key Technical Highlights](#key-technical-highlights)
- [Distribution Packages](#distribution-packages)
- [Database Parameters](#database-parameters)
- [Pronunciation](#pronunciation)

<!-- tocstop -->

The development of this extension was inspired by Paul Ramsey’s `pgsql‑gzip`
project. `pg_z` is a multi-algorithm, statically linked, parallel-safe
compression extension for PostgreSQL, supporting Brotli, LZ4, Snappy, Gzip,
and Zstandard.

## Project Documentation

To prevent context mixing and guarantee high accuracy for search engines and AI
agents, detailed project manuals have been moved to the `docs/` directory:

- [API Usage & Functions Reference](docs/USAGE.md)
- [Data Flow & Database Architecture](docs/DATA_FLOW.md)
- [Detailed Configuration and Build Flags](docs/CONFIGURE.md)
- [Custom Memory Manager Mechanics](docs/MEMORY_MANAGER.md)

## Key Technical Highlights

- **Zero-Copy Architecture**: Accumulates streaming compression payloads directly
  within a single memory region, optimizing multi-megabyte XML or JSON document
  ingestion hot-paths.
- **Decompression Bomb Protection**: Employs structural chunk length checks
  and defensive validation algorithms before expanding processing buffers.
- **Static Huge Pages Support**: Integrates OS Static Huge Pages via `mmap()` to
  dramatically lower Translation Lookaside Buffer (TLB) cache misses.
- **Tuple-Scoped Context Lifecycle**: Binds custom heap allocations to
  `CurrentMemoryContext` (row-lifecycle), preventing RAM bloat across
  multi-million row scan executions.
- **Advanced Zstandard Multi-Dictionary Cache**: Leverages statement-level
  isolated tracking (`fn_extra` inside `fn_mcxt`) to safely evaluate multiple
  independent dictionaries within a single row or complex `JOIN` plan.
- **Next-Gen Gzip Acceleration via `zlib-ng`**: Natively integrates with `zlib-ng`
  to utilize modern hardware-accelerated CRC32 and SIMD instruction sets,
  delivering up to 4x faster Gzip/Deflate throughput compared to stock standard
  `zlib` architectures.

## Distribution Packages

Distributed as pre-compiled binaries with statically linked libraries for:

- **Debian / Ubuntu LTS** (`.deb`)
- **Rocky Linux / RHEL** (`.rpm`)
- **macOS ARM64 Apple Silicon** (`.tar.gz` archive for local Homebrew testing)

*Statically linking dependencies prevents global symbols collision with
PostgreSQL core binaries, eliminating runtime shared library conflicts.*

## Database Parameters

- `pg_z.max_size`: Maximum uncompressed data size in bytes (Default: `256MB`).
- `pg_z.mem_chunk_size`: Base memory chunk allocation and step size.

## Pronunciation

It is pronounced as "pee-gee-zee". The letter "Z" stands for a universal
reference to compression as found in the legacy `.Z` file type.
