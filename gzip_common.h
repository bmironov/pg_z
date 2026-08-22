#include "pg_z.h"

#ifndef MY_Z_STREAM
#include <zlib.h>
#define MY_Z_STREAM z_stream
#define MY_DEFLATE_INIT2 deflateInit2
#define MY_DEFLATE deflate
#define MY_DEFLATE_END deflateEnd
#define MY_INFLATE_INIT2 inflateInit2
#define MY_INFLATE inflate
#define MY_INFLATE_END inflateEnd
#define MY_ZLIB_ALLOC pg_zlib_alloc
#endif

Datum MY_COMPRESS(PG_FUNCTION_ARGS);
Datum MY_DECOMPRESS(PG_FUNCTION_ARGS);

#define WINDOW_BITS 15	// window size 2^15 = 32kB
#define GZIP_WRAPPER 16 // this bit turns on gzip wrapper
#define AUTO_FORMAT 32	// this bit turn on auto format decoding, inflate only

/*
 * Custom memory allocator for zlib
 */
static void *
pg_zlib_alloc(void *opaque, unsigned int items, unsigned int size)
{
	return palloc_extended((size_t)items * (size_t)size, MCXT_ALLOC_NO_OOM);
}

static void
pg_zlib_free(void *opaque, void *address)
{
	if (address)
		pfree(address);
}

/**
 * gzip an uncompressed bytea
 */

Datum
MY_COMPRESS(PG_FUNCTION_ARGS)
{
	struct varlena *volatile in_varlena = PG_GETARG_VARLENA_PP(0);
	int32 compression_level = PG_GETARG_INT32(1);
	int32 window_bits = PG_GETARG_INT32(2);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	int volatile zs_initialized = 0;
	MY_Z_STREAM zs;
	uint8 *volatile out_buf = NULL, *tmp_buf = NULL;
	struct varlena *out_varlena = NULL;
	size_t allocated_size = 0, current_used = 0;
	int ret = Z_OK;
	int mem_level = 8; // 8 is balance between minimum and maximum memory
					   // consumption during compression

	if (in_size == 0)
		PG_RETURN_BYTEA_P(in_varlena);

	PG_TRY();
	{
		if (in_size > max_uncompressed_size)
			elog(ERROR,
				 "input data is limited by pg_z.max_size (%zu bytes)",
				 max_uncompressed_size);

		/*
		 * compression level -1 is default best effort (approx 6)
		 * level 0 is no compression, 1-9 are lowest to highest
		 */
		if (!(compression_level >= -1 && compression_level <= 9))
			elog(ERROR,
				 "invalid compression level (outside of -1..9): %d",
				 compression_level);

		zs.zalloc = pg_zlib_alloc;
		zs.zfree = pg_zlib_free;
		zs.opaque = Z_NULL;
		zs.next_in = (Bytef *)in_data;
		zs.avail_in = in_size;

		ret = MY_DEFLATE_INIT2(
				&zs,
				compression_level,
				Z_DEFLATED,
				window_bits,
				mem_level,
				Z_DEFAULT_STRATEGY);
		if (ret != Z_OK)
			elog(ERROR, "error running deflateInit2: %d", ret);

		zs_initialized = 1;

		// rough estimate for gzip format
		allocated_size = in_size + (in_size / 1000) + 32 + VARHDRSZ;
		// anti-fragmentation round up to next multiple of memory chunk size
		allocated_size = (allocated_size + (memory_chunk_size - 1)) &
						 ~(memory_chunk_size - 1);

		out_buf = (uint8 *)pg_hybrid_alloc(&allocated_size);
		if (out_buf == NULL)
			elog(ERROR,
				 "not enough memory for buffer of %zu bytes",
				 allocated_size);

		zs.next_out = out_buf + VARHDRSZ;
		zs.avail_out = allocated_size - VARHDRSZ;

		do {
			ret = MY_DEFLATE(&zs, Z_FINISH);
			current_used = allocated_size - zs.avail_out;

			/*
			 * zlib tells us that there are more data (Z_OK)
			 * and no more space left in the out_buf (zs.avail_out == 0)
			 */
			if (ret == Z_OK && zs.avail_out == 0) {
				allocated_size += memory_chunk_size;
				tmp_buf =
						(uint8 *)pg_hybrid_repalloc(out_buf, &allocated_size);
				if (tmp_buf == NULL)
					elog(ERROR,
						 "out of memory during compression buffer "
						 "reallocation to "
						 "%zu bytes",
						 allocated_size);

				out_buf = tmp_buf;

				zs.next_out = out_buf + current_used;
				zs.avail_out = allocated_size - current_used;
			}
		} while (ret == Z_OK);

		if (ret != Z_STREAM_END)
			elog(ERROR, "error during compression: %d", ret);
	}
	PG_CATCH();
	{
		PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
		if (zs_initialized)
			MY_DEFLATE_END(&zs);
		if (out_buf != NULL)
			pg_hybrid_free((void *)out_buf);

		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
	MY_DEFLATE_END(&zs);

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, current_used);

	PG_RETURN_BYTEA_P(out_varlena);
}

