
#include "c.h"
#include "pg_z.h"
#include "utils/palloc.h"

#include <zlib.h>

#define WINDOW_BITS 15	// window size 2^15 = 32kB
#define GZIP_WRAPPER 16 // this bit turns on gzip wrapper
#define AUTO_FORMAT 32	// this bit turn on auto format decoding, inflate only

PG_FUNCTION_INFO_V1(pg_any_gzip);
PG_FUNCTION_INFO_V1(pg_deflate);
PG_FUNCTION_INFO_V1(pg_gzip);
PG_FUNCTION_INFO_V1(pg_any_gunzip);
PG_FUNCTION_INFO_V1(pg_inflate);
PG_FUNCTION_INFO_V1(pg_gunzip);

/**
 * Custom memory allocator for zlib
 */

static void *
pg_zlib_alloc(void *opaque, unsigned int items, unsigned int size)
{
	return pg_hybrid_alloc((size_t)items * (size_t)size);
}

/**
 * gzip an uncompressed bytea
 */

Datum
pg_any_gzip(PG_FUNCTION_ARGS)
{
	z_stream zs;
	uint8 *out_buf = NULL, *tmp_buf = NULL;
	struct varlena *out_varlena = NULL;
	size_t allocated_size = 0, current_used = 0;
	int ret = Z_OK;
	int mem_level = 8; // 8 is balance between minimum and maximum memory
					   // consumption during compression

	struct varlena *in_varlena = PG_GETARG_VARLENA_PP(0);
	int32 compression_level = PG_GETARG_INT32(1);
	int32 window_bits = PG_GETARG_INT32(2);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	if (in_size == 0) {
		PG_RETURN_BYTEA_P(in_varlena);
	}

	if (max_uncompressed_size >= 0 && in_size > max_uncompressed_size)
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

	pg_mem_tracker_init();
	zs.zalloc = pg_zlib_alloc;
	zs.zfree = pg_hybrid_free;
	zs.opaque = Z_NULL;
	zs.next_in = (Bytef *)in_data;
	zs.avail_in = in_size;

	ret = deflateInit2(
			&zs,
			compression_level,
			Z_DEFLATED,
			window_bits,
			mem_level,
			Z_DEFAULT_STRATEGY);
	if (ret != Z_OK)
		elog(ERROR, "error running deflateInit2: %d", ret);

	// rough estimate for gzip format
	allocated_size = in_size + (in_size / 1000) + 32 + VARHDRSZ;
	// anti-fragmentation round up to next multiple of memory chunk size
	allocated_size = (allocated_size + (memory_chunk_size - 1)) &
					 ~(memory_chunk_size - 1);

	out_buf = (uint8 *)pg_hybrid_alloc(allocated_size);
	if (out_buf == NULL) {
		deflateEnd(&zs);
		elog(ERROR,
			 "not enough memory for buffer of %zu bytes",
			 allocated_size);
	}

	zs.next_out = out_buf + VARHDRSZ;
	zs.avail_out = allocated_size - VARHDRSZ;

	do {
		ret = deflate(&zs, Z_FINISH);
		current_used = allocated_size - zs.avail_out;

		/*
		 * zlib tells us that there are more data (Z_OK)
		 * and no more space left in the out_buf (zs.avail_out == 0)
		 */
		if (ret == Z_OK && zs.avail_out == 0) {
			allocated_size += memory_chunk_size;
			tmp_buf = (uint8 *)pg_hybrid_repalloc(
					out_buf,
					allocated_size - memory_chunk_size,
					allocated_size);
			if (tmp_buf == NULL) {
				deflateEnd(&zs);
				PG_FREE_IF_COPY(in_varlena, 0);
				elog(ERROR,
					 "out of memory during compression buffer reallocation to "
					 "%zu bytes",
					 allocated_size);
			}
			out_buf = tmp_buf;

			zs.next_out = out_buf + current_used;
			zs.avail_out = allocated_size - current_used;
		}
	} while (ret == Z_OK);

	deflateEnd(&zs);
	PG_FREE_IF_COPY(in_varlena, 0);
	pg_mem_tracker_untrack(out_buf);

	if (ret != Z_STREAM_END) {
		elog(ERROR, "error during compression: %d", ret);
	}

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, allocated_size - zs.avail_out);

	PG_RETURN_BYTEA_P(out_varlena);
}

/*
 * deflate an uncompressed bytea
 */

