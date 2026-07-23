# pg_z Memory Manager Architecture

<!-- toc -->

- [Overview & Architectural Goals](#overview--architectural-goals)
- [Component Details & Design Mechanics](#component-details--design-mechanics)
    * [Lazy Table Initialization](#lazy-table-initialization)
    * [Context-Switch Capture](#context-switch-capture)
    * [The O(1) Memory Registry Map](#the-o1-memory-registry-map)
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

1. **Performance**: Leverages OS Static Huge Pages (2MB) via `mmap` for large
   buffers, drastically lowering Translation Lookaside Buffer (TLB) misses.
2. **Strict Tuple-Scoped Lifecycle**: Guarantees that all resources are cleaned
   up automatically at the end of single row (tuple) cycles, eliminating bloat
   in multi-million row queries.
3. **O(1) Dynamic Tracking**: Employs a persistent PostgreSQL `dynahash` table
   mapping raw pointer addresses to allocation descriptors in constant time,
   obliterating older array-overflow bottlenecks.

## Component Details & Design Mechanics

### Lazy Table Initialization

The system implements a **Lazy Init + Fast Wipe** pattern. The dynamic hash
table backbone (`HTAB`) is created inside the long-lived `TopMemoryContext`
**exactly once** per database process backend lifecycle. This prevents the severe
CPU overhead of repeatedly invoking `hash_create()` on every single tuple.

### Context-Switch Capture

Inside `pg_mem_tracker_register()`, the manager continuously monitors engine
context boundaries:

```c
if (CurrentMemoryContext != last_registered_context) { ... }
```

When PostgreSQL transitions between expressions or moves to the next tuple (e.g.
inside short-lived `ExprContext` layers), the subsystem instantly attaches its
cleanup hook using `MemoryContextRegisterResetCallback()`. This locks down strict
resource isolation between individual data rows.

### The O(1) Memory Registry Map

By linking with PostgreSQL's core `utils/hsearch.h` API, the mapping registry
identifies and updates pointer metadata via exact key lookups:

```c
typedef struct PageHashEntry {
    PageHashKey key;    // Hash Key (MUST be 1st)
    size_t region_size; // exact allocated region size
    bool is_huge;       // allocation type: true (mmap), false (palloc)
} PageHashEntry;
```

### Multi-Megabyte Buffer Expansion Strategy

When streaming compressors hit internal limits and request data expansions via
`pg_hybrid_repalloc()`, the subsystem uses a defensive, non-shrinking approach.
If the new size request fits within the already mapped region capacity, the manager
instantly short-circuits the call, keeping the active buffer pointer unchanged.
If an expansion is mandatory, it provisions a fresh segment via `pg_hybrid_alloc`,
safely migrates original payload data via `memcpy`, and purges the old segment.

## Single-Pass Tuple Cleanup Protocol

At the end of processing a tuple, or in the event of an unexpected execution
failure via `elog(ERROR)`, PostgreSQL triggers a non-local `longjmp` and fires
the registered `pg_mem_tracker_cleanup()` hook.

To achieve maximum data throughput for high-speed streaming codecs like LZ4, the
cleanup routine avoids destroying the tracking infrastructure. Instead, it runs
a specialized, single-pass `do-while` loop that handles both system unmapping
and bucket clearing concurrently without corrupting the traversal index.