/**
 * gunzip a compressed bytea
 */

Datum
MY_DECOMPRESS(PG_FUNCTION_ARGS)
{
	struct varlena *volatile in_varlena = PG_GETARG_VARLENA_PP(0);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);
	int window_bits = PG_GETARG_INT32(1);

	int volatile zs_initialized = 0;
	MY_Z_STREAM zs;
	uint8 *volatile out_buf = NULL, *tmp_buf = NULL;
	size_t allocated_size = 0, current_used = 0, grow_facctor = 0;
	struct varlena *out_varlena = NULL;
	int ret = Z_OK;

	if (in_size == 0)
		PG_RETURN_BYTEA_P(in_varlena);

	PG_TRY();
	{
		zs.zalloc = pg_zlib_alloc;
		zs.zfree = pg_zlib_free;
		zs.opaque = Z_NULL;
		zs.next_in = (Bytef *)in_data;
		zs.avail_in = in_size;

		ret = MY_INFLATE_INIT2(&zs, window_bits);
		if (ret != Z_OK)
			elog(ERROR, "error running inflateInit2: %d", ret);

		zs_initialized = 1;

		// rough estimation to prevent memory overallocation
		allocated_size = in_size * 5;
		// anti-fragmentation round up to next multiple of memory chunk size
		allocated_size = (allocated_size + (memory_chunk_size - 1)) &
						 ~(memory_chunk_size - 1);

		out_buf = (uint8 *)pg_hybrid_alloc(&allocated_size);
		if (out_buf == NULL)
			elog(ERROR,
				 "not enough memory for buffer of %zu bytes",
				 allocated_size);

		zs.next_out = out_buf + VARHDRSZ;
		zs.avail_out = allocated_size - VARHDRSZ;

		do {
			ret = MY_INFLATE(&zs, Z_NO_FLUSH);
			current_used = allocated_size - zs.avail_out;

			if ((current_used - VARHDRSZ) > max_uncompressed_size)
				elog(ERROR,
					 "decompressed output exceeds pg_z.max_size (%zu bytes)",
					 max_uncompressed_size);

			if ((ret == Z_OK || ret == Z_BUF_ERROR) && zs.avail_out == 0) {
				grow_facctor = (allocated_size > 0) ? allocated_size
													: memory_chunk_size;
				allocated_size += grow_facctor;
				tmp_buf =
						(uint8 *)pg_hybrid_repalloc(out_buf, &allocated_size);
				if (tmp_buf == NULL)
					elog(ERROR,
						 "out of memory during decompression buffer "
						 "reallocation "
						 "to %zu bytes",
						 allocated_size);

				out_buf = tmp_buf;

				zs.next_out = out_buf + current_used;
				zs.avail_out = allocated_size - current_used;

				// Let's try it one more time
				if (ret == Z_BUF_ERROR)
					ret = Z_OK;
			}
		} while (ret == Z_OK);

		if (ret != Z_STREAM_END)
			elog(ERROR, "error during decompression: %d", ret);
	}
	PG_CATCH();
	{
		PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
		if (zs_initialized)
			MY_INFLATE_END(&zs);
		if (out_buf != NULL)
			pg_hybrid_free((void *)out_buf);

		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
	MY_INFLATE_END(&zs);

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, current_used);

	PG_RETURN_BYTEA_P(out_varlena);
}
