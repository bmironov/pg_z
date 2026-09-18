#include "pg_z.h"

#include <zstd.h>

#ifndef PG_Z_ZSTD_DICT_H
#define PG_Z_ZSTD_DICT_H

// Local cache structure stored directly inside the function's execution state
// (fn_extra)
typedef struct FunctionCDictCache {
	void *dict_source_addr; // To detect if the dictionary buffer has changed
	size_t dict_size;
	int32 compression_level;
	ZSTD_CDict *cdict;
} FunctionCDictCache;

// Local cache structure stored directly inside the function's execution state
// (fn_extra)
typedef struct FunctionDDictCache {
	void *dict_source_addr; // To detect if the dictionary buffer changed
	size_t dict_size;
	ZSTD_DDict *ddict;
} FunctionDDictCache;

extern void free_local_cdict_callback(void *arg);

extern void free_local_ddict_callback(void *arg);

extern ZSTD_CDict *get_or_create_cdict(
		FunctionCallInfo fcinfo,
		void *dict_data,
		size_t dict_size,
		int32 compression_level);

extern ZSTD_DDict *get_or_create_ddict(
		FunctionCallInfo fcinfo, void *dict_data, size_t dict_size);

#endif
