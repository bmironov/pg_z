#include "pg_z.h"

#include <alloca.h>
#include <brotli/decode.h>
#include <brotli/encode.h>

PG_FUNCTION_INFO_V1(pg_brotli);
PG_FUNCTION_INFO_V1(pg_unbrotli);

/*
 * Custom allocation wrapper for brotli. Helps to force usage of palloc
 * Opaque parameter passes the active PostgreSQL MemoryContext.
 */
static void *
pg_brotli_alloc(void *opaque, size_t size)
{
	return MemoryContextAllocExtended(
			(MemoryContext)opaque, size, MCXT_ALLOC_NO_OOM);
}

/*
 * Custom free wrapper for brotli.
 */

static void
pg_brotli_free(void *opaque, void *address)
{
	if (address != NULL)
		pfree(address);
}

/*
 * brotli an uncompressed bytea
 */

Datum
pg_brotli(PG_FUNCTION_ARGS)
{
	struct varlena *in_varlena = PG_GETARG_VARLENA_P(0);
	int32 compression_level = PG_GETARG_INT32(1);
	const uint8 *in_data = (uint8 *)(VARDATA(in_varlena));
	size_t in_size = VARSIZE(in_varlena) - VARHDRSZ;

	struct varlena *out_varlena = NULL;
	uint8 *out_buf = NULL, *next_out = NULL;
	size_t allocated_size = 0, initial_data_capacity = 0;

	BrotliEncoderState *state = NULL;
	BROTLI_BOOL status;
	size_t available_in = 0, available_out = 0;
	const uint8 *next_in = NULL;

	if (max_uncompressed_size >= 0 &&
		in_size > (size_t)max_uncompressed_size) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "input data is limited by pg_z.max_size (%zu bytes)",
			 max_uncompressed_size);
	}

	/* Input validation: Brotli quality levels range from 0 to 11 */
	if (compression_level < BROTLI_MIN_QUALITY ||
		compression_level > BROTLI_MAX_QUALITY) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "Brotli compression level must be between %d and %d",
			 BROTLI_MIN_QUALITY,
			 BROTLI_MAX_QUALITY);
	}

	if (in_size == 0) {
		out_varlena = (struct varlena *)palloc(VARHDRSZ);
		PG_FREE_IF_COPY(in_varlena, 0);
		SET_VARSIZE(out_varlena, VARHDRSZ);
		PG_RETURN_BYTEA_P(out_varlena);
	}

	// Pre-calculate the maximum bound for the compressed buffer size.
	allocated_size = BrotliEncoderMaxCompressedSize(in_size) + VARHDRSZ;

	out_varlena = (struct varlena *)pg_hybrid_alloc(&allocated_size);
	if (out_varlena == NULL) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "out of memory allocating %zu byte buffer",
			 allocated_size);
	}

	// Point to the actual data payload section of the bytea structure
	out_buf = (uint8 *)VARDATA(out_varlena);

	// Init stream compressor
	// This is the only way to use custom memory allocators
	state = BrotliEncoderCreateInstance(
			pg_brotli_alloc, pg_brotli_free, (void *)CurrentMemoryContext);
	if (state == NULL) {
		PG_FREE_IF_COPY(in_varlena, 0);
		pg_hybrid_free(out_buf);
		elog(ERROR,
			 "Brotli compression failed: could not initialize encoder state");
	}
	BrotliEncoderSetParameter(
			state, BROTLI_PARAM_QUALITY, (uint32_t)compression_level);
	BrotliEncoderSetParameter(
			state, BROTLI_PARAM_SIZE_HINT, (uint32_t)in_size);
	// Set sliding window size to 2^17 = 128kB
	BrotliEncoderSetParameter(state, BROTLI_PARAM_LGWIN, 17);

	available_in = in_size;
	next_in = in_data;
	initial_data_capacity = allocated_size - VARHDRSZ;
	available_out = initial_data_capacity;
	next_out = out_buf;

	status = BrotliEncoderCompressStream(
			state,
			BROTLI_OPERATION_FINISH,
			&available_in,
			&next_in,
			&available_out,
			&next_out,
			NULL);

	BrotliEncoderDestroyInstance(state);
	PG_FREE_IF_COPY(in_varlena, 0);

	if (status != BROTLI_TRUE) {
		elog(ERROR, "Brotli compression failed during stream processing");
	}

	SET_VARSIZE(out_varlena, initial_data_capacity - available_out + VARHDRSZ);

	PG_RETURN_BYTEA_P(out_varlena);
}

