#include <assert.h>

#include "pg_z.h"
#include "utils/memutils.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_z_version);

// GUC: memory allocation chunk size in bytes
size_t memory_chunk_size;
static int guc_memory_chunk_size;

// GUC: maximum decompressed output size in bytes; -1 = unlimited
size_t max_uncompressed_size;
static int guc_max_uncompressed_size;

static void
assign_memory_chunk_size(int newval, void *extra)
{
	guc_memory_chunk_size = newval;
	memory_chunk_size = (size_t)newval;
}

static bool
check_memory_chunk_size(int *newval, void **extra, GucSource source)
{
	if (*newval < MEM_8KB) {
		*newval = MEM_8KB;
	} else {
		// rounding up size to closest 8kB multiple
		*newval = (*newval + (MEM_8KB - 1)) & ~(MEM_8KB - 1);
	}

	return true;
}

static void
assign_max_uncompressed_size(int newval, void *extra)
{
	guc_max_uncompressed_size = newval;
	max_uncompressed_size = (size_t)newval;
}

static bool
check_max_uncompressed_size(int *newval, void **extra, GucSource source)
{
	if (*newval < 0)
		guc_max_uncompressed_size = 0;

	return true;
}

void
_PG_init(void)
{
	DefineCustomIntVariable(
			"pg_z.mem_chunk_size",
			"Memory allocation chunk size, in bytes. ",
			NULL,
			&guc_memory_chunk_size,
			256 * 1024, // default: 256kB
			MEM_8KB,	// min: 8kB
			1024 * 1024 * 1024,
			PGC_USERSET,
			GUC_UNIT_BYTE,
			check_memory_chunk_size,
			assign_memory_chunk_size,
			NULL);

	DefineCustomIntVariable(
			"pg_z.max_size",
			"Maximum allowed uncompressed document size, in bytes. "
			"0 will disable processing of any data.",
			NULL,
			&guc_max_uncompressed_size,
			256 * 1024 * 1024, // default: 256MB
			0,				   // min: 0 = disable any processing
			MaxAllocSize,	   // PostgreSQL won't accept more data
			PGC_USERSET,
			GUC_UNIT_BYTE,
			check_max_uncompressed_size,
			assign_max_uncompressed_size,
			NULL);

	pg_mem_tracker_init_hugepage_size();
}

Datum
pg_z_version(PG_FUNCTION_ARGS)
{
	/*
	 * Accumulate active userspace algorithms into a clean comma-separated list
	 */
	char buf[256] = "pg_z v" PG_Z_VERSION " (compiled with: ";
	bool first = true;

#ifdef USE_brotli
	strcat(buf, "brotli");
	first = false;
#endif

#ifdef USE_gzip
	if (!first)
		strcat(buf, ", ");
	strcat(buf, "gzip, deflate");
	first = false;
#endif

#ifdef USE_lz4
	if (!first)
		strcat(buf, ", ");
	strcat(buf, "lz4");
	first = false;
#endif

#ifdef USE_snappy
	if (!first)
		strcat(buf, ", ");
	strcat(buf, "snappy");
	first = false;
#endif

#ifdef USE_zstd
	if (!first)
		strcat(buf, ", ");
	strcat(buf, "zstd");
#endif

	strcat(buf, ")");

	PG_RETURN_TEXT_P(cstring_to_text(buf));
}

/*
 * Helper function to dump hex values
 */

void
dump_hex(const char *label, const uint8 *data, size_t size)
{
	char *hex_dump;
	size_t dump_bytes;
	size_t i;
	static const char hex_chars[] = "0123456789abcdef";

	if (size == 0) {
		elog(NOTICE, "Data dump '%s' is empty (size 0)", label);
		return;
	}

	dump_bytes = (size > 128) ? 128 : size;
	hex_dump = (char *)palloc(dump_bytes * 2 + 1);

	for (i = 0; i < dump_bytes; i++) {
		hex_dump[i * 2] = hex_chars[(data[i] >> 4) & 0x0F];
		hex_dump[i * 2 + 1] = hex_chars[data[i] & 0x0F];
	}
	hex_dump[dump_bytes * 2] = '\0';

	elog(NOTICE,
		 "Data dump '%s': Total size: %zu. First %zu bytes: %s",
		 label,
		 size,
		 dump_bytes,
		 hex_dump);
	pfree(hex_dump);
}
