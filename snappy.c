#include "pg_z.h"

#include "port/pg_crc32c.h"
#include <snappy-c.h>

PG_FUNCTION_INFO_V1(pg_snappy);
PG_FUNCTION_INFO_V1(pg_unsnappy);

// Constants according to the official Snappy Framing Format Specification

#define SNAPPY_CHUNK_SIZE 65536
#define CHUNK_STREAM_IDENTIFIER 0xff
#define CHUNK_COMPRESSED_DATA 0x00
#define CHUNK_UNCOMPRESSED_DATA 0x01

// Magic bytes for the stream identifier "sNaPpY" (6 bytes)
static const uint8_t SNAPPY_MAGIC[] = {0x73, 0x4e, 0x61, 0x50, 0x70, 0x59};

/*
 * Helper function to calculate CRC32C (Castagnoli polynomial).
 * Per Snappy spec, CRC is masked as: ((crc >> 15) | (crc << 17)) + 0xa282ead8
 */
static inline uint32_t
snappy_crc32c(const uint8_t *restrict data, size_t length)
{
	pg_crc32c crc;

	INIT_CRC32C(crc);
	COMP_CRC32C(crc, data, length);
	FIN_CRC32C(crc);

	// Apply Snappy's specific masking as per the specification
	return ((crc >> 15) | (crc << 17)) + 0xa282ead8;
}

/*
 * pg_snappy: compress data into official Snappy Framing Format
 */
Datum
pg_snappy(PG_FUNCTION_ARGS)
{
	struct varlena *volatile in_varlena = PG_GETARG_VARLENA_PP(0);
	const uint8 *in_data = (const uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	uint8 *volatile out_buf = NULL;

	struct varlena *out_varlena = NULL;
	size_t allocated_size = 0, out_offset = 0, new_allocated_size = 0;
	size_t src_offset = 0, estimated_chunks = 0;
	size_t block_size = 0, max_comp_len = 0, chunk_header_pos = 0;
	size_t total_chunk_len = 0;
	uint8_t chunk_type = CHUNK_COMPRESSED_DATA;
	uint32_t masked_crc = 0;

	snappy_status status;

	if (in_size == 0)
		PG_RETURN_BYTEA_P(in_varlena);

	PG_TRY();
	{
		if (in_size > max_uncompressed_size)
			elog(ERROR,
				 "input data is limited by pg_z.max_size (%zu bytes)",
				 max_uncompressed_size);

		/*
		 * Estimate buffer size:
		 * stream identifier + all chunks with metadata overhead
		 */
		estimated_chunks =
				(in_size + SNAPPY_CHUNK_SIZE - 1) / SNAPPY_CHUNK_SIZE;
		allocated_size =
				VARHDRSZ + 10 + estimated_chunks * (SNAPPY_CHUNK_SIZE + 32);
		// round up allocated size to multiple of memory_chunk_size
		allocated_size = (allocated_size + (memory_chunk_size - 1)) &
						 ~(memory_chunk_size - 1);

		out_buf = (uint8 *)pg_hybrid_alloc(&allocated_size);
		if (out_buf == NULL)
			elog(ERROR,
				 "out of memory allocating %zu byte buffer for Snappy",
				 allocated_size);

		out_offset = VARHDRSZ;

		// Form the Stram ID chunk (ID=0xff, Length=6)
		out_buf[out_offset++] = CHUNK_STREAM_IDENTIFIER;
		out_buf[out_offset++] = 0x06; // Length: 6 bytes (as little-endian)
		out_buf[out_offset++] = 0x00;
		out_buf[out_offset++] = 0x00;
		memcpy(out_buf + out_offset, SNAPPY_MAGIC, 6);
		out_offset += sizeof(SNAPPY_MAGIC);

		// Process input streaming data by 64kB chunks
		while (src_offset < in_size) {
			block_size = in_size - src_offset;
			chunk_type = CHUNK_COMPRESSED_DATA;

			if (block_size > SNAPPY_CHUNK_SIZE)
				block_size = SNAPPY_CHUNK_SIZE;

			max_comp_len = snappy_max_compressed_length(block_size);
			new_allocated_size = out_offset + max_comp_len + 32;

			if (new_allocated_size > allocated_size) {
				allocated_size = new_allocated_size;
				out_buf = (uint8 *)pg_hybrid_repalloc(
						(void *)out_buf, &allocated_size);
				if (out_buf == NULL)
					elog(ERROR,
						 "out of memory during Snappy compression buffer "
						 "reallocation to %zu bytes",
						 allocated_size);
			}

			masked_crc = snappy_crc32c(in_data + src_offset, block_size);

			// Reserve 8 bytes for Chunk ID (1), Length (3), and CRC32C (4)
			chunk_header_pos = out_offset;
			out_offset += 8;

			status = snappy_compress(
					(const char *)(in_data + src_offset),
					block_size,
					(char *)(out_buf + out_offset),
					&max_comp_len);

			if (status != SNAPPY_OK)
				elog(ERROR, "Snappy block compression failed");

			// If compression is inefficient, save data as-is
			if (max_comp_len >= block_size) {
				chunk_type = CHUNK_UNCOMPRESSED_DATA;
				max_comp_len = block_size;
				memcpy(out_buf + out_offset, in_data + src_offset, block_size);
			}

			total_chunk_len =
					max_comp_len + 4; // Payload size + 4 bytes of CRC32C

			out_buf[chunk_header_pos++] = chunk_type;
			out_buf[chunk_header_pos++] = (uint8_t)(total_chunk_len & 0xff);
			out_buf[chunk_header_pos++] =
					(uint8_t)((total_chunk_len >> 8) & 0xff);
			out_buf[chunk_header_pos++] =
					(uint8_t)((total_chunk_len >> 16) & 0xff);

			out_buf[chunk_header_pos++] = (uint8_t)(masked_crc & 0xff);
			out_buf[chunk_header_pos++] = (uint8_t)((masked_crc >> 8) & 0xff);
			out_buf[chunk_header_pos++] = (uint8_t)((masked_crc >> 16) & 0xff);
			out_buf[chunk_header_pos++] = (uint8_t)((masked_crc >> 24) & 0xff);

			out_offset += max_comp_len;
			src_offset += block_size;
		}
	}
	PG_CATCH();
	{
		PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);
		if (out_buf != NULL)
			pg_hybrid_free((void *)out_buf);

		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, out_offset);

	PG_RETURN_BYTEA_P(out_varlena);
}

