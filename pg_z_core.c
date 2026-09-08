#include "pg_z.h"

PG_FUNCTION_INFO_V1(pg_z_version);
PG_FUNCTION_INFO_V1(pg_z_version_num);

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
 * Builds tuple about compression library based on provided values
 */
Datum
pg_z_lib_details_tuple(FunctionCallInfo fcinfo, LibraryDetails *item)
{
	TupleDesc tupdesc;
	HeapTuple tuple;
	Datum values[LIB_DETAILS_TUPLE_COLUMNS];
	bool nulls[LIB_DETAILS_TUPLE_COLUMNS] = {false, false, false};

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE) {
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("function must be called in a composite context")));
	}

	values[0] = CStringGetTextDatum(item->algorithm);
	values[1] = CStringGetTextDatum(item->version);
	values[2] = CStringGetTextDatum(item->linking);

	tuple = heap_form_tuple(tupdesc, values, nulls);
	return HeapTupleGetDatum(tuple);
}
