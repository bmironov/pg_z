#include <assert.h>

#include "pg_z.h"
#include "utils/elog.h"
#include "utils/memutils.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_z_version);
PG_FUNCTION_INFO_V1(pg_z_version_num);

// GUC: memory allocation chunk size in bytes
static int guc_memory_chunk_size;
// shadow of GUC parameter of size_t type
size_t memory_chunk_size;

// GUC: maximum decompressed output size in bytes
static int guc_max_uncompressed_size;
// shadow of GUC parameter of size_t type
size_t max_uncompressed_size;

static void
assign_memory_chunk_size(int newval, void *extra)
{
	guc_memory_chunk_size = newval;
	memory_chunk_size = (size_t)newval;
}

static bool
check_memory_chunk_size(int *newval, void **extra, GucSource source)
{
	int orig_value = *newval;

	if (*newval < MIN_MEM_SIZE_8KB) {
		*newval = MIN_MEM_SIZE_8KB;
	} else {
		// rounding up size to closest 8kB multiple
		*newval = (*newval + (MIN_MEM_SIZE_8KB - 1)) & ~(MIN_MEM_SIZE_8KB - 1);
	}

	if (orig_value != *newval)
		ereport(NOTICE,
				errmsg("pg_z.mem_chunk_size has been rounded up to %d",
					   *newval));

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
	int orig_value = *newval;

	// Mostly dead code, but being extra cautious here in case of
	// accidental change of parameter definition in _PG_init
	if (*newval < 0)
		*newval = 0;
	if (*newval > MaxAllocSize)
		*newval = MaxAllocSize;

	if (orig_value != *newval)
		ereport(NOTICE,
				errmsg("pg_z.max_size has been adjusted to %d", *newval));

	return true;
}

void
_PG_init(void)
{
	DefineCustomIntVariable(
			"pg_z.mem_chunk_size",
			"Memory allocation chunk size, in bytes.",
			NULL,
			&guc_memory_chunk_size,
			256 * 1024,		  // default: 256kB
			MIN_MEM_SIZE_8KB, // min: 8kB
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

/*
 * pg_z_version returns list of compiled in libraries
 * after text version text version of the pg_z extension
 * Parameters: none
 */

Datum
pg_z_version(PG_FUNCTION_ARGS)
{
	/*
	 * Accumulate active userspace algorithms into a clean comma-separated list
	 */
	bool first = true;
	StringInfoData buf;
	initStringInfo(&buf);
	appendStringInfo(&buf, "pg_z v%s (compiled with: ", PG_Z_VERSION);

#ifdef USE_brotli
	appendStringInfo(&buf, "brotli");
	first = false;
#endif

#ifdef USE_gzip
	if (!first)
		appendStringInfo(&buf, ", ");
	appendStringInfo(&buf, "gzip, deflate");
	first = false;
#endif

#ifdef USE_gzip_ng
	if (!first)
		appendStringInfo(&buf, ", ");
	appendStringInfo(&buf, "gzip-ng, deflate-ng");
	first = false;
#endif

#ifdef USE_lz4
	if (!first)
		appendStringInfo(&buf, ", ");
	appendStringInfo(&buf, "lz4");
	first = false;
#endif

#ifdef USE_snappy
	if (!first)
		appendStringInfo(&buf, ", ");
	appendStringInfo(&buf, "snappy");
	first = false;
#endif

#ifdef USE_zstd
	if (!first)
		appendStringInfo(&buf, ", ");
	appendStringInfo(&buf, "zstd");
#endif

	appendStringInfo(&buf, ")");

	PG_RETURN_TEXT_P(cstring_to_text(buf.data));
}

/*
 * pg_z_version_num returns int32 value that is assembled from all 3 parts
 * of the version number as a bit mask:
 * bits 16-30 major version
 * bits  8-15 minor version
 * bits  0- 7 patch number
 * Parameters: none
 *
 * Example: v1.2.3 will convert into hex value 0x00010203
 */

Datum
pg_z_version_num(PG_FUNCTION_ARGS)
{
	const char *version = PG_Z_VERSION;
	unsigned int major = 0, minor = 0, patch = 0, parsed;
	int32_t result;

	parsed = sscanf(version, "%u.%u.%u", &major, &minor, &patch);
	if (parsed < 3) {
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("Invalid version format (%s). Expected 'X.Y.Z'",
						PG_Z_VERSION)));
	}
	if (major > 0x7FFF || minor > 0xFF || patch > 0xFF) {
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("Version components out of range (%s). "
						"Max allowed: 32767.255.255",
						version)));
	}

	result = (uint32_t)((major << 16) | (minor << 8) | patch);
	PG_RETURN_INT32(result);
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
