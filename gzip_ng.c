#include "pg_z.h"

#include <zlib-ng.h>

PG_FUNCTION_INFO_V1(pg_deflate_ng);
PG_FUNCTION_INFO_V1(pg_gzip_ng);
PG_FUNCTION_INFO_V1(pg_inflate_ng);
PG_FUNCTION_INFO_V1(pg_gunzip_ng);

#define MY_COMPRESS pg_zlib_ng_compress
#define MY_DECOMPRESS pg_zlib_ng_decompress
#define MY_Z_STREAM zng_stream
#define MY_DEFLATE_INIT2 zng_deflateInit2
#define MY_DEFLATE zng_deflate
#define MY_DEFLATE_END zng_deflateEnd
#define MY_INFLATE_INIT2 zng_inflateInit2
#define MY_INFLATE zng_inflate
#define MY_INFLATE_END zng_inflateEnd
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
 * deflate an uncompressed bytea
 */

Datum
pg_deflate_ng(PG_FUNCTION_ARGS)
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
pg_gzip_ng(PG_FUNCTION_ARGS)
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
pg_inflate_ng(PG_FUNCTION_ARGS)
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
pg_gunzip_ng(PG_FUNCTION_ARGS)
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
