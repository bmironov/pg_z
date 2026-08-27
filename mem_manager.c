#include "pg_z.h"
#include "utils/palloc.h"

#include <limits.h> // Required for SHRT_MAX
#include <stdlib.h> // Required for abs()

#include <errno.h>
#include <string.h>

#ifndef _WIN32
#include <sys/mman.h>
#endif

size_t huge_page_size; // actual size of Huge Page on the system

// Flag to ensure we log the mmap failure details exactly once per session
static bool huge_pages_warned = false;

/*
 * pg_hybrid_alloc attempts to allocate memory region in Static Huge Pages.
 * If requested size is smaller than memory_chunk_size, or if Huge Pages are
 * exhausted/unconfigured, it seamlessly falls back to standard
 * palloc_extended.
 *
 * DEBUG RETENTION: Both Huge Pages and standard palloc allocations are
 * registered in the tracker to support explicit context debugging and early
 * memory reclamation.
 */
void *
pg_hybrid_alloc(size_t *size)
{
	void *ptr = NULL;
	size_t huge_size; // separate size rounded up to Huge Page size
	size_t req_size = *size;

	if (pg_mem_tracker_overflow()) {
		elog(ERROR,
			 "pg_hybrid_alloc: memory tracker registry limit (%d entries) "
			 "reached. This indicates a critical allocation tracking defect.",
			 pg_mem_tracker_registry_size());
	}

	// Safeguard for both types of requests:
	// - specified  as real value (eg, 2GB)
	// - specified as result of "negative" number after some calculations,
	// which in case of size_t becomes quite huge value
	if (req_size > MaxAllocSize) {
		ereport(WARNING,
				errmsg("pg_hybrid_alloc: request for too much memory "
					   "(%zu bytes)",
					   req_size));
		return NULL;
	}

	if (req_size > memory_chunk_size) {
		// rounding up size to closest memory_chunk_size multiple
		req_size = (req_size + (memory_chunk_size - 1)) &
				   ~(memory_chunk_size - 1);
	}

#ifndef _WIN32
	if (req_size >= huge_page_size && !pg_mem_tracker_overflow()) {
		// Round up size to closest Huge Page size multiple
		huge_size = (req_size + (huge_page_size - 1)) & ~(huge_page_size - 1);
		ptr =
				mmap(NULL,
					 huge_size,
					 PROT_READ | PROT_WRITE,
					 MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
					 -1,
					 0);

		if (ptr != MAP_FAILED) {
			*size = huge_size;
			if (!pg_mem_tracker_register(ptr, huge_size, true)) {
				munmap(ptr, huge_size);
				elog(ERROR,
					 "memory tracker out of memory while expanding its HMP "
					 "registry");
			}
			return ptr;
		} else {
			if (!huge_pages_warned) {
				ereport(WARNING,
						errmsg("pg_hybrid_alloc: attempt to allocate "
							   "%zu bytes in Huge Memory Pages failed. "
							   "Falling back to standard pages.",
							   huge_size));
				huge_pages_warned = true;
			}
		}
	}
#endif

	ptr = palloc_extended(req_size, MCXT_ALLOC_NO_OOM);
	if (ptr == NULL)
		return NULL;

	*size = req_size;
	if (!pg_mem_tracker_register(ptr, req_size, false)) {
		pfree(ptr);
		elog(ERROR,
			 "memory tracker out of memory while expanding its registry");
	};

	return ptr;
}

/*
 * pg_hybrid_repalloc reallocates memory based on information from tracker
 */
void *
pg_hybrid_repalloc(void *address, size_t *size)
{
	registry_index index = -1;
	void *new_address = NULL;
	size_t req_size = *size, region_size = 0;

	if (address == NULL)
		return pg_hybrid_alloc(size);

	if (req_size == 0) {
		pg_hybrid_free(address);
		return NULL;
	}

	index = pg_mem_tracker_find(address);

	// Don't deal with allocations outside of our registry
	if (index < 0) {
		return NULL;
	}

	region_size = pg_mem_tracker_get_region_size(index);

	// Don't waste time for shrinking allocated segment
	// This allocation will be released after tuple processing is over anyway
	if (*size <= region_size) {
		*size = region_size;
		return address;
	}

	new_address = pg_hybrid_alloc(size);
	if (new_address == NULL)
		return NULL;

	memcpy(new_address, address, region_size);

	pg_hybrid_free(address);

	return new_address;
}

/*
 * pg_hybrid_free releases memory based on our registry
 */
void
pg_hybrid_free(void *address)
{
	registry_index index = -1;
	size_t region_size;

	if (address == NULL)
		return;

	index = pg_mem_tracker_find(address);

	// Don't deal with allocations outside of our registry
	if (index < 0) {
		return;
	}

	if (pg_mem_tracker_region_is_huge(index)) {
		// Just extra paranoia since we play with sign in region size
		region_size = pg_mem_tracker_get_region_size(index);
		munmap(address, region_size);
	} else {
		pfree(address);
	}

	pg_mem_tracker_unregister(index);
}
