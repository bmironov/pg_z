#include "pg_z.h"

#include <limits.h> // Required for SHRT_MAX
#include <stdlib.h> // Required for abs()
#include <sys/mman.h>

#define MIN_HUGE_PAGE_SIZE ((size_t)2 * 1024 * 1024)

size_t huge_page_size; // actual size of Huge Page on the system

#define REGISTRY_PAGE_SIZE 8192 // chunk step
typedef int16 registry_index;
#define MAX_REGISTRY_INDEX SHRT_MAX // 32767 is max for signed int16

// The full entry stored inside the hash table
typedef struct MemTracker {
	void *address;	   // pointer to memory region
	int32 region_size; // Positive: Huge Pages, Negative: palloc (4kB)
} MemTracker;

static MemTracker *page_registry = NULL;

// counter of tracked regions
static registry_index tracked_pages_count = 0;

// current capacity of memory tracker
static registry_index tracked_pages_capacity = 0;

// allocated memory size for tracker
static size_t allocated_size = 0;

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
 * Initializes the dynamic hash table.
 * It is invoked lazily and allocated inside TopMemoryContext to ensure
 * hash control blocks persist across row-level context resets.
 */
static void
pg_mem_tracker_init(void)
{
	registry_index init_capacity;
	MemoryContext old_context;

	if (page_registry != NULL)
		return;

	allocated_size = REGISTRY_PAGE_SIZE;
	init_capacity = allocated_size / sizeof(MemTracker);

	/*
	 * Switching to TopMemoryContext allows to initialize registry
	 * once per session
	 */
	old_context = MemoryContextSwitchTo(TopMemoryContext);

	page_registry = (MemTracker *)palloc_extended(
			REGISTRY_PAGE_SIZE, MCXT_ALLOC_NO_OOM);

	MemoryContextSwitchTo(old_context);

	if (page_registry == NULL) {
		allocated_size = 0;
		elog(ERROR, "failed to initialize memory tracking registry");
		return;
	}

	tracked_pages_capacity = init_capacity;
}

/*
 * Tuple Lifecycle Engine Callback.
 * Loops through active allocations when PostgreSQL
 * destroys/resets the row context.
 */
static void
pg_mem_tracker_cleanup(void *arg)
{
	registry_index i = 0;
	void *address = NULL;
	int32 region_size = 0;
	size_t actual_size;
	bool flag = true;

	if (tracked_pages_count == 0) {
		last_registered_context = NULL;
		return;
	}

	do {
		if (i >= tracked_pages_count) {
			flag = false;
		} else {
			address = page_registry[i].address;
			region_size = page_registry[i].region_size;

			// Positive region indicates an active OS Huge Page allocation
			if (address != NULL && region_size > 0) {
				actual_size = (size_t)abs(region_size);
				// just being paranoid after playing with sign bits in size
				munmap(address, actual_size);
			}
			i++;
		}
	} while (flag);

	tracked_pages_count = 0;
	last_registered_context = NULL;
}

/*
 * Registers newly allocated segment
 */
static void
pg_mem_tracker_register(void *address, size_t size, bool is_huge)
{
	registry_index new_capacity = 0;
	size_t new_size = 0;
	MemTracker *tmp_registry = NULL;

	if (address == NULL)
		return;

	pg_mem_tracker_init();

	/* Attach the tuple reset handler hook to CurrentMemoryContext */
	if (last_registered_context != CurrentMemoryContext) {
		registry_cleanup_callback.func = pg_mem_tracker_cleanup;
		registry_cleanup_callback.arg = NULL;
		MemoryContextRegisterResetCallback(
				CurrentMemoryContext, &registry_cleanup_callback);
		last_registered_context = CurrentMemoryContext;
	}

	// Are we at maximum capacity already?
	if (tracked_pages_count >= MAX_REGISTRY_INDEX) {
		elog(ERROR,
			 "Memory tracker overflow. "
			 "Too many allocations in a single query row.");
		return;
	}

	// Do we need to expand page_registry?
	if (tracked_pages_count >= tracked_pages_capacity) {
		new_size = allocated_size + REGISTRY_PAGE_SIZE;
		new_capacity = new_size / sizeof(MemTracker);

		if (new_capacity > MAX_REGISTRY_INDEX) {
			elog(ERROR, "cannot expand memory tracker: too many elements");
			return;
		}

		tmp_registry = (MemTracker *)repalloc(page_registry, new_size);
		if (tmp_registry == NULL) {
			elog(ERROR,
				 "Memory tracker overflow during active row "
				 "processing expansion.");
			return;
		}

		page_registry = tmp_registry;
		allocated_size = new_size;
		tracked_pages_capacity = new_capacity;
	}

	page_registry[tracked_pages_count].address = address;

	if (is_huge) {
		page_registry[tracked_pages_count].region_size = (int32)size;
	} else {
		// Encode regular page allocation as negative value
		page_registry[tracked_pages_count].region_size = -((int32)size);
	}

	tracked_pages_count++;
}

/*
 * Searches requested address in the registry
 */
static registry_index
pg_mem_tracker_find(void *address)
{
	registry_index i = 0;

	if (address == NULL)
		return -1;

	for (i = 0; i < tracked_pages_count; i++) {
		if (page_registry[i].address == address)
			return i;
	}

	return -1;
}

/*
 * Removes information about memory region from the registry
 */
static void
pg_mem_tracker_unregister(registry_index index)
{
	registry_index i;

	if (index < 0 || index >= tracked_pages_count)
		return;

	for (i = index; i < tracked_pages_count - 1; i++) {
		page_registry[i] = page_registry[i + 1];
	}

	tracked_pages_count--;
}

/*
 * =======================================================
 * Memory manager (allocates, reallocates and frees mem)
 * =======================================================
 */

/*
 * pg_hybrid_alloc attempts to allocate memory region in Static Huge Pages
 * if requested size smaller than memory_chunk_size, then standard
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

	region_size = (size_t)abs(page_registry[index].region_size);

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
 * pg_hybrid_free releases memory based on our hash
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

	if (page_registry[index].region_size > 0) {
		// Just extra paranoia since we play with sign in region size
		region_size = (size_t)abs(page_registry[index].region_size);
		munmap(address, region_size);
	} else {
		pfree(address);
	}

	pg_mem_tracker_unregister(index);
}
