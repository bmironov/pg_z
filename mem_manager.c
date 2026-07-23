#include "pg_z.h"

#include <utils/hsearch.h> // PostgreSQL hash library

#include <sys/mman.h>

#define MIN_HUGE_PAGE_SIZE ((size_t)2 * 1024 * 1024)

size_t huge_page_size; // actual size of Huge Page on the system

// The key used for hashing: the raw memory address pointer
typedef struct PageHashKey {
	void *address;
} PageHashKey;

// The full entry stored inside the hash table
typedef struct PageHashEntry {
	PageHashKey key;	// Hash Key (MUST be 1st)
	size_t region_size; // exact allocated region size
	bool is_huge;		// allocation type: true (mmap), false (palloc)
} PageHashEntry;

static HTAB *page_registry_hash = NULL; // Global pointer to runtime hash table

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
pg_mem_tracker_init_hash(void)
{
	HASHCTL ctl;
	int flags;

	if (page_registry_hash != NULL)
		return;

	memset(&ctl, 0, sizeof(ctl));
	ctl.keysize = sizeof(PageHashKey);
	ctl.entrysize = sizeof(PageHashEntry);
	ctl.hcxt =
			TopMemoryContext; /* Persistent memory context for hash metadata */

	flags = HASH_ELEM | HASH_CONTEXT;

	page_registry_hash = hash_create(
			"pg_z memory tracking registry",
			1024, /* Initial bucket segment allocation count */
			&ctl,
			flags);
}

/*
 * Tuple Lifecycle Engine Callback.
 * Loops through active allocations when PostgreSQL destroys/resets the row
 * context.
 */
static void
pg_mem_tracker_cleanup(void *arg)
{
	HASH_SEQ_STATUS status;
	PageHashEntry *curr_entry = NULL, *next_entry = NULL;
	bool flag = true;

	if (page_registry_hash == NULL)
		return;

	/* Initialize sequential hash scanning iterator across buckets */
	hash_seq_init(&status, page_registry_hash);
	curr_entry = (PageHashEntry *)hash_seq_search(&status);

	do {
		if (curr_entry == NULL) {
			flag = false;
		} else {
			next_entry = (PageHashEntry *)hash_seq_search(&status);
			// release the segment if it was allocated as Huge Memory Page
			if (curr_entry->key.address != NULL && curr_entry->is_huge) {
				munmap(curr_entry->key.address, curr_entry->region_size);
			}
			hash_search(
					page_registry_hash, &curr_entry->key, HASH_REMOVE, NULL);
			curr_entry = next_entry;
		}
	} while (flag);

	last_registered_context = NULL;
}

/*
 * O(1) Replacement for pg_mem_tracker_register.
 * Inserts or updates an allocation entry inside the hash layout without
 * risk of rigid static registry overflows.
 */
static void
pg_mem_tracker_register(void *address, size_t size, bool is_huge)
{
	PageHashKey hash_key;
	PageHashEntry *entry;
	bool found;

	if (address == NULL)
		return;

	pg_mem_tracker_init_hash();

	/*
	 * Ensure the lifecycle cleanup callback is pinned
	 * to the current row context
	 */
	if (last_registered_context != CurrentMemoryContext) {
		registry_cleanup_callback.func = pg_mem_tracker_cleanup;
		registry_cleanup_callback.arg = NULL;
		MemoryContextRegisterResetCallback(
				CurrentMemoryContext, &registry_cleanup_callback);
		last_registered_context = CurrentMemoryContext;
	}

	hash_key.address = address;

	/* Perform an O(1) entry insertion search */
	entry = (PageHashEntry *)hash_search(
			page_registry_hash, &hash_key, HASH_ENTER, &found);

	/* Populate or update tracking metadata metrics */
	entry->region_size = size;
	entry->is_huge = is_huge;
}

/*
 * O(1) Instant Lookup wrapper.
 * Safely replaces pg_mem_tracker_find_index to bypass linear iteration loops.
 */
static PageHashEntry *
pg_mem_tracker_find(void *address)
{
	PageHashKey hash_key;

	if (page_registry_hash == NULL || address == NULL)
		return NULL;

	hash_key.address = address;

	/* High-performance exact key bucket search */
	return (PageHashEntry *)hash_search(
			page_registry_hash, &hash_key, HASH_FIND, NULL);
}

/*
 * O(1) Entry Unregistration removal wrapper.
 * Completely eliminates old array-shifting routines and prevents garbage
 * memory indexing.
 */
static void
pg_mem_tracker_unregister(void *address)
{
	PageHashKey hash_key;

	if (page_registry_hash == NULL || address == NULL)
		return;

	hash_key.address = address;

	/* Evict the matching entry cleanly from the hash table bucket */
	hash_search(page_registry_hash, &hash_key, HASH_REMOVE, NULL);
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
 * pg_hybrid_repalloc reallocates memory based on HugePageHeader struct
 */
void *
pg_hybrid_repalloc(void *address, size_t *size)
{
	PageHashEntry *entry = NULL;
	void *new_address = NULL;
	size_t req_size = *size;

	if (address == NULL)
		return pg_hybrid_alloc(size);

	if (req_size == 0) {
		pg_hybrid_free(address);
		return NULL;
	}

	entry = pg_mem_tracker_find(address);

	// Don't deal with allocations outside of our registry
	if (entry == NULL) {
		return NULL;
	}

	// Don't waste time for shrinking allocated segment
	// This allocation will be released after tuple processing is over anyway
	if (*size <= entry->region_size) {
		*size = entry->region_size;
		return address;
	}

	new_address = pg_hybrid_alloc(size);
	if (new_address == NULL)
		return NULL;

	memcpy(new_address, address, entry->region_size);

	pg_hybrid_free(address);

	return new_address;
}

/*
 * pg_hybrid_free releases memory based on our hash
 */
void
pg_hybrid_free(void *address)
{
	PageHashEntry *entry;

	if (address == NULL)
		return;

	entry = pg_mem_tracker_find(address);

	// Don't deal with allocations outside of our registry
	if (entry == NULL) {
		return;
	}

	if (entry->is_huge) {
		munmap(address, entry->region_size);
	} else {
		pfree(address);
	}

	pg_mem_tracker_unregister(address);
}
