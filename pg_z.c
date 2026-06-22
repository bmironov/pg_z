
/* System */
#include <assert.h>

#include "pg_z.h"

/* Set up PgSQL */
PG_MODULE_MAGIC;

/* GUC: maximum decompressed output size in bytes; -1 = unlimited */
int max_uncompressed_size;

void _PG_init(void);
void
_PG_init(void)
{
	DefineCustomIntVariable(
			"pg_z.max_size",
			"Maximum allowed uncompressed document size, in bytes. "
			"-1 disables the limit.",
			NULL,
			&max_uncompressed_size,
			256 * 1024 * 1024, /* default: 256MB */
			-1,				   /* min: -1 = unlimited */
			INT_MAX,
			PGC_USERSET,
			GUC_UNIT_BYTE,
			NULL,
			NULL,
			NULL);

	pg_mem_tracker_init_hugepage_size();
}