Datum
pg_deflate(PG_FUNCTION_ARGS)
{
	Datum input = PG_GETARG_DATUM(0);
	Datum compression_level = PG_GETARG_DATUM(1);

	// Magic to initialize in gzip mode
	Datum window_bits = Int32GetDatum(-WINDOW_BITS);
	Datum result;

	PG_TRY();
	{
		result = DirectFunctionCall3(
				pg_any_gzip, input, compression_level, window_bits);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_RETURN_DATUM(result);
}

/*
 * gzip an uncompressed bytea
 */

Datum
pg_gzip(PG_FUNCTION_ARGS)
{
	Datum input = PG_GETARG_DATUM(0);
	Datum compression_level = PG_GETARG_DATUM(1);

	// Magic to initialize in gzip mode
	Datum window_bits = Int32GetDatum(WINDOW_BITS | GZIP_WRAPPER);
	Datum result;

	PG_TRY();
	{
		result = DirectFunctionCall3(
				pg_any_gzip, input, compression_level, window_bits);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_RETURN_DATUM(result);
}

/**
 * gunzip a compressed bytea
 */

Datum
pg_any_gunzip(PG_FUNCTION_ARGS)
{
	z_stream zs;
	uint8 *out_buf = NULL, *tmp_buf = NULL;
	size_t allocated_size = 0, current_used = 0;
	struct varlena *out_varlena = NULL;
	int ret = Z_OK;

	struct varlena *in_varlena = PG_GETARG_VARLENA_PP(0);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);
	int window_bits = PG_GETARG_INT32(1);

	if (in_size == 0) {
		PG_RETURN_BYTEA_P(in_varlena);
	}

	pg_mem_tracker_init();
	zs.zalloc = pg_zlib_alloc;
	zs.zfree = pg_hybrid_free;
	zs.opaque = Z_NULL;
	zs.next_in = (Bytef *)in_data;
	zs.avail_in = in_size;

	ret = inflateInit2(&zs, window_bits);
	if (ret != Z_OK)
		elog(ERROR, "error running inflateInit2: %d", ret);

	// rough estimation to prevent memory overallocation
	allocated_size = in_size * 5;
	// anti-fragmentation round up to next multiple of memory chunk size
	allocated_size = (allocated_size + (memory_chunk_size - 1)) &
					 ~(memory_chunk_size - 1);

	out_buf = (uint8 *)palloc_extended(allocated_size, MCXT_ALLOC_NO_OOM);
	if (out_buf == NULL) {
		inflateEnd(&zs);
		elog(ERROR,
			 "not enough memory for buffer of %zu bytes",
			 allocated_size);
	}

	zs.next_out = out_buf + VARHDRSZ;
	zs.avail_out = allocated_size - VARHDRSZ;

	do {
		ret = inflate(&zs, Z_NO_FLUSH);
		current_used = allocated_size - zs.avail_out;

		if (max_uncompressed_size >= 0 &&
			(current_used - VARHDRSZ) > (size_t)max_uncompressed_size) {

			inflateEnd(&zs);
			PG_FREE_IF_COPY(in_varlena, 0);

			elog(ERROR,
				 "decompressed output exceeds pg_z.max_size (%zu bytes)",
				 max_uncompressed_size);
		}

		if (ret == Z_OK && zs.avail_out == 0) {
			// double size of allocation to minimize number of repalloc calls
			allocated_size += memory_chunk_size;
			tmp_buf = (uint8 *)pg_hybrid_repalloc(
					out_buf,
					allocated_size - memory_chunk_size,
					allocated_size);
			if (tmp_buf == NULL) {
				inflateEnd(&zs);
				PG_FREE_IF_COPY(in_varlena, 0);
				elog(ERROR,
					 "out of memory during decompression buffer reallocation "
					 "to %zu bytes",
					 allocated_size);
			}
			out_buf = tmp_buf;

			zs.next_out = out_buf + current_used;
			zs.avail_out = allocated_size - current_used;
		}
	} while (ret == Z_OK);

	inflateEnd(&zs);
	PG_FREE_IF_COPY(in_varlena, 0);
	pg_mem_tracker_untrack(out_buf);

	if (ret != Z_STREAM_END)
		elog(ERROR, "error during decompression: %d", ret);

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, allocated_size - zs.avail_out);

	PG_RETURN_BYTEA_P(out_varlena);
}

/*
 * inflate an uncompressed bytea
 */

Datum
pg_inflate(PG_FUNCTION_ARGS)
{
	Datum input = PG_GETARG_DATUM(0);

	// Magic to initialize in gzip mode
	Datum window_bits = Int32GetDatum(-WINDOW_BITS);
	Datum result;

	PG_TRY();
	{
		result = DirectFunctionCall2(pg_any_gunzip, input, window_bits);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_RETURN_DATUM(result);
}

/*
 * gunzip an uncompressed bytea
 */

Datum
pg_gunzip(PG_FUNCTION_ARGS)
{
	Datum input = PG_GETARG_DATUM(0);

	// Magic to initialize in gzip mode
	Datum window_bits = Int32GetDatum(WINDOW_BITS | AUTO_FORMAT);
	Datum result;

	PG_TRY();
	{
		result = DirectFunctionCall2(pg_any_gunzip, input, window_bits);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_RETURN_DATUM(result);
}
