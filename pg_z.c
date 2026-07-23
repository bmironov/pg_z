#include <assert.h>

#include "pg_z.h"

PG_MODULE_MAGIC;

// GUC: memory allocation chunk size in bytes
size_t memory_chunk_size;
static int guc_memory_chunk_size;

// GUC: maximum decompressed output size in bytes; -1 = unlimited
size_t max_uncompressed_size;
static int guc_max_uncompressed_size;

static void
assign_memory_chunk_size(int newval, void *extra)
{
	// rounding up size to closest 8kB multiple
	guc_memory_chunk_size = (newval + (8 * 1024 - 1)) & ~(8 * 1024 - 1);
	memory_chunk_size = (size_t)guc_memory_chunk_size;
}

static void
assign_max_uncompressed_size(int newval, void *extra)
{
	if (newval > 8 * 1024) {
		// round up to closest 8kB mulltiple
		guc_max_uncompressed_size =
				(newval + (8 * 1024 - 1)) & ~(8 * 1024 - 1);
	} else {
		guc_max_uncompressed_size = newval;
	}

	max_uncompressed_size = (size_t)guc_max_uncompressed_size;
}

void _PG_init(void);
void
_PG_init(void)
{
	DefineCustomIntVariable(
			"pg_z.mem_chunk_size",
			"Memory allocation chunk size, in bytes. ",
			NULL,
			&guc_memory_chunk_size,
			256 * 1024, /* default: 256kB */
			8 * 1024,	/* min: 8kB */
			INT_MAX,
			PGC_USERSET,
			GUC_UNIT_BYTE,
			NULL,
			assign_memory_chunk_size,
			NULL);

	assign_memory_chunk_size(guc_memory_chunk_size, NULL);

	DefineCustomIntVariable(
			"pg_z.max_size",
			"Maximum allowed uncompressed document size, in bytes. "
			"-1 disables the limit.",
			NULL,
			&guc_max_uncompressed_size,
			256 * 1024 * 1024, /* default: 256MB */
			-1,				   /* min: -1 = unlimited */
			INT_MAX,
			PGC_USERSET,
			GUC_UNIT_BYTE,
			NULL,
			assign_max_uncompressed_size,
			NULL);

	assign_max_uncompressed_size(guc_max_uncompressed_size, NULL);

	pg_mem_tracker_init_hugepage_size();
}

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
