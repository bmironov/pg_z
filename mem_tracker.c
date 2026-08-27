#include "pg_z.h"
#include "utils/palloc.h"

#include <limits.h> // Required for SHRT_MAX
#include <stdlib.h> // Required for abs()

#include <errno.h>
#include <string.h>

#ifndef _WIN32
#include <sys/mman.h>
#endif

// Dynamic array of active hooked memory contexts
static MemoryContext *hooked_ctxs = NULL;
static int hooked_ctxs_count = 0;
static int max_hooked_ctxs = 0; // Will be calculated dynamically at init

#define REGISTRY_PAGE_SIZE MIN_MEM_SIZE_8KB // chunk step
#define MAX_REGISTRY_INDEX SHRT_MAX			// 32767 is max for signed int16

// The memory tracker registry of allocated memory regions
// PostgreSQL's limit is 1GB, so int32 is more than enough here
typedef struct MemTracker {
	void *address;	   // pointer to memory region
	int32 region_size; // Positive: Huge Pages, Negative: palloc (4kB)
} MemTracker;

static MemTracker *page_registry = NULL;

// counter of tracked regions
static registry_index tracked_pages_count = 0;

// allocated memory size for tracker
static size_t allocated_size = 0;

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
 * Returns true if mem tracker still has available slots
 */
bool
pg_mem_tracker_overflow(void)
{
	return (tracked_pages_count >= MAX_REGISTRY_INDEX);
}

registry_index
pg_mem_tracker_registry_size(void)
{
	return MAX_REGISTRY_INDEX;
}

bool
pg_mem_tracker_region_is_huge(registry_index index)
{
	return page_registry[index].region_size > 0;
}

size_t
pg_mem_tracker_get_region_size(registry_index index)
{
	return abs(page_registry[index].region_size);
}

/*
 * Initializes the dynamic registry table.
 */
static void
pg_mem_tracker_init(void)
{
	MemoryContext old_context;
	/*
	 * 8kB serves dual purpose here:
	 * - helps PostgreSQL to minimize memory fragmentation
	 * - it is big enough to hold up to 1024 pointers
	 */
	size_t ctx_array_size = MIN_MEM_SIZE_8KB;

	if (page_registry != NULL)
		return;

	allocated_size = REGISTRY_PAGE_SIZE;

	/*
	 * Switching to TopMemoryContext allows to initialize registry
	 * once per session
	 */
	old_context = MemoryContextSwitchTo(TopMemoryContext);

	page_registry =
			(MemTracker *)palloc_extended(allocated_size, MCXT_ALLOC_NO_OOM);
	hooked_ctxs = (MemoryContext *)palloc_extended(
			ctx_array_size, MCXT_ALLOC_NO_OOM);

	MemoryContextSwitchTo(old_context);

	hooked_ctxs_count = 0;
	if (page_registry == NULL || hooked_ctxs == NULL) {
		if (page_registry != NULL) {
			pfree(page_registry);
			page_registry = NULL;
		}

		if (hooked_ctxs != NULL) {
			pfree(hooked_ctxs);
			hooked_ctxs = NULL;
		}

		allocated_size = 0;
		max_hooked_ctxs = 0;
		elog(ERROR, "failed to initialize memory tracker");
	}

	max_hooked_ctxs = (int)(ctx_array_size / sizeof(MemoryContext));
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
		hooked_ctxs_count = 0;
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
				if (munmap(address, actual_size) != 0) {
					int save_errno = errno;
					ereport(WARNING,
							errmsg("mem tracker: munmap failed at %p "
								   "(size %zu): %s",
								   address,
								   actual_size,
								   strerror(save_errno)));
				}
			}
			i++;
		}
	} while (flag);

	tracked_pages_count = 0;
	hooked_ctxs_count = 0;
}

/*
 * Expands the page registry dynamic array when capacity is reached.
 * Allocates within TopMemoryContext to ensure registry persists across rows.
 * Returns true if successful, false if out of memory.
 */