/*
 * unbrotli a compressed bytea
 */

Datum
pg_unbrotli(PG_FUNCTION_ARGS)
{
	struct varlena *in_varlena = PG_GETARG_VARLENA_P(0);
	const uint8 *in_data = (const uint8 *)(VARDATA(in_varlena));
	size_t in_size = VARSIZE(in_varlena) - VARHDRSZ;

	struct varlena *out_varlena = NULL;
	uint8 *out_buf = NULL, *tmp_buf = NULL, *out_buf_ptr = NULL;
	const uint8 *next_in = NULL;
	size_t allocated_size = 0, out_offset = 0;
	size_t old_size = 0, dynamic_step = 0;
	size_t available_in = 0, available_out = 0, prev_available_out = 0;
	bool flag = true;

	BrotliDecoderState *state = NULL;
	BrotliDecoderResult result;

	if (in_size == 0) {
		out_varlena = (struct varlena *)palloc(VARHDRSZ);
		PG_FREE_IF_COPY(in_varlena, 0);
		SET_VARSIZE(out_varlena, VARHDRSZ);
		PG_RETURN_BYTEA_P(out_varlena);
	}

	// Usually text information compresses with ratio ~ 6:1
	allocated_size = in_size * 6 + VARHDRSZ;

	out_buf = (uint8 *)pg_hybrid_alloc(&allocated_size);
	if (out_buf == NULL) {
		PG_FREE_IF_COPY(in_varlena, 0);
		elog(ERROR,
			 "out of memory allocating buffer %zu bytes",
			 allocated_size);
	}

	state = BrotliDecoderCreateInstance(
			pg_brotli_alloc, pg_brotli_free, (void *)CurrentMemoryContext);
	if (state == NULL) {
		PG_FREE_IF_COPY(in_varlena, 0);
		pg_hybrid_free(out_buf);
		elog(ERROR, "failed to create Brotli decompression decoder");
	}

	available_in = in_size;
	next_in = in_data;
	out_offset = 0;

	available_out = allocated_size - VARHDRSZ;
	out_buf_ptr = (uint8 *)(out_buf + VARHDRSZ);

	flag = true;

	do {
		prev_available_out = available_out;

		result = BrotliDecoderDecompressStream(
				state,
				&available_in,
				&next_in,
				&available_out,
				&out_buf_ptr,
				NULL);

		out_offset += (prev_available_out - available_out);

		if (max_uncompressed_size >= 0 &&
			out_offset > (size_t)max_uncompressed_size) {
			BrotliDecoderDestroyInstance(state);
			PG_FREE_IF_COPY(in_varlena, 0);
			pg_hybrid_free(out_buf);
			elog(ERROR,
				 "decompressed output exceeds pg_z.max_size (%zu bytes)",
				 max_uncompressed_size);
		}

		switch (result) {
		case BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
			old_size = allocated_size;
			dynamic_step = (old_size > memory_chunk_size) ? old_size
														  : memory_chunk_size;
			allocated_size += dynamic_step;

			tmp_buf = (uint8 *)pg_hybrid_repalloc(out_buf, &allocated_size);
			if (tmp_buf == NULL) {
				PG_FREE_IF_COPY(in_varlena, 0);
				pg_hybrid_free(out_buf);
				elog(ERROR,
					 "out of memory during buffer resize to %zu bytes",
					 allocated_size);
			}

			if (tmp_buf != out_buf) {
				out_buf_ptr = tmp_buf + (out_buf_ptr - out_buf);
				out_buf = tmp_buf;
			}

			available_out += (allocated_size - old_size);
			break;

		case BROTLI_DECODER_RESULT_SUCCESS:
		case BROTLI_DECODER_RESULT_ERROR:
		case BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
			flag = false;
			break;
		}
	} while (flag);

	BrotliDecoderDestroyInstance(state);
	PG_FREE_IF_COPY(in_varlena, 0);

	if (result != BROTLI_DECODER_RESULT_SUCCESS) {
		elog(ERROR,
			 "decompression error %d: corrupted stream or invalid data",
			 (int)result);
	}

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, out_offset + VARHDRSZ);

	PG_RETURN_BYTEA_P(out_varlena);
}
