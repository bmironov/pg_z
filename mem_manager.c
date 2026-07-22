
#include "pg_z.h"

#include <sys/mman.h>

// Max number of tracked regions within single function call
#define MAX_TRACKED_PAGES 256

#define MIN_HUGE_PAGE_SIZE ((size_t)2 * 1024 * 1024)

typedef int16 registry_index;

typedef struct HugePageTrack {
	void *address;		// exact address returned by mmap or palloc
	size_t region_size; // exact allocated region size
	bool is_huge;		// allocation type: true (mmap), false (palloc)
} HugePageTrack;

// Memory region tracker
static HugePageTrack page_registry[MAX_TRACKED_PAGES];
static int tracked_pages_count = 0;

size_t huge_page_size;

static MemoryContextCallback registry_cleanup_callback;
static MemoryContext last_registered_context = NULL;
/*
 * ===========================================================
 * Memory region tracker (maintains registry of mem regions)
 * ===========================================================
 */

/*
 * Initializes Huge Memory page size used in the system
 */
void
pg_mem_tracker_init_hugepage_size(void)
{
	FILE *fp;
	unsigned long kbytes = 0;
	char line[256];

	huge_page_size = MIN_HUGE_PAGE_SIZE;

	/* Read the default huge page size directly from Linux kernel state */
	fp = fopen("/proc/meminfo", "r");
	if (fp != NULL) {
		while (fgets(line, sizeof(line), fp)) {
			if (strncmp(line, "Hugepagesize:", 13) == 0) {
				if (sscanf(line + 13, "%lu", &kbytes) == 1)
					huge_page_size = (size_t)kbytes * 1024;

				break;
			}
		}

		fclose(fp);
	}
}

/*
 * Automatic PostgreSQL Context Callback.
 */
static void
pg_mem_tracker_cleanup(void *arg)
{
	registry_index i;

	for (i = 0; i < tracked_pages_count; i++) {
		if (page_registry[i].address != NULL && page_registry[i].is_huge) {
			munmap(page_registry[i].address, page_registry[i].region_size);
			page_registry[i].address = NULL;
			page_registry[i].region_size = 0;
			page_registry[i].is_huge = false;
		}
	}

	tracked_pages_count = 0;
	last_registered_context = NULL;
}

// Initialize registry
// and attach tracking to PostgreSQL memory context life cycle
void
pg_mem_tracker_init(void)
{
	registry_index i;
	MemoryContextCallback *dynamic_cb;

	if (tracked_pages_count > 0) {
		tracked_pages_count = 0;
		return;
	}

	for (i = 0; i < MAX_TRACKED_PAGES; i++) {
		page_registry[i].address = NULL;
		page_registry[i].region_size = 0;
		page_registry[i].is_huge = false;
	}

	// Attach to Postgres callback to release all allocated memory after TXN
	dynamic_cb =
			(MemoryContextCallback *)palloc(sizeof(MemoryContextCallback));
	dynamic_cb->func = pg_mem_tracker_cleanup;
	dynamic_cb->arg = NULL;

	MemoryContextRegisterResetCallback(CurrentMemoryContext, dynamic_cb);
}

// Find region index in our registry
static registry_index
pg_mem_tracker_find_index(void *address)
{
	registry_index i;

	for (i = 0; i < tracked_pages_count; i++) {
		if (page_registry[i].address == address)
			return i;
	}

	return -1;
}

// Register new mem region
static void
pg_mem_tracker_register(void *address, size_t size, bool is_huge)
{
	registry_index i, j = 0;

	// Lazy cleanup of non Huge Pages is possible,
	// as they will be released by PostgreSQL automatically
	if (tracked_pages_count >= MAX_TRACKED_PAGES) {
		for (i = 0; i < tracked_pages_count; i++) {
			if (page_registry[i].is_huge) {
				if (i != j) {
					page_registry[j] = page_registry[i];
				}
				j++;
			}
		}
		tracked_pages_count = j;

		if (tracked_pages_count >= MAX_TRACKED_PAGES)
			elog(ERROR,
				 "Memory regions registry overflow."
				 " Too many allocations in a single query (%d).",
				 tracked_pages_count);
	}

	page_registry[tracked_pages_count].address = address;
	page_registry[tracked_pages_count].region_size = size;
	page_registry[tracked_pages_count].is_huge = is_huge;
	tracked_pages_count++;
}

// Unregister specific mem region
static void
pg_mem_tracker_unregister(registry_index index)
{
	registry_index i;

	if (index < 0 || index > tracked_pages_count)
		return;

	for (i = index; i < tracked_pages_count; i++) {
		page_registry[i] = page_registry[i + 1];
	}

	tracked_pages_count--;
	page_registry[tracked_pages_count].address = NULL;
	page_registry[tracked_pages_count].region_size = 0;
	page_registry[tracked_pages_count].is_huge = false;
}

