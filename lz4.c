#include "pg_z.h"

#define LZ4F_STATIC_LINKING_ONLY // Open access to custom memory management API
#include <lz4frame.h>

PG_FUNCTION_INFO_V1(pg_lz4);
PG_FUNCTION_INFO_V1(pg_unlz4);

/*
 *
 * Custom memory allocator makes sure to always use palloc
 * even with dynamically linked lz4 library that uses malloc
 *
 */
static void *
pg_lz4_alloc(void *opaque, size_t size)
{
	return palloc_extended(size, MCXT_ALLOC_NO_OOM);
}

static void
pg_lz4_free(void *opaque, void *address)
{
	if (address)
		pfree(address);
}

static LZ4F_CustomMem
get_pg_lz4_allocator(void)
{
	LZ4F_CustomMem cmem;
	memset(&cmem, 0, sizeof(cmem));
	cmem.customAlloc = pg_lz4_alloc;
	cmem.customFree = pg_lz4_free;
	cmem.opaqueState = NULL;
	return cmem;
}

/*
 * lz4 an uncompressed bytea
 */

Datum
pg_lz4(PG_FUNCTION_ARGS)
{
	struct varlena *in_varlena = PG_GETARG_VARLENA_PP(0);
	int32 compression_level = PG_GETARG_INT32(1);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	LZ4F_CustomMem cmem;
	LZ4F_cctx *cCtx;
	LZ4F_preferences_t prefs;

	size_t max_dst_len = 0, total_written = 0, ret = 0;
	uint8 *out_buf = NULL;
	char *dst_data = NULL;
	struct varlena *out_varlena = NULL;

	if (in_size == 0) {
		PG_FREE_IF_COPY(in_varlena, 0);
		PG_RETURN_BYTEA_P(in_varlena);
	}

	if (max_uncompressed_size >= 0 &&
		in_size > (size_t)max_uncompressed_size) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "input data is limited by pg_z.max_size (%zu bytes)",
			 max_uncompressed_size);
	}

	// Validate compression level
	if (!(compression_level >= 0 && compression_level <= 12)) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR, "compression level must be in range 0..12");
	}

	cmem = get_pg_lz4_allocator();
	cCtx = LZ4F_createCompressionContext_advanced(cmem, LZ4F_VERSION);
	if (cCtx == NULL) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "failed to create LZ4 compression context: %s",
			 LZ4F_getErrorName(ret));
	}

	memset(&prefs, 0, sizeof(prefs));
	prefs.compressionLevel = compression_level;
	prefs.frameInfo.blockMode = LZ4F_blockLinked;
	prefs.frameInfo.blockSizeID = LZ4F_max4MB;
	prefs.frameInfo.contentSize = (unsigned long long)in_size;

	max_dst_len = LZ4F_compressFrameBound(in_size, &prefs);
	if (LZ4F_isError(max_dst_len)) {
		LZ4F_freeCompressionContext(cCtx);
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "compression bound calculation failed: %s",
			 LZ4F_getErrorName(max_dst_len));
	}

	max_dst_len += VARHDRSZ;
	out_buf = (uint8 *)pg_hybrid_alloc(&max_dst_len);
	if (out_buf == NULL) {
		LZ4F_freeCompressionContext(cCtx);
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "out of memory allocating %zu byte buffer",
			 max_dst_len + VARHDRSZ);
	}
	dst_data = VARDATA(out_buf);

	/*
	 * Formation of LZ4 frame in palloc-memory
	 */

	// Writing frame header
	ret = LZ4F_compressBegin(cCtx, dst_data, max_dst_len, &prefs);
	if (LZ4F_isError(ret)) {
		LZ4F_freeCompressionContext(cCtx);
		PG_FREE_IF_COPY(in_varlena, 0);
		pg_hybrid_free(out_buf);
		elog(ERROR, "LZ4F_compressBegin failed: %s", LZ4F_getErrorName(ret));
	}
	total_written += ret;

	// Compress data
	ret = LZ4F_compressUpdate(
			cCtx,
			dst_data + total_written,
			max_dst_len - total_written,
			in_data,
			in_size,
			NULL);
	if (LZ4F_isError(ret)) {
		LZ4F_freeCompressionContext(cCtx);
		PG_FREE_IF_COPY(in_varlena, 0);
		pg_hybrid_free(out_buf);
		elog(ERROR, "LZ4F_compressUpdate failed: %s", LZ4F_getErrorName(ret));
	}
	total_written += ret;

	// Writing frame footer (checksums, EOF marker)
	ret = LZ4F_compressEnd(
			cCtx, dst_data + total_written, max_dst_len - total_written, NULL);

	// Destroy context (memory will be released via pg_lz4_free)
	LZ4F_freeCompressionContext(cCtx);
	PG_FREE_IF_COPY(in_varlena, 0);

	if (LZ4F_isError(ret))
		elog(ERROR, "LZ4F_compressEnd failed: %s", LZ4F_getErrorName(ret));

	total_written += ret;

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, total_written + VARHDRSZ);

	PG_RETURN_BYTEA_P(out_varlena);
}

