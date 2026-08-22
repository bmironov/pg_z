#include "pg_z.h"

#define ZSTD_STATIC_LINKING_ONLY // Expose advanced types like ZSTD_customMem
#include <zstd.h>

PG_FUNCTION_INFO_V1(pg_zstd);
PG_FUNCTION_INFO_V1(pg_unzstd);

// Struct to track and safely clean up multi-threaded Zstd contexts on
// abort/error
typedef struct ZstdCleanupArg {
	ZSTD_CCtx *cctx;
} ZstdCleanupArg;

/*
 * Fail-safe callback executed by PostgreSQL if the memory context is reset or
 * destroyed.
 */
static void
zstd_context_cleanup_callback(void *arg)
{
	ZstdCleanupArg *cleanup = (ZstdCleanupArg *)arg;

	if (cleanup->cctx != NULL) {
		ZSTD_freeCCtx(cleanup->cctx);
		cleanup->cctx = NULL;
	}
}

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
	struct varlena *volatile in_varlena = PG_GETARG_VARLENA_PP(0);
	int32 compression_level = PG_GETARG_INT32(1);
	int16 threads = PG_GETARG_INT16(2);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	uint8 *volatile out_buf = NULL;
	ZSTD_CCtx *volatile cctx = NULL;

	struct varlena *out_varlena = NULL;
	size_t comp_size = 0, max_dst_size = 0;
	char *dst_buf = NULL;

	ZstdCleanupArg *cleanup_arg = NULL;
	MemoryContextCallback *cleanup_cb = NULL;

	ZSTD_inBuffer input_stream;
	ZSTD_outBuffer output_stream;
	size_t init_status = 0, stream_status = 0;

	if (in_size == 0)
		PG_RETURN_BYTEA_P(in_varlena);

	PG_TRY();
	{
		if (in_size > max_uncompressed_size)
			elog(ERROR,
				 "input data is limited by pg_z.max_size (%zu bytes)",
				 max_uncompressed_size);

		if (!(compression_level >= 1 && compression_level <= 22))
			elog(ERROR,
				 "invalid compression level (outside of 1..22): %d",
				 compression_level);

		// Determine safe upper bound for output buffer size
		max_dst_size = ZSTD_compressBound(in_size);

		if (threads > 1) {
			cctx = ZSTD_createCCtx();

			if (cctx != NULL) {
				/*
				 * Register a fail-safe reset callback.
				 * If PostgreSQL aborts the query due to a timeout, rollback,
				 * or error, this memory context hook will execute and safely
				 * call ZSTD_freeCCtx() to prevent any malloc memory leaks.
				 */

				cleanup_arg = (ZstdCleanupArg *)palloc(sizeof(ZstdCleanupArg));
				cleanup_cb = (MemoryContextCallback *)palloc(
						sizeof(MemoryContextCallback));

				cleanup_arg->cctx = cctx;
				cleanup_cb->func = zstd_context_cleanup_callback;
				cleanup_cb->arg = (void *)cleanup_arg;

				MemoryContextRegisterResetCallback(
						CurrentMemoryContext, cleanup_cb);
			}
		} else {
			ZSTD_customMem zstd_allocator = {
					.customAlloc = pg_zstd_alloc,
					.customFree = pg_zstd_free,
					.opaque = (void *)CurrentMemoryContext};
			cctx = ZSTD_createCCtx_advanced(zstd_allocator);
		}
		if (cctx == NULL)
			elog(ERROR, "failed to create compression context");

		// Allocate space for the PostgreSQL bytea output structure
		max_dst_size += VARHDRSZ;
		out_buf = (uint8 *)pg_hybrid_alloc(&max_dst_size);
		if (out_buf == NULL)
			elog(ERROR,
				 "out of memory allocating %zu byte buffer",
				 max_dst_size);

		dst_buf = VARDATA(out_buf);

		// Configure multi-threading parameters
		ZSTD_CCtx_setParameter(
				cctx, ZSTD_c_compressionLevel, compression_level);
		ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, threads);

		init_status = ZSTD_CCtx_setPledgedSrcSize(cctx, in_size);
		if (ZSTD_isError(init_status))
			elog(ERROR,
				 "ZSTD stream initialization failed: %s",
				 ZSTD_getErrorName(init_status));

		input_stream.src = in_data;
		input_stream.size = in_size;
		input_stream.pos = 0;

		output_stream.dst = dst_buf;
		output_stream.size = max_dst_size - VARHDRSZ;
		output_stream.pos = 0;

		do {
			CHECK_FOR_INTERRUPTS();

			stream_status = ZSTD_compressStream2(
					cctx,
					&output_stream,
					&input_stream,
					ZSTD_e_end); /* Force end-of-frame job routing directly */

			if (ZSTD_isError(stream_status))
				elog(ERROR,
					 "ZSTD parallel processing failed: %s",
					 ZSTD_getErrorName(stream_status));

		} while (stream_status > 0);

		comp_size = output_stream.pos;

		// Check for runtime errors
		if (ZSTD_isError(comp_size))
			elog(ERROR, "compression error: %s", ZSTD_getErrorName(comp_size));
	}
	PG_CATCH();
	{
		PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
		if (cctx != NULL) {
			ZSTD_freeCCtx(cctx);
			if (cleanup_arg != NULL)
				cleanup_arg->cctx = NULL;
		}
		if (out_buf != NULL)
			pg_hybrid_free((void *)out_buf);

		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
	ZSTD_freeCCtx(cctx);
	if (cleanup_arg != NULL)
		cleanup_arg->cctx = NULL;

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
	struct varlena *volatile in_varlena = PG_GETARG_VARLENA_PP(0);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	uint8 *volatile out_buf = NULL;
	ZSTD_DCtx *volatile dctx = NULL;

	struct varlena *out_varlena = NULL;
	char *dst_buf = NULL;
	size_t uncomp_size = 0, uncompressed_size = 0;

	ZSTD_customMem zstd_allocator = {
			.customAlloc = pg_zstd_alloc,
			.customFree = pg_zstd_free,
			.opaque = (void *)CurrentMemoryContext};

	if (in_size == 0)
		PG_RETURN_BYTEA_P(in_varlena);

	PG_TRY();
	{
		// Find out the original uncompressed frame size
		uncompressed_size = ZSTD_getFrameContentSize(in_data, in_size);

		if (uncompressed_size == ZSTD_CONTENTSIZE_ERROR)
			elog(ERROR,
				 "decompression error: Not a valid compressed Zstd frame");

		if (uncompressed_size == ZSTD_CONTENTSIZE_UNKNOWN)
			elog(ERROR, "decompression error: Uncompressed size unknown");

		if (uncompressed_size > max_uncompressed_size)
			elog(ERROR,
				 "decompressed output will exceed pg_z.max_size (%zu bytes)",
				 max_uncompressed_size);

		// Instantiate Decompression Context using our Postgres palloc wrapper
		dctx = ZSTD_createDCtx_advanced(zstd_allocator);
		if (dctx == NULL)
			elog(ERROR,
				 "failed to allocate decompression context using palloc");

		// Allocate memory for uncompressed result
		uncompressed_size += VARHDRSZ;
		out_buf = (uint8 *)pg_hybrid_alloc(&uncompressed_size);
		if (out_buf == NULL)
			elog(ERROR,
				 "out of memory allocating %zu byte buffer",
				 (size_t)uncompressed_size);

		dst_buf = VARDATA(out_buf);

		// Decompress frame
		uncomp_size = ZSTD_decompress(
				dst_buf, uncompressed_size - VARHDRSZ, in_data, in_size);

		if (uncomp_size > max_uncompressed_size)
			elog(ERROR,
				 "decompressed output exceeds pg_z.max_size (%zu bytes)",
				 max_uncompressed_size);

		if (ZSTD_isError(uncomp_size))
			elog(ERROR,
				 "decompression error: %s",
				 ZSTD_getErrorName(uncomp_size));
	}
	PG_CATCH();
	{
		PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
		if (dctx != NULL)
			ZSTD_freeDCtx(dctx);
		if (out_buf != NULL)
			pg_hybrid_free((void *)out_buf);

		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
	ZSTD_freeDCtx(dctx);

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, uncomp_size + VARHDRSZ);

	PG_RETURN_BYTEA_P(out_varlena);
}
