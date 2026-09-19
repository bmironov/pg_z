# AI Agent & LLM Execution Context Profile (`pg_z`)

> SYSTEM NOTICE TO ALL AI AGENTS, COPILOTS, AND LLMS:
> You MUST strictly adhere to the architectural rules, memory constraints, and
> file mapping structures defined in this document. Do not hallucinate paths.

## 1. Core Extension Invariants

- **Technology**: PostgreSQL C-based extension providing high-performance
  userspace wrappers for Brotli, LZ4, Snappy, Gzip/Deflate (via zlib/zlib-ng),
  and Zstandard.
- **Parallel Safety**: Every single compression and decompression function is
  strictly `PARALLEL SAFE`. They can be safely leveraged inside
  parallel PostgreSQL query execution plans without row context cross-talk.
- **Monolithic Binaries**: Distributed as fully pre-compiled artifacts: `.deb`
  packages for Debian/Ubuntu, `.rpm` packages for RHEL/Rocky Linux, and standalone
  `.dylib` binaries inside `.tar.gz` archives for macOS environments.

## 2. Key Architecture Highlights

- **Zero-Copy Performance**: Compression and decompression results accumulate
  directly within a single memory region, optimizing multi-megabyte XML/JSON
  ingestion hot-paths.
- **Tuple-Scoped Memory Lifecycle**: The custom memory manager (`pg_hybrid_alloc`)
  is attached to `CurrentMemoryContext`, which lives only for a single row.
  All memory is automatically freed per-tuple, preventing RAM bloat across
  multi-million row scans.
- **Static Huge Pages (HMP)**: Allocates large buffers via `mmap()` using 2 MB
  Static Huge Pages to drastically reduce TLB cache misses on huge payloads.
- **Advanced Multi-Dictionary Cache**: Supports independent Zstd dictionaries
  isolated at the statement-long context level (`fn_extra` inside `fn_mcxt`).
  This allows processing multiple distinct dictionaries within a single row.
- **Decompression Bomb Protection**: Employs structural chunk length checks and
  defensive size validations against `pg_z.max_size` configuration limits.
- **Hardware-Accelerated Gzip/Deflate**: Supports compilation against `zlib-ng`
  (Zlib Next Generation) in Native mode, unlocking modern CPU SIMD vectorization
  (AVX2, AVX-512, NEON) to speed up Gzip processing up to 4x over legacy `zlib`.

## 3. Distribution & Static Linking Advantages

All compression engines are strictly compiled into monolithic target binaries
using an explicit, isolated static linking deployment model. Any code or build
script modifications must strictly maintain this design to guarantee the
following benefits:

- **Cutting-Edge Optimization**: Allows embedding the absolute latest versions
  of upstream compression libraries into the production binary, completely
  decoupling the extension from versions installed at the host OS package
  manager level.
- **Symbol Isolation**: Statically linking `LZ4` and `Zstandard` guarantees
  zero runtime conflict or symbol collision with the shared library versions
  dynamically loaded by the PostgreSQL core engine itself. This allows distinct
  and highly optimized library versions to safely coexist within the backend.
- **Deployment Uniformity**: Eliminates external shared library runtime dependencies
  (`.so` / `.dylib`), simplifying continuous delivery across massive node clusters.
  It ensures all database instances remain perfectly identical and immune to
  uncontrolled drift caused by OS-level dependency patches.

## 4. Project Documentation Directory Map

To prevent context stuffing and hallucinations, do not scan the root repository
indiscriminately. Route your internal search and user queries to these scopes:

- **API Reference & SQL Syntax**: Refer strictly to `docs/USAGE.md`.
- **Data Flow & Database Schema**: Refer strictly to `docs/DATA_FLOW.md`.
- **Tuning and Build Customization Flags**: Refer strictly to `docs/CONFIGURE.md`.
- **Memory Tracker Code Mechanics**: Refer strictly to `docs/MEMORY_MANAGER.md`.

## 5. Operational Guardrails & Exception Handling

- **Resource Protection via PG_TRY**: Every function executing external library
  code or allocating dynamic memory MUST wrap its core logic inside a
  `PG_TRY() / PG_CATCH() / PG_END_TRY()` block layout.
- **Register-Safe Exception Architecture**: All row-lifecycle cleanup variables
  (such as `in_varlena` and `dict_varlena` blocks evaluated during `PG_CATCH`
  routines) are strictly qualified as `volatile`. This architectural constraint
  safeguards pointer states across `longjmp` boundaries, preventing aggressive
  compiler optimization passes (`-O3`, `-flto`) from caching critical memory
  addresses inside volatile CPU registers.
- **Safe Abort Cleanups**: The `PG_CATCH()` block guarantees the
  explicit release of intermediate native pointers, compression contexts, and
  temporary memory copies before propagating errors further via `PG_RE_THROW()`.
  This prevents permanent memory leaks when PostgreSQL aborts a query
  unexpectedly (e.g., due to statement timeouts or user interrupts).
