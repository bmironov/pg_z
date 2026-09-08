#include "pg_z.h"

#include <zlib.h>

#ifndef ZLIB_LINK_TYPE
#define ZLIB_LIB_LINK_TYPE "N/A"
#else
#define ZLIB_LIB_LINK_TYPE ZLIB_LINK_TYPE
#endif

PG_FUNCTION_INFO_V1(pg_gzip_lib_details);
PG_FUNCTION_INFO_V1(pg_deflate);
PG_FUNCTION_INFO_V1(pg_gzip);
PG_FUNCTION_INFO_V1(pg_inflate);
PG_FUNCTION_INFO_V1(pg_gunzip);

#define MY_COMPRESS pg_zlib_compress
#define MY_DECOMPRESS pg_zlib_decompress
#define MY_Z_STREAM z_stream
#define MY_DEFLATE_INIT2 deflateInit2
#define MY_DEFLATE deflate
#define MY_DEFLATE_END deflateEnd
#define MY_INFLATE_INIT2 inflateInit2
#define MY_INFLATE inflate
#define MY_INFLATE_END inflateEnd
#include "gzip_common.h"
#undef MY_INFLATE_END
#undef MY_INFLATE
#undef MY_INFLATE_INIT2
#undef MY_DEFLATE_END
#undef MY_DEFLATE
#undef MY_DEFLATE_INIT2
#undef MY_Z_STREAM

PG_FUNCTION_INFO_V1(MY_COMPRESS);
PG_FUNCTION_INFO_V1(MY_DECOMPRESS);

/*
 * This function returns zlib library version used by this extension
 */
Datum
pg_gzip_lib_details(PG_FUNCTION_ARGS)
{
	LibraryDetails item = {"Gzip/Deflate", zlibVersion(), ZLIB_LIB_LINK_TYPE};

	PG_RETURN_DATUM(pg_z_lib_details_tuple(fcinfo, &item));
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
				MY_COMPRESS, input, compression_level, window_bits);
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
				MY_COMPRESS, input, compression_level, window_bits);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_RETURN_DATUM(result);
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
		result = DirectFunctionCall2(MY_DECOMPRESS, input, window_bits);
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
		result = DirectFunctionCall2(MY_DECOMPRESS, input, window_bits);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	PG_RETURN_DATUM(result);
}

#undef MY_DECOMPRESS
#undef MY_COMPRESS
