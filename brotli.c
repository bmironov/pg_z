#include "pg_z.h"

#include <alloca.h>
#include <brotli/decode.h>
#include <brotli/encode.h>

PG_FUNCTION_INFO_V1(pg_brotli);
PG_FUNCTION_INFO_V1(pg_unbrotli);

/* Stream chunk buffer size used to check for PostgreSQL backend signals */
#define WORK_CHUNK_SIZE (1024 * 1024)

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
	struct varlena *volatile in_varlena = PG_GETARG_VARLENA_P(0);
	int32 compression_level = PG_GETARG_INT32(1);
	const uint8 *in_data = (const uint8 *)(VARDATA(in_varlena));
	size_t in_size = VARSIZE(in_varlena) - VARHDRSZ;

	struct varlena *volatile out_varlena = NULL, *out_buf_tmp = NULL;
	BrotliEncoderState *volatile state = NULL;

	uint8 *out_buf = NULL, *next_out = NULL;
	size_t allocated_size = 0;

	BROTLI_BOOL status;
	size_t available_in = 0, available_out = 0, current_offset = 0,
		   grow_factor = 0;
	const uint8 *next_in = NULL;

	uint32_t lgwin = 19; /* Default to 512KB for balance */
	size_t bytes_to_feed = 0;
	BrotliEncoderOperation op = BROTLI_OPERATION_PROCESS;

	if (in_size == 0) {
		PG_RETURN_BYTEA_P(in_varlena);
	}

	PG_TRY();
	{
		if (in_size > max_uncompressed_size)
			elog(ERROR,
				 "input data is limited by pg_z.max_size (%zu bytes)",
				 max_uncompressed_size);

		/* Input validation: Brotli quality levels range from 0 to 11 */
		if (compression_level < BROTLI_MIN_QUALITY ||
			compression_level > BROTLI_MAX_QUALITY)
			elog(ERROR,
				 "Brotli compression level must be between %d and %d",
				 BROTLI_MIN_QUALITY,
				 BROTLI_MAX_QUALITY);

		if (compression_level >= 10) {
			if (in_size >= 16 * 1024 * 1024)
				lgwin = 24; /* Max 16MB window */
			else if (in_size >= 8 * 1024 * 1024)
				lgwin = 23; /* 8MB window */
			else if (in_size >= 4 * 1024 * 1024)
				lgwin = 22; /* 4MB window */
			else if (in_size >= 2 * 1024 * 1024)
				lgwin = 21; /* 2MB window */
			else
				lgwin = 20; /* 1MB window */
		}

		// Pre-calculate the maximum bound for the compressed buffer size.
		allocated_size = BrotliEncoderMaxCompressedSize(in_size);
		if (allocated_size == 0) {
			allocated_size = in_size + 1024;
		}
		allocated_size += VARHDRSZ;

		out_varlena = (struct varlena *)pg_hybrid_alloc(&allocated_size);
		if (out_varlena == NULL)
			elog(ERROR,
				 "out of memory allocating %zu byte buffer",
				 allocated_size);

		// Point to the actual data payload section of the bytea structure
		out_buf = (uint8 *)VARDATA(out_varlena);

		// Init stream compressor
		// This is the only way to use custom memory allocators
		state = BrotliEncoderCreateInstance(
				pg_brotli_alloc, pg_brotli_free, (void *)CurrentMemoryContext);
		if (state == NULL)
			elog(ERROR,
				 "Brotli compression failed: could not initialize encoder "
				 "state");

		BrotliEncoderSetParameter(
				state, BROTLI_PARAM_QUALITY, (uint32_t)compression_level);
		BrotliEncoderSetParameter(
				state, BROTLI_PARAM_SIZE_HINT, (uint32_t)in_size);
		BrotliEncoderSetParameter(state, BROTLI_PARAM_LGWIN, lgwin);

		available_in = 0;
		next_in = in_data;
		available_out = allocated_size - VARHDRSZ;
		next_out = out_buf;

		while (in_size > 0 || available_in > 0 ||
			   op == BROTLI_OPERATION_FINISH) {
			CHECK_FOR_INTERRUPTS();

			if (available_in == 0 && in_size > 0) {
				bytes_to_feed = (in_size > WORK_CHUNK_SIZE) ? WORK_CHUNK_SIZE
															: in_size;
				available_in = bytes_to_feed;
				in_size -= bytes_to_feed;
				op = (in_size == 0) ? BROTLI_OPERATION_FINISH
									: BROTLI_OPERATION_PROCESS;
			}

			status = BrotliEncoderCompressStream(
					state,
					op,
					&available_in,
					&next_in,
					&available_out,
					&next_out,
					NULL);
			if (status != BROTLI_TRUE)
				elog(ERROR,
					 "Brotli compression failed during stream processing");

			/*
			 * Recalculate how much total data has been written
			 * to the buffer payload
			 */
			current_offset =
					(size_t)(next_out - (uint8 *)VARDATA(out_varlena));

			if (op == BROTLI_OPERATION_FINISH &&
				BrotliEncoderIsFinished(state)) {
				break;
			}

			// Handle output space exhaustion and reallocate memory mid-stream
			if (available_out == 0) {
				if (allocated_size >= (max_uncompressed_size + VARHDRSZ)) {
					elog(ERROR,
						 "Brotli compressed data size grew beyond "
						 "pg_z.max_size limit (%zu bytes)",
						 max_uncompressed_size);
				}

				grow_factor = allocated_size;
				if (allocated_size + grow_factor >
					(max_uncompressed_size + VARHDRSZ)) {
					grow_factor = (max_uncompressed_size + VARHDRSZ) -
								  allocated_size;
				}
				allocated_size += grow_factor;

				out_buf_tmp = (struct varlena *)pg_hybrid_repalloc(
						(void *)out_varlena, &allocated_size);
				if (out_buf_tmp == NULL)
					elog(ERROR,
						 "Brotli out of memory growing compressed data "
						 "buffer (%zu bytes)",
						 allocated_size);
				out_varlena = out_buf_tmp;

				// Re-anchor pointers to the newly allocated pointer region
				available_out = allocated_size - VARHDRSZ - current_offset;
				next_out = (uint8 *)VARDATA(out_varlena) + current_offset;
			}
		}

		allocated_size = current_offset + VARHDRSZ;
	}

	PG_CATCH();
	{
		PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
		if (state != NULL)
			BrotliEncoderDestroyInstance((BrotliEncoderState *)state);
		if (out_varlena != NULL)
			pg_hybrid_free((void *)out_varlena);

		PG_RE_THROW();
	}

	PG_END_TRY();

	PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
	BrotliEncoderDestroyInstance((BrotliEncoderState *)state);

	SET_VARSIZE(out_varlena, allocated_size);

	PG_RETURN_BYTEA_P(out_varlena);
}