/*
 * pg_unsnappy: decompress Snappy Framing Format (.sz streams)
 * True zero-copy architecture accommodating custom chunk boundaries up to 16MB
 * Implements 2-way, zero-reallocation decompression
 */
Datum
pg_unsnappy(PG_FUNCTION_ARGS)
{
	struct varlena *volatile in_varlena = PG_GETARG_VARLENA_PP(0);
	const uint8 *in_data = (const uint8 *)(VARDATA_ANY(in_varlena));
	size_t in_size = VARSIZE_ANY_EXHDR(in_varlena);

	uint8 *volatile out_buf = NULL;

	struct varlena *out_varlena = NULL;
	size_t allocated_size = 0, out_offset = 0, src_offset = 0, chunk_len = 0;
	uint8_t chunk_type = 0;
	snappy_status status;

	size_t scan_offset = 0, exact_uncompressed_total = 0;
	bool scan_magic_verified = false;

	size_t data_len = 0, uncompressed_chunk_len = 0, working_len = 0;
	snappy_status decompress_status;

	if (in_size == 0)
		PG_RETURN_BYTEA_P(in_varlena);

	PG_TRY();
	{
		/*
		 * PASS 1: Pre-calculate the exact final uncompressed footprint.
		 * This loop evaluates frame headers sequentially in microseconds.
		 */
		while (scan_offset < in_size) {
			if (scan_offset + 4 > in_size)
				elog(ERROR,
					 "Snappy decompression failed: malformed chunk header");

			chunk_type = in_data[scan_offset++];
			chunk_len = in_data[scan_offset] |
						(in_data[scan_offset + 1] << 8) |
						(in_data[scan_offset + 2] << 16);
			scan_offset += 3;

			if (scan_offset + chunk_len > in_size)
				elog(ERROR,
					 "Snappy decompression failed: chunk length out of "
					 "bounds");

			if (!scan_magic_verified && chunk_type != CHUNK_STREAM_IDENTIFIER)
				elog(ERROR,
					 "Snappy decompression failed: missing stream identifier");

			if (chunk_type == CHUNK_STREAM_IDENTIFIER) {
				scan_magic_verified = true;
				scan_offset += chunk_len;
				continue;
			}

			if (chunk_type == CHUNK_COMPRESSED_DATA ||
				chunk_type == CHUNK_UNCOMPRESSED_DATA) {
				data_len = chunk_len - 4;
				scan_offset += 4; // Skip CRC32C field

				if (chunk_type == CHUNK_UNCOMPRESSED_DATA) {
					exact_uncompressed_total += data_len;
				} else {
					uncompressed_chunk_len = 0;
					status = snappy_uncompressed_length(
							(const char *)(in_data + scan_offset),
							data_len,
							&uncompressed_chunk_len);
					if (status != SNAPPY_OK)
						elog(ERROR,
							 "Snappy decompression failed: invalid chunk "
							 "compression header");

					exact_uncompressed_total += uncompressed_chunk_len;
				}
				scan_offset += data_len;
			} else {
				// Skip or custom chunk handling
				scan_offset += chunk_len;
			}

			if (exact_uncompressed_total > max_uncompressed_size)
				elog(ERROR,
					 "decompressed output exceeds pg_z.max_size (%zu bytes)",
					 max_uncompressed_size);
		}

		/* Allocating final required boundary size exactly once */
		allocated_size = exact_uncompressed_total + VARHDRSZ;
		out_buf = (uint8 *)pg_hybrid_alloc(&allocated_size);
		if (out_buf == NULL)
			elog(ERROR,
				 "out of memory allocating output buffer for Snappy "
				 "decompression %zu bytes",
				 allocated_size);

		/*
		 * PASS 2: Decompress the payload streams at pure hardware line-speed.
		 * Zero allocations will happen in this loop block.
		 */
		while (src_offset < in_size) {
			chunk_type = in_data[src_offset++];
			chunk_len = in_data[src_offset] | (in_data[src_offset + 1] << 8) |
						(in_data[src_offset + 2] << 16);
			src_offset += 3;

			if (chunk_type == CHUNK_STREAM_IDENTIFIER) {
				src_offset += chunk_len;
				continue;
			}

			if ((chunk_type >= 0x02 && chunk_type <= 0x7f) ||
				chunk_type == 0xfe) {
				src_offset += chunk_len;
				continue;
			}

			if (chunk_type == CHUNK_COMPRESSED_DATA ||
				chunk_type == CHUNK_UNCOMPRESSED_DATA) {
				data_len = chunk_len - 4;
				src_offset += 4; // Skip CRC32C field

				if (chunk_type == CHUNK_UNCOMPRESSED_DATA) {
					memcpy(out_buf + VARHDRSZ + out_offset,
						   in_data + src_offset,
						   data_len);
					out_offset += data_len;
				} else {
					working_len = allocated_size - VARHDRSZ - out_offset;
					decompress_status = snappy_uncompress(
							(const char *)(in_data + src_offset),
							data_len,
							(char *)(out_buf + VARHDRSZ + out_offset),
							&working_len);
					if (decompress_status != SNAPPY_OK)
						elog(ERROR, "Snappy chunk decompression failed");

					out_offset += working_len;
				}
				src_offset += data_len;
			}
		}
	}
	PG_CATCH();
	{
		PG_FREE_IF_COPY(in_varlena, 0);
		if (out_buf != NULL)
			pg_hybrid_free((void *)out_buf);

		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_FREE_IF_COPY((struct varlena *)in_varlena, 0);

	out_varlena = (struct varlena *)out_buf;
	SET_VARSIZE(out_varlena, out_offset + VARHDRSZ);

	PG_RETURN_BYTEA_P(out_varlena);
}
