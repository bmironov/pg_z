/* PostgreSQL */
#include <postgres.h>

#include <fmgr.h>
#include <funcapi.h>
#include <utils/guc.h>
#include <utils/memutils.h>
#include <varatt.h>

#ifndef PG_Z_H
#define PG_Z_H

#ifdef _WIN32
/* Windows compatibility stubs for compilation */
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_HUGETLB 0x40000
#define MAP_FAILED ((void *)-1)

/* Stub out mmap and munmap since Windows will always fallback to palloc */
static inline void *
mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return MAP_FAILED;
}
static inline int
munmap(void *addr, size_t length)
{
	return 0;
}
#endif

// GUC: memory allocation chunk size
extern size_t memory_chunk_size;

// GUC: maximum size of decompressed data in bytes; -1 = unlimited
extern size_t max_uncompressed_size;

/*
 * ===============================================================
 * Memory tracker
 * ===============================================================
 */

void pg_mem_tracker_init_hugepage_size(void);
void pg_mem_tracker_init(void);
void pg_mem_tracker_untrack(void *address);

/*
 * ===============================================================
 * Memory manager
 * ===============================================================
 */
/* Global variable to store the actual default Huge Page size of the OS */
extern size_t huge_page_size;

void *pg_hybrid_alloc(size_t *size);
void *pg_hybrid_repalloc(void *address, size_t prev_size, size_t *new_size);
void pg_hybrid_free(void *opaque, void *address);

/*
 * ===============================================================
 * brotli-related variables and functions
 * ===============================================================
 */

Datum pg_brotli(PG_FUNCTION_ARGS);
Datum pg_unbrotli(PG_FUNCTION_ARGS);
/*
 * ===============================================================
 * gzip-related variables and functions
 * ===============================================================
 */

Datum pg_gzip(PG_FUNCTION_ARGS);
Datum pg_gunzip(PG_FUNCTION_ARGS);
Datum pg_deflate(PG_FUNCTION_ARGS);
Datum pg_inflate(PG_FUNCTION_ARGS);

/*
 * ===============================================================
 * LZ4-related variables and functions
 * ===============================================================
 */

Datum pg_lz4(PG_FUNCTION_ARGS);
Datum pg_unlz4(PG_FUNCTION_ARGS);

/*
 * ===============================================================
 * Zstandard-related variables and functions
 * ===============================================================
 */

Datum pg_zstd(PG_FUNCTION_ARGS);
Datum pg_unzstd(PG_FUNCTION_ARGS);

/*
 * Global functions
 */
void dump_hex(const char *label, const uint8 *data, size_t size);

#endif