/*
 * unbrotli a compressed bytea
 */

Datum
pg_unbrotli(PG_FUNCTION_ARGS)
{
	struct varlena *volatile in_varlena = PG_GETARG_VARLENA_P(0);
	const uint8 *in_data = (const uint8 *)(VARDATA(in_varlena));
	size_t in_size = VARSIZE(in_varlena) - VARHDRSZ;

	uint8 *volatile out_buf = NULL; // flat buffer for decompressed data
	BrotliDecoderState *volatile state = NULL;
	BrotliDecoderResult result;

	struct varlena *out_varlena = NULL;
	uint8 *out_buf_tmp = NULL, *next_out = NULL;
	const uint8 *next_in = NULL;
	size_t allocated_size = 0, // real size of allocated memory for out_buf
			decompressed_bytes = 0, // offset track inside out_buf
			available_in = 0, last_available_in = 0, available_out = 0,
		   grow_factor = 0;
	bool flag = true;

	if (in_size == 0) {
		PG_RETURN_BYTEA_P((struct varlena *)in_varlena);
	}

	PG_TRY();
	{
		// Usually text information compresses with ratio ~ 6:1
		allocated_size = in_size * 6 + VARHDRSZ;
		if (allocated_size < memory_chunk_size)
			allocated_size = memory_chunk_size;
		if (allocated_size > max_uncompressed_size) {
			allocated_size = max_uncompressed_size;
		}

		out_buf = (uint8 *)pg_hybrid_alloc(&allocated_size);
		if (out_buf == NULL)
			elog(ERROR,
				 "out of memory allocating buffer %zu bytes",
				 allocated_size);

		state = BrotliDecoderCreateInstance(
				pg_brotli_alloc, pg_brotli_free, (void *)CurrentMemoryContext);
		if (state == NULL)
			elog(ERROR, "failed to create Brotli decompression decoder");

		available_in = in_size;
		next_in = in_data;

		// Point next_out directly to the payload area of out_buf
		// and skip VARHDRSZ
		next_out = out_buf + VARHDRSZ;
		available_out = allocated_size - VARHDRSZ;

		flag = true;

		do {
			CHECK_FOR_INTERRUPTS();

			last_available_in = available_in;

			result = BrotliDecoderDecompressStream(
					state,
					&available_in,
					&next_in,
					&available_out,
					&next_out,
					NULL);

			decompressed_bytes = (size_t)(next_out - out_buf);

			if (decompressed_bytes - VARHDRSZ > max_uncompressed_size)
				elog(ERROR,
					 "Brotli decompressed output exceeds pg_z.max_size "
					 "(%zu bytes)",
					 max_uncompressed_size);

			switch (result) {
			case BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT:
				if (available_in == 0 || available_in == last_available_in)
					flag = false;
				break;
			case BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT:
				if (allocated_size >= (max_uncompressed_size + VARHDRSZ)) {
					elog(ERROR,
						 "Brotli decompressed output data grew beyond "
						 "pg_z.max_size limit (%zu bytes)",
						 max_uncompressed_size);
				}
				grow_factor = allocated_size;
				if (allocated_size + grow_factor >
					(max_uncompressed_size + VARHDRSZ)) {
					grow_factor = (max_uncompressed_size + VARHDRSZ) -
								  allocated_size;
				}
				allocated_size += grow_factor;

				out_buf_tmp =
						(uint8 *)pg_hybrid_repalloc(out_buf, &allocated_size);
				if (out_buf_tmp == NULL)
					elog(ERROR,
						 "out of memory during buffer resize to %zu bytes",
						 allocated_size);

				out_buf = out_buf_tmp;
				next_out = out_buf + decompressed_bytes;
				available_out = allocated_size - decompressed_bytes;
				break;
			case BROTLI_DECODER_RESULT_SUCCESS:
			case BROTLI_DECODER_RESULT_ERROR:
			default:
				flag = false;
				break;
			}
		} while (flag);

		if (result != BROTLI_DECODER_RESULT_SUCCESS)
			elog(ERROR,
				 "Brotli decompression error %d: corrupted stream or invalid "
				 "data",
				 (int)result);
	}

	PG_CATCH();
	{
		PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
		if (state != NULL)
			BrotliDecoderDestroyInstance((BrotliDecoderState *)state);
		if (out_buf != NULL)
			pg_hybrid_free((void *)out_buf);

		PG_RE_THROW();
	}

	PG_END_TRY();

	PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
	BrotliDecoderDestroyInstance((BrotliDecoderState *)state);

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, decompressed_bytes);

	PG_RETURN_BYTEA_P(out_varlena);
}
