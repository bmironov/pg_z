
#include "pg_z.h"

#define ZSTD_STATIC_LINKING_ONLY // Expose advanced types like ZSTD_customMem
#include <zstd.h>

PG_FUNCTION_INFO_V1(pg_zstd);
PG_FUNCTION_INFO_V1(pg_unzstd);

/*
 * Custom allocation wrapper for Zstd. Helps to force usage of palloc
 * Opaque parameter passes the active PostgreSQL MemoryContext.
 */
static void *
pg_zstd_alloc(void *opaque, size_t size)
{
	MemoryContext ctx = (MemoryContext)opaque;

	// Allocate inside the specific context passed via the opaque pointer
	// Use MCXT_ALLOC_NO_OOM so palloc returns NULL instead of throwing an
	// immediate fat error
	return MemoryContextAllocExtended(ctx, size, MCXT_ALLOC_NO_OOM);
}

/*
 * Custom free wrapper for Zstd.
 */
static void
pg_zstd_free(void *opaque, void *address)
{
	if (address != NULL)
		pfree(address);
}

/*
 * zstd an uncompressed bytea
 */

Datum
pg_zstd(PG_FUNCTION_ARGS)
{
	struct varlena *in_varlena = PG_GETARG_VARLENA_PP(0);
	int32 compression_level = PG_GETARG_INT32(1);
	int16 threads = PG_GETARG_INT16(2);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	uint8 *out_buf = NULL;
	struct varlena *out_varlena = NULL;
	size_t comp_size = 0, max_dst_size = 0;
	char *dst_buf = NULL;

	ZSTD_CCtx *cctx;
	ZSTD_customMem zstd_allocator = {
			.customAlloc = pg_zstd_alloc,
			.customFree = pg_zstd_free,
			.opaque = (void *)CurrentMemoryContext};

	// Determine safe upper bound for output buffer size
	max_dst_size = ZSTD_compressBound(in_size);

	if (in_size == 0) {
		PG_FREE_IF_COPY(in_varlena, 0);
		PG_RETURN_BYTEA_P(in_varlena);
	}

	if (max_uncompressed_size >= 0 && in_size > max_uncompressed_size) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "input data is limited by pg_z.max_size (%d bytes)",
			 max_uncompressed_size);
	}

	if (!(compression_level >= 1 && compression_level <= 19)) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "invalid compression level (outside of 1..19): %d",
			 compression_level);
	}

	pg_mem_tracker_init();

	// Create a Zstd Context
	cctx = ZSTD_createCCtx_advanced(zstd_allocator);
	if (cctx == NULL) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR, "failed to create compression context");
	}

	// Allocate space for the PostgreSQL bytea output structure
	out_buf = (uint8 *)pg_hybrid_alloc(max_dst_size + VARHDRSZ);
	if (out_buf == NULL) {
		ZSTD_freeCCtx(cctx); // Free context to prevent memory leak
		PG_FREE_IF_COPY(in_varlena, 0);
		pg_mem_tracker_untrack(out_buf);
		elog(ERROR,
			 "out of memory allocating %zu byte buffer",
			 max_dst_size + VARHDRSZ);
	}
	dst_buf = VARDATA(out_buf);

	// Configure multi-threading parameters
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compression_level);
	ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, threads);

	// Perform multi-threaded compression
	comp_size = ZSTD_compressCCtx(
			cctx, dst_buf, max_dst_size, in_data, in_size, compression_level);
	ZSTD_freeCCtx(cctx);
	PG_FREE_IF_COPY(in_varlena, 0);
	pg_mem_tracker_untrack(out_buf);

	// Check for runtime errors
	if (ZSTD_isError(comp_size))
		elog(ERROR, "compression error: %s", ZSTD_getErrorName(comp_size));

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, comp_size + VARHDRSZ);

	PG_RETURN_BYTEA_P(out_varlena);
}

/*
 * unzstd a compressed bytea
 */

Datum
pg_unzstd(PG_FUNCTION_ARGS)
{
	struct varlena *in_varlena = PG_GETARG_VARLENA_PP(0);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	uint8 *out_buf = NULL;
	struct varlena *out_varlena = NULL;
	char *dst_buf = NULL;
	size_t uncomp_size = 0;

	ZSTD_DCtx *dctx;
	ZSTD_customMem zstd_allocator = {
			.customAlloc = pg_zstd_alloc,
			.customFree = pg_zstd_free,
			.opaque = (void *)CurrentMemoryContext};

	// Find out the original uncompressed frame size
	unsigned long long const uncompressed_size =
			ZSTD_getFrameContentSize(in_data, in_size);

	if (in_size == 0) {
		PG_RETURN_BYTEA_P(in_varlena);
	}

	if (uncompressed_size == ZSTD_CONTENTSIZE_ERROR) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR, "decompression error: Not a valid compressed Zstd frame");
	}
	if (uncompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR, "decompression error: Uncompressed size unknown");
	}

	pg_mem_tracker_init();

	// Instantiate Decompression Context using our Postgres palloc wrapper
	dctx = ZSTD_createDCtx_advanced(zstd_allocator);
	if (dctx == NULL) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR, "failed to allocate decompression context using palloc");
	}

	// Allocate memory for uncompressed result
	out_buf = (uint8 *)pg_hybrid_alloc((size_t)uncompressed_size + VARHDRSZ);
	if (out_buf == NULL) {
		ZSTD_freeDCtx(dctx); // Safely frees through pg_zstd_free wrapper
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "out of memory allocating %zu byte buffer",
			 (size_t)uncompressed_size + VARHDRSZ);
	}
	dst_buf = VARDATA(out_buf);

	// Decompress frame
	uncomp_size =
			ZSTD_decompress(dst_buf, uncompressed_size, in_data, in_size);

	ZSTD_freeDCtx(dctx);
	PG_FREE_IF_COPY(in_varlena, 0);
	pg_mem_tracker_untrack(out_buf);

	if (max_uncompressed_size >= 0 && uncomp_size > max_uncompressed_size)
		elog(ERROR,
			 "decompressed output exceeds pg_z.max_size (%d bytes)",
			 max_uncompressed_size);

	if (ZSTD_isError(uncomp_size))
		elog(ERROR, "decompression error: %s", ZSTD_getErrorName(uncomp_size));

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, uncomp_size + VARHDRSZ);

	PG_RETURN_BYTEA_P(out_varlena);
}