/*
 * High-level manager finalization call.
 * 1. Checks if iprovided address is a tracked Huge Page block.
 * 2. If true, unregisters it to protect it from instant unmapping, allowing
 *    PostgreSQL to safely send this row's Datum up to the executor pipeline.
 * 3. Instantly flushes (munmap) any remaining temporary garbage/working chunks
 *    that accumulated during loop reallocations, freeing RAM on every single
 * row.
 */
void
pg_mem_tracker_untrack(void *address)
{
	registry_index i, idx = -1;
	size_t final_size = 0;
	bool is_huge = false;

	if (address == NULL) {
		return;
	}

	idx = pg_mem_tracker_find_index(address);
	if (idx >= 0) {
		final_size = page_registry[idx].region_size;
		is_huge = page_registry[idx].is_huge;
	}

	/*
	 * Instantly unmap any temporary working buffers left in the registry
	 * before transitioning to the next row processing. Crucial for million-row
	 * transactions!
	 */
	for (i = 0; i < tracked_pages_count; i++) {
		if (page_registry[i].address != NULL &&
			page_registry[i].address != address) {
			if (page_registry[i].is_huge) {
				munmap(page_registry[i].address, page_registry[i].region_size);
			} else {
				pfree(page_registry[i].address);
			}
		}

		page_registry[i].address = NULL;
		page_registry[i].region_size = 0;
		page_registry[i].is_huge = false;
	}

	tracked_pages_count = 0;

	if (idx >= 0) {
		page_registry[0].address = address;
		page_registry[0].region_size = final_size;
		page_registry[0].is_huge = is_huge;
		tracked_pages_count = 1;
	}
}

/*
 * =======================================================
 * Memory manager (allocates, reallocates and frees mem)
 * =======================================================
 */

/*
 * pg_hybrid_alloc attempts to allocate memory region in Static Huge Pages
 * if requested size smaller than CHUNK_SIZE, then standard
 * palloc_extended is used.
 * palloc calls do not register in tracker
 */
void *
pg_hybrid_alloc(size_t *size)
{
	void *ptr = NULL;
	size_t huge_size; // separate size rounded up to Huge Page size
	size_t req_size = *size;

	if (req_size > memory_chunk_size) {
		// rounding up size to closest memory_chunk_size multiple
		req_size = (req_size + (memory_chunk_size - 1)) &
				   ~(memory_chunk_size - 1);
	}

	if (CurrentMemoryContext != last_registered_context) {
		registry_cleanup_callback.func = pg_mem_tracker_cleanup;
		registry_cleanup_callback.arg = NULL;

		MemoryContextRegisterResetCallback(
				CurrentMemoryContext, &registry_cleanup_callback);
		last_registered_context = CurrentMemoryContext;
	}

	if (req_size >= huge_page_size) {
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
			pg_mem_tracker_register(ptr, huge_size, true);
			return ptr;
		}
	}

	ptr = palloc_extended(req_size, MCXT_ALLOC_NO_OOM);
	if (ptr == NULL)
		return NULL;

	*size = req_size;
	pg_mem_tracker_register(ptr, req_size, false);

	return ptr;
}

/*
 * pg_hybrid_repalloc reallocates memory based on HugePageHeader struct
 */
void *
pg_hybrid_repalloc(void *address, size_t prev_size, size_t *new_size)
{
	void *new_address = NULL;
	registry_index old_idx = -1;
	bool old_is_huge = false;
	size_t old_size = 0, req_size = *new_size;

	if (address == NULL)
		return pg_hybrid_alloc(new_size);

	old_idx = pg_mem_tracker_find_index(address);
	if (old_idx < 0) {
		if (req_size < memory_chunk_size) {
			new_address = repalloc(address, req_size);
			*new_size = req_size;
			return new_address;
		}

		old_size = prev_size;
		old_is_huge = false;
	} else {
		old_size = page_registry[old_idx].region_size;
		old_is_huge = page_registry[old_idx].is_huge;

		if (req_size <= old_size) {
			*new_size = old_size;
			return address;
		}
	}

	new_address = pg_hybrid_alloc(new_size);
	if (new_address == NULL)
		return NULL;

	memcpy(new_address, address, old_size);

	if (old_is_huge)
		munmap(address, old_size);
	else
		pfree(address);

	if (old_idx >= 0)
		pg_mem_tracker_unregister(old_idx);

	return new_address;
}

/*
 * pg_hybrid_free releases memory based on HugePageHeader struct
 */
void
pg_hybrid_free(void *opaque, void *address)
{
	registry_index old_idx;

	if (address == NULL)
		return;

	old_idx = pg_mem_tracker_find_index(address);
	if (old_idx >= 0) {
		if (page_registry[old_idx].is_huge)
			munmap(address, page_registry[old_idx].region_size);
		else
			pfree(address);

		pg_mem_tracker_unregister(old_idx);
	} else {
		pfree(address);
	}
}