/*
 * unlz4 an compressed bytea
 */

Datum
pg_unlz4(PG_FUNCTION_ARGS)
{
	struct varlena *in_varlena = PG_GETARG_VARLENA_PP(0);
	const uint8 *in_data = (uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	LZ4F_CustomMem cmem;
	LZ4F_frameInfo_t frameInfo;
	LZ4F_dctx *dCtx;

	size_t src_header_size, hint;
	size_t dst_capacity = 0, allocated_size = 0, total_decompressed = 0,
		   src_read_ptr = 0, src_size_left = 0, next_dst_size = 0,
		   next_src_size = 0, ret = 0;
	uint8 *out_buf = NULL, *tmp_buf = NULL;
	struct varlena *out_varlena = NULL;

	if (in_size == 0) {
		PG_FREE_IF_COPY(in_varlena, 0);
		PG_RETURN_BYTEA_P(in_varlena);
	}

	cmem = get_pg_lz4_allocator();
	dCtx = LZ4F_createDecompressionContext_advanced(cmem, LZ4F_VERSION);
	if (dCtx == NULL) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR, "failed to create LZ4 decompression context");
	}

	src_header_size = in_size;
	hint = LZ4F_getFrameInfo(dCtx, &frameInfo, in_data, &src_header_size);
	if (LZ4F_isError(hint)) {
		LZ4F_freeDecompressionContext(dCtx);
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "failed to read LZ4 frame info: %s",
			 LZ4F_getErrorName(hint));
	}

	dst_capacity = (size_t)frameInfo.contentSize;
	if (dst_capacity == 0) {
		// Fallback, if metadata doesn't provide frame size
		dst_capacity = in_size * 4;
	}

	allocated_size = dst_capacity + VARHDRSZ;
	out_buf = (uint8 *)pg_hybrid_alloc(&allocated_size);
	if (out_buf == NULL) {
		LZ4F_freeDecompressionContext(dCtx);
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "out of memory allocating buffer %zu bytes",
			 allocated_size);
	}

	// Little optimization since we've parsed header already
	src_read_ptr = src_header_size;
	src_size_left = in_size - src_header_size; // bytes left for processing

	do {
		next_dst_size = allocated_size - VARHDRSZ - total_decompressed;
		next_src_size = src_size_left;

		ret = LZ4F_decompress(
				dCtx,
				(char *)(out_buf + VARHDRSZ + total_decompressed),
				&next_dst_size,
				in_data + src_read_ptr,
				&next_src_size,
				NULL);
		if (LZ4F_isError(ret)) {
			LZ4F_freeDecompressionContext(dCtx);
			PG_FREE_IF_COPY(in_varlena, 0);
			pg_hybrid_free(out_buf);
			elog(ERROR, "decompression failed: %s", LZ4F_getErrorName(ret));
		}

		total_decompressed += next_dst_size;
		src_read_ptr += next_src_size;
		src_size_left -= next_src_size;

		if (max_uncompressed_size >= 0 &&
			total_decompressed > (size_t)max_uncompressed_size) {

			LZ4F_freeDecompressionContext(dCtx);
			PG_FREE_IF_COPY(in_varlena, 0);
			pg_hybrid_free(out_buf);
			elog(ERROR,
				 "decompressed output exceeds pg_z.max_size (%zu bytes)",
				 max_uncompressed_size);
		}

		/*
		 * ret > 0 shows how many bytes is left for processing
		 */
		if (ret > 0 && src_size_left > 0 &&
			(allocated_size - VARHDRSZ == total_decompressed)) {

			allocated_size += memory_chunk_size;

			tmp_buf = (uint8 *)pg_hybrid_repalloc(out_buf, &allocated_size);
			if (tmp_buf == NULL) {
				LZ4F_freeDecompressionContext(dCtx);
				PG_FREE_IF_COPY(in_varlena, 0);
				pg_hybrid_free(out_buf);
				elog(ERROR,
					 "out of memory during buffer resize to %zu bytes",
					 allocated_size);
			}
			out_buf = tmp_buf;
		}

	} while (ret > 0 && src_size_left > 0);

	LZ4F_freeDecompressionContext(dCtx);
	PG_FREE_IF_COPY(in_varlena, 0);

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, total_decompressed + VARHDRSZ);

	PG_RETURN_BYTEA_P(out_varlena);
}
