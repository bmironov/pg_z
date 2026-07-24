# pg_z Memory Manager Architecture

<!-- toc -->

- [Overview & Architectural Goals](#overview--architectural-goals)
- [Component Details & Design Mechanics](#component-details--design-mechanics)
    * [Lazy Persistent Registry Initialization](#lazy-persistent-registry-initialization)
    * [Context-Switch Capture](#context-switch-capture)
    * [The Cache-Aligned Memory Registry Array](#the-cache-aligned-memory-registry-array)
    * [Multi-Megabyte Buffer Expansion Strategy](#multi-megabyte-buffer-expansion-strategy)
- [Single-Pass Tuple Cleanup Protocol](#single-pass-tuple-cleanup-protocol)

<!-- tocstop -->

This document describes the high-performance, tuple-scoped memory management
subsystem implemented in `mem_manager.c` for the `pg_z` PostgreSQL extension.

## Overview & Architectural Goals

Third-party compression libraries (such as LZ4, Zstandard, and Brotli) often
perform rapid internal allocations or require massive temporary sliding-window
buffers. Managing these demands using standard PostgreSQL `palloc` inside tight
execution loops can introduce severe heap fragmentation or memory bloat.

To solve this, `pg_z` uses a hybrid memory framework achieving three goals:

1. **Performance**: Leverages OS Static Huge Pages via `mmap` for large
   buffers, drastically lowering Translation Lookaside Buffer (TLB) misses.
2. **Strict Tuple-Scoped Lifecycle**: Guarantees that all resources are cleaned
   up automatically at the end of single row (tuple) cycles, eliminating bloat
   in multi-million row queries.
3. **Hyper-Fast Cache-Line Registry**: Employs a persistent resizable flat array
   mapping raw pointer addresses to allocation tracking fields, completely
   bypassing the performance overhead of dynamic hashing mechanisms.

## Component Details & Design Mechanics

### Lazy Persistent Registry Initialization

The system implements a **Lazy Init + Fast Wipe** pattern. The continuous flat
tracking registry array is allocated inside the long-lived `TopMemoryContext`
**exactly once** per database process backend lifecycle. This avoids any
re-allocation overhead on tight per-row query execution paths.

The initial container structure is optimized to match a single 8kB memory
segment boundary, provisioning 512 entries on 64-bit systems right from the
start, which completely covers standard row execution processing requirements.

### Context-Switch Capture

Inside `pg_mem_tracker_register()`, the manager continuously monitors engine
context boundaries:

```c
if (last_registered_context != CurrentMemoryContext) { ... }
```

When PostgreSQL transitions between expressions or moves to the next tuple (e.g.
inside short-lived `ExprContext` layers), the subsystem instantly attaches its
cleanup hook using `MemoryContextRegisterResetCallback()`. This locks down strict
resource isolation between individual data rows.

### The Cache-Aligned Memory Registry Array

Instead of storing explicit tracking boolean parameters or using heavy hashing
math routines, the architecture relies on a streamlined 16-byte aligned data
layout. The tracking flag is embedded directly within the sign bit of a single
signed 32-bit `region_size` field:

```c
typedef struct MemTracker {
    void *address;      // pointer to memory region
    int32 region_size;  // Positive: Huge Pages (mmap), Negative: palloc
} MemTracker;
```

This structural configuration achieves extreme hardware caching efficiency. It
fits exactly 4 element tracking tracks within a single 64-byte L1 CPU cache
line, driving linear array scan evaluation costs down to just 1-2 processor
clock cycles for typical row workloads.

### Multi-Megabyte Buffer Expansion Strategy

When streaming compressors hit internal limits and request data expansions via
`pg_hybrid_repalloc()`, the subsystem uses a defensive, non-shrinking approach.
If the new size request fits within the already mapped region capacity, the manager
instantly short-circuits the call, keeping the active buffer pointer unchanged.

If an expansion is mandatory, it provisions a fresh segment via `pg_hybrid_alloc`.
Since capacity values are encoded with signed representations, the manager reads
the original metrics safely using compiler-intrinsic absolute value operations
(`abs()`). It then migrates payload data via `memcpy` and purges the old segment.

If the internal tracker exceeds its active array bounds, it triggers linear
scaling increments by allocating exactly **8kB additional chunks** via `repalloc`
instead of performance-heavy geometric doubling. Sizing bounds are strictly
monitored to ensure total tracked row limits never overflow `int16` boundaries.

## Single-Pass Tuple Cleanup Protocol

At the end of processing a tuple, or in the event of an unexpected execution
failure via `elog(ERROR)`, PostgreSQL triggers a non-local `longjmp` and fires
the registered `pg_mem_tracker_cleanup()` hook.

To achieve maximum data throughput for high-speed streaming codecs like LZ4, the
cleanup routine avoids destroying the array infrastructure backbone. Instead, it
runs a specialized, single-pass `do-while` loop that sequentially sweeps the
flat tracking memory layer, executing `munmap` system calls strictly for active
Huge Page allocations (`region_size > 0`).

Standard memory chunks are left completely untouched during this loop, allowing
PostgreSQL to reclaim them natively and safely during the subsequent context
reset without risk of heap corruption or double-free panics. Once the scan is
complete, the manager performs a lightning-fast `O(1)` reset by clearing the
active count registry offset back to zero.
