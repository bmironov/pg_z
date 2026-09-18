#include "zstd_dict.h"

/*
 * Callback invoked automatically when PostgreSQL destroys fn_mcxt at the end
 * of the statement.
 */
void
free_local_cdict_callback(void *arg)
{
	FunctionCDictCache *cache = (FunctionCDictCache *)arg;
	if (cache != NULL && cache->cdict != NULL) {
		ZSTD_freeCDict(cache->cdict);
		cache->cdict = NULL;
	}
}

/*
 * Callback invoked automatically when PostgreSQL destroys fn_mcxt at the end
 * of the statement.
 */
void
free_local_ddict_callback(void *arg)
{
	FunctionDDictCache *cache = (FunctionDDictCache *)arg;
	if (cache != NULL && cache->ddict != NULL) {
		ZSTD_freeDDict(cache->ddict);
		cache->ddict = NULL;
	}
}

/*
 * Retrieves an existing compiled ZSTD_CDict from the function's internal local
 * cache (fn_extra), or compiles a new one inside fn_mcxt if it doesn't exist
 * or if dictionary parameters changed.
 */
ZSTD_CDict *
get_or_create_cdict(
		FunctionCallInfo fcinfo,
		void *dict_data,
		size_t dict_size,
		int32 compression_level)
{
	FunctionCDictCache *cache = (FunctionCDictCache *)fcinfo->flinfo->fn_extra;

	// First call for this specific function instance in the query execution
	// tree
	if (cache == NULL) {
		MemoryContext old_ctx;
		MemoryContextCallback *mem_cb;

		// Switch to the function's statement-long context
		old_ctx = MemoryContextSwitchTo(fcinfo->flinfo->fn_mcxt);

		cache = (FunctionCDictCache *)palloc0(sizeof(FunctionCDictCache));

		// Register callback to safely free Zstd compiled dictionary when the
		// query finishes
		mem_cb =
				(MemoryContextCallback *)palloc(sizeof(MemoryContextCallback));
		mem_cb->func = free_local_cdict_callback;
		mem_cb->arg = (void *)cache;
		MemoryContextRegisterResetCallback(fcinfo->flinfo->fn_mcxt, mem_cb);

		MemoryContextSwitchTo(old_ctx);
		fcinfo->flinfo->fn_extra = (void *)cache;
	}

	// Recompile if the dictionary data pointer, size, or compression level
	// changed
	if (cache->cdict == NULL || cache->dict_source_addr != dict_data ||
		cache->dict_size != dict_size ||
		cache->compression_level != compression_level) {
		if (cache->cdict != NULL) {
			ZSTD_freeCDict(cache->cdict);
			cache->cdict = NULL;
		}

		cache->cdict =
				ZSTD_createCDict(dict_data, dict_size, compression_level);
		cache->dict_source_addr = dict_data;
		cache->dict_size = dict_size;
		cache->compression_level = compression_level;

		if (cache->cdict == NULL)
			elog(ERROR, "failed to allocate Zstd CDict in fn_mcxt");
	}

	return cache->cdict;
}

/*
 * Retrieves an existing compiled ZSTD_DDict from the function's internal local
 * cache (fn_extra), or compiles a new one inside fn_mcxt if it doesn't exist
 * or if dictionary parameters changed.
 */
ZSTD_DDict *
get_or_create_ddict(FunctionCallInfo fcinfo, void *dict_data, size_t dict_size)
{
	FunctionDDictCache *cache = (FunctionDDictCache *)fcinfo->flinfo->fn_extra;

	// First call for this specific function instance in the query execution
	// tree
	if (cache == NULL) {
		MemoryContext old_ctx;
		MemoryContextCallback *mem_cb;

		// Switch to the function's statement-long context
		old_ctx = MemoryContextSwitchTo(fcinfo->flinfo->fn_mcxt);

		cache = (FunctionDDictCache *)palloc0(sizeof(FunctionDDictCache));

		// Register callback to safely free Zstd compiled dictionary when the
		// query finishes
		mem_cb =
				(MemoryContextCallback *)palloc(sizeof(MemoryContextCallback));
		mem_cb->func = free_local_ddict_callback;
		mem_cb->arg = (void *)cache;
		MemoryContextRegisterResetCallback(fcinfo->flinfo->fn_mcxt, mem_cb);

		MemoryContextSwitchTo(old_ctx);
		fcinfo->flinfo->fn_extra = (void *)cache;
	}

	// Recompile if the dictionary data pointer or size changed
	if (cache->ddict == NULL || cache->dict_source_addr != dict_data ||
		cache->dict_size != dict_size) {
		if (cache->ddict != NULL) {
			ZSTD_freeDDict(cache->ddict);
			cache->ddict = NULL;
		}

		cache->ddict = ZSTD_createDDict(dict_data, dict_size);
		cache->dict_source_addr = dict_data;
		cache->dict_size = dict_size;

		if (cache->ddict == NULL)
			elog(ERROR, "failed to allocate Zstd DDict in fn_mcxt");
	}

	return cache->ddict;
}
