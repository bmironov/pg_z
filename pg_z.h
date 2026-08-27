#include <postgres.h>

#include <fmgr.h>
#include <funcapi.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/elog.h>
#include <utils/guc.h>
#include <utils/memutils.h>
#include <varatt.h>

#ifndef PG_Z_H
#define PG_Z_H

#ifndef GIT_VERSION
#define PG_Z_VERSION "0.0.1"
#else
#define PG_Z_VERSION GIT_VERSION
#endif

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

// GUC: maximum size of decompressed data in bytes
extern size_t max_uncompressed_size;

/*
 * ===============================================================
 * Common functions for the pg_z extension
 * ===============================================================
 */

Datum pg_z_version(PG_FUNCTION_ARGS);

/*
 * ===============================================================
 * Memory tracker
 * ===============================================================
 */

typedef int16 registry_index;

void pg_mem_tracker_init_hugepage_size(void);
bool pg_mem_tracker_overflow(void);
registry_index pg_mem_tracker_registry_size(void);
bool pg_mem_tracker_region_is_huge(registry_index index);
size_t pg_mem_tracker_get_region_size(registry_index index);
bool pg_mem_tracker_register(void *address, size_t size, bool is_huge);
registry_index pg_mem_tracker_find(void *address);
void pg_mem_tracker_unregister(registry_index index);

/*
 * ===============================================================
 * Memory manager
 * ===============================================================
 */

#define MIN_MEM_SIZE_8KB 8192
#define MIN_HUGE_PAGE_SIZE ((size_t)2 * 1024 * 1024)

/* Global variable to store the actual default Huge Page size of the OS */
extern size_t huge_page_size;

void *pg_hybrid_alloc(size_t *size);
void *pg_hybrid_repalloc(void *address, size_t *new_size);
void pg_hybrid_free(void *address);

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

// Standard zlib
Datum pg_gzip(PG_FUNCTION_ARGS);
Datum pg_gunzip(PG_FUNCTION_ARGS);
Datum pg_deflate(PG_FUNCTION_ARGS);
Datum pg_inflate(PG_FUNCTION_ARGS);

// Zlib-NG
Datum pg_gzip_ng(PG_FUNCTION_ARGS);
Datum pg_gunzip_ng(PG_FUNCTION_ARGS);
Datum pg_deflate_ng(PG_FUNCTION_ARGS);
Datum pg_inflate_ng(PG_FUNCTION_ARGS);

/*
 * ===============================================================
 * LZ4-related variables and functions
 * ===============================================================
 */

Datum pg_lz4(PG_FUNCTION_ARGS);
Datum pg_unlz4(PG_FUNCTION_ARGS);

/*
 * ===============================================================
 * Snappy-related variables and functions
 * ===============================================================
 */

Datum pg_snappy(PG_FUNCTION_ARGS);
Datum pg_unsnappy(PG_FUNCTION_ARGS);

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