static bool
pg_mem_tracker_expand_registry(void)
{
	size_t new_size = 0, new_capacity = 0;
	MemTracker *tmp_registry = NULL;
	MemoryContext old_ctx = NULL;

	if (pg_mem_tracker_overflow())
		return false;

	new_size = allocated_size + REGISTRY_PAGE_SIZE;
	new_capacity = new_size / sizeof(MemTracker);

	if (new_capacity > MAX_REGISTRY_INDEX) {
		new_capacity = MAX_REGISTRY_INDEX;
		new_size = (size_t)new_capacity * sizeof(MemTracker);
	}

	if (new_size <= allocated_size) {
		return true;
	}

	// Protect the allocator from causing an unhandled longjmp escape
	PG_TRY();
	{
		old_ctx = MemoryContextSwitchTo(TopMemoryContext);
		tmp_registry = (MemTracker *)repalloc(page_registry, new_size);
		MemoryContextSwitchTo(old_ctx);
	}
	PG_CATCH();
	{
		if (old_ctx != NULL) {
			MemoryContextSwitchTo(old_ctx);
		}
		// Suppress the PostgreSQL error state to prevent transaction abort
		FlushErrorState();
	}
	PG_END_TRY();

	if (tmp_registry == NULL) {
		return false;
	}

	page_registry = tmp_registry;
	allocated_size = new_size;

	return true;
}

/*
 * Registers newly allocated segment
 */
bool
pg_mem_tracker_register(void *address, size_t size, bool is_huge)
{
	size_t current_capacity = 0, new_ctxs_size = 0;
	bool already_hooked = false;
	MemoryContext old_ctx = NULL, *tmp_ctxs = NULL;
	MemoryContextCallback *registry_cleanup_callback;

	if (address == NULL)
		return false;

	pg_mem_tracker_init();

	for (int i = 0; i < hooked_ctxs_count; i++) {
		if (hooked_ctxs[i] == CurrentMemoryContext) {
			already_hooked = true;
			break;
		}
	}

	if (!already_hooked) {
		registry_cleanup_callback = (MemoryContextCallback *)palloc_extended(
				sizeof(MemoryContextCallback), MCXT_ALLOC_NO_OOM);

		if (registry_cleanup_callback == NULL) {
			return false;
		}

		registry_cleanup_callback->func = pg_mem_tracker_cleanup;
		registry_cleanup_callback->arg = NULL;
		MemoryContextRegisterResetCallback(
				CurrentMemoryContext, registry_cleanup_callback);

		if (hooked_ctxs_count < max_hooked_ctxs) {
			hooked_ctxs[hooked_ctxs_count++] = CurrentMemoryContext;
		} else {
			new_ctxs_size = ((size_t)max_hooked_ctxs * sizeof(MemoryContext)) +
							MIN_MEM_SIZE_8KB;
			PG_TRY();
			{
				old_ctx = MemoryContextSwitchTo(TopMemoryContext);
				tmp_ctxs =
						(MemoryContext *)repalloc(hooked_ctxs, new_ctxs_size);
				MemoryContextSwitchTo(old_ctx);
			}
			PG_CATCH();
			{
				if (old_ctx != NULL)
					MemoryContextSwitchTo(old_ctx);
				FlushErrorState();
				tmp_ctxs = NULL;
			}
			PG_END_TRY();

			if (tmp_ctxs != NULL) {
				hooked_ctxs = tmp_ctxs;
				max_hooked_ctxs = (int)(new_ctxs_size / sizeof(MemoryContext));
				hooked_ctxs[hooked_ctxs_count++] = CurrentMemoryContext;
			}
		}
	}

	current_capacity = allocated_size / sizeof(MemTracker);

	// Do we need to expand page_registry?
	if ((size_t)tracked_pages_count >= current_capacity &&
		!pg_mem_tracker_expand_registry()) {
		return false;
	}

	if (pg_mem_tracker_overflow())
		return false;

	page_registry[tracked_pages_count].address = address;

	if (is_huge) {
		page_registry[tracked_pages_count].region_size = (int32)size;
	} else {
		// Encode regular page allocation as negative value
		page_registry[tracked_pages_count].region_size = -((int32)size);
	}

	tracked_pages_count++;

	return true;
}

/*
 * Searches requested address in the registry
 */
registry_index
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
void
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
